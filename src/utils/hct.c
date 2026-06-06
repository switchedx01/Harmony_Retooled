#include "hct.h"
#include "color_utils.h"
#include <math.h>
#include <stdio.h>

// CIELAB L* function
static double lstar_from_y(double y) {
  // y is 0..100
  double y_normalized = y / 100.0;
  if (y_normalized <= 0.00885645) {
    return y_normalized * 903.2962962962963;
  }
  return 116.0 * pow(y_normalized, 1.0 / 3.0) - 16.0;
}

static double y_from_lstar(double lstar) {
  if (lstar <= 8.0) {
    return (lstar / 903.2962962962963) * 100.0;
  }
  double temp = (lstar + 16.0) / 116.0;
  return pow(temp, 3.0) * 100.0;
}

Hct Hct_fromInt(uint32_t argb) {
  // Get CAM16 attributes
  Cam16 cam = Cam16_fromInt(argb);

  // Get L* from RGB->XYZ->L*
  // Re-calculate XYZ locally to get Y
  // TODO: Optimize by reusing the XYZ from CAM16 step if possible?
  // For now re-compute for code cleanliness and separation.

  uint8_t r = red_from_argb(argb);
  uint8_t g = green_from_argb(argb);
  uint8_t b = blue_from_argb(argb);

  double r_lin = linearize_component(r);
  double g_lin = linearize_component(g);
  double b_lin = linearize_component(b);

  double y = 100.0 * (r_lin * 0.2126 + g_lin * 0.7152 + b_lin * 0.0722);
  double tone = lstar_from_y(y);

  Hct result = {cam.hue, cam.chroma, tone};
  return result;
}

Hct Hct_new(double hue, double chroma, double tone) {
  Hct h = {hue, chroma, tone};
  return h;
}

// Fast HSL-based approximation for HCT -> RGB
// This sacrifices perfect perceptual accuracy for speed
static uint32_t hsl_to_rgb(double h, double s, double l) {
  // Normalize
  h = fmod(h, 360.0);
  if (h < 0)
    h += 360.0;
  s = (s > 100.0) ? 100.0 : ((s < 0.0) ? 0.0 : s);
  l = (l > 100.0) ? 100.0 : ((l < 0.0) ? 0.0 : l);

  s /= 100.0;
  l /= 100.0;

  double c = (1.0 - fabs(2.0 * l - 1.0)) * s;
  double x = c * (1.0 - fabs(fmod(h / 60.0, 2.0) - 1.0));
  double m = l - c / 2.0;

  double r1, g1, b1;
  if (h < 60) {
    r1 = c;
    g1 = x;
    b1 = 0;
  } else if (h < 120) {
    r1 = x;
    g1 = c;
    b1 = 0;
  } else if (h < 180) {
    r1 = 0;
    g1 = c;
    b1 = x;
  } else if (h < 240) {
    r1 = 0;
    g1 = x;
    b1 = c;
  } else if (h < 300) {
    r1 = x;
    g1 = 0;
    b1 = c;
  } else {
    r1 = c;
    g1 = 0;
    b1 = x;
  }

  uint8_t r = (uint8_t)((r1 + m) * 255.0);
  uint8_t g = (uint8_t)((g1 + m) * 255.0);
  uint8_t b = (uint8_t)((b1 + m) * 255.0);

  return argb_from_rgb(r, g, b);
}

static uint32_t solve_int(double hue, double chroma, double tone) {
  // Fast approximation: Map HCT to HSL
  // Tone (0-100) -> Lightness (0-100)
  // Chroma (0-~150) -> Saturation (0-100), clamped
  double lightness = tone;
  double saturation = (chroma / 150.0) * 100.0;
  if (saturation > 100.0)
    saturation = 100.0;

  return hsl_to_rgb(hue, saturation, lightness);
}

uint32_t Hct_solveToInt(double hue, double chroma, double tone) {
  return solve_int(hue, chroma, tone);
}
