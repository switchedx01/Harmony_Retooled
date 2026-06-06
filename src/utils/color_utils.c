#include "color_utils.h"

uint32_t argb_from_rgb(uint8_t r, uint8_t g, uint8_t b) {
  return (0xFF << 24) | (r << 16) | (g << 8) | b;
}

uint8_t alpha_from_argb(uint32_t argb) { return (argb >> 24) & 0xFF; }

uint8_t red_from_argb(uint32_t argb) { return (argb >> 16) & 0xFF; }

uint8_t green_from_argb(uint32_t argb) { return (argb >> 8) & 0xFF; }

uint8_t blue_from_argb(uint32_t argb) { return argb & 0xFF; }

double linearize_component(uint8_t c) {
  double val = c / 255.0;
  if (val <= 0.04045) {
    return val / 12.92;
  }
  return pow((val + 0.055) / 1.055, 2.4);
}

uint8_t delinearize_component(double val) {
  double result;
  if (val <= 0.0031308) {
    result = val * 12.92;
  } else {
    result = 1.055 * pow(val, 1.0 / 2.4) - 0.055;
  }

  // Clamp
  if (result < 0.0)
    result = 0.0;
  if (result > 1.0)
    result = 1.0;

  return (uint8_t)(result * 255.0 + 0.5);
}

void matrix_multiply(const double row[3], double x, double y, double z,
                     double *result) {
  *result = row[0] * x + row[1] * y + row[2] * z;
}
