#include "mathlib.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Comparison function for sorting coefficients by amplitude (largest first) */
static int compare_coeffs_by_amplitude(const void *a, const void *b) {
    const FourierCoefficient *ca = (const FourierCoefficient *)a;
    const FourierCoefficient *cb = (const FourierCoefficient *)b;

    /* Sort in descending order */
    if (ca->amplitude > cb->amplitude) return -1;
    if (ca->amplitude < cb->amplitude) return 1;
    return 0;
}

/* Analyze points with FFT and return sorted coefficients */
FourierResult fourier_analyze(const Complex *points, size_t n_points, size_t n_coeffs) {
    FourierResult result;
    result.coefficients = NULL;
    result.count = 0;

    if (n_points == 0 || n_coeffs == 0) {
        return result;
    }

    /* Ensure we don't ask for more coefficients than we have points */
    if (n_coeffs > n_points) {
        n_coeffs = n_points;
    }

    /* Perform FFT */
    Complex *fft_output = malloc(n_points * sizeof(Complex));
    if (!fft_output) {
        return result;
    }

    fft(points, n_points, fft_output);

    /* Extract all coefficients with their properties */
    FourierCoefficient *all_coeffs = malloc(n_points * sizeof(FourierCoefficient));
    if (!all_coeffs) {
        free(fft_output);
        return result;
    }

    for (size_t i = 0; i < n_points; i++) {
        all_coeffs[i].amplitude = cabs(fft_output[i]);
        all_coeffs[i].phase = carg(fft_output[i]);

        /* Calculate frequency index (handle both positive and negative frequencies)
         * FFT output is: [0, 1, 2, ..., n/2-1, -n/2, -n/2+1, ..., -1]
         * We want: [0, 1, 2, ..., n/2-1, -(n/2), -(n/2-1), ..., -1]
         */
        if (i <= n_points / 2) {
            all_coeffs[i].frequency = (double)i;
        } else {
            all_coeffs[i].frequency = (double)i - (double)n_points;
        }
    }

    /* Sort by amplitude (largest first) */
    qsort(all_coeffs, n_points, sizeof(FourierCoefficient), compare_coeffs_by_amplitude);

    /* Keep only the n_coeffs largest */
    result.coefficients = malloc(n_coeffs * sizeof(FourierCoefficient));
    if (!result.coefficients) {
        free(all_coeffs);
        free(fft_output);
        return result;
    }

    memcpy(result.coefficients, all_coeffs, n_coeffs * sizeof(FourierCoefficient));
    result.count = n_coeffs;

    /* Clean up */
    free(all_coeffs);
    free(fft_output);

    return result;
}

/* Free memory allocated by fourier_analyze */
void fourier_result_free(FourierResult *result) {
    if (result && result->coefficients) {
        free(result->coefficients);
        result->coefficients = NULL;
        result->count = 0;
    }
}

/* Calculate the position of epicycles at a given time
 *
 * Formula: sum of amplitude[i] * exp(I * (phase[i] + t * frequency[i]))
 */
Complex epicycles_at_time(const FourierResult *result, double t, Complex *positions) {
    if (!result || !result->coefficients || result->count == 0) {
        if (positions) {
            positions[0] = 0;
        }
        return 0;
    }

    /* Start at origin */
    Complex cumulative = 0;

    if (positions) {
        positions[0] = 0;
    }

    /* Add each rotating circle */
    for (size_t i = 0; i < result->count; i++) {
        double amplitude = result->coefficients[i].amplitude;
        double frequency = result->coefficients[i].frequency;
        double phase = result->coefficients[i].phase;

        /* Calculate the contribution of this epicycle:
         * amplitude * exp(I * (phase + t * frequency))
         */
        Complex contribution = amplitude * cexp(I * (phase + t * frequency));
        cumulative += contribution;

        if (positions) {
            positions[i + 1] = cumulative;
        }
    }

    return cumulative;
}
