#ifndef MATHLIB_H
#define MATHLIB_H

#include <complex.h>
#include <stddef.h>

/* Math library for Fourier Series Epicycles
 *
 * This library provides functionality for:
 * - Fast Fourier Transform (FFT) on complex data
 * - Complex number operations
 * - Epicycle calculations for animation
 */

/* Complex number type (using C99 complex.h) */
typedef double complex Complex;

/* FFT coefficient structure */
typedef struct {
    double amplitude;   /* Magnitude of the coefficient */
    double frequency;   /* Frequency (index in FFT) */
    double phase;       /* Phase angle (argument of complex number) */
} FourierCoefficient;

/* Result of FFT analysis */
typedef struct {
    FourierCoefficient *coefficients;  /* Array of coefficients */
    size_t count;                       /* Number of coefficients */
} FourierResult;

/* ===== FFT Functions ===== */

/* Perform FFT on an array of complex points
 *
 * Parameters:
 *   points: Input array of complex points
 *   n: Number of points (must be power of 2)
 *   output: Output array for FFT result (must be pre-allocated)
 *
 * Note: For non-power-of-2 sizes, use DFT instead
 */
void fft(const Complex *points, size_t n, Complex *output);

/* Perform inverse FFT
 *
 * Parameters:
 *   coefficients: Input FFT coefficients
 *   n: Number of coefficients
 *   output: Output array for time-domain signal
 */
void ifft(const Complex *coefficients, size_t n, Complex *output);

/* Discrete Fourier Transform (works for any size, slower than FFT)
 *
 * Parameters:
 *   points: Input array of complex points
 *   n: Number of points
 *   output: Output array for DFT result (must be pre-allocated)
 */
void dft(const Complex *points, size_t n, Complex *output);

/* ===== Fourier Analysis Functions ===== */

/* Analyze points with FFT and return sorted coefficients
 *
 * This function performs FFT and extracts the N largest coefficients
 * sorted by amplitude (largest first).
 *
 * Parameters:
 *   points: Input array of complex points
 *   n_points: Number of input points
 *   n_coeffs: Number of coefficients to keep
 *
 * Returns:
 *   FourierResult with sorted coefficients (caller must free)
 */
FourierResult fourier_analyze(const Complex *points, size_t n_points, size_t n_coeffs);

/* Free memory allocated by fourier_analyze */
void fourier_result_free(FourierResult *result);

/* ===== Epicycle Functions ===== */

/* Calculate the position of epicycles at a given time
 *
 * This computes: sum of a[i] * exp(I * (phase[i] + t * freq[i]))
 *
 * Parameters:
 *   result: Fourier analysis result
 *   t: Time parameter (typically 0 to 2*PI for one rotation)
 *   positions: Output array for cumulative positions (size = result.count + 1)
 *              positions[0] = origin (0+0i)
 *              positions[i] = sum of first i epicycles
 *
 * Returns:
 *   Final position (tip of last epicycle)
 */
Complex epicycles_at_time(const FourierResult *result, double t, Complex *positions);

/* ===== Utility Functions ===== */

/* Check if n is a power of 2 */
int is_power_of_2(size_t n);

/* Find next power of 2 >= n */
size_t next_power_of_2(size_t n);

/* Complex number utilities */
static inline double complex_abs(Complex z) {
    return cabs(z);
}

static inline double complex_arg(Complex z) {
    return carg(z);
}

static inline Complex complex_from_polar(double r, double theta) {
    return r * cexp(I * theta);
}

/* ===== Path Loading Functions ===== */

/* Load path from binary file
 *
 * Binary format:
 *   - 4 bytes: number of points (uint32_t)
 *   - For each point:
 *       - 8 bytes: real part (double)
 *       - 8 bytes: imaginary part (double)
 *
 * Parameters:
 *   filename: Path to binary file
 *   n_points: Output parameter for number of points loaded
 *
 * Returns:
 *   Pointer to array of Complex points (caller must free), or NULL on error
 */
Complex *load_path_binary(const char *filename, size_t *n_points);

/* Load path from text file
 *
 * Text format: one point per line as "real,imaginary"
 *
 * Parameters:
 *   filename: Path to text file
 *   n_points: Output parameter for number of points loaded
 *
 * Returns:
 *   Pointer to array of Complex points (caller must free), or NULL on error
 */
Complex *load_path_text(const char *filename, size_t *n_points);

#endif /* MATHLIB_H */
