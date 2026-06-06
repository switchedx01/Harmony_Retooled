#ifndef QUANTIZER_H
#define QUANTIZER_H

#include "hct.h"
#include <stdint.h>

// Pixel format standard: ARGB (uint32_t)

typedef struct {
  uint32_t color; // ARGB
  int count;      // Population
} QuantizedColor;

typedef struct {
  QuantizedColor *colors;
  int count;
} QuantizerResult;

/**
 * Quantize an image to find dominant colors.
 * Uses a combination of Wu's algorithm (for seeding) and K-Means (Celebi) for
 * refinement.
 *
 * @param pixels Array of ARGB pixels
 * @param count Number of pixels
 * @param max_colors Maximum number of clusters to return
 * @return QuantizerResult (Must be freed by caller)
 */
QuantizerResult Quantizer_quantize(const uint32_t *pixels, int count,
                                   int max_colors);

/**
 * Score colors to find the best seed color.
 *
 * @param result The result from quantization
 * @return The scoring algorithm's top choice as an ARGB integer.
 */
uint32_t Quantizer_score(const QuantizerResult *result);

/**
 * Free quantizer result.
 */
void Quantizer_free(QuantizerResult *result);

#endif // QUANTIZER_H
