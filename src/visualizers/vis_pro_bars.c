#include "builtin_visualizers.h"
#include "visualizer_interface.h"
#include <SDL2/SDL.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* ═══════════════════════════════════════════════════════════════════════════
 * Pro Bars Visualizer
 * ═══════════════════════════════════════════════════════════════════════════
 * A professional-grade bars visualizer with extensive customization options.
 * Features: FFT-style bars, peak indicators, glow effects, color gradients,
 * mirroring, spacing controls, and animation smoothing.
 */

/* ─────────────────────────────────────────────────────────────────────────────
 * Global Parameters (Tweakable via UI)
 * ─────────────────────────────────────────────────────────────────────────────
 */

// === General Settings ===
static int g_bar_count = 64; // Number of bars
static float g_bar_spacing =
    0.3f; // Space between bars (0.0 = touching, 1.0 = max gap)
static float g_bar_height_scale = 1.0f; // Overall height multiplier
static float g_sensitivity = 1.5f;      // Audio sensitivity
static float g_smoothness = 0.7f; // Smoothing factor (higher = slower decay)

// === Bar Shape ===
static int g_bar_style =
    0; // 0: Solid, 1: Outlined, 2: Gradient Fill, 3: Segmented
static float g_corner_radius =
    0.0f; // Corner rounding (0.0 = sharp, 1.0 = max round)
static int g_segment_count = 8;     // For segmented style - number of segments
static float g_segment_gap = 0.15f; // Gap between segments

// === Peak Indicators ===
static bool g_show_peaks = true;      // Show peak indicator dots
static float g_peak_decay = 0.02f;    // How fast peaks fall
static float g_peak_hold_time = 0.5f; // Seconds to hold peak at top
static int g_peak_thickness = 3;      // Peak indicator height

// === Color Settings ===
static int g_color_mode = 0;              // Color mode selection
static float g_gradient_intensity = 0.7f; // Gradient strength for bar fill
static float g_glow_intensity = 0.0f;     // Glow effect intensity (0 = off)
static float g_opacity = 1.0f;            // Overall opacity

// === Layout & Mirroring ===
static int g_alignment = 1;   // 0: Top, 1: Bottom, 2: Center
static int g_mirror_mode = 0; // 0: Off, 1: Horizontal, 2: Vertical, 3: Both
static float g_horizontal_padding = 0.02f; // Padding from edges (percentage)
static float g_vertical_padding = 0.05f;   // Padding from top/bottom

// === Animation ===
static float g_animation_speed = 1.0f; // Speed of animated effects
static int g_frequency_scale = 0;      // 0: Linear, 1: Log (emphasize bass)
static float g_bass_boost = 1.0f;      // Extra boost for low frequencies

/* ─────────────────────────────────────────────────────────────────────────────
 * Internal State
 * ─────────────────────────────────────────────────────────────────────────────
 */
static float *g_bar_values = NULL;  // Current bar heights (smoothed)
static float *g_peak_values = NULL; // Peak heights
static float *g_peak_timers = NULL; // Peak hold timers
static size_t g_buffer_size = 0;

/* ─────────────────────────────────────────────────────────────────────────────
 * Option String Arrays
 * ─────────────────────────────────────────────────────────────────────────────
 */
static const char *BAR_STYLE_OPTIONS[] = {"Solid", "Outlined", "Gradient Fill",
                                          "Segmented", NULL};

static const char *COLOR_MODE_OPTIONS[] = {"Default (White)",
                                           "Theme Primary",
                                           "Theme Secondary",
                                           "Theme Tertiary",
                                           "Theme Surface",
                                           "Rainbow Spectrum",
                                           "Heat Map",
                                           "Ocean Wave",
                                           "Neon Pulse",
                                           "Frequency Gradient",
                                           NULL};

static const char *ALIGNMENT_OPTIONS[] = {"Top", "Bottom", "Center", NULL};

static const char *MIRROR_MODE_OPTIONS[] = {"Off", "Horizontal", "Vertical",
                                            "Both", NULL};

static const char *FREQUENCY_SCALE_OPTIONS[] = {"Linear", "Logarithmic", NULL};

/* ═══════════════════════════════════════════════════════════════════════════
 * Lifecycle Functions
 * ═══════════════════════════════════════════════════════════════════════════
 */

static void pro_bars_init(int width, int height) {
  (void)width;
  (void)height;
  g_bar_values = NULL;
  g_peak_values = NULL;
  g_peak_timers = NULL;
  g_buffer_size = 0;
}

static void pro_bars_cleanup(void) {
  if (g_bar_values) {
    free(g_bar_values);
    g_bar_values = NULL;
  }
  if (g_peak_values) {
    free(g_peak_values);
    g_peak_values = NULL;
  }
  if (g_peak_timers) {
    free(g_peak_timers);
    g_peak_timers = NULL;
  }
  g_buffer_size = 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Parameter Interface
 * ═══════════════════════════════════════════════════════════════════════════
 */

static int pro_bars_get_param_count(void) {
  return 23; // Total number of tweakable parameters
}

static const VisParam *pro_bars_get_param(int index) {
  static VisParam params[24];

  // === General Settings ===
  params[0].name = "Bar Count";
  params[0].type = VIS_PARAM_INT;
  params[0].value_ptr = &g_bar_count;
  params[0].min = 8.0f;
  params[0].max = 256.0f;

  params[1].name = "Bar Spacing";
  params[1].type = VIS_PARAM_FLOAT;
  params[1].value_ptr = &g_bar_spacing;
  params[1].min = 0.0f;
  params[1].max = 0.9f;

  params[2].name = "Height Scale";
  params[2].type = VIS_PARAM_FLOAT;
  params[2].value_ptr = &g_bar_height_scale;
  params[2].min = 0.1f;
  params[2].max = 3.0f;

  params[3].name = "Sensitivity";
  params[3].type = VIS_PARAM_FLOAT;
  params[3].value_ptr = &g_sensitivity;
  params[3].min = 0.1f;
  params[3].max = 5.0f;

  params[4].name = "Smoothness";
  params[4].type = VIS_PARAM_FLOAT;
  params[4].value_ptr = &g_smoothness;
  params[4].min = 0.0f;
  params[4].max = 0.98f;

  // === Bar Shape ===
  params[5].name = "Bar Style";
  params[5].type = VIS_PARAM_ENUM;
  params[5].value_ptr = &g_bar_style;
  params[5].options = BAR_STYLE_OPTIONS;

  params[6].name = "Corner Radius";
  params[6].type = VIS_PARAM_FLOAT;
  params[6].value_ptr = &g_corner_radius;
  params[6].min = 0.0f;
  params[6].max = 1.0f;

  params[7].name = "Segment Count";
  params[7].type = VIS_PARAM_INT;
  params[7].value_ptr = &g_segment_count;
  params[7].min = 2.0f;
  params[7].max = 32.0f;

  params[8].name = "Segment Gap";
  params[8].type = VIS_PARAM_FLOAT;
  params[8].value_ptr = &g_segment_gap;
  params[8].min = 0.0f;
  params[8].max = 0.5f;

  // === Peak Indicators ===
  params[9].name = "Show Peaks";
  params[9].type = VIS_PARAM_BOOL;
  params[9].value_ptr = &g_show_peaks;

  params[10].name = "Peak Decay";
  params[10].type = VIS_PARAM_FLOAT;
  params[10].value_ptr = &g_peak_decay;
  params[10].min = 0.001f;
  params[10].max = 0.2f;

  params[11].name = "Peak Hold Time";
  params[11].type = VIS_PARAM_FLOAT;
  params[11].value_ptr = &g_peak_hold_time;
  params[11].min = 0.0f;
  params[11].max = 2.0f;

  params[12].name = "Peak Thickness";
  params[12].type = VIS_PARAM_INT;
  params[12].value_ptr = &g_peak_thickness;
  params[12].min = 1.0f;
  params[12].max = 10.0f;

  // === Color Settings ===
  params[13].name = "Color Mode";
  params[13].type = VIS_PARAM_ENUM;
  params[13].value_ptr = &g_color_mode;
  params[13].options = COLOR_MODE_OPTIONS;

  params[14].name = "Gradient Intensity";
  params[14].type = VIS_PARAM_FLOAT;
  params[14].value_ptr = &g_gradient_intensity;
  params[14].min = 0.0f;
  params[14].max = 1.0f;

  params[15].name = "Glow Intensity";
  params[15].type = VIS_PARAM_FLOAT;
  params[15].value_ptr = &g_glow_intensity;
  params[15].min = 0.0f;
  params[15].max = 1.0f;

  params[16].name = "Opacity";
  params[16].type = VIS_PARAM_FLOAT;
  params[16].value_ptr = &g_opacity;
  params[16].min = 0.1f;
  params[16].max = 1.0f;

  // === Layout & Mirroring ===
  params[17].name = "Alignment";
  params[17].type = VIS_PARAM_ENUM;
  params[17].value_ptr = &g_alignment;
  params[17].options = ALIGNMENT_OPTIONS;

  params[18].name = "Mirror Mode";
  params[18].type = VIS_PARAM_ENUM;
  params[18].value_ptr = &g_mirror_mode;
  params[18].options = MIRROR_MODE_OPTIONS;

  params[19].name = "Horizontal Padding";
  params[19].type = VIS_PARAM_FLOAT;
  params[19].value_ptr = &g_horizontal_padding;
  params[19].min = 0.0f;
  params[19].max = 0.25f;

  params[20].name = "Vertical Padding";
  params[20].type = VIS_PARAM_FLOAT;
  params[20].value_ptr = &g_vertical_padding;
  params[20].min = 0.0f;
  params[20].max = 0.25f;

  // === Animation & Frequency ===
  params[21].name = "Animation Speed";
  params[21].type = VIS_PARAM_FLOAT;
  params[21].value_ptr = &g_animation_speed;
  params[21].min = 0.1f;
  params[21].max = 3.0f;

  params[22].name = "Frequency Scale";
  params[22].type = VIS_PARAM_ENUM;
  params[22].value_ptr = &g_frequency_scale;
  params[22].options = FREQUENCY_SCALE_OPTIONS;

  if (index >= 0 && index < 23) {
    return &params[index];
  }
  return NULL;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Color Utility Functions
 * ═══════════════════════════════════════════════════════════════════════════
 */

static void hsv_to_rgb(float h, float s, float v, uint8_t *r, uint8_t *g,
                       uint8_t *b) {
  float c = v * s;
  float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
  float m = v - c;
  float rf, gf, bf;

  if (h < 60.0f) {
    rf = c;
    gf = x;
    bf = 0;
  } else if (h < 120.0f) {
    rf = x;
    gf = c;
    bf = 0;
  } else if (h < 180.0f) {
    rf = 0;
    gf = c;
    bf = x;
  } else if (h < 240.0f) {
    rf = 0;
    gf = x;
    bf = c;
  } else if (h < 300.0f) {
    rf = x;
    gf = 0;
    bf = c;
  } else {
    rf = c;
    gf = 0;
    bf = x;
  }

  *r = (uint8_t)((rf + m) * 255.0f);
  *g = (uint8_t)((gf + m) * 255.0f);
  *b = (uint8_t)((bf + m) * 255.0f);
}

static void get_bar_color(int bar_index, int total_bars, float bar_height,
                          const ThemeColors *theme, float time, uint8_t *r,
                          uint8_t *g, uint8_t *b) {
  float position = (float)bar_index / (float)total_bars; // 0.0 to 1.0

  switch (g_color_mode) {
  case 0: // Default White
    *r = 255;
    *g = 255;
    *b = 255;
    break;

  case 1: // Theme Primary
    if (theme) {
      *r = theme->primary.r;
      *g = theme->primary.g;
      *b = theme->primary.b;
    } else {
      *r = 255;
      *g = 100;
      *b = 100;
    }
    break;

  case 2: // Theme Secondary
    if (theme) {
      *r = theme->secondary.r;
      *g = theme->secondary.g;
      *b = theme->secondary.b;
    } else {
      *r = 100;
      *g = 255;
      *b = 100;
    }
    break;

  case 3: // Theme Tertiary
    if (theme) {
      *r = theme->tertiary.r;
      *g = theme->tertiary.g;
      *b = theme->tertiary.b;
    } else {
      *r = 100;
      *g = 100;
      *b = 255;
    }
    break;

  case 4: // Theme Surface
    if (theme) {
      *r = theme->surface.r;
      *g = theme->surface.g;
      *b = theme->surface.b;
    } else {
      *r = 80;
      *g = 80;
      *b = 80;
    }
    break;

  case 5: // Rainbow Spectrum
    hsv_to_rgb(position * 360.0f, 1.0f, 1.0f, r, g, b);
    break;

  case 6: // Heat Map (blue -> green -> yellow -> red based on height)
  {
    float h = bar_height; // clamp to 0-1
    if (h < 0.0f)
      h = 0.0f;
    if (h > 1.0f)
      h = 1.0f;
    // Blue (240) -> Cyan (180) -> Green (120) -> Yellow (60) -> Red (0)
    float hue = 240.0f - (h * 240.0f);
    hsv_to_rgb(hue, 1.0f, 1.0f, r, g, b);
  } break;

  case 7: // Ocean Wave
  {
    float wave =
        sinf(position * 3.14159f * 2.0f + time * g_animation_speed) * 0.5f +
        0.5f;
    *r = (uint8_t)(50 + wave * 80);
    *g = (uint8_t)(150 + wave * 50);
    *b = (uint8_t)(200 + wave * 55);
  } break;

  case 8: // Neon Pulse
  {
    float pulse = sinf(time * g_animation_speed * 3.0f) * 0.3f + 0.7f;
    float hue =
        fmodf(position * 180.0f + time * 50.0f * g_animation_speed, 360.0f);
    hsv_to_rgb(hue, 1.0f, pulse, r, g, b);
  } break;

  case 9: // Frequency Gradient (Bass=Red, Mid=Green, Treble=Blue)
  {
    if (position < 0.33f) {
      // Red to Orange
      *r = 255;
      *g = (uint8_t)(position * 3.0f * 165);
      *b = 0;
    } else if (position < 0.66f) {
      // Yellow to Green to Cyan
      float t = (position - 0.33f) * 3.0f;
      *r = (uint8_t)(255 - t * 255);
      *g = 255;
      *b = (uint8_t)(t * 255);
    } else {
      // Cyan to Blue to Purple
      float t = (position - 0.66f) * 3.0f;
      *r = (uint8_t)(t * 128);
      *g = (uint8_t)(255 - t * 255);
      *b = 255;
    }
  } break;

  default:
    *r = 255;
    *g = 255;
    *b = 255;
    break;
  }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Drawing Helpers
 * ═══════════════════════════════════════════════════════════════════════════
 */

static void draw_rounded_rect(SDL_Renderer *renderer, int x, int y, int w,
                              int h, float radius_factor) {
  int radius = (int)(fminf(w, h) * 0.5f * radius_factor);
  if (radius < 1 || radius_factor < 0.01f) {
    SDL_Rect rect = {x, y, w, h};
    SDL_RenderFillRect(renderer, &rect);
    return;
  }

  // Draw rounded rect as a collection of scanline rects (single batch call)
  SDL_Rect rects[66];
  int rect_count = 0;

  // Center body
  if (h - 2 * radius > 0) {
    rects[rect_count++] = (SDL_Rect){x, y + radius, w, h - 2 * radius};
  }

  // Top and bottom rows
  for (int r = 0; r < radius; r++) {
    int dy = radius - 1 - r;
    int dx = (int)sqrtf(radius * radius - dy * dy);
    int offset = radius - dx;

    // Top row
    rects[rect_count++] = (SDL_Rect){x + offset, y + r, w - 2 * offset, 1};
    // Bottom row
    rects[rect_count++] = (SDL_Rect){x + offset, y + h - 1 - r, w - 2 * offset, 1};

    if (rect_count >= 64) {
      break;
    }
  }

  SDL_RenderFillRects(renderer, rects, rect_count);
}

static void draw_bar_glow(SDL_Renderer *renderer, int x, int y, int w, int h,
                          uint8_t r, uint8_t g, uint8_t b) {
  if (g_glow_intensity < 0.01f)
    return;

  int glow_layers = 3;
  for (int layer = glow_layers; layer > 0; layer--) {
    float alpha_factor = (float)layer / (float)glow_layers;
    uint8_t alpha = (uint8_t)(50 * g_glow_intensity * alpha_factor);
    int expand = layer * 2;

    SDL_SetRenderDrawColor(renderer, r, g, b, alpha);
    SDL_Rect glow_rect = {x - expand, y - expand, w + expand * 2,
                          h + expand * 2};
    SDL_RenderFillRect(renderer, &glow_rect);
  }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Main Render Function
 * ═══════════════════════════════════════════════════════════════════════════
 */

static void pro_bars_render(SDL_Renderer *renderer, const float *audio_data,
                            size_t sample_count, int x, int y, int w, int h,
                            const ThemeColors *theme) {
  if (!audio_data || sample_count == 0 || g_bar_count < 1)
    return;

  // Ensure buffers are allocated
  if (g_buffer_size != (size_t)g_bar_count) {
    g_bar_values = realloc(g_bar_values, sizeof(float) * g_bar_count);
    g_peak_values = realloc(g_peak_values, sizeof(float) * g_bar_count);
    g_peak_timers = realloc(g_peak_timers, sizeof(float) * g_bar_count);

    for (int i = 0; i < g_bar_count; i++) {
      g_bar_values[i] = 0.0f;
      g_peak_values[i] = 0.0f;
      g_peak_timers[i] = 0.0f;
    }
    g_buffer_size = g_bar_count;
  }

  float time = (float)SDL_GetTicks() * 0.001f;
  float dt = 0.016f; // Assume ~60fps for timing

  // Calculate working area with padding
  int pad_x = (int)(w * g_horizontal_padding);
  int pad_y = (int)(h * g_vertical_padding);
  int work_x = x + pad_x;
  int work_y = y + pad_y;
  int work_w = w - 2 * pad_x;
  int work_h = h - 2 * pad_y;

  // Adjust for mirror modes
  int bars_to_draw = g_bar_count;
  int actual_work_w = work_w;

  if (g_mirror_mode == 1 || g_mirror_mode == 3) { // Horizontal mirror
    actual_work_w = work_w / 2;
  }

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

  // Calculate bar dimensions
  float total_bar_width = (float)actual_work_w / (float)bars_to_draw;
  float bar_width = total_bar_width * (1.0f - g_bar_spacing);
  float gap_width = total_bar_width * g_bar_spacing;

  if (bar_width < 1.0f)
    bar_width = 1.0f;

  // Process audio and draw bars
  float samples_per_bar = (float)sample_count / (float)bars_to_draw;

  for (int i = 0; i < bars_to_draw; i++) {
    // Get audio value for this bar
    int sample_start, sample_end;

    if (g_frequency_scale == 1) { // Logarithmic
      float log_pos = (float)i / (float)bars_to_draw;
      float log_scaled = powf(log_pos, 2.0f); // Emphasize lower frequencies
      sample_start = (int)(log_scaled * sample_count);

      float next_log_pos = (float)(i + 1) / (float)bars_to_draw;
      float next_log_scaled = powf(next_log_pos, 2.0f);
      sample_end = (int)(next_log_scaled * sample_count);
    } else { // Linear
      sample_start = (int)(i * samples_per_bar);
      sample_end = (int)((i + 1) * samples_per_bar);
    }

    if (sample_end >= (int)sample_count)
      sample_end = (int)sample_count - 1;
    if (sample_start < 0)
      sample_start = 0;
    if (sample_end < sample_start)
      sample_end = sample_start;

    // Calculate average amplitude for this bar
    float sum = 0.0f;
    int count = 0;
    for (int s = sample_start; s <= sample_end && s < (int)sample_count; s++) {
      sum += fabsf(audio_data[s]);
      count++;
    }
    float amplitude = (count > 0) ? (sum / count) : 0.0f;

    // Apply bass boost for lower frequency bars
    if (i < bars_to_draw / 4) {
      float bass_factor =
          1.0f + (g_bass_boost - 1.0f) *
                     (1.0f - (float)i / ((float)bars_to_draw / 4.0f));
      amplitude *= bass_factor;
    }

    amplitude *= g_sensitivity;

    // Apply smoothing
    g_bar_values[i] =
        g_bar_values[i] * g_smoothness + amplitude * (1.0f - g_smoothness);

    // Clamp
    if (g_bar_values[i] > 1.0f)
      g_bar_values[i] = 1.0f;
    if (g_bar_values[i] < 0.0f)
      g_bar_values[i] = 0.0f;

    // Update peak
    if (g_bar_values[i] > g_peak_values[i]) {
      g_peak_values[i] = g_bar_values[i];
      g_peak_timers[i] = g_peak_hold_time;
    } else {
      g_peak_timers[i] -= dt;
      if (g_peak_timers[i] < 0.0f) {
        g_peak_values[i] -= g_peak_decay * g_animation_speed;
        if (g_peak_values[i] < 0.0f)
          g_peak_values[i] = 0.0f;
      }
    }

    // Calculate bar height
    float bar_height_normalized = g_bar_values[i] * g_bar_height_scale;
    if (bar_height_normalized > 1.0f)
      bar_height_normalized = 1.0f;

    int max_bar_height = work_h;
    if (g_mirror_mode == 2 || g_mirror_mode == 3) { // Vertical mirror
      max_bar_height = work_h / 2;
    }

    int bar_h = (int)(bar_height_normalized * max_bar_height);
    int bar_w = (int)bar_width;
    if (bar_w < 1)
      bar_w = 1;

    // Calculate bar position
    int bar_x = work_x + (int)(i * total_bar_width + gap_width * 0.5f);
    int bar_y;

    switch (g_alignment) {
    case 0: // Top
      bar_y = work_y;
      break;
    case 2: // Center
      bar_y = work_y + (work_h - bar_h) / 2;
      break;
    case 1: // Bottom (default)
    default:
      bar_y = work_y + work_h - bar_h;
      break;
    }

    // Get color for this bar
    uint8_t r, g_col, b;
    get_bar_color(i, bars_to_draw, bar_height_normalized, theme, time, &r,
                  &g_col, &b);
    uint8_t alpha = (uint8_t)(g_opacity * 255.0f);

    // Draw glow effect first (behind bar)
    draw_bar_glow(renderer, bar_x, bar_y, bar_w, bar_h, r, g_col, b);

    // Draw main bar based on style
    SDL_SetRenderDrawColor(renderer, r, g_col, b, alpha);

    if (g_bar_style == 0) { // Solid
      if (g_corner_radius > 0.01f) {
        draw_rounded_rect(renderer, bar_x, bar_y, bar_w, bar_h,
                          g_corner_radius);
      } else {
        SDL_Rect bar_rect = {bar_x, bar_y, bar_w, bar_h};
        SDL_RenderFillRect(renderer, &bar_rect);
      }
    } else if (g_bar_style == 1) { // Outlined
      SDL_Rect bar_rect = {bar_x, bar_y, bar_w, bar_h};
      SDL_RenderDrawRect(renderer, &bar_rect);
    } else if (g_bar_style == 2) { // Gradient Fill
      for (int row = 0; row < bar_h; row++) {
        float row_factor = (float)row / (float)bar_h;
        float dimming = 1.0f - (row_factor * g_gradient_intensity);
        SDL_SetRenderDrawColor(renderer, (uint8_t)(r * dimming),
                               (uint8_t)(g_col * dimming),
                               (uint8_t)(b * dimming), alpha);
        SDL_RenderDrawLine(renderer, bar_x, bar_y + row, bar_x + bar_w - 1,
                           bar_y + row);
      }
    } else if (g_bar_style == 3) { // Segmented
      int seg_h = bar_h / g_segment_count;
      if (seg_h < 2)
        seg_h = 2;
      int seg_gap = (int)(seg_h * g_segment_gap);

      SDL_Rect seg_rects[64];
      int seg_to_draw = 0;

      for (int seg = 0; seg < g_segment_count && seg < 64; seg++) {
        int seg_y = bar_y + bar_h - (seg + 1) * seg_h + seg_gap / 2;
        if (seg_y < bar_y)
          break;

        SDL_Rect seg_rect = {bar_x, seg_y, bar_w, seg_h - seg_gap};
        if (seg_rect.h > 0) {
          seg_rects[seg_to_draw++] = seg_rect;
        }
      }
      if (seg_to_draw > 0) {
        SDL_RenderFillRects(renderer, seg_rects, seg_to_draw);
      }
    }

    // Draw peak indicator
    if (g_show_peaks && g_peak_values[i] > 0.01f) {
      int peak_y = bar_y + bar_h - (int)(g_peak_values[i] * max_bar_height);
      if (g_alignment == 0) { // Top
        peak_y = work_y + (int)(g_peak_values[i] * max_bar_height) -
                 g_peak_thickness;
      }

      SDL_SetRenderDrawColor(renderer, 255, 255, 255, alpha);
      SDL_Rect peak_rect = {bar_x, peak_y, bar_w, g_peak_thickness};
      SDL_RenderFillRect(renderer, &peak_rect);
    }

    // Handle horizontal mirroring
    if (g_mirror_mode == 1 || g_mirror_mode == 3) {
      int mirror_x = work_x + work_w - (bar_x - work_x) - bar_w;

      SDL_SetRenderDrawColor(renderer, r, g_col, b, alpha);
      draw_bar_glow(renderer, mirror_x, bar_y, bar_w, bar_h, r, g_col, b);

      if (g_bar_style == 0 || g_bar_style == 2) {
        SDL_Rect mirror_rect = {mirror_x, bar_y, bar_w, bar_h};
        SDL_RenderFillRect(renderer, &mirror_rect);
      } else if (g_bar_style == 1) {
        SDL_Rect mirror_rect = {mirror_x, bar_y, bar_w, bar_h};
        SDL_RenderDrawRect(renderer, &mirror_rect);
      }

      if (g_show_peaks && g_peak_values[i] > 0.01f) {
        int peak_y = bar_y + bar_h - (int)(g_peak_values[i] * max_bar_height);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, alpha);
        SDL_Rect peak_rect = {mirror_x, peak_y, bar_w, g_peak_thickness};
        SDL_RenderFillRect(renderer, &peak_rect);
      }
    }

    // Handle vertical mirroring
    if (g_mirror_mode == 2 || g_mirror_mode == 3) {
      int center_y = work_y + work_h / 2;
      int mirror_bar_y = center_y;

      SDL_SetRenderDrawColor(renderer, r, g_col, b, alpha);
      draw_bar_glow(renderer, bar_x, mirror_bar_y, bar_w, bar_h, r, g_col, b);

      SDL_Rect mirror_rect = {bar_x, mirror_bar_y, bar_w, bar_h};
      SDL_RenderFillRect(renderer, &mirror_rect);

      // Top bar (grows upward)
      int top_bar_y = center_y - bar_h;
      SDL_Rect top_rect = {bar_x, top_bar_y, bar_w, bar_h};
      SDL_RenderFillRect(renderer, &top_rect);

      // Also horizontal mirror if both enabled
      if (g_mirror_mode == 3) {
        int mirror_x = work_x + work_w - (bar_x - work_x) - bar_w;
        SDL_Rect hv_rect1 = {mirror_x, mirror_bar_y, bar_w, bar_h};
        SDL_Rect hv_rect2 = {mirror_x, top_bar_y, bar_w, bar_h};
        SDL_RenderFillRect(renderer, &hv_rect1);
        SDL_RenderFillRect(renderer, &hv_rect2);
      }
    }
  }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Plugin Export
 * ═══════════════════════════════════════════════════════════════════════════
 */

const VisPlugin g_vis_pro_bars = {.name = "Pro Bars",
                                  .author = "Harmony Team",
                                  .init = pro_bars_init,
                                  .render = pro_bars_render,
                                  .cleanup = pro_bars_cleanup,
                                  .resize = NULL,
                                  .get_param_count = pro_bars_get_param_count,
                                  .get_param = pro_bars_get_param};

/* Required export function for dynamic loading */
const VisPlugin *visualizer_get_info(void) { return &g_vis_pro_bars; }
