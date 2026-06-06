#include "color_extractor.h"
#include "logging.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Helper structure for color clustering */
typedef struct {
  Color color;
  float hue;
  float saturation;
  float lightness;
  int count;
} ColorBucket;

/* Convert RGB to HSL */
static void rgb_to_hsl(Color c, float *h, float *s, float *l) {
  float r = c.r / 255.0f;
  float g = c.g / 255.0f;
  float b = c.b / 255.0f;

  float max = fmaxf(r, fmaxf(g, b));
  float min = fminf(r, fminf(g, b));
  float delta = max - min;

  *l = (max + min) / 2.0f;

  if (delta < 0.001f) {
    *s = 0.0f;
    *h = 0.0f;
    return;
  }

  *s = (*l > 0.5f) ? delta / (2.0f - max - min) : delta / (max + min);

  if (max == r) {
    *h = fmodf(((g - b) / delta), 6.0f);
  } else if (max == g) {
    *h = ((b - r) / delta) + 2.0f;
  } else {
    *h = ((r - g) / delta) + 4.0f;
  }
  *h = *h * 60.0f;
  if (*h < 0)
    *h += 360.0f;
}

/* Convert HSL to RGB - Material You uses tone (lightness) as key dimension */
static Color hsl_to_rgb(float h, float s, float l) {
  Color result = {0, 0, 0};

  if (s < 0.001f) {
    uint8_t gray = (uint8_t)(l * 255.0f);
    result.r = result.g = result.b = gray;
    return result;
  }

  float c = (1.0f - fabsf(2.0f * l - 1.0f)) * s;
  float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
  float m = l - c / 2.0f;

  float r1, g1, b1;
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

  result.r = (uint8_t)((r1 + m) * 255.0f);
  result.g = (uint8_t)((g1 + m) * 255.0f);
  result.b = (uint8_t)((b1 + m) * 255.0f);

  return result;
}

/* Generate color at specific tone (0-100) using seed hue/saturation */
Color color_at_tone(float hue, float saturation, float tone) {
  /* Tone is equivalent to L* in CIELAB, approximated via HSL lightness */
  /* Material You uses 0=black, 100=white */
  float l = tone / 100.0f;

  /* Reduce saturation at extreme tones for better contrast */
  float s = saturation;
  if (tone < 20.0f || tone > 80.0f) {
    s *= 0.5f;
  }

  return hsl_to_rgb(hue, s, l);
}

float color_luminance(Color c) {
  float r = c.r / 255.0f;
  float g = c.g / 255.0f;
  float b = c.b / 255.0f;

  r = (r <= 0.03928f) ? r / 12.92f : powf((r + 0.055f) / 1.055f, 2.4f);
  g = (g <= 0.03928f) ? g / 12.92f : powf((g + 0.055f) / 1.055f, 2.4f);
  b = (b <= 0.03928f) ? b / 12.92f : powf((b + 0.055f) / 1.055f, 2.4f);

  return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

bool color_is_light(Color c) { return color_luminance(c) > 0.5f; }

Color color_lerp(Color a, Color b, float t) {
  Color result;
  result.r = (uint8_t)(a.r + (b.r - a.r) * t);
  result.g = (uint8_t)(a.g + (b.g - a.g) * t);
  result.b = (uint8_t)(a.b + (b.b - a.b) * t);
  return result;
}

static int compare_buckets(const void *a, const void *b) {
  const ColorBucket *ca = (const ColorBucket *)a;
  const ColorBucket *cb = (const ColorBucket *)b;
  /* Prioritize saturation, then population */
  float score_a = ca->saturation * 0.7f + (float)ca->count * 0.3f;
  float score_b = cb->saturation * 0.7f + (float)cb->count * 0.3f;
  if (score_b > score_a)
    return 1;
  if (score_b < score_a)
    return -1;
  return 0;
}

/* Generate Material You palette from seed hue */
static void generate_material_palette(ThemeColors *theme, float hue, float sat,
                                      bool is_dark) {
  theme->seed_hue = hue;
  theme->seed_saturation = sat;

  /* Tertiary hue is shifted +60 degrees (complementary) */
  float tertiary_hue = fmodf(hue + 60.0f, 360.0f);

  /* Neutral has very low saturation */
  float neutral_sat = sat * 0.08f;
  float neutral_var_sat = sat * 0.15f;

  if (is_dark) {
    /* Dark theme tones - more saturated, vibrant colors */
    theme->primary = color_at_tone(hue, sat, 60);
    theme->on_primary = color_at_tone(hue, sat, 10);
    theme->primary_container = color_at_tone(hue, sat, 25);
    theme->on_primary_container = color_at_tone(hue, sat, 90);

    theme->secondary = color_at_tone(hue, sat * 0.6f, 55);
    theme->on_secondary = color_at_tone(hue, sat * 0.6f, 10);
    theme->secondary_container = color_at_tone(hue, sat * 0.6f, 25);
    theme->on_secondary_container = color_at_tone(hue, sat * 0.6f, 90);

    theme->tertiary = color_at_tone(tertiary_hue, sat * 0.8f, 55);
    theme->on_tertiary = color_at_tone(tertiary_hue, sat * 0.8f, 10);
    theme->tertiary_container = color_at_tone(tertiary_hue, sat * 0.8f, 25);
    theme->on_tertiary_container = color_at_tone(tertiary_hue, sat * 0.8f, 90);

    theme->surface = color_at_tone(hue, neutral_sat, 8);
    theme->on_surface = color_at_tone(hue, neutral_sat, 90);
    theme->surface_variant = color_at_tone(hue, neutral_var_sat, 25);
    theme->on_surface_variant = color_at_tone(hue, neutral_var_sat, 80);

    theme->outline = color_at_tone(hue, neutral_var_sat, 50);
    theme->outline_variant = color_at_tone(hue, neutral_var_sat, 25);
  } else {
    /* Light theme tones */
    theme->primary = color_at_tone(hue, sat, 40);
    theme->on_primary = color_at_tone(hue, sat, 100);
    theme->primary_container = color_at_tone(hue, sat, 90);
    theme->on_primary_container = color_at_tone(hue, sat, 10);

    theme->secondary = color_at_tone(hue, sat * 0.3f, 40);
    theme->on_secondary = color_at_tone(hue, sat * 0.3f, 100);
    theme->secondary_container = color_at_tone(hue, sat * 0.3f, 90);
    theme->on_secondary_container = color_at_tone(hue, sat * 0.3f, 10);

    theme->tertiary = color_at_tone(tertiary_hue, sat * 0.6f, 40);
    theme->on_tertiary = color_at_tone(tertiary_hue, sat * 0.6f, 100);
    theme->tertiary_container = color_at_tone(tertiary_hue, sat * 0.6f, 90);
    theme->on_tertiary_container = color_at_tone(tertiary_hue, sat * 0.6f, 10);

    theme->surface = color_at_tone(hue, neutral_sat, 98);
    theme->on_surface = color_at_tone(hue, neutral_sat, 10);
    theme->surface_variant = color_at_tone(hue, neutral_var_sat, 90);
    theme->on_surface_variant = color_at_tone(hue, neutral_var_sat, 30);

    theme->outline = color_at_tone(hue, neutral_var_sat, 50);
    theme->outline_variant = color_at_tone(hue, neutral_var_sat, 80);
  }

  /* Compute luminance values */
  theme->primary_luminance = color_luminance(theme->primary);
  theme->secondary_luminance = color_luminance(theme->secondary);
  theme->tertiary_luminance = color_luminance(theme->tertiary);
  theme->primary_is_light = color_is_light(theme->primary);
  theme->secondary_is_light = color_is_light(theme->secondary);
  theme->tertiary_is_light = color_is_light(theme->tertiary);
}

void extract_theme_colors(const unsigned char *image_data, int width,
                          int height, ThemeColors *out_theme) {
  if (!image_data || !out_theme) {
    log_message("ERROR", "Invalid parameters for color extraction");
    return;
  }

  const int SAMPLE_RATE = 4;
  const int MAX_BUCKETS = 256;
  ColorBucket buckets[MAX_BUCKETS];
  int bucket_count = 0;

  /* Extract and cluster colors */
  for (int y = 0; y < height; y += SAMPLE_RATE) {
    for (int x = 0; x < width; x += SAMPLE_RATE) {
      int idx = (y * width + x) * 4;

      if (image_data[idx + 3] < 10)
        continue;

      Color pixel = {image_data[idx], image_data[idx + 1], image_data[idx + 2]};

      float h, s, l;
      rgb_to_hsl(pixel, &h, &s, &l);

      /* Filter out very dark/light and unsaturated - prefer medium brightness
       */
      if (l < 0.25f || l > 0.80f || s < 0.25f)
        continue;

      bool found = false;
      for (int i = 0; i < bucket_count; i++) {
        int dr = abs(buckets[i].color.r - pixel.r);
        int dg = abs(buckets[i].color.g - pixel.g);
        int db = abs(buckets[i].color.b - pixel.b);

        if (dr < 30 && dg < 30 && db < 30) {
          buckets[i].count++;
          /* Update running average */
          int n = buckets[i].count;
          buckets[i].color.r = (buckets[i].color.r * (n - 1) + pixel.r) / n;
          buckets[i].color.g = (buckets[i].color.g * (n - 1) + pixel.g) / n;
          buckets[i].color.b = (buckets[i].color.b * (n - 1) + pixel.b) / n;
          found = true;
          break;
        }
      }

      if (!found && bucket_count < MAX_BUCKETS) {
        buckets[bucket_count].color = pixel;
        buckets[bucket_count].hue = h;
        buckets[bucket_count].saturation = s;
        buckets[bucket_count].lightness = l;
        buckets[bucket_count].count = 1;
        bucket_count++;
      }
    }
  }

  qsort(buckets, bucket_count, sizeof(ColorBucket), compare_buckets);

  /* Use actual extracted colors from image */
  Color primary_seed = {100, 100, 255};
  Color secondary_seed = {150, 150, 255};
  Color tertiary_seed = {200, 150, 255};
  float seed_hue = 220.0f;
  float seed_sat = 0.5f;

  if (bucket_count > 0) {
    primary_seed = buckets[0].color;
    seed_hue = buckets[0].hue;
    seed_sat = buckets[0].saturation;
  }
  if (bucket_count > 1) {
    secondary_seed = buckets[1].color;
  } else {
    secondary_seed = primary_seed;
  }
  if (bucket_count > 2) {
    tertiary_seed = buckets[2].color;
  } else {
    tertiary_seed = secondary_seed;
  }

  /* Apply tonal adjustments to extracted colors for dark theme */
  /* Tone 60 = medium vibrant for dark backgrounds */
  out_theme->primary = primary_seed;
  out_theme->secondary = secondary_seed;
  out_theme->tertiary = tertiary_seed;

  /* Generate on-colors (contrasting text) */
  out_theme->on_primary = color_is_light(primary_seed) ? (Color){20, 20, 20}
                                                       : (Color){240, 240, 240};
  out_theme->on_secondary = color_is_light(secondary_seed)
                                ? (Color){20, 20, 20}
                                : (Color){240, 240, 240};
  out_theme->on_tertiary = color_is_light(tertiary_seed)
                               ? (Color){20, 20, 20}
                               : (Color){240, 240, 240};

  /* Container colors - darker versions */
  out_theme->primary_container =
      color_lerp(primary_seed, (Color){0, 0, 0}, 0.6f);
  out_theme->secondary_container =
      color_lerp(secondary_seed, (Color){0, 0, 0}, 0.6f);
  out_theme->tertiary_container =
      color_lerp(tertiary_seed, (Color){0, 0, 0}, 0.6f);

  out_theme->on_primary_container =
      color_lerp(primary_seed, (Color){255, 255, 255}, 0.3f);
  out_theme->on_secondary_container =
      color_lerp(secondary_seed, (Color){255, 255, 255}, 0.3f);
  out_theme->on_tertiary_container =
      color_lerp(tertiary_seed, (Color){255, 255, 255}, 0.3f);

  /* Surface colors with subtle tint from primary */
  out_theme->surface = (Color){20, 18, 22};
  out_theme->on_surface = (Color){230, 225, 235};
  out_theme->surface_variant =
      color_lerp(out_theme->surface, primary_seed, 0.1f);
  out_theme->on_surface_variant = (Color){200, 195, 210};

  out_theme->outline = color_lerp(primary_seed, (Color){128, 128, 128}, 0.5f);
  out_theme->outline_variant =
      color_lerp(primary_seed, (Color){60, 60, 60}, 0.7f);

  /* Store metadata */
  out_theme->seed_hue = seed_hue;
  out_theme->seed_saturation = seed_sat;
  out_theme->primary_luminance = color_luminance(out_theme->primary);
  out_theme->secondary_luminance = color_luminance(out_theme->secondary);
  out_theme->tertiary_luminance = color_luminance(out_theme->tertiary);
  out_theme->primary_is_light = color_is_light(out_theme->primary);
  out_theme->secondary_is_light = color_is_light(out_theme->secondary);
  out_theme->tertiary_is_light = color_is_light(out_theme->tertiary);

  char log_msg[256];
  snprintf(log_msg, sizeof(log_msg),
           "Material You - Primary: (%d,%d,%d), Secondary: (%d,%d,%d), "
           "Tertiary: (%d,%d,%d)",
           out_theme->primary.r, out_theme->primary.g, out_theme->primary.b,
           out_theme->secondary.r, out_theme->secondary.g,
           out_theme->secondary.b, out_theme->tertiary.r, out_theme->tertiary.g,
           out_theme->tertiary.b);
  log_message("INFO", log_msg);
}
