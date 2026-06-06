#include "palette.h"
#include "color_utils.h"
#include "hct.h"

TonalPalette TonalPalette_new(double hue, double chroma) {
  TonalPalette p;
  p.hue = hue;
  p.chroma = chroma;
  return p;
}

uint32_t TonalPalette_get(TonalPalette palette, double tone) {
  return Hct_solveToInt(palette.hue, palette.chroma, tone);
}

// Simplified palette generation - bypass expensive CAM16
CorePalette CorePalette_of(uint32_t argb) {
  // Extract RGB and convert to HSL for hue
  uint8_t r = (argb >> 16) & 0xFF;
  uint8_t g = (argb >> 8) & 0xFF;
  uint8_t b = argb & 0xFF;

  // Simple HSL conversion for hue
  double r_norm = r / 255.0;
  double g_norm = g / 255.0;
  double b_norm = b / 255.0;

  double max_c = (r_norm > g_norm) ? ((r_norm > b_norm) ? r_norm : b_norm)
                                   : ((g_norm > b_norm) ? g_norm : b_norm);
  double min_c = (r_norm < g_norm) ? ((r_norm < b_norm) ? r_norm : b_norm)
                                   : ((g_norm < b_norm) ? g_norm : b_norm);
  double delta = max_c - min_c;

  double hue = 0.0;
  if (delta > 0.0001) {
    if (max_c == r_norm) {
      hue = 60.0 * fmod((g_norm - b_norm) / delta, 6.0);
    } else if (max_c == g_norm) {
      hue = 60.0 * (((b_norm - r_norm) / delta) + 2.0);
    } else {
      hue = 60.0 * (((r_norm - g_norm) / delta) + 4.0);
    }
  }
  if (hue < 0)
    hue += 360.0;

  double chroma_approx = delta * 100.0; // 0-100 range

  CorePalette p;
  // Tonal Spot strategy with HSL hues
  p.primary = TonalPalette_new(hue, chroma_approx);
  p.secondary = TonalPalette_new(hue, 16.0);

  double tertiary_hue = sanitize_degrees(hue + 60.0);
  p.tertiary = TonalPalette_new(tertiary_hue, 24.0);

  p.neutral = TonalPalette_new(hue, 4.0);
  p.neutral_variant = TonalPalette_new(hue, 8.0);
  p.error = TonalPalette_new(25.0, 84.0);

  return p;
}
