#ifndef SCHEME_H
#define SCHEME_H

#include "palette.h"
#include <stdbool.h>
#include <stdint.h>

// Material Design 3 Scheme
// Maps semantic roles to ARGB colors.
typedef struct {
  uint32_t primary;
  uint32_t on_primary;
  uint32_t primary_container;
  uint32_t on_primary_container;

  uint32_t secondary;
  uint32_t on_secondary;
  uint32_t secondary_container;
  uint32_t on_secondary_container;

  uint32_t tertiary;
  uint32_t on_tertiary;
  uint32_t tertiary_container;
  uint32_t on_tertiary_container;

  uint32_t error;
  uint32_t on_error;
  uint32_t error_container;
  uint32_t on_error_container;

  uint32_t background;
  uint32_t on_background;

  uint32_t surface;
  uint32_t on_surface;
  uint32_t surface_variant;
  uint32_t on_surface_variant;

  uint32_t outline;
  uint32_t outline_variant;

  uint32_t shadow;
  uint32_t scrim;

  uint32_t inverse_surface;
  uint32_t inverse_on_surface;
  uint32_t inverse_primary;
} MaterialScheme;

/**
 * Generate a Material Scheme from a CorePalette.
 * @param core The core tonal palettes.
 * @param is_dark Whether to generate a Dark Mode scheme.
 * @param contrast 0.0 for standard, 0.5 for medium, 1.0 for high contrast.
 * (Placeholder for now)
 */
MaterialScheme MaterialScheme_create(CorePalette core, bool is_dark,
                                     double contrast);

/**
 * Convenience: Generate directly from seed color.
 */
MaterialScheme MaterialScheme_fromSeed(uint32_t argb, bool is_dark);

#endif // SCHEME_H
