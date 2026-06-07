#include "builtin_visualizers.h"
#include "visualizer_interface.h"
#include <SDL2/SDL.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>

static int g_points_count = 120;
static float g_base_radius = 0.2f;
static float g_deformation = 0.2f;
static float g_smoothness = 0.8f;
static float g_sensitivity = 1.5f;
static int g_color_mode = 0;
static float g_opacity = 1.0f;

static float *g_point_values = NULL;
static size_t g_buffer_size = 0;

static const char *COLOR_MODE_OPTIONS[] = {"Theme Primary", "Theme Secondary", "Theme Tertiary", "Theme Surface", NULL};

static void plasma_init(int width, int height) {
    (void)width;
    (void)height;
    g_point_values = NULL;
    g_buffer_size = 0;
}

static void plasma_cleanup(void) {
    if (g_point_values) {
        free(g_point_values);
        g_point_values = NULL;
    }
    g_buffer_size = 0;
}

static int plasma_get_param_count(void) { return 7; }

static const VisParam *plasma_get_param(int index) {
    static VisParam params[7];

    params[0].name = "Points Count";
    params[0].type = VIS_PARAM_INT;
    params[0].value_ptr = &g_points_count;
    params[0].min = 32.0f;
    params[0].max = 360.0f;

    params[1].name = "Base Radius";
    params[1].type = VIS_PARAM_FLOAT;
    params[1].value_ptr = &g_base_radius;
    params[1].min = 0.05f;
    params[1].max = 0.5f;

    params[2].name = "Deformation Scale";
    params[2].type = VIS_PARAM_FLOAT;
    params[2].value_ptr = &g_deformation;
    params[2].min = 0.0f;
    params[2].max = 0.5f;

    params[3].name = "Smoothness";
    params[3].type = VIS_PARAM_FLOAT;
    params[3].value_ptr = &g_smoothness;
    params[3].min = 0.0f;
    params[3].max = 0.98f;

    params[4].name = "Sensitivity";
    params[4].type = VIS_PARAM_FLOAT;
    params[4].value_ptr = &g_sensitivity;
    params[4].min = 0.1f;
    params[4].max = 5.0f;

    params[5].name = "Color Mode";
    params[5].type = VIS_PARAM_ENUM;
    params[5].value_ptr = &g_color_mode;
    params[5].options = COLOR_MODE_OPTIONS;

    params[6].name = "Opacity";
    params[6].type = VIS_PARAM_FLOAT;
    params[6].value_ptr = &g_opacity;
    params[6].min = 0.1f;
    params[6].max = 1.0f;

    if (index >= 0 && index < 7) {
        return &params[index];
    }
    return NULL;
}

static void plasma_render(SDL_Renderer *renderer, const float *audio_data,
                          size_t sample_count, int x, int y, int w, int h,
                          const ThemeColors *theme) {
    if (!audio_data || sample_count == 0 || g_points_count < 1) return;

    if (g_buffer_size != (size_t)g_points_count) {
        g_point_values = realloc(g_point_values, sizeof(float) * g_points_count);
        for (int i = 0; i < g_points_count; i++) {
            g_point_values[i] = 0.0f;
        }
        g_buffer_size = g_points_count;
    }

    int cx = x + w / 2;
    int cy = y + h / 2;
    int min_dim = w < h ? w : h;
    float base_r = min_dim * g_base_radius;
    float def_max = min_dim * g_deformation;

    float samples_per_point = (float)sample_count / (float)g_points_count;

    for (int i = 0; i < g_points_count; i++) {
        int sample_start = (int)(i * samples_per_point);
        int sample_end = (int)((i + 1) * samples_per_point);

        if (sample_end >= (int)sample_count) sample_end = (int)sample_count - 1;
        if (sample_start < 0) sample_start = 0;
        if (sample_end < sample_start) sample_end = sample_start;

        float sum = 0.0f;
        int count = 0;
        for (int s = sample_start; s <= sample_end && s < (int)sample_count; s++) {
            sum += fabsf(audio_data[s]);
            count++;
        }
        float amplitude = (count > 0) ? (sum / count) : 0.0f;
        amplitude *= g_sensitivity;

        g_point_values[i] = g_point_values[i] * g_smoothness + amplitude * (1.0f - g_smoothness);
    }

    // Apply a simple spatial smoothing (low pass filter) across the ring to make it look like a fluid
    float *smoothed_values = malloc(sizeof(float) * g_points_count);
    if (smoothed_values) {
        for (int i = 0; i < g_points_count; i++) {
            int prev = (i - 1 + g_points_count) % g_points_count;
            int next = (i + 1) % g_points_count;
            int pprev = (i - 2 + g_points_count) % g_points_count;
            int nnext = (i + 2) % g_points_count;
            
            smoothed_values[i] = (g_point_values[pprev] * 0.1f + 
                                  g_point_values[prev] * 0.2f + 
                                  g_point_values[i] * 0.4f + 
                                  g_point_values[next] * 0.2f + 
                                  g_point_values[nnext] * 0.1f);
        }
    } else {
        return;
    }

    uint8_t r = 255, g_col = 255, b = 255;
    if (g_color_mode == 0 && theme) {
        r = theme->primary.r; g_col = theme->primary.g; b = theme->primary.b;
    } else if (g_color_mode == 1 && theme) {
        r = theme->secondary.r; g_col = theme->secondary.g; b = theme->secondary.b;
    } else if (g_color_mode == 2 && theme) {
        r = theme->tertiary.r; g_col = theme->tertiary.g; b = theme->tertiary.b;
    } else if (g_color_mode == 3 && theme) {
        r = theme->surface.r; g_col = theme->surface.g; b = theme->surface.b;
    }
    
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    uint8_t alpha = (uint8_t)(g_opacity * 255.0f);
    SDL_SetRenderDrawColor(renderer, r, g_col, b, alpha);

    // Draw lines from center to outer points to fill the blob
    // Also draw the outline to ensure it's solid
    for (int i = 0; i < g_points_count; i++) {
        float angle = ((float)i / (float)g_points_count) * 2.0f * M_PI;
        float r_val = base_r + smoothed_values[i] * def_max;
        
        float px = cx + cosf(angle) * r_val;
        float py = cy + sinf(angle) * r_val;

        // Radiating line from center to edge (fills the shape)
        SDL_RenderDrawLine(renderer, cx, cy, (int)px, (int)py);

        // Draw boundary to next point
        int next = (i + 1) % g_points_count;
        float angle_next = ((float)next / (float)g_points_count) * 2.0f * M_PI;
        float r_val_next = base_r + smoothed_values[next] * def_max;
        float px_next = cx + cosf(angle_next) * r_val_next;
        float py_next = cy + sinf(angle_next) * r_val_next;

        SDL_RenderDrawLine(renderer, (int)px, (int)py, (int)px_next, (int)py_next);
        SDL_RenderDrawLine(renderer, (int)px + 1, (int)py, (int)px_next + 1, (int)py_next);
        SDL_RenderDrawLine(renderer, (int)px, (int)py + 1, (int)px_next, (int)py_next + 1);
    }

    free(smoothed_values);
}

const VisPlugin g_vis_liquid_plasma = {
    .name = "Liquid Plasma",
    .author = "Harmony Team",
    .init = plasma_init,
    .render = plasma_render,
    .cleanup = plasma_cleanup,
    .resize = NULL,
    .get_param_count = plasma_get_param_count,
    .get_param = plasma_get_param
};

const VisPlugin *visualizer_get_info(void) { return &g_vis_liquid_plasma; }
