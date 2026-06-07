#include "builtin_visualizers.h"
#include "visualizer_interface.h"
#include <SDL2/SDL.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>

static int g_bar_count = 128;
static float g_base_radius = 0.25f; // relative to min(w, h)
static float g_bar_length = 0.5f;   // relative to min(w, h)
static float g_sensitivity = 1.5f;
static float g_smoothness = 0.6f;
static float g_rotation_speed = 0.5f;
static int g_color_mode = 0; // 0: Theme Primary, 1: Theme Secondary, 2: Theme Tertiary, 3: Rainbow
static float g_opacity = 1.0f;

static float *g_bar_values = NULL;
static size_t g_buffer_size = 0;

static const char *COLOR_MODE_OPTIONS[] = {"Theme Primary", "Theme Secondary", "Theme Tertiary", "Rainbow", NULL};

static void circular_init(int width, int height) {
    (void)width;
    (void)height;
    g_bar_values = NULL;
    g_buffer_size = 0;
}

static void circular_cleanup(void) {
    if (g_bar_values) {
        free(g_bar_values);
        g_bar_values = NULL;
    }
    g_buffer_size = 0;
}

static int circular_get_param_count(void) {
    return 8;
}

static const VisParam *circular_get_param(int index) {
    static VisParam params[8];

    params[0].name = "Bar Count";
    params[0].type = VIS_PARAM_INT;
    params[0].value_ptr = &g_bar_count;
    params[0].min = 16.0f;
    params[0].max = 512.0f;

    params[1].name = "Base Radius";
    params[1].type = VIS_PARAM_FLOAT;
    params[1].value_ptr = &g_base_radius;
    params[1].min = 0.05f;
    params[1].max = 0.5f;

    params[2].name = "Bar Length Max";
    params[2].type = VIS_PARAM_FLOAT;
    params[2].value_ptr = &g_bar_length;
    params[2].min = 0.1f;
    params[2].max = 1.0f;

    params[3].name = "Sensitivity";
    params[3].type = VIS_PARAM_FLOAT;
    params[3].value_ptr = &g_sensitivity;
    params[3].min = 0.1f;
    params[3].max = 5.0f;

    params[4].name = "Smoothness";
    params[4].type = VIS_PARAM_FLOAT;
    params[4].value_ptr = &g_smoothness;
    params[4].min = 0.0f;
    params[4].max = 0.95f;

    params[5].name = "Rotation Speed";
    params[5].type = VIS_PARAM_FLOAT;
    params[5].value_ptr = &g_rotation_speed;
    params[5].min = -5.0f;
    params[5].max = 5.0f;

    params[6].name = "Color Mode";
    params[6].type = VIS_PARAM_ENUM;
    params[6].value_ptr = &g_color_mode;
    params[6].options = COLOR_MODE_OPTIONS;

    params[7].name = "Opacity";
    params[7].type = VIS_PARAM_FLOAT;
    params[7].value_ptr = &g_opacity;
    params[7].min = 0.1f;
    params[7].max = 1.0f;

    if (index >= 0 && index < 8) {
        return &params[index];
    }
    return NULL;
}

static void hsv_to_rgb(float h, float s, float v, uint8_t *r, uint8_t *g, uint8_t *b) {
    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float rf = 0, gf = 0, bf = 0;

    if (h < 60.0f) { rf = c; gf = x; }
    else if (h < 120.0f) { rf = x; gf = c; }
    else if (h < 180.0f) { gf = c; bf = x; }
    else if (h < 240.0f) { gf = x; bf = c; }
    else if (h < 300.0f) { rf = x; bf = c; }
    else { rf = c; bf = x; }

    *r = (uint8_t)((rf + m) * 255.0f);
    *g = (uint8_t)((gf + m) * 255.0f);
    *b = (uint8_t)((bf + m) * 255.0f);
}

static void circular_render(SDL_Renderer *renderer, const float *audio_data,
                            size_t sample_count, int x, int y, int w, int h,
                            const ThemeColors *theme) {
    if (!audio_data || sample_count == 0 || g_bar_count < 1) return;

    if (g_buffer_size != (size_t)g_bar_count) {
        g_bar_values = realloc(g_bar_values, sizeof(float) * g_bar_count);
        for (int i = 0; i < g_bar_count; i++) {
            g_bar_values[i] = 0.0f;
        }
        g_buffer_size = g_bar_count;
    }

    float time = (float)SDL_GetTicks() * 0.001f;
    int cx = x + w / 2;
    int cy = y + h / 2;
    int min_dim = w < h ? w : h;
    float base_r = min_dim * g_base_radius;
    float max_len = min_dim * g_bar_length;

    float samples_per_bar = (float)sample_count / (float)g_bar_count;

    for (int i = 0; i < g_bar_count; i++) {
        int sample_start = (int)(i * samples_per_bar);
        int sample_end = (int)((i + 1) * samples_per_bar);

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

        g_bar_values[i] = g_bar_values[i] * g_smoothness + amplitude * (1.0f - g_smoothness);
        if (g_bar_values[i] > 1.0f) g_bar_values[i] = 1.0f;
        if (g_bar_values[i] < 0.0f) g_bar_values[i] = 0.0f;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    float global_angle_offset = time * g_rotation_speed;

    for (int i = 0; i < g_bar_count; i++) {
        float angle = ((float)i / (float)g_bar_count) * 2.0f * M_PI + global_angle_offset;
        float val = g_bar_values[i];
        
        float start_x = cx + cosf(angle) * base_r;
        float start_y = cy + sinf(angle) * base_r;
        float end_x = cx + cosf(angle) * (base_r + val * max_len);
        float end_y = cy + sinf(angle) * (base_r + val * max_len);

        uint8_t r = 255, g_col = 255, b = 255;
        if (g_color_mode == 0 && theme) {
            r = theme->primary.r; g_col = theme->primary.g; b = theme->primary.b;
        } else if (g_color_mode == 1 && theme) {
            r = theme->secondary.r; g_col = theme->secondary.g; b = theme->secondary.b;
        } else if (g_color_mode == 2 && theme) {
            r = theme->tertiary.r; g_col = theme->tertiary.g; b = theme->tertiary.b;
        } else if (g_color_mode == 3) {
            float hue = fmodf(((float)i / (float)g_bar_count) * 360.0f + time * 50.0f, 360.0f);
            hsv_to_rgb(hue, 1.0f, 1.0f, &r, &g_col, &b);
        }

        uint8_t alpha = (uint8_t)(g_opacity * 255.0f);
        SDL_SetRenderDrawColor(renderer, r, g_col, b, alpha);
        
        // Draw thicker line by drawing offset lines
        SDL_RenderDrawLine(renderer, (int)start_x, (int)start_y, (int)end_x, (int)end_y);
        SDL_RenderDrawLine(renderer, (int)start_x + 1, (int)start_y, (int)end_x + 1, (int)end_y);
        SDL_RenderDrawLine(renderer, (int)start_x, (int)start_y + 1, (int)end_x, (int)end_y + 1);
    }
}

const VisPlugin g_vis_circular_spectrum = {
    .name = "Circular Spectrum",
    .author = "Harmony Team",
    .init = circular_init,
    .render = circular_render,
    .cleanup = circular_cleanup,
    .resize = NULL,
    .get_param_count = circular_get_param_count,
    .get_param = circular_get_param
};

const VisPlugin *visualizer_get_info(void) { return &g_vis_circular_spectrum; }
