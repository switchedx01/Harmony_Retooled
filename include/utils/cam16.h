#ifndef CAM16_H
#define CAM16_H

#include "color_utils.h"
#include <stdint.h>

// CAM16 color appearance model components
typedef struct {
  double hue;    // h - 0..360
  double chroma; // C
  double j;      // J - Lightness
  double q;      // Q - Brightness
  double m;      // M - Colorfulness
  double s;      // s - Saturation

  // Internal values useful for reconstruction
  double jstar; // J* - UCS Lightness
  double astar; // a* - UCS a
  double bstar; // b* - UCS b
} Cam16;

/**
 * Convert an ARGB integer to CAM16.
 * Uses default viewing conditions (Scanning Surface).
 */
Cam16 Cam16_fromInt(uint32_t argb);

/**
 * Convert CAM16 to ARGB integer.
 */
uint32_t Cam16_toInt(Cam16 cam);

/**
 * Convert XYZ to CAM16.
 * x, y, z are 0..100 relative to D65.
 */
Cam16 Cam16_fromXyz(double x, double y, double z);

/**
 * Convert CAM16 to XYZ.
 */
void Cam16_toXyz(Cam16 cam, double *x, double *y, double *z);

/**
 * Calculate the distance between two colors in CAM16-UCS space.
 */
double Cam16_distance(Cam16 a, Cam16 b);

#endif // CAM16_H
