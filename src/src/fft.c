#include "mathlib.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/* Define M_PI if not available (C99 doesn't require it) */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Check if n is a power of 2 */
int is_power_of_2(size_t n) {
    return n > 0 && (n & (n - 1)) == 0;
}

/* Find next power of 2 >= n */
size_t next_power_of_2(size_t n) {
    if (n == 0) return 1;
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
#if SIZE_MAX > 0xFFFFFFFFUL
    /* Only shift by 32 if size_t is larger than 32 bits */
    n |= n >> 32;
#endif
    return n + 1;
}

/* Bit-reversal permutation for FFT */
static void bit_reverse_copy(const Complex *input, Complex *output, size_t n) {
    size_t bits = 0;
    size_t temp = n;
    while (temp > 1) {
        bits++;
        temp >>= 1;
    }

    for (size_t i = 0; i < n; i++) {
        size_t rev = 0;
        size_t temp_i = i;
        for (size_t b = 0; b < bits; b++) {
            rev = (rev << 1) | (temp_i & 1);
            temp_i >>= 1;
        }
        output[rev] = input[i];
    }
}

/* Cooley-Tukey FFT algorithm (iterative, in-place)
 *
 * This is a standard radix-2 decimation-in-time FFT.
 * Time complexity: O(n log n)
 */
void fft(const Complex *points, size_t n, Complex *output) {
    if (!is_power_of_2(n)) {
        /* Fall back to DFT for non-power-of-2 sizes */
        dft(points, n, output);
        return;
    }

    /* Bit-reversal permutation */
    bit_reverse_copy(points, output, n);

    /* Cooley-Tukey FFT */
    for (size_t s = 1; s <= log2(n); s++) {
        size_t m = 1 << s;  /* 2^s */
        Complex wm = cexp(-2.0 * M_PI * I / m);  /* exp(-2πi/m) */

        for (size_t k = 0; k < n; k += m) {
            Complex w = 1.0;
            for (size_t j = 0; j < m/2; j++) {
                Complex t = w * output[k + j + m/2];
                Complex u = output[k + j];
                output[k + j] = u + t;
                output[k + j + m/2] = u - t;
                w *= wm;
            }
        }
    }

    /* Normalize (divide by n) */
    for (size_t i = 0; i < n; i++) {
        output[i] /= n;
    }
}

/* Inverse FFT (same as FFT but with conjugate and different normalization) */
void ifft(const Complex *coefficients, size_t n, Complex *output) {
    if (!is_power_of_2(n)) {
        /* Fall back to inverse DFT */
        for (size_t i = 0; i < n; i++) {
            output[i] = 0;
            for (size_t k = 0; k < n; k++) {
                output[i] += coefficients[k] * cexp(2.0 * M_PI * I * k * i / n);
            }
        }
        return;
    }

    /* Conjugate input */
    Complex *temp = malloc(n * sizeof(Complex));
    for (size_t i = 0; i < n; i++) {
        temp[i] = conj(coefficients[i]) * n;  /* Multiply by n to undo FFT normalization */
    }

    /* Perform FFT on conjugated input */
    fft(temp, n, output);

    /* Conjugate output */
    for (size_t i = 0; i < n; i++) {
        output[i] = conj(output[i]);
    }

    free(temp);
}

/* Discrete Fourier Transform (naive O(n^2) algorithm)
 *
 * This works for any size n, not just powers of 2.
 * Formula: X[k] = (1/n) * sum(x[j] * exp(-2πi * j * k / n)) for j=0 to n-1
 */
void dft(const Complex *points, size_t n, Complex *output) {
    for (size_t k = 0; k < n; k++) {
        output[k] = 0;
        for (size_t j = 0; j < n; j++) {
            double angle = -2.0 * M_PI * j * k / n;
            output[k] += points[j] * cexp(I * angle);
        }
        /* Normalize */
        output[k] /= n;
    }
}
