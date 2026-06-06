#include "builtin_visualizers.h"
#include "visualizer_interface.h"
#include <SDL2/SDL.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>

static float g_amplitude_scale = 1.0f;
static int g_thickness = 2;
static float g_sensitivity = 1.0f;
static float g_smoothness = 0.5f;
static int g_resolution = 512;
static int g_draw_style = 0;   // 0: Lines, 1: Points, 2: Bars
static int g_color_source = 0; // 0: Default, 1: Primary, 2: Secondary...
static bool g_mirror_mode = false;
static float g_opacity = 1.0f;

static float *g_smooth_buffer = NULL;
static size_t g_smooth_size = 0;

static const char *DRAW_STYLE_OPTIONS[] = {"Lines", "Points", "Bars", NULL};
static const char *COLOR_SOURCE_OPTIONS[] = {"Default (White)",
                                             "Theme Primary",
                                             "Theme Secondary",
                                             "Theme Tertiary",
                                             "Theme Surface",
                                             "Dynamic Rainbow",
                                             NULL};

static void basic_init(int width, int height) {
  (void)width;
  (void)height;
  g_smooth_buffer = NULL;
  g_smooth_size = 0;
}

static void basic_cleanup(void) {
  if (g_smooth_buffer) {
    free(g_smooth_buffer);
    g_smooth_buffer = NULL;
  }
}

/* Parameter Interface */
static int basic_get_param_count(void) { return 9; }

static const VisParam *basic_get_param(int index) {
  static VisParam params[10];

  params[0].name = "Amplitude";
  params[0].type = VIS_PARAM_FLOAT;
  params[0].value_ptr = &g_amplitude_scale;
  params[0].min = 0.1f;
  params[0].max = 5.0f;

  params[1].name = "Thickness";
  params[1].type = VIS_PARAM_INT;
  params[1].value_ptr = &g_thickness;
  params[1].min = 1.0f;
  params[1].max = 10.0f;

  params[2].name = "Sensitivity";
  params[2].type = VIS_PARAM_FLOAT;
  params[2].value_ptr = &g_sensitivity;
  params[2].min = 0.1f;
  params[2].max = 3.0f;

  params[3].name = "Smoothness";
  params[3].type = VIS_PARAM_FLOAT;
  params[3].value_ptr = &g_smoothness;
  params[3].min = 0.0f;
  params[3].max = 0.95f;

  params[4].name = "Resolution";
  params[4].type = VIS_PARAM_INT;
  params[4].value_ptr = &g_resolution;
  params[4].min = 64.0f;
  params[4].max = 2048.0f;

  params[5].name = "Draw Style";
  params[5].type = VIS_PARAM_ENUM;
  params[5].value_ptr = &g_draw_style;
  params[5].options = DRAW_STYLE_OPTIONS;

  params[6].name = "Color Source";
  params[6].type = VIS_PARAM_ENUM;
  params[6].value_ptr = &g_color_source;
  params[6].options = COLOR_SOURCE_OPTIONS;

  params[7].name = "Mirror Mode";
  params[7].type = VIS_PARAM_BOOL;
  params[7].value_ptr = &g_mirror_mode;

  params[8].name = "Opacity";
  params[8].type = VIS_PARAM_FLOAT;
  params[8].value_ptr = &g_opacity;
  params[8].min = 0.1f;
  params[8].max = 1.0f;

  if (index >= 0 && index < 9) {
    return &params[index];
  }
  return NULL;
}

static void basic_render(SDL_Renderer *renderer, const float *audio_data,
                         size_t sample_count, int x, int y, int w, int h,
                         const ThemeColors *theme) {
  if (!audio_data || sample_count == 0)
    return;

  // Smoothing Logic
  if (g_smooth_size != (size_t)g_resolution) {
    g_smooth_buffer = realloc(g_smooth_buffer, sizeof(float) * g_resolution);
    for (int i = 0; i < g_resolution; i++)
      g_smooth_buffer[i] = 0;
    g_smooth_size = g_resolution;
  }

  // Set color based on theme and color source
  uint8_t r = 255, g = 255, b = 255;
  if (g_color_source == 1 && theme) {
    r = theme->primary.r;
    g = theme->primary.g;
    b = theme->primary.b;
  } else if (g_color_source == 2 && theme) {
    r = theme->secondary.r;
    g = theme->secondary.g;
    b = theme->secondary.b;
  } else if (g_color_source == 3 && theme) {
    r = theme->tertiary.r;
    g = theme->tertiary.g;
    b = theme->tertiary.b;
  } else if (g_color_source == 4 && theme) {
    r = theme->surface.r;
    g = theme->surface.g;
    b = theme->surface.b;
  } else if (g_color_source == 5) {
    float t = (float)SDL_GetTicks() * 0.001f;
    r = (uint8_t)(sinf(t) * 127 + 128);
    g = (uint8_t)(sinf(t + 2.0f) * 127 + 128);
    b = (uint8_t)(sinf(t + 4.0f) * 127 + 128);
  }
  uint8_t a = (uint8_t)(g_opacity * 255.0f);
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, r, g, b, a);

  int cy = y + h / 2;
  float step = (float)sample_count / (float)g_resolution;
  float x_step = (float)w / (float)g_resolution;

  if (g_mirror_mode) {
    x_step *= 0.5f;
  }

  int res_limit = g_resolution;
  if (res_limit > 2048) res_limit = 2048;
  if (res_limit < 1) res_limit = 1;

  int *computed_x = malloc(sizeof(int) * res_limit);
  int *computed_y = malloc(sizeof(int) * res_limit);
  if (!computed_x || !computed_y) {
    if (computed_x) free(computed_x);
    if (computed_y) free(computed_y);
    return;
  }

  for (int i = 0; i < res_limit; i++) {
    int idx = (int)(i * step);
    if (idx >= (int)sample_count)
      idx = (int)sample_count - 1;

    float sample = audio_data[idx] * g_amplitude_scale * g_sensitivity;

    // Apply smoothing
    g_smooth_buffer[i] =
        g_smooth_buffer[i] * g_smoothness + sample * (1.0f - g_smoothness);

    float val = g_smooth_buffer[i];
    int py = cy + (int)(val * (h * 0.45f));

    // Clamp
    if (py < y)
      py = y;
    if (py > y + h)
      py = y + h;

    computed_x[i] = x + (int)(i * x_step);
    computed_y[i] = py;
  }

  // Draw style 0: Lines
  if (g_draw_style == 0) {
    SDL_Point *pts = malloc(sizeof(SDL_Point) * res_limit);
    if (pts) {
      for (int t = 0; t < g_thickness; t++) {
        int offset = t - g_thickness / 2;
        for (int i = 0; i < res_limit; i++) {
          pts[i].x = computed_x[i];
          pts[i].y = computed_y[i] + offset;
        }
        SDL_RenderDrawLines(renderer, pts, res_limit);

        if (g_mirror_mode) {
          for (int i = 0; i < res_limit; i++) {
            pts[i].x = (x + w) - (computed_x[i] - x);
            pts[i].y = computed_y[i] + offset;
          }
          SDL_RenderDrawLines(renderer, pts, res_limit);
        }
      }
      free(pts);
    }
  }
  // Draw style 1: Points
  else if (g_draw_style == 1) {
    int max_rects = g_mirror_mode ? res_limit * 2 : res_limit;
    SDL_Rect *pts = malloc(sizeof(SDL_Rect) * max_rects);
    if (pts) {
      int rect_count = 0;
      for (int i = 0; i < res_limit; i++) {
        pts[rect_count++] = (SDL_Rect){
          computed_x[i] - g_thickness / 2,
          computed_y[i] - g_thickness / 2,
          g_thickness,
          g_thickness
        };
        if (g_mirror_mode) {
          pts[rect_count++] = (SDL_Rect){
            (x + w) - (computed_x[i] - x) - g_thickness / 2,
            computed_y[i] - g_thickness / 2,
            g_thickness,
            g_thickness
          };
        }
      }
      SDL_RenderFillRects(renderer, pts, rect_count);
      free(pts);
    }
  }
  // Draw style 2: Bars
  else if (g_draw_style == 2) {
    int max_rects = g_mirror_mode ? res_limit * 2 : res_limit;
    SDL_Rect *bars = malloc(sizeof(SDL_Rect) * max_rects);
    if (bars) {
      int rect_count = 0;
      for (int i = 0; i < res_limit; i++) {
        int py = computed_y[i];
        int bar_h = abs(py - cy);
        int bar_w = (int)x_step;
        if (bar_w < 1) bar_w = 1;

        bars[rect_count++] = (SDL_Rect){
          computed_x[i],
          (py < cy ? py : cy),
          bar_w,
          bar_h
        };
        if (g_mirror_mode) {
          bars[rect_count++] = (SDL_Rect){
            (x + w) - (computed_x[i] - x) - bar_w,
            (py < cy ? py : cy),
            bar_w,
            bar_h
          };
        }
      }
      SDL_RenderFillRects(renderer, bars, rect_count);
      free(bars);
    }
  }

  free(computed_x);
  free(computed_y);
}

const VisPlugin g_vis_basic_wave = {.name = "Basic Waveform",
                                    .author = "Harmony Team",
                                    .init = basic_init,
                                    .render = basic_render,
                                    .cleanup = basic_cleanup,
                                    .resize = NULL,
                                    .get_param_count = basic_get_param_count,
                                    .get_param = basic_get_param};

const VisPlugin *visualizer_get_info(void) { return &g_vis_basic_wave; }
