#include "builtin_visualizers.h"
#include "visualizer_interface.h"
#include <SDL2/SDL.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>

typedef struct {
    float x, y;
    float vx, vy;
    float radius;
} Particle;

static int g_particle_count = 100;
static float g_base_speed = 1.0f;
static float g_connection_dist = 100.0f;
static float g_bass_threshold = 0.6f;
static float g_sensitivity = 1.5f;
static int g_color_mode = 0; // 0: Theme Primary, 1: Theme Secondary, 2: Theme Tertiary, 3: White
static float g_opacity = 1.0f;
static bool g_enable_toy = false;

static Particle *g_particles = NULL;
static size_t g_particles_size = 0;

static const char *COLOR_MODE_OPTIONS[] = {"Theme Primary", "Theme Secondary", "Theme Tertiary", "White", NULL};

static void reset_particles(int w, int h) {
    if (!g_particles) return;
    for (int i = 0; i < g_particle_count; i++) {
        g_particles[i].x = (float)(rand() % w);
        g_particles[i].y = (float)(rand() % h);
        float angle = (float)(rand() % 360) * M_PI / 180.0f;
        float speed = ((float)(rand() % 100) / 100.0f) * g_base_speed + 0.1f;
        g_particles[i].vx = cosf(angle) * speed;
        g_particles[i].vy = sinf(angle) * speed;
        g_particles[i].radius = ((float)(rand() % 100) / 100.0f) * 2.0f + 1.0f;
    }
}

static void particle_init(int width, int height) {
    (void)width;
    (void)height;
    g_particles = NULL;
    g_particles_size = 0;
}

static void particle_cleanup(void) {
    if (g_particles) {
        free(g_particles);
        g_particles = NULL;
    }
    g_particles_size = 0;
}

static int particle_get_param_count(void) { return 8; }

static const VisParam *particle_get_param(int index) {
    static VisParam params[8];

    params[0].name = "Particle Count";
    params[0].type = VIS_PARAM_INT;
    params[0].value_ptr = &g_particle_count;
    params[0].min = 10.0f;
    params[0].max = 500.0f;

    params[1].name = "Base Speed";
    params[1].type = VIS_PARAM_FLOAT;
    params[1].value_ptr = &g_base_speed;
    params[1].min = 0.1f;
    params[1].max = 5.0f;

    params[2].name = "Connection Dist";
    params[2].type = VIS_PARAM_FLOAT;
    params[2].value_ptr = &g_connection_dist;
    params[2].min = 20.0f;
    params[2].max = 300.0f;

    params[3].name = "Bass Threshold";
    params[3].type = VIS_PARAM_FLOAT;
    params[3].value_ptr = &g_bass_threshold;
    params[3].min = 0.1f;
    params[3].max = 2.0f;

    params[4].name = "Sensitivity";
    params[4].type = VIS_PARAM_FLOAT;
    params[4].value_ptr = &g_sensitivity;
    params[4].min = 0.1f;
    params[4].max = 3.0f;

    params[5].name = "Color Mode";
    params[5].type = VIS_PARAM_ENUM;
    params[5].value_ptr = &g_color_mode;
    params[5].options = COLOR_MODE_OPTIONS;

    params[6].name = "Opacity";
    params[6].type = VIS_PARAM_FLOAT;
    params[6].value_ptr = &g_opacity;
    params[6].min = 0.1f;
    params[6].max = 1.0f;

    params[7].name = "Enable Toy Box";
    params[7].type = VIS_PARAM_BOOL;
    params[7].value_ptr = &g_enable_toy;

    if (index >= 0 && index < 8) {
        return &params[index];
    }
    return NULL;
}

static void particle_render(SDL_Renderer *renderer, const float *audio_data,
                            size_t sample_count, int x, int y, int w, int h,
                            const ThemeColors *theme) {
    if (!audio_data || sample_count == 0 || g_particle_count < 1) return;

    if (g_particles_size != (size_t)g_particle_count) {
        g_particles = realloc(g_particles, sizeof(Particle) * g_particle_count);
        g_particles_size = g_particle_count;
        reset_particles(w, h);
    }

    float sum = 0.0f;
    for (size_t i = 0; i < sample_count; i++) {
        sum += fabsf(audio_data[i]);
    }
    float avg_amplitude = (sum / sample_count) * g_sensitivity;
    
    float kick_multiplier = 1.0f;
    if (avg_amplitude > g_bass_threshold) {
        kick_multiplier = 1.0f + (avg_amplitude - g_bass_threshold) * 5.0f;
    }

    uint8_t r = 255, g_col = 255, b = 255;
    if (g_color_mode == 0 && theme) {
        r = theme->primary.r; g_col = theme->primary.g; b = theme->primary.b;
    } else if (g_color_mode == 1 && theme) {
        r = theme->secondary.r; g_col = theme->secondary.g; b = theme->secondary.b;
    } else if (g_color_mode == 2 && theme) {
        r = theme->tertiary.r; g_col = theme->tertiary.g; b = theme->tertiary.b;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    int mx, my;
    bool has_toy = false;
    float toy_x = 0, toy_y = 0;
    float toy_size = 40.0f;
    
    if (g_enable_toy) {
        SDL_GetMouseState(&mx, &my);
        if (mx >= x && mx <= x + w && my >= y && my <= y + h) {
            has_toy = true;
            toy_x = (float)(mx - x);
            toy_y = (float)(my - y);
            // Draw the toy orb
            SDL_SetRenderDrawColor(renderer, 255, 100, 100, 200);
            int r_toy = (int)toy_size / 2;
            for (int w_y = -r_toy; w_y <= r_toy; w_y++) {
                int w_x = (int)sqrtf(r_toy*r_toy - w_y*w_y);
                SDL_RenderDrawLine(renderer, x + (int)toy_x - w_x, y + (int)toy_y + w_y, x + (int)toy_x + w_x, y + (int)toy_y + w_y);
            }
        }
    }

    // Update and draw particles
    for (int i = 0; i < g_particle_count; i++) {
        // Move
        g_particles[i].x += g_particles[i].vx * kick_multiplier;
        g_particles[i].y += g_particles[i].vy * kick_multiplier;

        // Bounce
        if (g_particles[i].x < 0) { g_particles[i].x = 0; g_particles[i].vx *= -1; }
        if (g_particles[i].x > w) { g_particles[i].x = w; g_particles[i].vx *= -1; }
        if (g_particles[i].y < 0) { g_particles[i].y = 0; g_particles[i].vy *= -1; }
        if (g_particles[i].y > h) { g_particles[i].y = h; g_particles[i].vy *= -1; }

        // Toy collision (Orb reflection)
        if (has_toy) {
            float dx = g_particles[i].x - toy_x;
            float dy = g_particles[i].y - toy_y;
            float dist = sqrtf(dx * dx + dy * dy);
            float min_dist = toy_size/2.0f + g_particles[i].radius;
            
            if (dist < min_dist && dist > 0.001f) {
                float nx = dx / dist;
                float ny = dy / dist;
                
                // Push out to prevent sticking
                float overlap = min_dist - dist;
                g_particles[i].x += nx * overlap;
                g_particles[i].y += ny * overlap;
                
                // Reflect velocity: v = v - 2(v.n)n
                float dot = g_particles[i].vx * nx + g_particles[i].vy * ny;
                if (dot < 0) {
                    g_particles[i].vx = g_particles[i].vx - 2.0f * dot * nx;
                    g_particles[i].vy = g_particles[i].vy - 2.0f * dot * ny;
                    
                    // Add slight bat force
                    g_particles[i].vx *= 1.2f;
                    g_particles[i].vy *= 1.2f;
                }
            }
        }


        // Draw connections
        for (int j = i + 1; j < g_particle_count; j++) {
            float dx = g_particles[i].x - g_particles[j].x;
            float dy = g_particles[i].y - g_particles[j].y;
            float dist = sqrtf(dx * dx + dy * dy);
            
            if (dist < g_connection_dist) {
                float alpha_factor = 1.0f - (dist / g_connection_dist);
                uint8_t alpha = (uint8_t)(alpha_factor * g_opacity * 255.0f);
                SDL_SetRenderDrawColor(renderer, r, g_col, b, alpha);
                SDL_RenderDrawLine(renderer, x + (int)g_particles[i].x, y + (int)g_particles[i].y,
                                   x + (int)g_particles[j].x, y + (int)g_particles[j].y);
            }
        }

        // Draw particle dot
        SDL_SetRenderDrawColor(renderer, r, g_col, b, (uint8_t)(g_opacity * 255.0f));
        SDL_Rect rect = {
            x + (int)g_particles[i].x - (int)g_particles[i].radius,
            y + (int)g_particles[i].y - (int)g_particles[i].radius,
            (int)g_particles[i].radius * 2,
            (int)g_particles[i].radius * 2
        };
        SDL_RenderFillRect(renderer, &rect);
    }
}

const VisPlugin g_vis_particle_constellation = {
    .name = "Particle Constellation",
    .author = "Harmony Team",
    .init = particle_init,
    .render = particle_render,
    .cleanup = particle_cleanup,
    .resize = NULL,
    .get_param_count = particle_get_param_count,
    .get_param = particle_get_param
};

const VisPlugin *visualizer_get_info(void) { return &g_vis_particle_constellation; }
