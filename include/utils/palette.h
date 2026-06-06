#ifndef PALETTE_H
#define PALETTE_H

#include "hct.h"
#include <stdint.h>

// Tonal Palette
// Defined by a specific Hue and Chroma.
// Generates colors of varying Tones (0..100).
typedef struct {
  double hue;
  double chroma;
} TonalPalette;

// Core Palette
// Constellation of Tonal Palettes generated from a source color.
typedef struct {
  TonalPalette primary;
  TonalPalette secondary;
  TonalPalette tertiary;
  TonalPalette neutral;
  TonalPalette neutral_variant;
  TonalPalette error;
} CorePalette;

/**
 * create a new Tonal Palette.
 */
TonalPalette TonalPalette_new(double hue, double chroma);

/**
 * Get a color from the palette at a specific tone.
 */
uint32_t TonalPalette_get(TonalPalette palette, double tone);

/**
 * Create a Core Palette from a source seed color.
 */
CorePalette CorePalette_of(uint32_t argb);

#endif // PALETTE_H
