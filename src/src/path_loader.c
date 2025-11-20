#include "mathlib.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/* Load path from binary file */
Complex *load_path_binary(const char *filename, size_t *n_points) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        fprintf(stderr, "Error: Could not open file '%s'\n", filename);
        return NULL;
    }

    /* Read number of points */
    uint32_t count;
    if (fread(&count, sizeof(uint32_t), 1, file) != 1) {
        fprintf(stderr, "Error: Could not read point count from '%s'\n", filename);
        fclose(file);
        return NULL;
    }

    if (count == 0) {
        fprintf(stderr, "Error: File '%s' contains no points\n", filename);
        fclose(file);
        return NULL;
    }

    /* Allocate memory for points */
    Complex *points = malloc(count * sizeof(Complex));
    if (!points) {
        fprintf(stderr, "Error: Could not allocate memory for %u points\n", count);
        fclose(file);
        return NULL;
    }

    /* Read points */
    for (uint32_t i = 0; i < count; i++) {
        double real, imag;
        if (fread(&real, sizeof(double), 1, file) != 1 ||
            fread(&imag, sizeof(double), 1, file) != 1) {
            fprintf(stderr, "Error: Could not read point %u from '%s'\n", i, filename);
            free(points);
            fclose(file);
            return NULL;
        }
        points[i] = real + I * imag;
    }

    fclose(file);
    *n_points = count;
    return points;
}

/* Load path from text file */
Complex *load_path_text(const char *filename, size_t *n_points) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Error: Could not open file '%s'\n", filename);
        return NULL;
    }

    /* Count lines first */
    size_t count = 0;
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        if (line[0] != '\n' && line[0] != '#') {
            count++;
        }
    }

    if (count == 0) {
        fprintf(stderr, "Error: File '%s' contains no valid points\n", filename);
        fclose(file);
        return NULL;
    }

    /* Allocate memory */
    Complex *points = malloc(count * sizeof(Complex));
    if (!points) {
        fprintf(stderr, "Error: Could not allocate memory for %zu points\n", count);
        fclose(file);
        return NULL;
    }

    /* Read points */
    rewind(file);
    size_t i = 0;
    while (fgets(line, sizeof(line), file) && i < count) {
        if (line[0] == '\n' || line[0] == '#') {
            continue;
        }

        double real, imag;
        if (sscanf(line, "%lf,%lf", &real, &imag) != 2) {
            fprintf(stderr, "Warning: Could not parse line: %s", line);
            continue;
        }

        points[i] = real + I * imag;
        i++;
    }

    fclose(file);
    *n_points = i;
    return points;
}
