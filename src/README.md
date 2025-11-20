# Fourier Series Epicycles

A tiny math library and interactive visualizations demonstrating Fourier Series using epicycles (rotating circles) to draw arbitrary paths.

## Overview

This project demonstrates how any closed path can be approximated using a sum of rotating circles (epicycles) through Fourier analysis. The project includes:

- **Mathlib**: A lightweight C math library implementing Fast Fourier Transform (FFT)
- **Simple Example**: 2D interactive animation with keyboard controls
- **Museum**: 3D first-person walkthrough with animated paintings
- **Python Tools**: Scripts to convert images to path data

## Features

- Pure C99 implementation with minimal dependencies
- Fast Fourier Transform for efficient computation
- Interactive controls for real-time parameter adjustment
- Image-to-path conversion using edge detection
- 3D museum environment to showcase different animations
- Built with Zig compiler toolchain for cross-platform support

## Dependencies

### Build Dependencies

- **Zig compiler** (0.15.1 or later) - Compiler and build system
- **Wayland development libraries** (Linux) - For windowing
    - `libwayland-dev`, `libxkbcommon-dev`, `libegl1-mesa-dev`
    - Or use X11 with `-Dlinux_display_backend=X11`

### Runtime Dependencies

- **Raylib** (master branch) - Graphics library (**automatically downloaded by Zig**)
- **Wayland/X11** - Display server
- **EGL** - Graphics context

### Python Tools (Optional)

- **Python 3**
- **OpenCV** (`opencv-python`)
- **NumPy**

## Building

The Zig build system automatically handles dependency fetching!

```bash
# Build the math library (default)
zig build

# Build examples (automatically downloads and builds raylib)
zig build examples

# Build individual examples
zig build simple
zig build museum
```

### Build Options

```bash
# Build with release optimizations
zig build -Doptimize=ReleaseFast

# Use X11 instead of Wayland on Linux
zig build -Dlinux_display_backend=X11
```

### Building for WebAssembly

You can compile the examples to WebAssembly for running in a web browser.

**Using Docker (Recommended - works on all systems):**

```bash
# Multi-stage production build - No Emscripten installation needed!
./scripts/build_web_docker.sh

# Run the production server
docker run --rm -p 8080:8080 fourier-web:latest
# Open http://localhost:8080/simple_example.html
```

**Using Native Emscripten (glibc systems only):**

```bash
# Install Emscripten SDK first (see README-WASM.md)
source /path/to/emsdk/emsdk_env.sh
./scripts/build_web.sh

# Serve locally
cd web-build && python3 -m http.server 8080
```

For detailed instructions on WebAssembly compilation, deployment, and troubleshooting, see **[README-WASM.md](README-WASM.md)**.

**Note:** Video recording features are disabled in the web version.

## Running the Examples

### Simple Example (2D Animation)

```bash
zig build run-simple
# Or directly:
./zig-out/bin/simple_example
```

**Controls:**

- `SPACE` - Pause/Play animation
- `UP/DOWN` - Increase/Decrease animation speed
- `LEFT/RIGHT` - Decrease/Increase number of epicycles
- `R` - Reset animation
- `ESC` - Exit

### Museum (3D Environment)

```bash
zig build run-museum
# Or directly:
./zig-out/bin/museum
```

**Controls:**

- `W/A/S/D` - Move around
- `Mouse` - Look around
- `ESC` - Exit

## Using the Python Tools

### Install Python Dependencies

```bash
python3 -m venv venv
source venv/bin/activate  # On Windows: venv\Scripts\activate
pip install opencv-python numpy
```

### Convert an Image to Path Data

```bash
./scripts/image_to_path.py input.png output.bin
```

**Options:**

- `--threshold1 N` - First Canny threshold (default: 100)
- `--threshold2 N` - Second Canny threshold (default: 100)
- `--min-contour-length N` - Minimum contour length (default: 50)
- `--no-normalize` - Skip normalization
- `--preview` - Show preview of detected edges

**Examples:**

```bash
# Basic usage
./scripts/image_to_path.py image.png path.bin

# With custom thresholds
./scripts/image_to_path.py image.png path.bin --threshold1 50 --threshold2 150

# Show preview before saving
./scripts/image_to_path.py image.png path.bin --preview

# Save as text format
./scripts/image_to_path.py image.png path.txt
```

### Loading Path Data in C

```c
#include "mathlib.h"

// Load binary path file
size_t n_points;
Complex *points = load_path_binary("path.bin", &n_points);

// Or load text path file
Complex *points = load_path_text("path.txt", &n_points);

// Perform Fourier analysis
FourierResult fourier = fourier_analyze(points, n_points, 100);

// Use the results...
Complex tip = epicycles_at_time(&fourier, time, positions);

// Cleanup
free(points);
fourier_result_free(&fourier);
```

## Project Structure

```
.
├── src/                    # Math library source code
│   ├── mathlib.h          # Main header file
│   ├── fft.c              # FFT implementation
│   ├── fourier.c          # Fourier analysis functions
│   └── path_loader.c      # Path loading utilities
├── examples/              # Example programs
│   ├── simple_example/    # 2D animation
│   │   └── main.c
│   ├── museum/           # 3D museum
│   │   └── main.c
│   └── assets/           # Generated paths, models, etc.
├── scripts/              # Python tools
│   └── image_to_path.py  # Image to path converter
├── deps/                 # Dependencies (gitignored)
│   └── raylib/          # Raylib graphics library
├── Makefile             # Build system
└── README.md            # This file
```

## How It Works

### Fourier Series

Any periodic function can be represented as a sum of sine and cosine waves (or equivalently, rotating complex exponentials):

### Fast Fourier Transform

The FFT algorithm efficiently computes these coefficients from a discrete set of points:

1. Take sample points along the path
2. Apply FFT to get frequency domain representation
3. Extract amplitudes, frequencies, and phases
4. Sort by amplitude (largest circles first)
5. Animate by rotating each circle at its frequency

### Epicycles

Each Fourier coefficient corresponds to a rotating circle:

- The circle rotates at a constant angular velocity
- Circles are connected tip-to-tail
- The final tip traces out the original path

## API Reference

### FFT Functions

```c
// Cooley-Tukey FFT (n must be power of 2)
void fft(const Complex *points, size_t n, Complex *output);

// Inverse FFT
void ifft(const Complex *coefficients, size_t n, Complex *output);

// Discrete Fourier Transform (any n)
void dft(const Complex *points, size_t n, Complex *output);
```

### Fourier Analysis

```c
// Analyze path and extract N largest coefficients
FourierResult fourier_analyze(const Complex *points,
                              size_t n_points,
                              size_t n_coeffs);

// Free result
void fourier_result_free(FourierResult *result);
```

### Epicycle Calculation

```c
// Calculate epicycle positions at time t
Complex epicycles_at_time(const FourierResult *result,
                          double t,
                          Complex *positions);
```

### Path Loading

```c
// Load from binary file
Complex *load_path_binary(const char *filename, size_t *n_points);

// Load from text file
Complex *load_path_text(const char *filename, size_t *n_points);
```

## Platform Support

Currently tested on:

- **Linux** (Wayland by default, X11 with `-Dlinux_display_backend=X11`)
- **Web** (WebAssembly via Emscripten - see [README-WASM.md](README-WASM.md))

The Zig build system makes cross-compilation straightforward!

## References

- [Raylib](https://www.raylib.com/) - Graphics library
- [3Blue1Brown](https://www.3blue1brown.com/) - Inspiration for visualization
- Fast Fourier Transform algorithm by Cooley and Tukey
- [Fourier Transform (Wikipedia)](https://en.wikipedia.org/wiki/Fourier_transform)
- [Fast Fourier Transform (Wikipedia)](https://en.wikipedia.org/wiki/Fast_Fourier_transform)
- [Drawing with Fourier Epicycles](https://en.wikipedia.org/wiki/Deferent_and_epicycle)

