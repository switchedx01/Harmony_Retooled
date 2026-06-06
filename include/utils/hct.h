#ifndef HCT_H
#define HCT_H

#include "cam16.h"
#include <stdint.h>

// HCT Color Space
// Hue (0..360), Chroma (0..?), Tone (0..100)
typedef struct {
  double hue;
  double chroma;
  double tone;
} Hct;

/**
 * Convert ARGB to HCT.
 */
Hct Hct_fromInt(uint32_t argb);

/**
 * Convert HCT to ARGB.
 * This solves the color model to find the RGB value that best matches the HCT.
 */
uint32_t Hct_solveToInt(double hue, double chroma, double tone);

/**
 * Helper: HCT from components
 */
Hct Hct_new(double hue, double chroma, double tone);

#endif // HCT_H
