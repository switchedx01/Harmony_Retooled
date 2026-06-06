#ifndef COLOR_EXTRACTOR_H
#define COLOR_EXTRACTOR_H

#include <stdbool.h>
#include <stdint.h>

/* Color structure */
typedef struct {
  uint8_t r;
  uint8_t g;
  uint8_t b;
} Color;

/* Material You Theme Colors - Generated from seed color */
typedef struct {
  /* Core palette colors at different tones */
  Color primary;           /* Main accent - Tone 40 (light) / 80 (dark) */
  Color on_primary;        /* Text on primary - Tone 100 (light) / 20 (dark) */
  Color primary_container; /* Lighter fill - Tone 90 (light) / 30 (dark) */
  Color on_primary_container; /* Text on container */

  Color secondary; /* Less prominent accent */
  Color on_secondary;
  Color secondary_container;
  Color on_secondary_container;

  Color tertiary; /* Complementary accent (+60 hue) */
  Color on_tertiary;
  Color tertiary_container;
  Color on_tertiary_container;

  /* Surface colors */
  Color surface;            /* App background */
  Color on_surface;         /* Primary text */
  Color surface_variant;    /* Card background */
  Color on_surface_variant; /* Secondary text */

  /* Additional roles */
  Color outline;         /* Borders and dividers */
  Color outline_variant; /* Subtle borders */

  /* Computed properties */
  float primary_luminance;
  float secondary_luminance;
  float tertiary_luminance;
  bool primary_is_light;
  bool secondary_is_light;
  bool tertiary_is_light;

  /* Seed color hue for palette generation */
  float seed_hue;
  float seed_saturation;
} ThemeColors;

/* Extract theme colors from image data */
void extract_theme_colors(const unsigned char *image_data, int width,
                          int height, ThemeColors *out_theme);

/* Calculate luminance of a color (0.0 = black, 1.0 = white) */
float color_luminance(Color c);

/* Check if a color is considered "light" (luminance > 0.5) */
bool color_is_light(Color c);

/* Interpolate between two colors */
Color color_lerp(Color a, Color b, float t);

/* Generate color at specific tone (0-100) from hue/saturation */
Color color_at_tone(float hue, float saturation, float tone);

#endif /* COLOR_EXTRACTOR_H */
