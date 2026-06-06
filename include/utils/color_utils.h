#ifndef COLOR_UTILS_H
#define COLOR_UTILS_H

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Utility functions for Color Math

static inline double to_radians(double degrees) {
  return degrees * M_PI / 180.0;
}

static inline double to_degrees(double radians) {
  return radians * 180.0 / M_PI;
}

static inline double sanitize_degrees(double degrees) {
  degrees = fmod(degrees, 360.0);
  if (degrees < 0) {
    degrees += 360.0;
  }
  return degrees;
}

static inline int clamp_int(int min, int val, int max) {
  if (val < min)
    return min;
  if (val > max)
    return max;
  return val;
}

static inline double lerp(double start, double stop, double amount) {
  return (1.0 - amount) * start + amount * stop;
}

// Color conversion helpers
uint32_t argb_from_rgb(uint8_t r, uint8_t g, uint8_t b);
uint8_t alpha_from_argb(uint32_t argb);
uint8_t red_from_argb(uint32_t argb);
uint8_t green_from_argb(uint32_t argb);
uint8_t blue_from_argb(uint32_t argb);

// Linearization
double linearize_component(uint8_t c);
uint8_t delinearize_component(double val);

// Matrix multiplication (3x3 * 3x1)
void matrix_multiply(const double row[3], double x, double y, double z,
                     double *result);

#endif // COLOR_UTILS_H
