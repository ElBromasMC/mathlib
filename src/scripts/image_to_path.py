#!/usr/bin/env python3
"""
Image to Path Converter for Fourier Epicycles

This script converts images to path data that can be used by the
Fourier epicycles C programs.
"""

import argparse
import struct
import sys
from pathlib import Path

try:
    import cv2
    import numpy as np
except ImportError:
    print("Error: Required libraries not found.", file=sys.stderr)
    print("Please install: pip install opencv-python numpy", file=sys.stderr)
    sys.exit(1)


def greedy_shortest_path(points):
    """
    Order points using pure greedy nearest-neighbor algorithm.

    This follows the natural edge contour ordering without forcing loop closure.
    For edge-detected contours, this naturally produces closed paths.

    Args:
        points: Array of complex numbers representing 2D points

    Returns:
        Reordered array where each point is close to the next
    """
    pts = np.unique(points)  # Remove duplicates
    if pts.size == 0:
        return pts

    # Start from lowest y (then lowest x) for stability
    start_idx = np.lexsort((pts.real, pts.imag))[0]

    order = [start_idx]
    used = np.zeros(pts.size, dtype=bool)
    used[start_idx] = True
    cur = pts[start_idx]

    # Pure greedy nearest-neighbor - no loop-closing bias
    for _ in range(pts.size - 1):
        dists = np.abs(pts - cur)
        dists[used] = np.inf  # Exclude already-used points
        j = int(np.argmin(dists))
        order.append(j)
        used[j] = True
        cur = pts[j]

    return pts[np.array(order, dtype=int)]


def resample_path_evenly(points, num_samples, closed=True):
    """
    Resample path at evenly spaced intervals along arc length.

    This is CRITICAL for Fourier analysis - DFT assumes evenly spaced samples in time.
    For closed paths, we sample at 0%, 1/M%, 2/M%, ..., (M-1)/M% to avoid
    duplicating the endpoint (which equals the start point in a closed loop).

    Args:
        points: Array of complex numbers (ordered path)
        num_samples: Number of evenly spaced samples (M)
        closed: If True, treat as closed loop (connect last->first)

    Returns:
        Resampled array with M points at even arc-length intervals
    """
    z = np.asarray(points, dtype=complex)
    n = z.size

    if n < 2 or num_samples <= 0:
        return z.copy()

    # Number of segments: n if closed (includes last->first), n-1 if open
    segs = n if closed else (n - 1)

    # Calculate segment lengths
    seglen = np.empty(segs, dtype=float)
    for i in range(segs):
        a = z[i]
        b = z[(i + 1) % n]  # wraps to 0 if closed
        seglen[i] = abs(b - a)

    total = float(seglen.sum())
    if total <= 1e-12:
        return np.repeat(z[:1], num_samples)

    # Cumulative arc lengths
    cum = np.concatenate([[0.0], np.cumsum(seglen)])

    # Sample at k/M for k=0,1,...,M-1 (not including endpoint at 100%)
    out = np.empty(num_samples, dtype=complex)
    for k in range(num_samples):
        d = (k / num_samples) * total  # Key: k/M, not k/(M-1)

        # Find segment containing this arc length
        i = np.searchsorted(cum, d, side='right') - 1
        if i >= segs:
            i = segs - 1

        a = z[i]
        b = z[(i + 1) % n]
        local = d - cum[i]
        L = seglen[i] if seglen[i] > 0 else 1.0
        u = local / L
        out[k] = a + (b - a) * u

    return out


def subsample_path(points, max_points=800):
    """
    Intelligently subsample path to reduce point count while preserving shape.

    Args:
        points: Array of complex numbers
        max_points: Maximum number of points to keep

    Returns:
        Subsampled array of points
    """
    if len(points) <= max_points:
        return points

    # Calculate step size to achieve target point count
    step = max(1, len(points) // max_points)
    return points[::step]


def extract_edges(image, threshold1=100, threshold2=100, min_contour_length=50, max_points=1024):
    """
    Extract edges from an image and convert to complex points.
    Replicates the exact behavior of the working script's load_image_edges().

    Args:
        image: Input image (grayscale or color)
        threshold1: First threshold for Canny edge detection
        threshold2: Second threshold for Canny edge detection
        min_contour_length: Minimum number of points in a contour
        max_points: Maximum points to output (for performance)

    Returns:
        numpy array of complex numbers representing the path
    """
    # Convert to grayscale if needed
    if len(image.shape) == 3:
        gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)
    else:
        gray = image

    # Find edges using Canny
    edges = cv2.Canny(gray, threshold1, threshold2)

    # Find contours
    contours, _ = cv2.findContours(edges, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_NONE)

    # Concatenate ALL contours
    pts_list = []
    for c in contours:
        if len(c) < min_contour_length:
            continue
        xy = c.reshape(-1, 2).astype(float)
        pts_list.append(xy[:, 0] + 1j * xy[:, 1])

    if not pts_list:
        raise ValueError("No significant contours (>={} points) detected".format(min_contour_length))

    points = np.concatenate(pts_list)
    print(f"  Extracted {len(points)} edge points from {len(pts_list)} contours")

    # Order points using greedy shortest path
    print(f"  Ordering {len(points)} points...")
    points = greedy_shortest_path(points)

    # Detect and remove problematic large jumps at the end of the path
    # These occur when greedy algorithm connects distant contours
    if len(points) > 100:
        # Calculate distances between consecutive points
        distances = np.abs(np.diff(points))
        median_dist = np.median(distances)

        # Find points in the last 10% that create jumps > 5x median distance
        start_check = int(len(points) * 0.9)
        threshold = median_dist * 5.0

        # Find first bad jump in the tail
        trim_from = len(points)
        for i in range(start_check, len(points) - 1):
            if distances[i] > threshold:
                trim_from = i
                break

        if trim_from < len(points):
            trimmed = len(points) - trim_from
            points = points[:trim_from]
            print(f"  Trimmed {trimmed} points with large jumps (>{threshold:.1f}) from end")

    # Resample at evenly spaced arc-length intervals
    # Use closed=True to treat as closed loop (avoids duplicate endpoint)
    target_points = min(len(points), max_points)
    if len(points) > max_points:
        print(f"  Resampling from {len(points)} to {target_points} evenly spaced points (closed loop)...")
    else:
        print(f"  Resampling {len(points)} points at even arc-length intervals (closed loop)...")

    points = resample_path_evenly(points, target_points, closed=True)
    print(f"  Final point count: {len(points)}")

    return points


def normalize_points(points):
    """
    Normalize points to center them and scale them appropriately.

    Args:
        points: Array of complex numbers

    Returns:
        Normalized array of complex numbers
    """
    if len(points) == 0:
        return points

    real_parts = points.real
    imag_parts = points.imag

    # Center the points
    center_x = (real_parts.max() + real_parts.min()) / 2
    center_y = (imag_parts.max() + imag_parts.min()) / 2
    points = points - complex(center_x, center_y)

    # Scale to fit in a reasonable range (e.g., -5 to 5)
    max_extent = max(
        real_parts.max() - real_parts.min(),
        imag_parts.max() - imag_parts.min()
    )

    if max_extent > 0:
        scale_factor = 10.0 / max_extent
        points = points * scale_factor

    return points


def save_binary_path(points, output_file):
    """
    Save points in binary format for C program.

    Format:
        - 4 bytes: number of points (uint32)
        - For each point:
            - 8 bytes: real part (double)
            - 8 bytes: imaginary part (double)
    """
    with open(output_file, 'wb') as f:
        # Write number of points
        f.write(struct.pack('I', len(points)))

        # Write each point
        for point in points:
            f.write(struct.pack('dd', point.real, point.imag))


def save_text_path(points, output_file):
    """
    Save points in text format (one complex number per line).

    Format: real,imaginary
    """
    with open(output_file, 'w') as f:
        for point in points:
            f.write(f"{point.real},{point.imag}\n")


def main():
    parser = argparse.ArgumentParser(
        description='Convert images to path data for Fourier epicycles'
    )
    parser.add_argument('input', help='Input image file')
    parser.add_argument('output', help='Output path file (.bin or .txt)')
    parser.add_argument(
        '--threshold1',
        type=int,
        default=100,
        help='First threshold for Canny edge detection (default: 100)'
    )
    parser.add_argument(
        '--threshold2',
        type=int,
        default=100,
        help='Second threshold for Canny edge detection (default: 100)'
    )
    parser.add_argument(
        '--min-contour-length',
        type=int,
        default=50,
        help='Minimum contour length in pixels (default: 50)'
    )
    parser.add_argument(
        '--max-points',
        type=int,
        default=1024,
        help='Maximum number of points to output (default: 1024)'
    )
    parser.add_argument(
        '--no-normalize',
        action='store_true',
        help='Do not normalize the points'
    )
    parser.add_argument(
        '--preview',
        action='store_true',
        help='Show a preview of the detected edges'
    )

    args = parser.parse_args()

    # Check if input file exists
    input_path = Path(args.input)
    if not input_path.exists():
        print(f"Error: Input file '{args.input}' not found.", file=sys.stderr)
        sys.exit(1)

    # Load image
    print(f"Loading image: {args.input}")
    image = cv2.imread(str(input_path))
    if image is None:
        print(f"Error: Could not load image '{args.input}'.", file=sys.stderr)
        sys.exit(1)

    # Extract edges and convert to points
    print("Extracting edges...")
    try:
        points = extract_edges(
            image,
            args.threshold1,
            args.threshold2,
            args.min_contour_length,
            args.max_points
        )
    except ValueError as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)

    print(f"Final path has {len(points)} points")

    # Normalize points
    if not args.no_normalize:
        print("Normalizing points...")
        points = normalize_points(points)

    # Show preview if requested
    if args.preview:
        print("Showing preview (close window to continue)...")
        preview = np.zeros_like(image)
        for point in points:
            x = int(point.real * 50 + image.shape[1] / 2)
            y = int(-point.imag * 50 + image.shape[0] / 2)
            if 0 <= x < preview.shape[1] and 0 <= y < preview.shape[0]:
                preview[y, x] = [255, 255, 255]
        cv2.imshow('Preview', preview)
        cv2.waitKey(0)
        cv2.destroyAllWindows()

    # Save output
    output_path = Path(args.output)
    print(f"Saving to: {args.output}")

    if output_path.suffix == '.bin':
        save_binary_path(points, output_path)
    elif output_path.suffix == '.txt':
        save_text_path(points, output_path)
    else:
        print("Warning: Unknown file extension. Saving as binary.", file=sys.stderr)
        save_binary_path(points, output_path)

    print("Done!")


if __name__ == '__main__':
    main()
