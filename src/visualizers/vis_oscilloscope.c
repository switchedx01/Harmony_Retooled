#include "builtin_visualizers.h"
#include "visualizer_interface.h"
#include <SDL2/SDL.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* Parameters */
static float g_persistence = 0.85f;
static int g_line_thickness = 2;
static float g_gain = 1.0f;
static bool g_circular_mode = true;
static float g_rotation_speed = 0.5f;
static int g_trigger_mode = 1; // 0: None, 1: Rising Edge
static bool g_xy_mode = false;
static float g_xy_scale = 1.0f;
static int g_color_source = 1; // 0: White, 1: Primary, 2: Secondary...

static const char *COLOR_SOURCE_OPTIONS[] = {"Default (White)",
                                             "Theme Primary",
                                             "Theme Secondary",
                                             "Theme Tertiary",
                                             "Theme Surface",
                                             "Dynamic Rainbow",
                                             NULL};

/* Internal State */
static SDL_Texture *g_trail_tex = NULL;
static int g_buffer_w = 0, g_buffer_h = 0;
static float g_angle_off = 0.0f;

/* For Rising Edge trigger */
#define TRIGGER_THRESHOLD 0.01f

static void osc_init(int width, int height) {
  g_buffer_w = width;
  g_buffer_h = height;
  /* Texture will be created/recreated in render if needed */
}

static void osc_cleanup(void) {
  if (g_trail_tex) {
    SDL_DestroyTexture(g_trail_tex);
    g_trail_tex = NULL;
  }
}

static int osc_get_param_count(void) { return 9; }

static const VisParam *osc_get_param(int index) {
  static VisParam params[9];
  params[0] = (VisParam){
      "Persistence", VIS_PARAM_FLOAT, &g_persistence, 0.0f, 1.0f, NULL};
  params[1] = (VisParam){"Gain", VIS_PARAM_FLOAT, &g_gain, 0.1f, 10.0f, NULL};
  params[2] = (VisParam){"Thickness", VIS_PARAM_INT, &g_line_thickness,
                         1.0f,        10.0f,         NULL};
  params[3] =
      (VisParam){"Circular Mode", VIS_PARAM_BOOL, &g_circular_mode, 0, 0, NULL};
  params[4] = (VisParam){
      "Rotation Speed", VIS_PARAM_FLOAT, &g_rotation_speed, 0.0f, 5.0f, NULL};
  params[5] =
      (VisParam){"Trigger Mode", VIS_PARAM_INT, &g_trigger_mode, 0, 1, NULL};
  params[6] = (VisParam){"X-Y Mode", VIS_PARAM_BOOL, &g_xy_mode, 0, 0, NULL};
  params[7] =
      (VisParam){"X-Y Scale", VIS_PARAM_FLOAT, &g_xy_scale, 0.1f, 5.0f, NULL};
  params[8] =
      (VisParam){"Color Source",      VIS_PARAM_ENUM, &g_color_source, 0, 0,
                 COLOR_SOURCE_OPTIONS};

  if (index >= 0 && index < 9)
    return &params[index];
  return NULL;
}

static void osc_render(SDL_Renderer *renderer, const float *audio_data,
                       size_t sample_count, int x, int y, int w, int h,
                       const ThemeColors *theme) {
  if (!audio_data || sample_count < 2)
    return;

  /* Set color based on color source */
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
  SDL_SetRenderDrawColor(renderer, r, g, b, 255);

  int centerX = x + w / 2;
  int centerY = y + h / 2;
  g_angle_off += 0.01f * g_rotation_speed;

  int prevX = -1, prevY = -1;
  /* Use a fixed window of samples for better view */
  size_t plot_samples = 512;
  if (plot_samples > sample_count / 2)
    plot_samples = sample_count / 2;

  /* Trigger Logic */
  size_t start_idx = 0;
  if (!g_xy_mode && g_trigger_mode == 1) {
    for (size_t i = 0; i < sample_count / 2 - plot_samples; i++) {
      if (audio_data[i * 2] < -TRIGGER_THRESHOLD &&
          audio_data[i * 2 + 2] >= -TRIGGER_THRESHOLD) {
        start_idx = i;
        break;
      }
    }
  }

  for (size_t i = 0; i < plot_samples; i++) {
    int curX, curY;
    size_t idx = (start_idx + i);

    if (g_xy_mode) {
      /* X-Y Mode: Left = X, Right = Y */
      float sampL = audio_data[idx * 2] * g_gain * g_xy_scale;
      float sampR = audio_data[idx * 2 + 1] * g_gain * g_xy_scale;
      curX = centerX + (int)(sampL * (w * 0.4f));
      curY = centerY - (int)(sampR * (h * 0.4f));
    } else {
      /* Waveform Modes */
      float sample = audio_data[idx * 2] * g_gain;

      if (g_circular_mode) {
        float angle =
            (float)i / (float)plot_samples * 2.0f * M_PI + g_angle_off;
        float radius = (h * 0.25f) + (sample * h * 0.2f);
        curX = centerX + (int)(cosf(angle) * radius);
        curY = centerY + (int)(sinf(angle) * radius);
      } else {
        curX = x + (int)((float)i / (float)plot_samples * w);
        curY = centerY + (int)(sample * h * 0.45f);
      }
    }

    if (prevX != -1) {
      for (int t = 0; t < g_line_thickness; t++) {
        SDL_RenderDrawLine(renderer, prevX, prevY + t - g_line_thickness / 2,
                           curX, curY + t - g_line_thickness / 2);
      }
    }
    prevX = curX;
    prevY = curY;
  }
}

const VisPlugin g_vis_oscilloscope = {.name = "Pro Oscilloscope",
                                      .author = "Harmony Team",
                                      .init = osc_init,
                                      .render = osc_render,
                                      .cleanup = osc_cleanup,
                                      .resize = NULL,
                                      .get_param_count = osc_get_param_count,
                                      .get_param = osc_get_param};

const VisPlugin *visualizer_get_info(void) { return &g_vis_oscilloscope; }
