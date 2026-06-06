#include "scheme.h"

MaterialScheme MaterialScheme_create(CorePalette core, bool is_dark,
                                     double contrast) {
  MaterialScheme s;

  if (is_dark) {
    // --- Dark Theme Mapping ---
    s.primary = TonalPalette_get(core.primary, 80);
    s.on_primary = TonalPalette_get(core.primary, 20);
    s.primary_container = TonalPalette_get(core.primary, 30);
    s.on_primary_container = TonalPalette_get(core.primary, 90);

    s.secondary = TonalPalette_get(core.secondary, 80);
    s.on_secondary = TonalPalette_get(core.secondary, 20);
    s.secondary_container = TonalPalette_get(core.secondary, 30);
    s.on_secondary_container = TonalPalette_get(core.secondary, 90);

    s.tertiary = TonalPalette_get(core.tertiary, 80);
    s.on_tertiary = TonalPalette_get(core.tertiary, 20);
    s.tertiary_container = TonalPalette_get(core.tertiary, 30);
    s.on_tertiary_container = TonalPalette_get(core.tertiary, 90);

    s.error = TonalPalette_get(core.error, 80);
    s.on_error = TonalPalette_get(core.error, 20);
    s.error_container = TonalPalette_get(core.error, 30);
    s.on_error_container = TonalPalette_get(core.error, 90);

    s.background =
        TonalPalette_get(core.neutral, 6); // Or 10? Standard M3 uses 6 for Dark
    s.on_background = TonalPalette_get(core.neutral, 90);

    s.surface = TonalPalette_get(core.neutral, 6);
    s.on_surface = TonalPalette_get(core.neutral, 90);
    s.surface_variant = TonalPalette_get(core.neutral_variant, 30);
    s.on_surface_variant = TonalPalette_get(core.neutral_variant, 80);

    s.outline = TonalPalette_get(core.neutral_variant, 60);
    s.outline_variant = TonalPalette_get(core.neutral_variant, 30);

    s.shadow = TonalPalette_get(core.neutral, 0);
    s.scrim = TonalPalette_get(core.neutral, 0);

    s.inverse_surface = TonalPalette_get(core.neutral, 90);
    s.inverse_on_surface = TonalPalette_get(core.neutral, 20);
    s.inverse_primary = TonalPalette_get(core.primary, 40);

  } else {
    // --- Light Theme Mapping ---
    s.primary = TonalPalette_get(core.primary, 40);
    s.on_primary = TonalPalette_get(core.primary, 100);
    s.primary_container = TonalPalette_get(core.primary, 90);
    s.on_primary_container = TonalPalette_get(core.primary, 10);

    s.secondary = TonalPalette_get(core.secondary, 40);
    s.on_secondary = TonalPalette_get(core.secondary, 100);
    s.secondary_container = TonalPalette_get(core.secondary, 90);
    s.on_secondary_container = TonalPalette_get(core.secondary, 10);

    s.tertiary = TonalPalette_get(core.tertiary, 40);
    s.on_tertiary = TonalPalette_get(core.tertiary, 100);
    s.tertiary_container = TonalPalette_get(core.tertiary, 90);
    s.on_tertiary_container = TonalPalette_get(core.tertiary, 10);

    s.error = TonalPalette_get(core.error, 40);
    s.on_error = TonalPalette_get(core.error, 100);
    s.error_container = TonalPalette_get(core.error, 90);
    s.on_error_container = TonalPalette_get(core.error, 10);

    s.background =
        TonalPalette_get(core.neutral, 98); // M3 Light BG is Tone 98 of Neutral
    s.on_background = TonalPalette_get(core.neutral, 10);

    s.surface = TonalPalette_get(core.neutral, 98);
    s.on_surface = TonalPalette_get(core.neutral, 10);
    s.surface_variant = TonalPalette_get(core.neutral_variant, 90);
    s.on_surface_variant = TonalPalette_get(core.neutral_variant, 30);

    s.outline = TonalPalette_get(core.neutral_variant, 50);
    s.outline_variant = TonalPalette_get(core.neutral_variant, 80);

    s.shadow = TonalPalette_get(core.neutral, 0);
    s.scrim = TonalPalette_get(core.neutral, 0);

    s.inverse_surface = TonalPalette_get(core.neutral, 20);
    s.inverse_on_surface = TonalPalette_get(core.neutral, 95);
    s.inverse_primary = TonalPalette_get(core.primary, 80);
  }

  return s;
}

MaterialScheme MaterialScheme_fromSeed(uint32_t argb, bool is_dark) {
  CorePalette core = CorePalette_of(argb);
  return MaterialScheme_create(core, is_dark, 0.0);
}
