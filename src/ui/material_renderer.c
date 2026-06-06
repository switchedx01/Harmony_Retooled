#include "material_renderer.h"
#include "builtin_visualizers.h"
#include "color_extractor.h"
#include "common.h"
#include "database.h"
#include "font_renderer.h"
#include "image_loader.h"
#include "logging.h"
#include "material_renderer.h"
#include "mini_player.h"
#include "player.h"
#include "string_utils.h"
#include "toast_overlay.h"
#include "visualizer_buffer.h"
#include "visualizer_interface.h"
#include "visualizer_loader.h"
#include "window.h"

#include <SDL2/SDL.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define MIN_CLICK_DELAY_MS 200
static uint32_t g_last_click_time = 0;
static bool g_debug_view = false;

/* EQ slider geometry shared with command_dispatch for Y→dB mapping */
int g_eq_slider_top = 0;
int g_eq_slider_h = 0;

/* Unified Grid Calculation Helper */
int material_get_grid_cols(int w) {
  int card_w = 180;
  int padding = 20;
  int cols = (w - 40 + padding) / (card_w + padding);
  if (cols < 1)
    cols = 1;
  return cols;
}

void material_handle_input(SDL_Event *e) {
  if (e->type == SDL_KEYDOWN) {
    if (e->key.keysym.sym == SDLK_F3) {
      /* Only toggle if Shift is held */
      if (e->key.keysym.mod & KMOD_SHIFT) {
        g_debug_view = !g_debug_view;
        char msg[64];
        snprintf(msg, sizeof(msg), "Debug View toggled: %s",
                 g_debug_view ? "ON" : "OFF");
        log_message("DEBUG", msg);
      }
    }
  }
}

static void draw_aa_triangle(float x1, float y1, float x2, float y2, float x3,
                             float y3, Color c);
static void draw_filled_circle(int cx, int cy, int r, Color c, Uint8 alpha);

static SDL_Renderer *g_renderer = NULL;
static SDL_Texture *g_bg_texture = NULL; /* Sharp texture for center display */
static SDL_Texture *g_bg_blurred = NULL; /* Blurred texture for background */
static ThemeColors g_theme = {0};        /* Current theme colors */
/* static float g_gradient_offset = 0.0f; */ /* For animated gradient */

static SDL_Texture *g_settings_icon = NULL;
static SDL_Texture *g_knob_icon = NULL;

/* Splash Screen Constants & Globals */
#define SPLASH_HOLD_MS 800
#define SPLASH_FADE_MS 700
static bool g_splash_initialized = false;
static Uint32 g_splash_start_time = 0;

/* External Font Alpha Control */
void font_set_global_alpha(uint8_t alpha);

typedef struct {
  char path[MAX_PATH_LENGTH];
  SDL_Texture *tex;
  bool failed; /* Track failed loads to avoid repeated error messages */
} IconEntry;

#define MAX_ICONS 64
static IconEntry g_icon_cache[MAX_ICONS];
static int g_icon_cache_count = 0;

static void draw_play_icon(int x, int y, int size, Color c) {
  SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
  /* Circle background */
  draw_filled_circle(x, y, size, (Color){30, 30, 30}, 180);
  /* Triangle */
  float tr_sz = size * 0.5f;
  draw_aa_triangle((float)x - tr_sz * 0.4f, (float)y - tr_sz,
                   (float)x - tr_sz * 0.4f, (float)y + tr_sz,
                   (float)x + tr_sz * 0.8f, (float)y, c);
}

static SDL_Texture *get_or_load_icon(const char *path) {
  if (!path || path[0] == '\0')
    return NULL;

  for (int i = 0; i < g_icon_cache_count; i++) {
    if (strcmp(g_icon_cache[i].path, path) == 0) {
      /* Return cached texture (or NULL if previously failed) */
      return g_icon_cache[i].tex;
    }
  }

  if (g_icon_cache_count < MAX_ICONS) {
    SDL_Texture *tex = load_texture_from_file(g_renderer, path, NULL, NULL);
    /* Cache result regardless of success/failure to prevent retry spam */
    safe_strncpy(g_icon_cache[g_icon_cache_count].path, path, MAX_PATH_LENGTH);
    g_icon_cache[g_icon_cache_count].tex = tex;
    g_icon_cache[g_icon_cache_count].failed = (tex == NULL);
    g_icon_cache_count++;
    return tex;
  }
  return NULL;
}

/* Layout Constants */
#define PROGRESS_BAR_H 6
#define FLYOUT_BTN_W 60
#define FLYOUT_BTN_H 60
#define SETTINGS_ICON_SIZE 40
#define BLUR_INTENSITY 180

/* Color Constants - P10 Compliance: No magic numbers */
#define COL_PRIMARY g_theme.primary
#define COL_TERTIARY g_theme.tertiary
#define COL_WHITE (Color){255, 255, 255}
#define COL_BLACK (Color){0, 0, 0}
#define COL_GRAY_DARK (Color){40, 40, 40}
#define COL_GRAY_MED (Color){80, 80, 80}
#define COL_GRAY_LIGHT (Color){200, 200, 200}
#define COL_BG_DARK (Color){15, 15, 15}

/* UI Metrics */
#define TAB_HEIGHT 40
#define BTN_HEIGHT 36
#define PAD_SM 10
#define PAD_MD 20

static void update_animations(PlayerContext *ctx) {
  float speed = 0.15f; /* Animation speed per frame */

  if (ctx->sidebar_left_open) {
    if (ctx->sidebar_left_anim < 1.0f) {
      ctx->sidebar_left_anim += speed;
      if (ctx->sidebar_left_anim > 1.0f)
        ctx->sidebar_left_anim = 1.0f;
    }
  } else {
    if (ctx->sidebar_left_anim > 0.0f) {
      ctx->sidebar_left_anim -= speed;
      if (ctx->sidebar_left_anim < 0.0f)
        ctx->sidebar_left_anim = 0.0f;
    }
  }

  if (ctx->sidebar_right_open) {
    if (ctx->sidebar_right_anim < 1.0f) {
      ctx->sidebar_right_anim += speed;
      if (ctx->sidebar_right_anim > 1.0f)
        ctx->sidebar_right_anim = 1.0f;
    }
  } else {
    if (ctx->sidebar_right_anim > 0.0f) {
      ctx->sidebar_right_anim -= speed;
      if (ctx->sidebar_right_anim < 0.0f)
        ctx->sidebar_right_anim = 0.0f;
    }
  }

  if (ctx->settings_popup_open) {
    if (ctx->settings_popup_anim < 1.0f) {
      ctx->settings_popup_anim += speed;
      if (ctx->settings_popup_anim > 1.0f)
        ctx->settings_popup_anim = 1.0f;
    }
  } else {
    if (ctx->settings_popup_anim > 0.0f) {
      ctx->settings_popup_anim -= speed;
      if (ctx->settings_popup_anim < 0.0f)
        ctx->settings_popup_anim = 0.0f;
    }
  }

  /* Spotlight Animations (5-Phase Logic Map) */
  if (ctx->spotlight_active) {
    if (ctx->spotlight_is_closing) {
      /* Phase 5: Closing (Return to Grid) */
      if (ctx->spotlight_expand_anim > 0.0f) {
        ctx->spotlight_expand_anim -= speed;
        if (ctx->spotlight_expand_anim < 0.0f)
          ctx->spotlight_expand_anim = 0.0f;
      } else if (ctx->spotlight_flip_anim > 0.0f) {
        ctx->spotlight_flip_anim -= speed;
        if (ctx->spotlight_flip_anim < 0.0f)
          ctx->spotlight_flip_anim = 0.0f;
      } else {
        ctx->spotlight_anim -= 0.08f;
        if (ctx->spotlight_anim <= 0.0f) {
          ctx->spotlight_anim = 0.0f;
          ctx->spotlight_active = false;
          ctx->spotlight_is_closing = false;
          /* Grid item returns to Opacity 1 naturally as active becomes false */
        }
      }
    } else {
      /* Phases 1-4: Activation and Transitions */
      switch (ctx->spotlight_phase) {
      case SPOTLIGHT_PHASE_1_INIT:
        if (ctx->spotlight_anim < 1.0f) {
          ctx->spotlight_anim += 0.08f;
          if (ctx->spotlight_anim >= 1.0f) {
            ctx->spotlight_anim = 1.0f;
            ctx->spotlight_phase = SPOTLIGHT_PHASE_2_HUB;
          }
        }
        break;

      case SPOTLIGHT_PHASE_2_HUB:
        /* Settled state. Ensure other anims are at correct rest positions if
         * returning */
        if (ctx->spotlight_flip_anim > 0.0f) {
          ctx->spotlight_flip_anim -= speed;
          if (ctx->spotlight_flip_anim < 0.0f)
            ctx->spotlight_flip_anim = 0.0f;
        }
        if (ctx->spotlight_expand_anim > 0.0f) {
          ctx->spotlight_expand_anim -= speed;
          if (ctx->spotlight_expand_anim < 0.0f)
            ctx->spotlight_expand_anim = 0.0f;
        }
        break;

      case SPOTLIGHT_PHASE_3_META:
        if (ctx->spotlight_flip_anim < 1.0f) {
          ctx->spotlight_flip_anim += 0.1f;
          if (ctx->spotlight_flip_anim > 1.0f)
            ctx->spotlight_flip_anim = 1.0f;
        }
        break;

      case SPOTLIGHT_PHASE_4_EXPAND:
        if (ctx->spotlight_expand_anim < 1.0f) {
          ctx->spotlight_expand_anim += speed;
          if (ctx->spotlight_expand_anim > 1.0f)
            ctx->spotlight_expand_anim = 1.0f;
        }
        break;

      default:
        break;
      }
    }
  } else {
    ctx->spotlight_anim = 0.0f;
    ctx->spotlight_flip_anim = 0.0f;
    ctx->spotlight_expand_anim = 0.0f;
  }
}

void material_init(SDL_Renderer *renderer) {
  g_renderer = renderer;

  /* Load premium assets */
  g_settings_icon =
      load_texture_from_file(renderer, "assets/settings.png", NULL, NULL);
  g_knob_icon = load_texture_from_file(renderer, "assets/knob.png", NULL, NULL);

  /* Try to load default placeholder */
  material_set_background("placeholder.png");

  /* Init standard font */
  font_init(renderer, "assets/fonts/Roboto-Regular.ttf", 24.0f);
}

void material_shutdown(void) {
  if (g_bg_texture) {
    SDL_DestroyTexture(g_bg_texture);
    g_bg_texture = NULL;
  }
  if (g_bg_blurred) {
    SDL_DestroyTexture(g_bg_blurred);
    g_bg_blurred = NULL;
  }
  if (g_settings_icon) {
    SDL_DestroyTexture(g_settings_icon);
    g_settings_icon = NULL;
  }
  if (g_knob_icon) {
    SDL_DestroyTexture(g_knob_icon);
    g_knob_icon = NULL;
  }

  for (int i = 0; i < g_icon_cache_count; i++) {
    if (g_icon_cache[i].tex) {
      SDL_DestroyTexture(g_icon_cache[i].tex);
      g_icon_cache[i].tex = NULL;
    }
  }
  g_icon_cache_count = 0;

  font_shutdown();
  g_renderer = NULL;

  log_message("INFO", "Material renderer and cached textures shut down.");
}

ThemeColors *material_get_theme(void) { return &g_theme; }

void material_set_background(const char *image_path) {
  SDL_Texture *new_tex, *new_blurred;

  if (g_renderer == NULL) {
    /* Headless mode: only extract colors from the image, no textures */
    image_extract_theme(image_path, &g_theme);
    return;
  }

  new_tex = load_texture_with_blurred_bg(g_renderer, image_path, &g_theme,
                                         &new_blurred);
  if (new_tex) {
    if (g_bg_texture)
      SDL_DestroyTexture(g_bg_texture);
    if (g_bg_blurred)
      SDL_DestroyTexture(g_bg_blurred);

    g_bg_texture = new_tex;
    g_bg_blurred = new_blurred;

    /* Pre-load this into icon cache for sidebar/mini-icons */
    get_or_load_icon(image_path);

    log_message("INFO",
                "Theme updated from image with advanced color extraction.");
  }
}

static void draw_aa_corner(int cx, int cy, int r, float start_angle, Color c,
                           Uint8 alpha) {
  int segments = r * 2;
  if (segments < 12)
    segments = 12;
  if (segments > 32)
    segments = 32;

  /* Vertices: 1 Center + N Inner + N Outer */
  /* Indices: N*3 + N*6 */
  int num_verts = 1 + 2 * (segments + 1);
  int num_indices = segments * 9;

  SDL_Vertex verts[67]; /* 1 + 2 * 33 */
  int indices[288];

  float rad_inner = (float)r - 0.5f;
  if (rad_inner < 0)
    rad_inner = 0;
  float rad_outer = (float)r + 0.5f;

  verts[0].position.x = (float)cx;
  verts[0].position.y = (float)cy;
  verts[0].color = (SDL_Color){c.r, c.g, c.b, alpha};

  /* Arc covers PI/2 (90 deg) */
  float angular_step = (3.14159f / 2.0f) / segments;

  for (int i = 0; i <= segments; i++) {
    float angle = start_angle + i * angular_step;
    float cos_a = cosf(angle);
    float sin_a = sinf(angle);

    int idx_in = 1 + i;
    verts[idx_in].position.x = cx + cos_a * rad_inner;
    verts[idx_in].position.y = cy + sin_a * rad_inner;
    verts[idx_in].color = (SDL_Color){c.r, c.g, c.b, alpha};

    int idx_out = 1 + segments + 1 + i;
    verts[idx_out].position.x = cx + cos_a * rad_outer;
    verts[idx_out].position.y = cy + sin_a * rad_outer;
    verts[idx_out].color = (SDL_Color){c.r, c.g, c.b, 0};
  }

  /* Indices */
  int idx_ptr = 0;
  for (int i = 0; i < segments; i++) {
    int next = i + 1;

    /* Center Fan */
    indices[idx_ptr++] = 0;
    indices[idx_ptr++] = 1 + i;
    indices[idx_ptr++] = 1 + next;

    /* AA Strip */
    int in_cur = 1 + i;
    int in_next = 1 + next;
    int out_cur = 1 + segments + 1 + i;
    int out_next = 1 + segments + 1 + next;

    indices[idx_ptr++] = in_cur;
    indices[idx_ptr++] = out_cur;
    indices[idx_ptr++] = out_next;

    indices[idx_ptr++] = in_cur;
    indices[idx_ptr++] = out_next;
    indices[idx_ptr++] = in_next;
  }

  SDL_RenderGeometry(g_renderer, NULL, verts, num_verts, indices, num_indices);
}

void material_draw_rounded_rect(int x, int y, int w, int h, int r, Color c,
                                Uint8 alpha) {
  if (r <= 0) {
    SDL_Rect rect = {x, y, w, h};
    SDL_SetRenderDrawColor(g_renderer, c.r, c.g, c.b, alpha);
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
    SDL_RenderFillRect(g_renderer, &rect);
    return;
  }

  /* Clamp Radius */
  if (r > w / 2)
    r = w / 2;
  if (r > h / 2)
    r = h / 2;

  SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(g_renderer, c.r, c.g, c.b, alpha);

  /* Center blocks */
  /* Horizontal center band */
  SDL_Rect rect_h = {x, y + r, w, h - 2 * r};
  SDL_RenderFillRect(g_renderer, &rect_h);

  /* Vertical top band (between corners) */
  SDL_Rect rect_vt = {x + r, y, w - 2 * r, r};
  SDL_RenderFillRect(g_renderer, &rect_vt);

  /* Vertical bottom band (between corners) */
  SDL_Rect rect_vb = {x + r, y + h - r, w - 2 * r, r};
  SDL_RenderFillRect(g_renderer, &rect_vb);

  /* Draw AA Corners */
  /* Top Left: 180 deg (PI) */
  draw_aa_corner(x + r, y + r, r, 3.14159f, c, alpha);

  /* Top Right: 270 deg (1.5 PI) -> -90 deg (-PI/2) */
  /* cos(270) = 0, sin(270) = -1. */
  draw_aa_corner(x + w - r, y + r, r, -3.14159f / 2.0f, c, alpha);

  /* Bottom Right: 0 deg */
  draw_aa_corner(x + w - r, y + h - r, r, 0.0f, c, alpha);

  /* Bottom Left: 90 deg (PI/2) */
  draw_aa_corner(x + r, y + h - r, r, 3.14159f / 2.0f, c, alpha);
}

static void draw_filled_circle(int cx, int cy, int r, Color c, Uint8 alpha) {
  if (r <= 0)
    return;

  /* Optimize segments based on radius size */
  int segments = 4 * r;
  if (segments < 24)
    segments = 24;
  if (segments > 64)
    segments = 64; /* Cap at 64 for performance/stack safety */

  /* Vertices: 1 Center + N Inner + N Outer */
  /* Indices: N*3 (Center fan) + N*6 (AA ring) */
  int num_verts = 1 + 2 * segments;
  int num_indices = segments * 9;

  SDL_Vertex verts[129]; /* 1 + 2*64 = 129 */
  int indices[576];      /* 64 * 9 = 576 */

  /* Clamp if calculation was off (sanity check) */
  if (num_verts > 129)
    num_verts = 129;
  if (num_indices > 576)
    num_indices = 576;

  float rad_inner = (float)r - 0.5f;
  if (rad_inner < 0)
    rad_inner = 0;
  float rad_outer = (float)r + 0.5f;

  /* Center Vertex */
  verts[0].position.x = (float)cx;
  verts[0].position.y = (float)cy;
  verts[0].color = (SDL_Color){c.r, c.g, c.b, alpha};
  /* tex_coord handled by zero-init or ignored */

  for (int i = 0; i < segments; i++) {
    float angle = 2.0f * 3.14159f * i / segments;
    float cos_a = cosf(angle);
    float sin_a = sinf(angle);

    /* Inner Ring (Solid) */
    int idx_in = 1 + i;
    verts[idx_in].position.x = cx + cos_a * rad_inner;
    verts[idx_in].position.y = cy + sin_a * rad_inner;
    verts[idx_in].color = (SDL_Color){c.r, c.g, c.b, alpha};

    /* Outer Ring (Transparent) */
    int idx_out = 1 + segments + i;
    verts[idx_out].position.x = cx + cos_a * rad_outer;
    verts[idx_out].position.y = cy + sin_a * rad_outer;
    verts[idx_out].color = (SDL_Color){c.r, c.g, c.b, 0};

    /* Indices Construction */
    int next = (i + 1) % segments;
    int base_idx = i * 9;

    /* Center Fan: 0, In(i), In(next) */
    indices[base_idx + 0] = 0;
    indices[base_idx + 1] = 1 + i;
    indices[base_idx + 2] = 1 + next;

    /* AA Quad 1: In(i), Out(i), Out(next) */
    indices[base_idx + 3] = 1 + i;
    indices[base_idx + 4] = 1 + segments + i;
    indices[base_idx + 5] = 1 + segments + next;

    /* AA Quad 2: In(i), Out(next), In(next) */
    indices[base_idx + 6] = 1 + i;
    indices[base_idx + 7] = 1 + segments + next;
    indices[base_idx + 8] = 1 + next;
  }

  SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
  SDL_RenderGeometry(g_renderer, NULL, verts, num_verts, indices, num_indices);
}

static void render_background(int w, int h) {
  SDL_Rect bg_rect = {0, 0, w, h};
  if (g_bg_blurred) {
    /* Render blurred background */
    SDL_RenderCopy(g_renderer, g_bg_blurred, NULL, &bg_rect);
    /* Light overlay to darken slightly */
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(g_renderer, 15, 15, 15, 180);
    SDL_RenderFillRect(g_renderer, &bg_rect);
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_NONE);
  } else {
    /* Use primary color as fallback background */
    SDL_SetRenderDrawColor(g_renderer, g_theme.primary.r / 5,
                           g_theme.primary.g / 5, g_theme.primary.b / 5, 255);
    SDL_RenderClear(g_renderer);
  }
}

static void render_sidebar_item(SDL_Renderer *renderer, const char *label,
                                const char *icon_name, int x, int y,
                                bool active) {
  (void)icon_name; /* Unused for now */
  uint8_t r = 255, g = 255, b = 255;
  if (active) {
    /* Tertiary highlight for active */
    r = g_theme.tertiary.r;
    g = g_theme.tertiary.g;
    b = g_theme.tertiary.b;
  }

  /* Adjusted y + 33 for vertical centering */
  int ty;
  /* assuming button height is 50 based on call site usage */
  font_get_text_center_offset(label, 0, 50, NULL, &ty);
  font_draw_text(renderer, label, x + 40, y + ty, r, g, b);
  /* TODO: Draw icon if available */
}

static bool material_hit_test_rect(int x, int y, int w, int h, int mx, int my) {
  return (mx >= x && mx <= x + w && my >= y && my <= y + h);
}

static void render_sidebar_left_bg(int x_offset, int h) {
  /* Frosted Glass Effect */
  SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
  SDL_Rect sb = {x_offset, 0, SIDEBAR_W, h - CONTROL_BAR_H};

  SDL_SetRenderDrawColor(g_renderer, 15, 15, 15, 230);
  SDL_RenderFillRect(g_renderer, &sb);

  /* Highlights/Glassmorphism border */
  SDL_SetRenderDrawColor(g_renderer, 255, 255, 255, 40);
  SDL_Rect border = {x_offset + SIDEBAR_W - 1, 0, 1, h - CONTROL_BAR_H};
  SDL_RenderFillRect(g_renderer, &border);

  /* Inner Edge Close Handle */
  SDL_SetRenderDrawColor(g_renderer, 255, 255, 255, 20);
  SDL_Rect close_strip = {x_offset + SIDEBAR_W - 30, 0, 30, h - CONTROL_BAR_H};
  SDL_RenderFillRect(g_renderer, &close_strip);
}

static void render_sidebar_left_items(PlayerContext *ctx, int x_offset, int mx,
                                      int my) {
  /* Only process clicks when sidebar is fully open to prevent accidental
   * triggers */
  bool can_click = ctx->sidebar_left_anim >= 0.95f;
  bool mouse_down = can_click && (SDL_GetMouseState(NULL, NULL) &
                                  SDL_BUTTON(SDL_BUTTON_LEFT));
  int item_x = x_offset + 20;
  int item_y = 100;

  /* Now Playing */
  if (material_hit_test_rect(item_x, item_y, 240, 50, mx, my)) {
    if (mouse_down)
      ctx->current_scene = SCENE_NOW_PLAYING;
  }
  render_sidebar_item(g_renderer, "Now Playing", "queue_music", item_x, item_y,
                      ctx->current_scene == SCENE_NOW_PLAYING);

  /* Library */
  item_y += 60;
  if (material_hit_test_rect(item_x, item_y, 240, 50, mx, my)) {
    if (mouse_down) {
      if (ctx->current_scene != SCENE_LIBRARY) {
        ctx->current_scene = SCENE_LIBRARY;
        /* Default to Grid view when entering Library */
        ctx->library_view_mode = LIBRARY_VIEW_GRID;
      }
    }
  }
  render_sidebar_item(g_renderer, "Library", "library_music", item_x, item_y,
                      ctx->current_scene == SCENE_LIBRARY);

  /* Playlists */
  item_y += 60;
  bool is_playlist_view = (ctx->current_scene == SCENE_PLAYLISTS);
  if (material_hit_test_rect(item_x, item_y, 240, 50, mx, my)) {
    if (mouse_down) {
      ctx->current_scene = SCENE_PLAYLISTS;
    }
  }
  render_sidebar_item(g_renderer, "Playlists", "playlist_play", item_x, item_y,
                      is_playlist_view);

  /* Visualizers */
  item_y += 60;
  if (material_hit_test_rect(item_x, item_y, 240, 50, mx, my)) {
    if (mouse_down)
      ctx->current_scene = SCENE_VISUALIZER;
  }
  render_sidebar_item(g_renderer, "Visualizers", "equalizer", item_x, item_y,
                      ctx->current_scene == SCENE_VISUALIZER);

  /* Indicator bar for left sidebar (using tertiary color) */
  SDL_SetRenderDrawColor(g_renderer, g_theme.tertiary.r, g_theme.tertiary.g,
                         g_theme.tertiary.b, 200);
  /* Adjusted up 6px as requested (110->104, 170->164) */
  int ind_y = 104;
  if (ctx->current_scene == SCENE_LIBRARY) {
    ind_y = 164;
  } else if (ctx->current_scene == SCENE_PLAYLISTS) {
    ind_y = 224;
  } else if (ctx->current_scene == SCENE_VISUALIZER)
    ind_y = item_y + 4; /* Dynamic based on list */

  SDL_Rect ind_bar = {x_offset + SIDEBAR_W - 5, ind_y, 5, 30};
  SDL_RenderFillRect(g_renderer, &ind_bar);
}

static void render_sidebar_left_cog(int x_offset, int h) {
  /* Settings Cog at bottom left */
  int cog_sz = 35;
  int cog_x = x_offset + 30;
  int cog_y = h - CONTROL_BAR_H - 70;

  if (g_settings_icon) {
    SDL_Rect cog_rect = {cog_x, cog_y, cog_sz, cog_sz};
    SDL_RenderCopy(g_renderer, g_settings_icon, NULL, &cog_rect);
  } else {
    /* Fallback to procedural */
    SDL_SetRenderDrawColor(g_renderer, 220, 220, 220, 255);
    draw_filled_circle(cog_x + cog_sz / 2, cog_y + cog_sz / 2, 8,
                       (Color){220, 220, 220}, 255);
    for (int i = 0; i < 8; i++) {
      float angle = i * (3.14159f / 4.0f);
      int tx = cog_x + cog_sz / 2 + (int)(cosf(angle) * 11);
      int ty = cog_y + cog_sz / 2 + (int)(sinf(angle) * 11);
      draw_filled_circle(tx, ty, 3, (Color){220, 220, 220}, 255);
    }
    draw_filled_circle(cog_x + cog_sz / 2, cog_y + cog_sz / 2, 3,
                       (Color){15, 15, 15}, 255);
  }
}

static void render_sidebar_left(PlayerContext *ctx, int h) {
  if (!c_assert(ctx != NULL))
    return;
  if (ctx->sidebar_left_anim <= 0.0f)
    return;

  int x_offset = (int)((ctx->sidebar_left_anim - 1.0f) * SIDEBAR_W);
  int mx, my;
  window_get_mouse_pos(&mx, &my);

  render_sidebar_left_bg(x_offset, h);
  render_sidebar_left_items(ctx, x_offset, mx, my);
  render_sidebar_left_cog(x_offset, h);

  SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_NONE);
}

static void render_sidebar_right_bg(int x_offset, int h) {
  /* Frosted Glass Effect */
  SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
  SDL_Rect sb = {x_offset, 0, SIDEBAR_W, h - CONTROL_BAR_H};

  SDL_SetRenderDrawColor(g_renderer, 15, 15, 15, 230);
  SDL_RenderFillRect(g_renderer, &sb);

  /* Border */
  SDL_SetRenderDrawColor(g_renderer, 255, 255, 255, 40);
  SDL_Rect border = {x_offset, 0, 1, h - CONTROL_BAR_H};
  SDL_RenderFillRect(g_renderer, &border);

  /* Inner Edge Close Handle */
  SDL_SetRenderDrawColor(g_renderer, 255, 255, 255, 20);
  SDL_Rect close_strip = {x_offset, 0, 30, h - CONTROL_BAR_H};
  SDL_RenderFillRect(g_renderer, &close_strip);

  SDL_SetRenderDrawColor(g_renderer, g_theme.tertiary.r, g_theme.tertiary.g,
                         g_theme.tertiary.b, 200);
  SDL_Rect ind_bar = {x_offset, 20, 5, 60};
  SDL_RenderFillRect(g_renderer, &ind_bar);
}

static void render_sidebar_right_header(PlayerContext *ctx, int x_offset,
                                        int *out_start_y) {
  /* Dynamic Content Logic */
  const char *title = "Recents";

  if (ctx->sidebar_is_browsing) {
    if (ctx->browse_context_name[0] != '\0') {
      title = ctx->browse_context_name;
    } else {
      title = "Album Details";
    }
  } else if (ctx->queue_type == QUEUE_TYPE_ALBUM ||
             ctx->queue_type == QUEUE_TYPE_PLAYLIST) {
    if (ctx->queue_context_name[0] != '\0') {
      title = ctx->queue_context_name;
    }
  } else if (ctx->count > 1) {
    title = "Current Queue";
  }

  font_draw_text(g_renderer, title, x_offset + 50, 40, 255, 255, 255);

  int start_y = 100;

  /* Browsing Mode Art */
  if (ctx->sidebar_is_browsing && ctx->browse_context_art_path[0] != '\0') {
    SDL_Texture *art = get_or_load_icon(ctx->browse_context_art_path);
    if (art) {
      SDL_Rect art_rect = {x_offset + 50, 80, 180, 180};
      SDL_RenderCopy(g_renderer, art, NULL, &art_rect);
      /* Overlay Play Icon on Header Art */
      draw_play_icon(x_offset + 50 + 90, 80 + 90, 30, g_theme.primary);
      start_y = 280;
    }
  }
  /* Album mode special: Show large art (If not browsing, and is Queue Album) */
  else if (ctx->queue_type == QUEUE_TYPE_ALBUM &&
           ctx->queue_context_art_path[0] != '\0') {
    SDL_Texture *art = get_or_load_icon(ctx->queue_context_art_path);
    if (art) {
      SDL_Rect art_rect = {x_offset + 50, 80, 180, 180};
      SDL_RenderCopy(g_renderer, art, NULL, &art_rect);
      /* Overlay Play Icon on Header Art */
      draw_play_icon(x_offset + 50 + 90, 80 + 90, 30, g_theme.primary);
      start_y = 280;
    }
  }
  *out_start_y = start_y;
}

static void render_sidebar_right_list(PlayerContext *ctx, int x_offset,
                                      int start_y, int h) {
  /* Handle Browsing Mode vs Queue/Recents */
  if (ctx->sidebar_is_browsing) {
    size_t count = ctx->browse_track_count;
    int list_area_h = h - start_y - CONTROL_BAR_H;
    if (list_area_h < 0)
      list_area_h = 0;

    ctx->sidebar_right_max_scroll = (float)((int)count * 50 - list_area_h);
    if (ctx->sidebar_right_max_scroll < 0)
      ctx->sidebar_right_max_scroll = 0;

    SDL_Rect clip = {x_offset, start_y, SIDEBAR_W, list_area_h};
    SDL_RenderSetClipRect(g_renderer, &clip);

    for (size_t i = 0; i < count; i++) {
      int row_y = start_y + (int)i * 50 - (int)ctx->sidebar_right_scroll_y;
      if (row_y + 50 < start_y)
        continue;
      if (row_y > start_y + list_area_h)
        break;

      Track *t = &ctx->browse_tracks[i];
      uint8_t r = 200, g = 200, b = 200;

      /* Highlight if playing */
      if (ctx->state == PLAYER_STATE_PLAYING &&
          strcmp(t->filepath, ctx->songs[ctx->current_index].path) == 0) {
        r = g = b = 255;
        /* Maybe add a playing indicator icon too? */
      }

      draw_play_icon(x_offset + 60, row_y + 25, 12, (Color){255, 255, 255});

      int ty;
      font_get_text_center_offset(t->title, 180, 50, NULL, &ty);
      font_draw_text_limit(g_renderer, t->title, x_offset + 92, row_y + ty, r,
                           g, b, 170);
    }
    SDL_RenderSetClipRect(g_renderer, NULL);
    return;
  }

  size_t list_count = ctx->recents_count;
  Song *list = ctx->recents;
  bool is_queue = false;

  if (ctx->queue_type == QUEUE_TYPE_ALBUM ||
      ctx->queue_type == QUEUE_TYPE_PLAYLIST) {
    list_count = ctx->count;
    list = ctx->songs;
    is_queue = true;
  } else if (ctx->count > 1) {
    list_count = ctx->count;
    list = ctx->songs;
    is_queue = true;
  }

  int list_area_h = h - start_y - CONTROL_BAR_H;
  if (list_area_h < 0)
    list_area_h = 0;

  /* Update max scroll */
  ctx->sidebar_right_max_scroll = (float)((int)list_count * 50 - list_area_h);
  if (ctx->sidebar_right_max_scroll < 0)
    ctx->sidebar_right_max_scroll = 0;

  /* Clip to sidebar area */
  SDL_Rect clip = {x_offset, start_y, SIDEBAR_W, list_area_h};
  SDL_RenderSetClipRect(g_renderer, &clip);

  /* Song List */
  for (size_t i = 0; i < list_count; i++) {
    int row_y = start_y + (int)i * 50 - (int)ctx->sidebar_right_scroll_y;

    /* Simple Culling */
    if (row_y + 50 < start_y)
      continue;
    if (row_y > start_y + list_area_h)
      break;

    uint8_t r = 200, g = 200, b = 200;
    if (is_queue && i == (size_t)ctx->current_index) {
      r = 255;
      g = 255;
      b = 255;
    }

    /* Play Button instead of Album Art */
    /* Play Button instead of Album Art */
    draw_play_icon(x_offset + 60, row_y + 25, 12, (Color){255, 255, 255});

    /* Song Title - truncates if too long */
    /* Adjusted y + 33 for vertical centering (was 18) */
    int ty;
    font_get_text_center_offset(list[i].title, 180, 50, NULL, &ty);

    /* Request: "remove that song name in right side bar ... and enact
     * spotlight" */
    bool hide_title = false;
    if (ctx->spotlight_active && ctx->spotlight_is_singular) {
      if (strcmp(list[i].path, ctx->spotlight_song_path) == 0) {
        hide_title = true;
      }
    }

    if (!hide_title) {
      font_draw_text_limit(g_renderer, list[i].title, x_offset + 92, row_y + ty,
                           r, g, b, 170);
    }
  }

  SDL_RenderSetClipRect(g_renderer, NULL);
}

static void render_sidebar_right(PlayerContext *ctx, int w, int h) {
  if (!c_assert(ctx != NULL))
    return;
  if (ctx->sidebar_right_anim <= 0.0f)
    return;

  int x_offset = (int)(w - (ctx->sidebar_right_anim * SIDEBAR_W));

  render_sidebar_right_bg(x_offset, h);

  if (ctx->current_scene == SCENE_VISUALIZER) {
    int pad = 40;
    int btn_w = 220;
    int header_y = 20;
    const VisPlugin *active = visualizer_get_active();

    if (ctx->visualizer_show_settings) {
      /* --- Settings Mode --- */
      int back_y = header_y + 50;
      material_draw_rounded_rect(x_offset + pad, back_y, btn_w, 40, 8,
                                 (Color){80, 80, 80}, 255);
      int tw_back = font_get_text_width("Back");
      font_draw_text(g_renderer, "Back", x_offset + pad + (btn_w - tw_back) / 2,
                     back_y + 28, 255, 255, 255);

      SDL_Rect viewport = {x_offset, back_y + 50, SIDEBAR_W,
                           h - CONTROL_BAR_H - (back_y + 50)};
      SDL_RenderSetClipRect(g_renderer, &viewport);

      int sy = back_y + 70 - (int)ctx->visualizer_scroll_y;
      if (active && active->get_param_count && active->get_param) {
        int count = active->get_param_count();
        for (int i = 0; i < count; i++) {
          const VisParam *p = active->get_param(i);
          if (!p)
            continue;
          font_draw_text(g_renderer, p->name, x_offset + pad, sy + 22, 200, 200,
                         200);
          sy += 35;
          if (p->type == VIS_PARAM_FLOAT || p->type == VIS_PARAM_INT) {
            void *val_ptr = p->value_ptr;
            SDL_SetRenderDrawColor(g_renderer, 80, 80, 80, 255);
            SDL_Rect track = {x_offset + pad, sy + 10, 200, 4};
            SDL_RenderFillRect(g_renderer, &track);
            float val_f = (p->type == VIS_PARAM_FLOAT) ? *(float *)val_ptr
                                                       : (float)*(int *)val_ptr;
            float mk = (val_f - p->min) / (p->max - p->min);
            mk = mk < 0 ? 0 : (mk > 1 ? 1 : mk);
            SDL_SetRenderDrawColor(g_renderer, g_theme.primary.r,
                                   g_theme.primary.g, g_theme.primary.b, 255);
            SDL_Rect knob = {x_offset + pad + (int)(mk * 200) - 8, sy + 4, 16,
                             16};
            SDL_RenderFillRect(g_renderer, &knob);
            sy += 40;
          } else if (p->type == VIS_PARAM_ENUM) {
            /* Dropdown style for enums */
            SDL_SetRenderDrawColor(g_renderer, 60, 60, 60, 255);
            SDL_Rect box = {x_offset + pad, sy, 200, 32};
            SDL_RenderFillRect(g_renderer, &box);
            int *val = (int *)p->value_ptr;
            const char *opt_text =
                (p->options && p->options[*val]) ? p->options[*val] : "None";
            font_draw_text(g_renderer, opt_text, x_offset + pad + 10, sy + 24,
                           255, 255, 255);
            sy += 45;
          } else if (p->type == VIS_PARAM_BOOL) {
            /* Checkbox style for booleans - small square */
            SDL_SetRenderDrawColor(g_renderer, 60, 60, 60, 255);
            SDL_Rect box = {x_offset + pad, sy, 32, 32};
            SDL_RenderFillRect(g_renderer, &box);
            if (*(bool *)p->value_ptr) {
              SDL_SetRenderDrawColor(g_renderer, g_theme.primary.r,
                                     g_theme.primary.g, g_theme.primary.b, 255);
              SDL_Rect check = {x_offset + pad + 4, sy + 4, 24, 24};
              SDL_RenderFillRect(g_renderer, &check);
            }
            sy += 45;
          }
        }
      }
      ctx->visualizer_max_scroll =
          (float)(sy + (int)ctx->visualizer_scroll_y - (back_y + 50));
      SDL_RenderSetClipRect(g_renderer, NULL);

    } else if (ctx->visualizer_show_list) {
      /* --- List Mode: Select Visualizer --- */
      int back_y = header_y + 50;
      material_draw_rounded_rect(x_offset + pad, back_y, btn_w, 40, 8,
                                 (Color){80, 80, 80}, 255);
      int tw_back = font_get_text_width("Back");
      font_draw_text(g_renderer, "Back", x_offset + pad + (btn_w - tw_back) / 2,
                     back_y + 28, 255, 255, 255);

      int sy = back_y + 60;
      /* Active first */
      int active_idx = g_active_visualizer_index;
      for (int i = 0; i < g_visualizer_count; i++) {
        if (i == active_idx) {
          material_draw_rounded_rect(x_offset + pad, sy, btn_w, 40, 8,
                                     g_theme.primary, 255);
          int tw = font_get_text_width(g_visualizers[i].name);
          font_draw_text(g_renderer, g_visualizers[i].name,
                         x_offset + pad + (btn_w - tw) / 2, sy + 28, 255, 255,
                         255);
          sy += 50;
          break;
        }
      }
      /* Others */
      for (int i = 0; i < g_visualizer_count; i++) {
        if (i == active_idx)
          continue;
        material_draw_rounded_rect(x_offset + pad, sy, btn_w, 40, 8,
                                   (Color){60, 60, 60}, 255);
        int tw = font_get_text_width(g_visualizers[i].name);
        font_draw_text(g_renderer, g_visualizers[i].name,
                       x_offset + pad + (btn_w - tw) / 2, sy + 28, 200, 200,
                       200);
        sy += 50;
      }
    } else {
      /* --- Default Mode: Header + Buttons --- */
      const char *active_name = active ? active->name : "None";

      /* 1. Header showing current visualizer */
      material_draw_rounded_rect(x_offset + pad, header_y, btn_w, 40, 6,
                                 (Color){60, 60, 60}, 255);
      int tw_name = font_get_text_width(active_name);
      font_draw_text(g_renderer, active_name,
                     x_offset + pad + (btn_w - tw_name) / 2, header_y + 28, 255,
                     255, 255);

      /* 2. Change Visualizer Button */
      int sy = header_y + 50;
      material_draw_rounded_rect(x_offset + pad, sy, btn_w, 40, 8,
                                 g_theme.primary, 255);
      int tw_cv = font_get_text_width("Change Visualizer");
      font_draw_text(g_renderer, "Change Visualizer",
                     x_offset + pad + (btn_w - tw_cv) / 2, sy + 28, 255, 255,
                     255);

      /* 3. Visualizer Settings Button */
      sy += 50;
      material_draw_rounded_rect(x_offset + pad, sy, btn_w, 40, 8,
                                 (Color){80, 80, 80}, 255);
      int tw_s = font_get_text_width("Visualizer Settings");
      font_draw_text(g_renderer, "Visualizer Settings",
                     x_offset + pad + (btn_w - tw_s) / 2, sy + 28, 255, 255,
                     255);
    }
  } else {
    int start_y = 0;
    render_sidebar_right_header(ctx, x_offset, &start_y);
    render_sidebar_right_list(ctx, x_offset, start_y, h);
  }

  SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_NONE);
}

static void render_handle_button(SDL_Renderer *renderer, int bx, int by,
                                 int btn_w, int btn_h, bool is_left, int mx,
                                 int my) {
  /* Calculate distance to center of button */
  int cx = bx + btn_w / 2;
  int c_y = by + btn_h / 2;
  float dist = sqrtf(powf(mx - cx, 2) + powf(my - c_y, 2));

  float range = 75.0f;
  float norm = 1.0f - (dist / range);
  if (norm < 0.0f)
    norm = 0.0f;

  /* Opacity: Base 0, ramps to 255 when close */
  Uint8 alpha = (Uint8)(norm * 255.0f);

  if (alpha < 5)
    return; /* Don't draw if invisible */

  float scale = 1.0f + (norm * 0.2f); /* Up to 1.2x */
  if (scale < 1.0f)
    scale = 1.0f;

  /* Draw Scaled Box */
  int sw = (int)(btn_w * scale);
  int sh = (int)(btn_h * scale);
  int sx = cx - sw / 2;
  int sy = c_y - sh / 2;

  /* Use smooth rounded rect */
  material_draw_rounded_rect(sx, sy, sw, sh, 4, (Color){20, 20, 20}, alpha);

  /* Border */
  /* Inner border effect by drawing smaller rounded rect? or just leave solid
   */

  /* Arrow */
  SDL_SetRenderDrawColor(renderer, 255, 255, 255, alpha);
  /* Use AA lines or simple lines, alpha handles blending */
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

  int acx = sx + sw / 2;
  int acy = sy + sh / 2;

  /* Simple arrow geometry */
  if (is_left) {
    SDL_RenderDrawLine(renderer, acx - 3, acy - 6, acx + 3, acy);
    SDL_RenderDrawLine(renderer, acx - 3, acy + 6, acx + 3, acy);
  } else {
    SDL_RenderDrawLine(renderer, acx + 3, acy - 6, acx - 3, acy);
    SDL_RenderDrawLine(renderer, acx + 3, acy + 6, acx - 3, acy);
  }
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

static void render_flyout_buttons(PlayerContext *ctx, int w, int h) {
  int mx, my;
  window_get_mouse_pos(&mx, &my);
  int cy = (h - CONTROL_BAR_H) / 2;
  int btn_w = 24;
  int btn_h = 60;
  int btn_y = cy - btn_h / 2;

  /* Left Flyout Handle */
  if (ctx->sidebar_left_anim < 0.2f) {
    render_handle_button(g_renderer, 0, btn_y, btn_w, btn_h, true, mx, my);
  }

  /* Right Flyout Handle */
  if (ctx->sidebar_right_anim < 0.2f) {
    render_handle_button(g_renderer, w - btn_w, btn_y, btn_w, btn_h, false, mx,
                         my);
  }
}

static void render_now_playing_art(SDL_Renderer *renderer, PlayerContext *ctx,
                                   int x, int y, int w, int h) {
  (void)ctx;
  (void)y;
  if (g_bg_texture) {
    int art_size = 350;
    /* Adjust art_size if content area is too small */
    if (art_size > w - 40)
      art_size = w - 40;

    int art_x = x + (w - art_size) / 2;
    int art_y = (h - CONTROL_BAR_H - art_size) / 2 - 20;

    /* Ghosting Effect: Skip rendering art if spotlight is active and this is
     * the playing album */
    bool is_picked_up = false;
    if (ctx->spotlight_active && ctx->current_index >= 0) {
      if (strcasecmp(ctx->library_albums[ctx->spotlight_album_idx].name,
                     ctx->songs[ctx->current_index].album) == 0) {
        is_picked_up = true;
      }
    }

    if (!is_picked_up) {
      /* Subtle accent glow effect around album art */
      SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
      for (int i = 3; i >= 1; i--) {
        int alpha = 20 + (3 - i) * 15; /* 20, 35, 50 */
        SDL_SetRenderDrawColor(renderer, g_theme.primary.r, g_theme.primary.g,
                               g_theme.primary.b, (Uint8)alpha);
        SDL_Rect glow = {art_x - i * 2, art_y - i * 2, art_size + i * 4,
                         art_size + i * 4};
        SDL_RenderDrawRect(renderer, &glow);
      }
      SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

      SDL_Rect art_rect = {art_x, art_y, art_size, art_size};
      SDL_RenderCopy(renderer, g_bg_texture, NULL, &art_rect);
    }
  } else {
    int tw = font_get_text_width("Drag & Drop Music");
    font_draw_text(renderer, "Drag & Drop Music", x + (w - tw) / 2, h / 2, 255,
                   255, 255);
  }
}

static void render_now_playing_info(SDL_Renderer *renderer, PlayerContext *ctx,
                                    int x, int y, int w, int h) {
  (void)y;
  if (g_bg_texture && ctx->current_index >= 0) {
    int art_size = 350;
    if (art_size > w - 40)
      art_size = w - 40;
    int art_y = (h - CONTROL_BAR_H - art_size) / 2 - 20;

    const char *title = ctx->songs[ctx->current_index].title;
    int tw = font_get_text_width(title);
    font_draw_text(renderer, title, x + (w - tw) / 2, art_y + art_size + 40,
                   255, 255, 255);

    const char *artist = ctx->songs[ctx->current_index].artist;
    if (artist[0] == '\0')
      artist = "Unknown Artist";

    tw = font_get_text_width(artist);
    font_draw_text(renderer, artist, x + (w - tw) / 2, art_y + art_size + 70,
                   180, 180, 180);
  }
}

static void render_library_header(PlayerContext *ctx, int x, int y, int w) {
  int offset_y = (int)ctx->library_header_offset;

  /* Header Plate - solid background to cover items scrolling behind */
  /* Header Plate - solid background to cover items scrolling behind */
  /* Extended background for search bar area with gradient fade */
  int header_h = 130 + offset_y;
  if (header_h > 0) {
    /* Main solid bg */
    SDL_SetRenderDrawColor(g_renderer, 18, 18, 18, 255);
    SDL_Rect plate = {x, y, w, header_h};
    SDL_RenderFillRect(g_renderer, &plate);

    /* Gradient fade at bottom */
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
    for (int i = 0; i < 20; i++) {
      SDL_SetRenderDrawColor(g_renderer, 18, 18, 18,
                             (Uint8)(255 * (1.0f - (float)i / 20.0f)));
      SDL_Rect line = {x, y + header_h + i, w, 1};
      SDL_RenderFillRect(g_renderer, &line);
    }
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_NONE);
  }

  /* Search Bar (Only in Library List/Grid) */
  int search_w = w - 120; // Reduced to make space for view toggle
  int search_h = 40;

  /* Vertically center text (used for toggle button too) */
  int text_y = y + 20 + (search_h - 16) / 2 + 14 + offset_y;

  if (ctx->library_view_mode == LIBRARY_VIEW_PLAYLISTS) {
    /* Playlist Mode Header */
    font_draw_text(g_renderer, "All Playlists", x + 30, text_y, 255, 255, 255);
  } else {
    /* Search Input */
    int r = 6;
    /* Border (Outer) */
    Color border_color =
        ctx->is_typing_search ? g_theme.primary : (Color){80, 80, 80};
    material_draw_rounded_rect(x + 20, y + 20 + offset_y, search_w, search_h, r,
                               border_color, 255);

    /* Background (Inner) */
    material_draw_rounded_rect(x + 20 + 1, y + 20 + 1 + offset_y, search_w - 2,
                               search_h - 2, r - 1, (Color){40, 40, 40}, 200);

    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_NONE);

    /* text_y is already calc above */

    if (ctx->is_typing_search) {
      char display_buf[128];
      if ((SDL_GetTicks() / 500) % 2 == 0) {
        snprintf(display_buf, sizeof(display_buf), "%s|", ctx->search_query);
      } else {
        snprintf(display_buf, sizeof(display_buf), "%s", ctx->search_query);
      }
      font_draw_text(g_renderer, display_buf, x + 30, text_y, 255, 255, 255);
    } else {
      font_draw_text(g_renderer,
                     ctx->search_query[0] ? ctx->search_query
                                          : "Search library...",
                     x + 30, text_y, 180, 180, 180);
    }
  }

  /* View Mode Toggle Button */
  int vt_x = x + 20 + search_w + 10;
  int vt_y = y + 20 + offset_y;
  int vt_w = 70;

  /* Logic for 3-way toggle or separate buttons? Let's use the existing slot for
   * Mode Toggle */
  /* If in List/Grid -> toggle to other. */
  /* If in Playlist -> Show "Grid" to go back to grid? */

  const char *vt_text = "Grid";
  if (ctx->library_view_mode == LIBRARY_VIEW_GRID)
    vt_text = "List";
  if (ctx->library_view_mode == LIBRARY_VIEW_LIST)
    vt_text = "Grid";
  if (ctx->library_view_mode == LIBRARY_VIEW_PLAYLISTS)
    vt_text = "Back";

  material_draw_rounded_rect(vt_x, vt_y, vt_w, search_h, 6, (Color){60, 60, 60},
                             255);
  int vt_tw = font_get_text_width(vt_text);
  font_draw_text(g_renderer, vt_text, vt_x + (vt_w - vt_tw) / 2, text_y, 255,
                 255, 255);

  /* Chips (Filter) */
  int chip_y = y + 75 + offset_y;
  int cur_x = x + 20;
  const char *filters[] = {"All", "Favorites", "Recent"};
  for (int i = 0; i < 3; i++) {
    int tw = font_get_text_width(filters[i]);
    int cw = tw + 30;
    bool active = (ctx->library_filter_mode == (LibraryFilterMode)i);
    material_draw_rounded_rect(cur_x, chip_y, cw, 32, 16,
                               active ? g_theme.primary : (Color){50, 50, 50},
                               255);
    font_draw_text(g_renderer, filters[i], cur_x + 15, chip_y + 22,
                   active ? 255 : 200, active ? 255 : 200, active ? 255 : 200);
    cur_x += cw + 10;
  }

  /* Sort Display Area (Bottom Right) */
  int sort_x = x + w - 160;
  font_draw_text(g_renderer, "Sort:", sort_x, chip_y + 22, 150, 150, 150);
  const char *sort_names[] = {"A-Z", "Artist", "Year", "Recent"};

  font_draw_text(g_renderer, sort_names[ctx->library_sort_mode], sort_x + 50,
                 chip_y + 22, 255, 255, 255);
}

static void draw_heart(int x, int y, int size, Color c, bool filled) {
  SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(g_renderer, c.r, c.g, c.b, 255);

  float r = size * 0.3f;
  /* Top two lobes */
  draw_filled_circle(x - (int)r, y - (int)r, (int)r, c, 255);
  draw_filled_circle(x + (int)r, y - (int)r, (int)r, c, 255);

  /* Bottom triangle */
  draw_aa_triangle((float)x - size * 0.65f, (float)y - r * 0.5f,
                   (float)x + size * 0.65f, (float)y - r * 0.5f, (float)x,
                   (float)y + size * 0.7f, c);

  if (!filled) {
    /* Draw a smaller dark heart inside to create outline effect if not filled
     */
    /* This is easier than AA lines for a solid heart silhouette */
    Color bg = {30, 30, 30};
    float inner_scale = 0.7f;
    int isize = (int)(size * inner_scale);
    float ir = r * inner_scale;
    draw_filled_circle(x - (int)ir, y - (int)ir, (int)ir, bg, 255);
    draw_filled_circle(x + (int)ir, y - (int)ir, (int)ir, bg, 255);
    draw_aa_triangle((float)x - isize * 0.65f, (float)y - ir * 0.5f,
                     (float)x + isize * 0.65f, (float)y - ir * 0.5f, (float)x,
                     (float)y + isize * 0.7f, bg);
  }
}

static void render_library_item(PlayerContext *ctx, Album *album, int idx,
                                int px, int py, int card_w, int card_h) {
  if (!album) {
    log_message("ERROR", "render_library_item: album is NULL");
    return;
  }
  bool is_hovered = (ctx->library_hovered_album_idx == idx);
  bool heart_hovered = (ctx->library_hovered_heart_idx == idx);

  /* Hover Lift Effect */
  if (is_hovered) {
    py -= 6;
  }

  /* Album Card Background & Accent Glow */
  if (is_hovered) {
    /* Subtle glow behind card */
    material_draw_rounded_rect(px - 2, py - 2, card_w + 4, card_h + 4, 14,
                               g_theme.primary, 100);
  }

  /* Ghost Effect (Phase 1 Logic) */
  Uint8 alpha = 255;
  if (ctx->spotlight_active && ctx->spotlight_album_idx == (int)album->id) {
    alpha = 0;
  }

  material_draw_rounded_rect(px, py, card_w, card_h, 12, g_theme.surface,
                             alpha);

  /* Art (Square) */
  int art_sz = card_w - 20;
  if (album->art_filename[0] != '\0') {
    SDL_Texture *art = get_or_load_icon(album->art_filename);
    if (art) {
      /* Ghosting Effect: Skip rendering art if spotlight is active for this
       * album */
      bool is_picked_up =
          (ctx->spotlight_active && ctx->spotlight_album_idx == idx);

      if (!is_picked_up) {
        SDL_Rect art_rect = {px + 10, py + 10, art_sz, art_sz};
        SDL_RenderCopy(g_renderer, art, NULL, &art_rect);

        /* Play Icon Overlay on Hover */
        if (is_hovered && !heart_hovered) {
          draw_play_icon(px + 10 + art_sz / 2, py + 10 + art_sz / 2, 25,
                         g_theme.primary);
        }
      }
    } else {
      material_draw_rounded_rect(px + 10, py + 10, art_sz, art_sz, 8,
                                 (Color){60, 60, 60}, 255);
    }
  } else {
    material_draw_rounded_rect(px + 10, py + 10, art_sz, art_sz, 8,
                               (Color){60, 60, 60}, 255);
  }

  /* Heart Icon (Top Right) */
  int heart_x = px + card_w - 25;
  int heart_y = py + 25;
  Color heart_col =
      (album->is_favorite) ? (Color){255, 50, 80} : (Color){150, 150, 150};
  if (heart_hovered) {
    heart_col = (Color){255, 100, 120};
  }
  draw_heart(heart_x, heart_y, 10, heart_col,
             album->is_favorite || heart_hovered);

  /* Text */
  /* Title: Adjusted from +20 to +45 to clear art (baseline logic) */
  font_draw_text_limit(g_renderer, album->name, px + 10, py + art_sz + 45, 255,
                       255, 255, card_w - 20);

  /* Artist (if available) - Gray */
  /* Artist: Adjusted from +45 to +75 */
  if (album->artist[0] != '\0') {
    font_draw_text_limit(g_renderer, album->artist, px + 10, py + art_sz + 75,
                         180, 180, 180, card_w - 20);
  } else {
    font_draw_text_limit(g_renderer, "Various Artists", px + 10,
                         py + art_sz + 45, 150, 150, 150, card_w - 20);
  }
}

/* Case-insensitive substring match helper */
static bool str_contains_icase(const char *haystack, const char *needle) {
  if (!needle || needle[0] == '\0')
    return true;
  if (!haystack)
    return false;

  size_t needle_len = strlen(needle);
  size_t haystack_len = strlen(haystack);

  if (needle_len > haystack_len)
    return false;

  for (size_t i = 0; i <= haystack_len - needle_len; i++) {
    bool match = true;
    for (size_t j = 0; j < needle_len; j++) {
      char h = haystack[i + j];
      char n = needle[j];
      /* Simple ASCII tolower */
      if (h >= 'A' && h <= 'Z')
        h += 32;
      if (n >= 'A' && n <= 'Z')
        n += 32;
      if (h != n) {
        match = false;
        break;
      }
    }
    if (match)
      return true;
  }
  return false;
}

#include <stdlib.h>

static int compare_albums_name(const void *a, const void *b, void *ctx) {
  PlayerContext *p = (PlayerContext *)ctx;
  size_t idx_a = *(const size_t *)a;
  size_t idx_b = *(const size_t *)b;
  return strcasecmp(p->library_albums[idx_a].name,
                    p->library_albums[idx_b].name);
}

static int compare_albums_artist(const void *a, const void *b, void *ctx) {
  PlayerContext *p = (PlayerContext *)ctx;
  size_t idx_a = *(const size_t *)a;
  size_t idx_b = *(const size_t *)b;
  int res = strcasecmp(p->library_albums[idx_a].artist,
                       p->library_albums[idx_b].artist);
  if (res == 0)
    return strcasecmp(p->library_albums[idx_a].name,
                      p->library_albums[idx_b].name);
  return res;
}

static int compare_albums_year(const void *a, const void *b, void *ctx) {
  PlayerContext *p = (PlayerContext *)ctx;
  size_t idx_a = *(const size_t *)a;
  size_t idx_b = *(const size_t *)b;
  int res = p->library_albums[idx_b].year - p->library_albums[idx_a].year;
  if (res == 0)
    return strcasecmp(p->library_albums[idx_a].name,
                      p->library_albums[idx_b].name);
  return res;
}

static int compare_albums_id(const void *a, const void *b, void *ctx) {
  PlayerContext *p = (PlayerContext *)ctx;
  size_t idx_a = *(const size_t *)a;
  size_t idx_b = *(const size_t *)b;
  return (int)p->library_albums[idx_b].id - (int)p->library_albums[idx_a].id;
}

static int compare_tracks_title(const void *a, const void *b, void *ctx) {
  PlayerContext *p = (PlayerContext *)ctx;
  size_t idx_a = *(const size_t *)a;
  size_t idx_b = *(const size_t *)b;
  return strcasecmp(p->library_tracks[idx_a].title,
                    p->library_tracks[idx_b].title);
}

static int compare_tracks_artist(const void *a, const void *b, void *ctx) {
  PlayerContext *p = (PlayerContext *)ctx;
  size_t idx_a = *(const size_t *)a;
  size_t idx_b = *(const size_t *)b;
  int res = strcasecmp(p->library_tracks[idx_a].artist,
                       p->library_tracks[idx_b].artist);
  if (res == 0)
    return strcasecmp(p->library_tracks[idx_a].title,
                      p->library_tracks[idx_b].title);
  return res;
}

static int compare_tracks_year(const void *a, const void *b, void *ctx) {
  PlayerContext *p = (PlayerContext *)ctx;
  size_t idx_a = *(const size_t *)a;
  size_t idx_b = *(const size_t *)b;
  int res = p->library_tracks[idx_b].year - p->library_tracks[idx_a].year;
  if (res == 0)
    return strcasecmp(p->library_tracks[idx_a].title,
                      p->library_tracks[idx_b].title);
  return res;
}

static int compare_tracks_id(const void *a, const void *b, void *ctx) {
  PlayerContext *p = (PlayerContext *)ctx;
  size_t idx_a = *(const size_t *)a;
  size_t idx_b = *(const size_t *)b;
  return (int)p->library_tracks[idx_b].id - (int)p->library_tracks[idx_a].id;
}

static void update_library_filter(PlayerContext *ctx) {
  /* Albums Filter */
  if (ctx->library_albums && ctx->library_album_count > 0) {

    /* Ensure buffer is large enough for all possible albums */
    ctx->library_filtered_indices =
        realloc(ctx->library_filtered_indices,
                sizeof(size_t) * ctx->library_album_count);
    if (ctx->library_filtered_indices == NULL) {
      log_message("ERROR",
                  "update_library_filter: realloc FAILED for album indices");
    }
    ctx->library_filtered_count = 0;
    for (size_t i = 0; i < ctx->library_album_count; i++) {
      Album *album = &ctx->library_albums[i];

      /* Filter by Favorites */
      if (ctx->library_filter_mode == LIBRARY_FILTER_FAVORITES &&
          !album->is_favorite)
        continue;

      /* Filter by Recent */
      if (ctx->library_filter_mode == LIBRARY_FILTER_RECENT) {
        bool in_recent = false;
        for (size_t r = 0; r < ctx->recents_count; r++) {
          /* Heuristic: Match by Album Name or Art Path */
          if (strcasecmp(ctx->recents[r].album, album->name) == 0) {
            in_recent = true;
            break;
          }
          /* Fallback: Check if song title matches (less reliable for album
           * filter but helpful) */
        }
        if (!in_recent)
          continue;
      }

      /* Search Query Filter */
      if (ctx->search_query[0] == '\0' ||
          str_contains_icase(album->name, ctx->search_query) ||
          str_contains_icase(album->artist, ctx->search_query)) {
        ctx->library_filtered_indices[ctx->library_filtered_count++] = i;
      }
    }

    /* Apply Sorting */
    if (ctx->library_filtered_count > 1) {
      int (*cmp)(const void *, const void *, void *) = compare_albums_name;
      if (ctx->library_sort_mode == LIBRARY_SORT_ARTIST)
        cmp = compare_albums_artist;
      else if (ctx->library_sort_mode == LIBRARY_SORT_YEAR)
        cmp = compare_albums_year;
      else if (ctx->library_sort_mode == LIBRARY_SORT_RECENT)
        cmp = compare_albums_id;

      qsort_r(ctx->library_filtered_indices, ctx->library_filtered_count,
              sizeof(size_t), cmp, ctx);
    }
  } else {
    ctx->library_filtered_count = 0;
  }

  /* Tracks Filter */
  if (ctx->library_tracks && ctx->library_track_count > 0) {

    /* Ensure buffer is large enough for all possible tracks */
    ctx->library_filtered_track_indices =
        realloc(ctx->library_filtered_track_indices,
                sizeof(size_t) * ctx->library_track_count);
    if (ctx->library_filtered_track_indices == NULL) {
      log_message("ERROR",
                  "update_library_filter: realloc FAILED for track indices");
    }
    ctx->library_filtered_track_count = 0;
    for (size_t i = 0; i < ctx->library_track_count; i++) {
      Track *track = &ctx->library_tracks[i];

      /* Filter by Favorites - TODO: Add track favorite support if DB updated */
      if (ctx->library_filter_mode == LIBRARY_FILTER_FAVORITES &&
          !track->is_favorite) {
        continue;
      }

      /* Filter by Recent */
      if (ctx->library_filter_mode == LIBRARY_FILTER_RECENT) {
        bool in_recent = false;
        for (size_t r = 0; r < ctx->recents_count; r++) {
          if (strcmp(ctx->recents[r].path, track->filepath) == 0) {
            in_recent = true;
            break;
          }
        }
        if (!in_recent)
          continue;
      }
      if (ctx->search_query[0] == '\0' ||
          str_contains_icase(track->title, ctx->search_query) ||
          str_contains_icase(track->artist, ctx->search_query)) {
        ctx->library_filtered_track_indices
            [ctx->library_filtered_track_count++] = i;
      }
    }

    /* Apply Sorting for Tracks */
    if (ctx->library_filtered_track_count > 1) {
      int (*cmp)(const void *, const void *, void *) = compare_tracks_title;
      if (ctx->library_sort_mode == LIBRARY_SORT_ARTIST)
        cmp = compare_tracks_artist;
      else if (ctx->library_sort_mode == LIBRARY_SORT_YEAR)
        cmp = compare_tracks_year;
      else if (ctx->library_sort_mode == LIBRARY_SORT_RECENT)
        cmp = compare_tracks_id;

      qsort_r(ctx->library_filtered_track_indices,
              ctx->library_filtered_track_count, sizeof(size_t), cmp, ctx);
    }
  } else {
    ctx->library_filtered_track_count = 0;
  }

  ctx->library_needs_filter = false;
}

static void render_search_album_row(PlayerContext *ctx, int x, int y, int w) {
  int card_w = 160;
  int card_h = 230;
  int padding = 20;

  SDL_Rect clip = {x + 20, y, w - 40, card_h};
  SDL_RenderSetClipRect(g_renderer, &clip);

  int mx, my;
  window_get_mouse_pos(&mx, &my);

  ctx->library_hovered_album_idx = -1;
  ctx->library_hovered_heart_idx = -1;

  for (size_t i = 0; i < ctx->library_filtered_count; i++) {
    size_t album_idx = ctx->library_filtered_indices[i];
    Album *album = &ctx->library_albums[album_idx];

    int px = x + 20 + (int)i * (card_w + padding) -
             (int)ctx->library_search_album_scroll_x;

    /* Visibility check */
    if (px + card_w < x + 20)
      continue;
    if (px > x + w - 20)
      break;

    /* Hover Detection */
    if (mx >= px && mx <= px + card_w && my >= y && my <= y + card_h) {
      ctx->library_hovered_album_idx = (int)album_idx;
      /* Heart hit area with padding */
      int hx = px + card_w - 25;
      int hy = y + 25;
      if (mx >= hx - 15 && mx <= hx + 15 && my >= hy - 15 && my <= hy + 15) {
        ctx->library_hovered_heart_idx = (int)album_idx;
      }
    }

    render_library_item(ctx, album, (int)album_idx, px, y, card_w, card_h);
  }

  SDL_RenderSetClipRect(g_renderer, NULL);
}

static void render_search_song_list(PlayerContext *ctx, int x, int y, int w,
                                    int h) {
  int item_h = 56;

  for (size_t i = 0; i < ctx->library_filtered_track_count; i++) {
    size_t track_idx = ctx->library_filtered_track_indices[i];
    Track *t = &ctx->library_tracks[track_idx];

    int py = y + (int)i * item_h;

    /* Visibility check */
    if (py + item_h < 0)
      continue;
    if (py > h)
      break;

    /* Hover State */
    int mx, my;
    window_get_mouse_pos(&mx, &my);
    bool is_hovered = (mx >= x && mx <= x + w && my >= py && my <= py + item_h);

    if (is_hovered) {
      /* Draw Hover Background */
      SDL_SetRenderDrawColor(g_renderer, 60, 60, 60, 100);
      SDL_Rect row_rect = {x + 10, py + 2, w - 20, item_h - 4};
      SDL_RenderFillRect(g_renderer, &row_rect);
    }

    /* Heart Icon (Left) */
    int hx = x + 35;
    int hy = py + item_h / 2 + 4; /* Centered visually */

    /* Using track favorite status */
    bool is_fav = t->is_favorite;
    /* We need a hovered track index for hearts in search song list.
       Reusing library_hovered_heart_idx but treating it as track index
       if we are in search-song-list area?
       Actually, `material_hit_test` sets `library_hovered_heart_idx` only for
       albums currently. We should probably add `library_hovered_track_idx` or
       repurpose. For now, let's assume we'll fix hit testing to set a value we
       can read, or just rely on the click action without hover effect if
       complexity is high. Let's add a check: */
    // bool heart_hovered = ... (will be implemented in hit test)
    // For now simple render:
    Color heart_col = (is_fav) ? (Color){255, 50, 80} : (Color){150, 150, 150};
    draw_heart(hx, hy, 10, heart_col, is_fav);

    /* Title & Artist - Adjusted offsets for centering */
    /* Title: py + 10 -> py + 30 */
    font_draw_text_limit(g_renderer, t->title, x + 75, py + 30, 255, 255, 255,
                         w - 180); /* Reduced width to make room for duration */
    /* Artist: py + 32 -> py + 50 */
    font_draw_text_limit(g_renderer, t->artist, x + 75, py + 50, 180, 180, 180,
                         w - 180);

    /* Duration */
    int total_sec = (int)t->duration;
    int min = total_sec / 60;
    int sec = total_sec % 60;

    char dur_str[16];
    if (total_sec > 0) {
      snprintf(dur_str, sizeof(dur_str), "%d:%02d", min, sec);
    } else {
      snprintf(dur_str, sizeof(dur_str), "--:--");
    }

    int dw = font_get_text_width(dur_str);
    font_draw_text(g_renderer, dur_str, x + w - dw - 30, py + 40, 150, 150,
                   150);

    /* Separator */
    SDL_SetRenderDrawColor(g_renderer, 40, 40, 40, 255);
    SDL_Rect sep = {x + 20, py + item_h - 1, w - 40, 1};
    SDL_RenderFillRect(g_renderer, &sep);
  }
}

static void render_search_results(PlayerContext *ctx, int x, int y, int w,
                                  int h) {
  if (ctx->library_needs_filter) {
    update_library_filter(ctx);
    /* In search scene, we don't necessarily clear it here if it's used
       elsewhere, but usually we should to avoid repeated work. */
    ctx->library_needs_filter = false;
  }

  /* Search Background */
  SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(g_renderer, 18, 18, 18,
                         252); /* Slightly more opaque */
  SDL_Rect bg = {x, y, w, h};  /* Cover entire library area */
  SDL_RenderFillRect(g_renderer, &bg);
  SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_NONE);

  int grid_y =
      y + 80 + (int)ctx->library_header_offset - (int)ctx->library_scroll_y;
  int cur_y = grid_y;

  /* Albums Section */
  if (ctx->library_filtered_count > 0) {
    font_draw_text(g_renderer, "Albums", x + 20, cur_y, 255, 255, 255);
    cur_y += 40;
    render_search_album_row(ctx, x, cur_y, w);
    cur_y += 250; /* Row height + padding */
  }

  /* Songs Section */
  if (ctx->library_filtered_track_count > 0) {
    font_draw_text(g_renderer, "Songs", x + 20, cur_y, 255, 255, 255);
    cur_y += 50; /* Increased margin */
    render_search_song_list(ctx, x, cur_y, w, h);
  }

  /* Update scroll bounds */
  int total_h = 0;
  if (ctx->library_filtered_count > 0)
    total_h += 300;
  if (ctx->library_filtered_track_count > 0)
    total_h += 50 + (int)ctx->library_filtered_track_count * 56;

  int content_h = 80 + total_h;
  ctx->library_max_scroll = (float)(content_h - h);
  if (ctx->library_max_scroll < 0)
    ctx->library_max_scroll = 0;
}

static void render_library_list(PlayerContext *ctx, int x, int y, int w,
                                int h) {
  int item_h = 60; /* Increased from 50 to fit 50px art + padding */
  int mx, my;
  window_get_mouse_pos(&mx, &my);

  /* Render Songs instead of Albums in List View */
  for (size_t idx = 0; idx < ctx->library_filtered_track_count; idx++) {
    size_t track_idx = ctx->library_filtered_track_indices[idx];
    if (track_idx >= ctx->library_track_count)
      continue; /* Safety check */

    Track *track = &ctx->library_tracks[track_idx];

    int py = y + (int)idx * item_h;

    /* Visibility check - Pre-culling */
    if (py + item_h < 0)
      continue;
    if (py > h - CONTROL_BAR_H)
      break;

    bool is_hovered = (mx >= x && mx <= x + w && my >= py && my <= py + item_h);

    if (is_hovered) {
      /* Hover Logic */
      SDL_SetRenderDrawColor(g_renderer, 60, 60, 60, 100);
      SDL_Rect row_rect = {x + 10, py + 2, w - 20, item_h - 4};
      SDL_RenderFillRect(g_renderer, &row_rect);
    }

    /* Album Art (Left specific) */
    int art_sz = 50;
    int art_x = x + 20;
    int art_y = py + (item_h - art_sz) / 2;

    if (track->art_filename[0] != '\0') {
      SDL_Texture *art = get_or_load_icon(track->art_filename);
      if (art) {
        SDL_Rect art_rect = {art_x, art_y, art_sz, art_sz};
        SDL_RenderCopy(g_renderer, art, NULL, &art_rect);
      } else {
        material_draw_rounded_rect(art_x, art_y, art_sz, art_sz, 4,
                                   (Color){50, 50, 50}, 255);
      }
    } else {
      /* Try to fallback to album art from cached albums if track art missing */
      /* Simplified: just draw placeholder */
      material_draw_rounded_rect(art_x, art_y, art_sz, art_sz, 4,
                                 (Color){50, 50, 50}, 255);
    }

    /* Play Overlay on Art Hover */
    bool art_hovered = (mx >= art_x && mx <= art_x + art_sz && my >= art_y &&
                        my <= art_y + art_sz);
    if (art_hovered) {
      SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 100);
      SDL_Rect overlay = {art_x, art_y, art_sz, art_sz};
      SDL_RenderFillRect(g_renderer, &overlay);
      draw_play_icon(art_x + art_sz / 2, art_y + art_sz / 2, 16,
                     g_theme.primary);
    }

    /* Heart Icon Position: Let's move it to after the art or keep it relative?
     */
    /* Original was x + 35. Art is at x+20, width 50 -> x+70. */
    /* Let's put Heart at x + 90 */
    int hx = x + 90;
    int hy = py + item_h / 2;
    bool is_fav = track->is_favorite;
    bool heart_hovered =
        (mx >= hx - 15 && mx <= hx + 15 && my >= hy - 15 && my <= hy + 15);

    Color heart_col = (is_fav) ? (Color){255, 50, 80} : (Color){150, 150, 150};
    if (heart_hovered) {
      heart_col = (Color){255, 100, 120};
    }
    draw_heart(hx, hy, 10, heart_col, is_fav || heart_hovered);

    /* Text */
    /* Adjusted x offset: 20 (pad) + 50 (art) + 20 (gap) + 30 (heart area) ->
     * ~120 */
    int text_x = x + 130;
    font_draw_text_limit(g_renderer, track->title, text_x, py + 18, 255, 255,
                         255, w - text_x - 100);
    font_draw_text_limit(g_renderer, track->artist, text_x, py + 38, 180, 180,
                         180, w - text_x - 100);

    /* Duration */
    int total_sec = (int)track->duration;
    int min = total_sec / 60;
    int sec = total_sec % 60;

    char dur_str[16];
    if (total_sec > 0) {
      snprintf(dur_str, sizeof(dur_str), "%d:%02d", min, sec);
    } else {
      snprintf(dur_str, sizeof(dur_str), "--:--");
    }

    int dw = font_get_text_width(dur_str);
    font_draw_text(g_renderer, dur_str, x + w - dw - 30, py + 22, 150, 150,
                   150);

    /* Separator */
    SDL_SetRenderDrawColor(g_renderer, 40, 40, 40, 255);
    SDL_Rect sep = {x + 20, py + item_h - 1, w - 40, 1};
    SDL_RenderFillRect(g_renderer, &sep);
  }
}

static void render_library_grid(PlayerContext *ctx, int x, int y, int w,
                                int h) {
  if (ctx->library_needs_filter || ctx->library_filtered_indices == NULL) {

    update_library_filter(ctx);
    ctx->library_needs_filter = false;
  }

  /* Adjust for increased header height (120 instead of 80) */
  int grid_top = y + 120 + (int)ctx->library_header_offset;
  int cur_y = grid_top - (int)ctx->library_scroll_y;

  ctx->library_hovered_album_idx = -1;
  ctx->library_hovered_heart_idx = -1;

  if (ctx->library_view_mode == LIBRARY_VIEW_LIST) {
    render_library_list(ctx, x, cur_y, w, h);
    /* Update max scroll for list */
    ctx->library_max_scroll =
        (float)(ctx->library_filtered_track_count * 60 + 130) - h;
    if (ctx->library_max_scroll < 0)
      ctx->library_max_scroll = 0;
    return;
  }

  int base_card_w = 180;
  int padding = 20;
  int cols = material_get_grid_cols(w);

  /* Calculate exact width needed to fill the container perfectly */
  int content_w_area = w - 40; /* 20px padding on left/right edges */
  int card_w =
      (cols > 0) ? (content_w_area - (cols - 1) * padding) / cols : base_card_w;

  /* Scale height proportionally to maintain the square album art aspect */
  int card_h = 260 + (card_w - base_card_w);

  int mx, my;
  window_get_mouse_pos(&mx, &my);

  /* O(1) Hover Calculation */
  int hover_col = -1;
  int hover_row = -1;
  int world_mx = mx - (x + 20); /* Grid start X */
  int world_my = my - cur_y;    /* Grid start Y (includes scroll) */

  /* Debug: Use raw grid_top for calculation if valid */
  /* cur_y = grid_top - scroll; => world_my = my - (grid_top - scroll) = my -
   * grid_top + scroll */

  if (world_mx >= 0 && world_my >= 0) {
    if (world_mx % (card_w + padding) < card_w &&
        world_my % (card_h + padding) < card_h) {
      hover_col = world_mx / (card_w + padding);
      hover_row = world_my / (card_h + padding);
    }
  }

  SDL_Rect debug_red_rect = {0};
  SDL_Rect debug_green_rect = {0};
  bool draw_green = false;

  /* Red Rect: Where code thinks mouse is (Grid Slot) */
  if (hover_col >= 0 && hover_col < cols && hover_row >= 0) {
    int rx = x + 20 + hover_col * (card_w + padding);
    int ry = cur_y + hover_row * (card_h + padding);
    debug_red_rect.x = rx;
    debug_red_rect.y = ry;
    debug_red_rect.w = card_w;
    debug_red_rect.h = card_h;
  }

  /* Albums Section */
  if (ctx->library_filtered_count > 0) {
    font_draw_text(g_renderer, "Albums", x + 20, cur_y, 255, 255, 255);
    cur_y += 40; /* Space for "Albums" label? Wait, original code was inside
                    loop? No. */

    /* IMPORTANT: The original code logic had `cur_y += 40` inside the `if`
       block but BEFORE the loop. But wait, `render_library_grid` logic in
       snippet showed: `font_draw_text... cur_y += 40`. However, my O(1)
       calculation used `cur_y` based on `grid_top`. If there is an offset for
       "Albums" text, checking lines 1780-1781: It draws text then `cur_y +=
       40`. So the GRID starts at `cur_y + 40`? Let's re-verify line 1781 in
       previous view. Yes: `cur_y += 40`. So I need to adjust my WorldY
       calculation: `world_my = my - (cur_y + 40)`? Or simply apply the += 40
       before the O(1) calc? Actually, let's keep the calc robust.
    */
    /* Adjust calculated O(1) baselines for the text offset */
    world_my -= 40;
    if (world_my >= 0 && hover_row >= 0) {
      /* Re-evaluate row based on offset */
      hover_row = world_my / (card_h + padding);
      /* Update Red Rect Y */
      if (debug_red_rect.w > 0)
        debug_red_rect.y += 40;
    } else {
      /* Mouse is in the header space */
      hover_row = -1;
      debug_red_rect.w = 0; /* Hide if invalid */
    }

    for (size_t idx = 0; idx < ctx->library_filtered_count; idx++) {
      int row = (int)idx / cols;
      int col = (int)idx % cols;

      int px = x + 20 + col * (card_w + padding);
      int py = cur_y + row * (card_h + padding);

      if (py > h)
        break;
      if (py + card_h >= 0) {
        size_t album_idx = ctx->library_filtered_indices[idx];

        /* Hover Logic from O(1) */
        if (row == hover_row && col == hover_col) {
          ctx->library_hovered_album_idx = (int)album_idx;
          debug_green_rect.x = px;
          debug_green_rect.y = py;
          debug_green_rect.w = card_w;
          debug_green_rect.h = card_h;
          draw_green = true;
        }

        /* Draw Card */
        render_library_item(ctx, &ctx->library_albums[album_idx],
                            (int)album_idx, px, py, card_w, card_h);

        /* Heart Hover check (Visual Only) */
        if (row == hover_row && col == hover_col) {
          int hx = px + card_w - 25;
          int hy = py + 25;
          if (mx >= hx - 15 && mx <= hx + 15 && my >= hy - 15 &&
              my <= hy + 15) {
            ctx->library_hovered_heart_idx = (int)album_idx;
          }
        }
      }
    }
  }

  /* Draw Debug Rects (Z-order Max) */
  if (g_debug_view) {
    if (debug_red_rect.w > 0) {
      SDL_SetRenderDrawColor(g_renderer, 255, 0, 0, 255);
      SDL_RenderDrawRect(g_renderer, &debug_red_rect);
    }
    if (draw_green) {
      SDL_SetRenderDrawColor(g_renderer, 0, 255, 0, 255);
      SDL_RenderDrawRect(g_renderer, &debug_green_rect);
    }
  }

  /* Update max scroll */
  int total_rows =
      (int)(ctx->library_filtered_count + cols - 1) / (cols > 0 ? cols : 1);
  int header_h = (ctx->library_filtered_count > 0 ? 40 : 0);
  int content_h = 120 + header_h + total_rows * (card_h + padding);
  ctx->library_max_scroll = (float)(content_h - h);
  if (ctx->library_max_scroll < 0)
    ctx->library_max_scroll = 0;
}

/* ========================================
 *       Playlist Rendering
 * ======================================== */

void render_playlist_collage_art(int x, int y, int w, int h, int playlist_id) {
  /* Draw 2x2 grid of first 4 tracks' art */
  Track tracks[4];
  size_t count = 4;

  db_get_playlist_tracks(playlist_id, tracks, &count);

  if (count == 0) {
    /* Placeholder */
    SDL_SetRenderDrawColor(g_renderer, 30, 30, 30, 255);
    SDL_Rect r = {x, y, w, h};
    SDL_RenderFillRect(g_renderer, &r);
    return;
  }

  int half_w = w / 2;
  int half_h = h / 2;

  for (size_t i = 0; i < 4; i++) {
    if (i >= count)
      break;
    int px = x + (i % 2) * half_w;
    int py = y + (i / 2) * half_h;

    SDL_Texture *tex = get_or_load_icon(tracks[i].art_filename);
    if (tex) {
      SDL_Rect r = {px, py, half_w, half_h};
      SDL_RenderCopy(g_renderer, tex, NULL, &r);
    } else {
      SDL_SetRenderDrawColor(g_renderer, 20 + (i * 10), 20 + (i * 10),
                             20 + (i * 10), 255);
      SDL_Rect r = {px, py, half_w, half_h};
      SDL_RenderFillRect(g_renderer, &r);
    }
  }
}

void render_playlists_grid(PlayerContext *ctx, int x, int y, int w, int h) {
  int pad = 20;
  int cols = material_get_grid_cols(w);
  int cw = (w - (cols + 1) * pad) / cols;
  int ch = cw + 60; /* Art (cw) + Title (30) */

  /* Fetch playlists if needed (naive refresh for now) */
  if (ctx->library_playlists == NULL) {
    /* Setup cache */
    size_t count = 0;
    db_get_playlists(NULL, &count);
    ctx->library_playlists = malloc(sizeof(Playlist) * (count + 1));
    ctx->library_playlist_count = count;
    db_get_playlists(ctx->library_playlists, &count);
  }

  int start_y =
      y + 80 + (int)ctx->library_header_offset - (int)ctx->library_scroll_y;

  /* Calculate max scroll */
  int rows = (int)(ctx->library_playlist_count + cols - 1) / cols;
  ctx->library_max_scroll = (float)(rows * (ch + pad) - (h - 100));
  if (ctx->library_max_scroll < 0)
    ctx->library_max_scroll = 0;

  SDL_Rect clip = {x, y + 60, w, h - 60};
  SDL_RenderSetClipRect(g_renderer, &clip);

  for (size_t i = 0; i < ctx->library_playlist_count; i++) {
    int col = i % cols;
    int row = i / cols;

    int item_x = x + pad + col * (cw + pad);
    int item_y = start_y + row * (ch + pad);

    if (item_y + ch < y)
      continue;
    if (item_y > y + h)
      break;

    /* Interaction */
    int mx, my;
    window_get_mouse_pos(&mx, &my);
    bool hover = material_hit_test_rect(item_x, item_y, cw, ch, mx, my);

    /* Card BG */
    Color bg = g_theme.surface;
    if (hover) {
      bg = (Color){(Uint8)fmin(bg.r + 20, 255), (Uint8)fmin(bg.g + 20, 255),
                   (Uint8)fmin(bg.b + 20, 255)};
      if (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(SDL_BUTTON_LEFT)) {
        if (SDL_GetTicks() - g_last_click_time > MIN_CLICK_DELAY_MS) {
          char cmd[64];
          snprintf(cmd, sizeof(cmd), "open_playlist_%d",
                   ctx->library_playlists[i].id);
          /* Direct call or push event? window.c usually handles events,
             but here we are in render loop. Usually we should return command
             string, but this function returns void. However, main.c loop
             handles commands from hit_test. This function is RENDER. Wait,
             material_hit_test is separate! Ah, `material_render` calls this.
             `material_hit_test` should handle clicks? Or we can dispatch a
             custom event or check clicks here if we don't have a separate hit
             test for grid items. Looking at other grid items, they often handle
             clicks in render if they are complex dynamic lists, OR
             `material_hit_test` handles them. But `material_hit_test` (lines
             3275+) doesn't seem to iterate playlists grid? Let's check
             `material_hit_test` again. If it doesn't, we can do it here by
             pushing an event or setting a flag. Actually, the simplest way for
             immediate feedback is to handle it here if `window.c` allows. But
             strictly separation: logic should be in `hit_test`. However,
             `material_render` does side-effects like `SDL_StartTextInput`.
             Let's push an SDL_UserEvent or just rely on the fact that we can't
             easily return a string here. Actually, many other controls in this
             file seem to handle logic in render (like FAB). So let's push a
             command via event or side-channel? Actually, `window.c` processes
             `material_hit_test` results. If we handle it here, we might
             conflict. But FAB at line 2176 handles clicks directly in render!
             So it is safe to handle clicks here. */

          /* We need to trigger the command. We can't return string.
             We can call a helper or push an event.
             Let's push an SDL_Event with code or just use a hack?
             Actually, `main.c` relies on `window_update` returning the string.
             If we handle it here, `main.c` won't know unless we change state
             directly. But `open_playlist` logic is in `main.c`. We can't call
             `main` logic from `ui`. We should probably push an SDL User Event
             with the command string as data? Or, simpler: use
             `window_push_command(cmd)`. Is there a `window_push_command`? Let's
             check window.h or common.h. If not, checking `material_renderer.c`
             imports... It includes `window.h`. Let's check `window.h` for event
             pushing. If not, we can implement `material_hit_test` for playlist
             grid. That is the CLEANER way. Let's check `material_hit_test` at
             the end of file. */
        }
      }
    }
    material_draw_rounded_rect(item_x, item_y, cw, ch, 8, bg, 255);

    /* Art Collage */
    render_playlist_collage_art(item_x, item_y, cw, cw,
                                ctx->library_playlists[i].id);

    /* Text */
    char count_buf[32];
    snprintf(count_buf, sizeof(count_buf), "%d Tracks",
             ctx->library_playlists[i].track_count);

    font_draw_text(g_renderer, ctx->library_playlists[i].name, item_x + 10,
                   item_y + cw + 10, 255, 255, 255);
    font_draw_text(g_renderer, count_buf, item_x + 10, item_y + cw + 35, 150,
                   150, 150);
  }

  SDL_RenderSetClipRect(g_renderer, NULL);

  /* Render FAB (Floating Action Button) for adding playlist */
  int fab_size = 56;
  int fab_x = x + w - fab_size - 30;
  int fab_y = y + h - fab_size - 30;

  /* Shadow */
  draw_filled_circle(fab_x + fab_size / 2, fab_y + fab_size / 2 + 4,
                     fab_size / 2, (Color){0, 0, 0}, 100);
  /* Button */
  draw_filled_circle(fab_x + fab_size / 2, fab_y + fab_size / 2, fab_size / 2,
                     g_theme.primary, 255);
  /* Plus Icon */
  int cx = fab_x + fab_size / 2;
  int cy = fab_y + fab_size / 2;
  SDL_SetRenderDrawColor(g_renderer, 255, 255, 255, 255);
  SDL_Rect h_bar = {cx - 10, cy - 2, 20, 4};
  SDL_Rect v_bar = {cx - 2, cy - 10, 4, 20};
  SDL_RenderFillRect(g_renderer, &h_bar);
  SDL_RenderFillRect(g_renderer, &v_bar);
}

static void render_library(SDL_Renderer *renderer, PlayerContext *ctx, int x,
                           int y, int w, int h) {
  /* Tab Content - Render FIRST so Header is on Top */

  if (ctx->library_needs_filter || ctx->library_filtered_indices == NULL) {
    update_library_filter(ctx);
    ctx->library_needs_filter = false;
  }

  int header_base_h = 120;
  int top_offset = y + header_base_h + (int)ctx->library_header_offset;
  int cur_y = top_offset - (int)ctx->library_scroll_y;

  if (ctx->library_view_mode == LIBRARY_VIEW_LIST) {
    render_library_list(ctx, x, cur_y, w, h);
    /* Update max scroll for list */
    /* Approx height calculation */
    int content_h = top_offset + (int)ctx->library_filtered_track_count * 60;
    ctx->library_max_scroll = (float)(content_h - h);
    if (ctx->library_max_scroll < 0)
      ctx->library_max_scroll = 0;

  } else {
    render_library_grid(ctx, x, y, w, h);
  }

  /* Search Overlay - Render BEFORE Header so Header (Search Box) is on top */
  if (ctx->is_typing_search || ctx->search_query[0] != '\0') {
    render_search_results(ctx, x, y, w, h);
  }

  /* Render Header LAST (Z-Order Top) */
  render_library_header(ctx, x, y, w);
}

static void render_visualizer_view(PlayerContext *ctx, int x, int y, int w,
                                   int h) {
  (void)ctx;
  (void)x;
  (void)y;
  (void)w;
  (void)h;
  /* Read data from lock-free buffer WITHOUT consuming it */
  /* Using peek ensures we always get the most recent samples each frame */
  /* This prevents flickering when frame rate exceeds audio callback rate */
  static float vis_data[2048];
  size_t count = vis_buffer_peek(vis_data, 2048);

  /* Get Active Plugin */
  const VisPlugin *plugin = visualizer_get_active();
  if (!plugin) {
    plugin = &g_vis_basic_wave; /* Fallback */
  }

  /* Render */
  if (plugin && plugin->render) {
    /* Theme Context - Always pass theme if we want color sources to work */
    const ThemeColors *theme = material_get_theme();
    plugin->render(g_renderer, vis_data, count, x, y, w, h, theme);
  }
}

static void refresh_playlists_cache(PlayerContext *ctx) {
  if (ctx->library_playlists) {
    free(ctx->library_playlists);
    ctx->library_playlists = NULL;
  }
  size_t count = 0;
  if (db_get_playlists(NULL, &count) == RESULT_SUCCESS) {
    if (count > 0) {
      ctx->library_playlists = malloc(sizeof(Playlist) * count);
      if (ctx->library_playlists) {
        db_get_playlists(ctx->library_playlists, &count);
      }
    }
  }
  ctx->library_playlist_count = count;
}

static void render_playlists_scene(SDL_Renderer *renderer, PlayerContext *ctx,
                                   int x, int y, int w, int h) {
  (void)ctx;
  /* Header for Playlists */
  font_draw_text(renderer, "Playlists", x + 50, y + 40, 255, 255, 255);

  /* Modern Glassmorphic Container */
  int card_w = 480;
  int card_h = 240;
  int card_x = x + (w - card_w) / 2;
  int card_y = y + (h - card_h) / 2;

  /* Draw frosted glass base */
  material_draw_rounded_rect(card_x, card_y, card_w, card_h, 16,
                             (Color){25, 25, 25}, 200);

  /* Draw a subtle colored top bar using the theme's primary color to feel
   * premium */
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, g_theme.primary.r, g_theme.primary.g,
                         g_theme.primary.b, 255);
  SDL_Rect accent_bar = {card_x + 20, card_y, card_w - 40, 3};
  SDL_RenderFillRect(renderer, &accent_bar);

  /* Main Title: Under Development */
  const char *title = "Under Development";
  int tw_title = font_get_text_width(title);
  int tx_title = card_x + (card_w - tw_title) / 2;
  int ty_title = card_y + 85;
  font_draw_text(renderer, title, tx_title, ty_title, 255, 255, 255);

  /* Subtitle/Description */
  const char *desc = "This feature is currently being redesigned.";
  int tw_desc = font_get_text_width(desc);
  int tx_desc = card_x + (card_w - tw_desc) / 2;
  int ty_desc = card_y + 135;
  font_draw_text(renderer, desc, tx_desc, ty_desc, 150, 150, 150);

  const char *desc2 = "Stay tuned for updates!";
  int tw_desc2 = font_get_text_width(desc2);
  int tx_desc2 = card_x + (card_w - tw_desc2) / 2;
  int ty_desc2 = card_y + 170;
  font_draw_text(renderer, desc2, tx_desc2, ty_desc2, 110, 110, 110);
}

static void render_content(PlayerContext *ctx, int w, int h) {
  /* Calculate content area based on sidebars */
  int left_w = (int)(ctx->sidebar_left_anim * SIDEBAR_W);
  int right_w = (int)(ctx->sidebar_right_anim * SIDEBAR_W);

  int content_x = left_w;
  int content_w = w - left_w - right_w;

  if (ctx->current_scene == SCENE_LIBRARY) {
    render_library(g_renderer, ctx, content_x, 0, content_w, h - CONTROL_BAR_H);
    return;
  }

  if (ctx->current_scene == SCENE_PLAYLISTS) {
    render_playlists_scene(g_renderer, ctx, content_x, 0, content_w,
                           h - CONTROL_BAR_H);
    return;
  }

  if (ctx->current_scene == SCENE_VISUALIZER) {
    /* Render Visualizer first (background) */
    render_visualizer_view(ctx, content_x, 0, content_w, h - CONTROL_BAR_H);
    /* Controls have moved to right sidebar */
    return;
  }

  /* Existing Now Playing view */
  render_now_playing_art(g_renderer, ctx, content_x, 0, content_w,
                         h - CONTROL_BAR_H);
  render_now_playing_info(g_renderer, ctx, content_x, 0, content_w,
                          h - CONTROL_BAR_H);
}

static void render_settings_tabs(PlayerContext *ctx, int x, int y, int w) {
  /* Tabs - aligned to top left of window content */
  int tab_h = 40;
  int tab_count = 3;
  int tab_w = w / tab_count;

  const char *tabs[] = {"Library", "Audio", "About"};

  for (int i = 0; i < tab_count; i++) {
    int tx = x + i * tab_w;
    // int ty = y; // Original line, not needed with new text_y calculation

    /* Center Text */
    int text_w = font_get_text_width(tabs[i]);
    int text_x = tx + (tab_w - text_w) / 2;
    int ty;
    font_get_text_center_offset(tabs[i], 0, tab_h, NULL, &ty);
    int text_y = y + ty;

    /* Highlight Active */
    if (i == ctx->settings_active_tab) {
      SDL_SetRenderDrawColor(g_renderer, 60, 60, 60, 255);
      SDL_Rect tr = {tx, y, tab_w, tab_h};
      SDL_RenderFillRect(g_renderer, &tr);

      /* Bottom accent line for active tab */
      SDL_SetRenderDrawColor(g_renderer, g_theme.primary.r, g_theme.primary.g,
                             g_theme.primary.b, 255);
      SDL_Rect accent = {tx, y + tab_h - 2, tab_w, 2};
      SDL_RenderFillRect(g_renderer, &accent);

      font_draw_text(g_renderer, tabs[i], text_x, text_y, 255, 255, 255);
    } else {
      /* Inactive Tab */
      font_draw_text(g_renderer, tabs[i], text_x, text_y, 150, 150, 150);
    }
  }
}

static int render_settings_library_folders(PlayerContext *ctx, int x, int y,
                                           int w) {
  font_draw_text(g_renderer, "Library Folders", x, y, 200, 200, 200);
  int current_y = y + 40;

  for (size_t i = 0; i < ctx->library_path_count; i++) {
    int item_h = 40;
    int remove_w = font_get_text_width("Remove") + 20;
    int path_w = w - remove_w - 10;

    material_draw_rounded_rect(x, current_y, path_w, item_h, 6,
                               (Color){40, 40, 40}, 255);
    int ty;
    font_get_text_center_offset(ctx->library_paths[i].path, 0, item_h, NULL,
                                &ty);
    font_draw_text_limit(g_renderer, ctx->library_paths[i].path, x + 10,
                         current_y + ty, 255, 255, 255, path_w - 20);

    int rx = x + path_w + 10;
    material_draw_rounded_rect(rx, current_y, remove_w, item_h, 6,
                               (Color){150, 50, 50}, 255);
    int r_tw = font_get_text_width("Remove");
    font_get_text_center_offset("Remove", 0, item_h, NULL, &ty);
    font_draw_text(g_renderer, "Remove", rx + (remove_w - r_tw) / 2,
                   current_y + ty, 255, 255, 255);

    current_y += item_h + 10;
  }

  int btn_h = 40;
  int btw_browse = font_get_text_width("Browse");
  int btn_w_browse = btw_browse + 32;
  int btw_add = font_get_text_width("Add");
  int btn_w_add = btw_add + 32;
  int gap = 10;
  int input_w = w - btn_w_browse - btn_w_add - gap * 2;

  material_draw_rounded_rect(x, current_y, input_w, btn_h, 6,
                             (Color){80, 80, 80}, 255);
  material_draw_rounded_rect(x + 1, current_y + 1, input_w - 2, btn_h - 2, 5,
                             (Color){30, 30, 30}, 255);

  int ty;
  font_get_text_center_offset("Browse", 0, btn_h, NULL, &ty);
  if (ctx->library_input_buffer[0] != '\0') {
    font_draw_text_limit(g_renderer, ctx->library_input_buffer, x + 10,
                         current_y + ty, 255, 255, 255, input_w - 20);
  } else {
    font_draw_text(g_renderer, "Select or type a folder...", x + 10,
                   current_y + ty, 100, 100, 100);
  }

  int bx = x + input_w + gap;
  material_draw_rounded_rect(bx, current_y, btn_w_browse, btn_h, 6,
                             (Color){80, 80, 80}, 255);
  font_draw_text(g_renderer, "Browse", bx + (btn_w_browse - btw_browse) / 2,
                 current_y + ty, 255, 255, 255);

  int ax = bx + btn_w_browse + gap;
  material_draw_rounded_rect(ax, current_y, btn_w_add, btn_h, 6,
                             (Color){50, 150, 50}, 255);
  font_draw_text(g_renderer, "Add", ax + (btn_w_add - btw_add) / 2,
                 current_y + ty, 255, 255, 255);

  current_y += btn_h + 30;
  return current_y;
}

static void render_settings_library_actions(PlayerContext *ctx, int x, int y,
                                            int w) {
  (void)w;
  int btn_h = 40;
  int btw_scan = font_get_text_width("Scan Library");
  int btn_w_scan = btw_scan + 32;

  material_draw_rounded_rect(x, y, btn_w_scan, btn_h, 6, (Color){80, 80, 80},
                             255);
  int ty;
  font_get_text_center_offset("Scan Library", 0, btn_h, NULL, &ty);
  int text_y = y + ty;
  font_draw_text(g_renderer, "Scan Library", x + (btn_w_scan - btw_scan) / 2,
                 text_y, 255, 255, 255);

  int tx = x + btn_w_scan + 20;
  int toggle_y = y + (btn_h - 20) / 2;
  SDL_Rect toggle_box = {tx, toggle_y, 20, 20};
  SDL_SetRenderDrawColor(g_renderer, 80, 80, 80, 255);
  SDL_RenderDrawRect(g_renderer, &toggle_box);

  if (ctx->setting_group_albums) {
    SDL_SetRenderDrawColor(g_renderer, g_theme.primary.r, g_theme.primary.g,
                           g_theme.primary.b, 255);
    SDL_Rect inner = {tx + 4, toggle_y + 4, 12, 12};
    SDL_RenderFillRect(g_renderer, &inner);
  }

  font_draw_text(g_renderer, "Group by album name", tx + 30, text_y, 200, 200,
                 200);

  /* Clean Up Database on Scan Toggle */
  int tw_group = font_get_text_width("Group by album name");
  int toggle2_tx = tx + 30 + tw_group + 30;
  SDL_Rect toggle2_box = {toggle2_tx, toggle_y, 20, 20};
  SDL_SetRenderDrawColor(g_renderer, 80, 80, 80, 255);
  SDL_RenderDrawRect(g_renderer, &toggle2_box);

  if (ctx->setting_clean_db_on_scan) {
    SDL_SetRenderDrawColor(g_renderer, g_theme.primary.r, g_theme.primary.g,
                           g_theme.primary.b, 255);
    SDL_Rect inner2 = {toggle2_tx + 4, toggle_y + 4, 12, 12};
    SDL_RenderFillRect(g_renderer, &inner2);
  }

  font_draw_text(g_renderer, "Clean DB on Scan", toggle2_tx + 30, text_y, 200,
                 200, 200);

  /* Reset Database Button - Red/Warning Color */
  int reset_y = y + 60; /* Added spacing to fit taller buttons */
  int btw_reset = font_get_text_width("Reset Database");
  int btn_w_reset = btw_reset + 32;
  material_draw_rounded_rect(x, reset_y, btn_w_reset, btn_h, 6,
                             (Color){180, 50, 50}, 255);
  font_draw_text(g_renderer, "Reset Database",
                 x + (btn_w_reset - btw_reset) / 2, reset_y + ty, 255, 255,
                 255);
}

static void render_settings_library_tab(PlayerContext *ctx, int x, int y,
                                        int w) {
  int current_y = render_settings_library_folders(ctx, x, y, w);
  render_settings_library_actions(ctx, x, current_y, w);
}

static void render_settings_popup(PlayerContext *ctx, int w, int h) {
  if (!ctx->settings_popup_open)
    return;

  SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 150);
  SDL_Rect screen = {0, 0, w, h};
  SDL_RenderFillRect(g_renderer, &screen);

  int pw = (int)(w * 0.8f);
  int ph = (int)(h * 0.8f);
  if (pw < 600)
    pw = 600;
  if (pw > 900)
    pw = 900;
  if (ph < 400)
    ph = 400;
  if (ph > 700)
    ph = 700;

  if (pw > w - 40)
    pw = w - 40;
  if (ph > h - 40)
    ph = h - 40;

  int px = (w - pw) / 2;
  int py = (h - ph) / 2;

  material_draw_rounded_rect(px, py, pw, ph, 16, (Color){25, 25, 25}, 255);

  render_settings_tabs(ctx, px, py, pw);

  int tab_content_y = py + 80;
  if (ctx->settings_active_tab == 0) {
    render_settings_library_tab(ctx, px + 30, tab_content_y, pw - 60);
  } else if (ctx->settings_active_tab == 1) {
    int current_y = tab_content_y;
    int ty_off;
    font_get_text_center_offset("Master Equalizer", 0, 30, NULL, &ty_off);

    /* Master EQ Toggle */
    {
      int tx = px + 30;
      int toggle_y = current_y + (30 - 20) / 2;
      SDL_Rect toggle_box = {tx, toggle_y, 20, 20};
      SDL_SetRenderDrawColor(g_renderer, 80, 80, 80, 255);
      SDL_RenderDrawRect(g_renderer, &toggle_box);

      if (ctx->eq_enabled) {
        SDL_SetRenderDrawColor(g_renderer, g_theme.primary.r, g_theme.primary.g,
                               g_theme.primary.b, 255);
        SDL_Rect inner = {tx + 4, toggle_y + 4, 12, 12};
        SDL_RenderFillRect(g_renderer, &inner);
      }
      font_draw_text(g_renderer, "Enable Equalizer (Master Toggle)", tx + 30,
                     current_y + ty_off, 200, 200, 200);
      current_y += 40;
    }

    /* Open EQ Button */
    {
      int btn_w = 200;
      int btn_h = 40;
      material_draw_rounded_rect(px + 30, current_y, btn_w, btn_h, 6,
                                 g_theme.primary, 255);
      int btw = font_get_text_width("Open Equalizer UI");
      int btn_ty;
      font_get_text_center_offset("Open Equalizer UI", btn_w, btn_h, NULL,
                                  &btn_ty);
      font_draw_text(g_renderer, "Open Equalizer UI",
                     px + 30 + (btn_w - btw) / 2, current_y + btn_ty, 255, 255,
                     255);
      current_y += 60;
    }

    /* Gapless Playback (Stub) */
    {
      int tx = px + 30;
      int toggle_y = current_y + (30 - 20) / 2;
      SDL_Rect toggle_box = {tx, toggle_y, 20, 20};
      SDL_SetRenderDrawColor(g_renderer, 80, 80, 80, 255);
      SDL_RenderDrawRect(g_renderer, &toggle_box);

      if (ctx->setting_gapless) {
        SDL_SetRenderDrawColor(g_renderer, g_theme.primary.r, g_theme.primary.g,
                               g_theme.primary.b, 255);
        SDL_Rect inner = {tx + 4, toggle_y + 4, 12, 12};
        SDL_RenderFillRect(g_renderer, &inner);
      }
      font_draw_text(g_renderer, "Gapless Playback (Experimental)", tx + 30,
                     current_y + ty_off, 150, 150, 150);
      current_y += 40;
    }

    /* Volume Normalization (Stub) */
    {
      int tx = px + 30;
      int toggle_y = current_y + (30 - 20) / 2;
      SDL_Rect toggle_box = {tx, toggle_y, 20, 20};
      SDL_SetRenderDrawColor(g_renderer, 80, 80, 80, 255);
      SDL_RenderDrawRect(g_renderer, &toggle_box);

      if (ctx->setting_normalization) {
        SDL_SetRenderDrawColor(g_renderer, g_theme.primary.r, g_theme.primary.g,
                               g_theme.primary.b, 255);
        SDL_Rect inner = {tx + 4, toggle_y + 4, 12, 12};
        SDL_RenderFillRect(g_renderer, &inner);
      }
      font_draw_text(g_renderer, "Automatic Volume Normalization", tx + 30,
                     current_y + ty_off, 150, 150, 150);
      current_y += 40;
    }
  } else {
    font_draw_text(g_renderer, "Harmony " HARMONY_VERSION, px + 30,
                   tab_content_y, 255, 255, 255);
    font_draw_text(g_renderer, "Built with C and SDL2", px + 30,
                   tab_content_y + 40, 150, 150, 150);

    /* Manual Updater Section */
    int btn_w = 200;
    int btn_h = 40;
    int btn_y = tab_content_y + 80;
    material_draw_rounded_rect(px + 30, btn_y, btn_w, btn_h, 6, g_theme.primary,
                               255);
    int btw = font_get_text_width("Check for Updates");
    int btn_ty;
    font_get_text_center_offset("Check for Updates", btn_w, btn_h, NULL,
                                &btn_ty);
    font_draw_text(g_renderer, "Check for Updates", px + 30 + (btn_w - btw) / 2,
                   btn_y + btn_ty, 255, 255, 255);

    if (ctx->update_status_msg[0] != '\0') {
      font_draw_text(g_renderer, ctx->update_status_msg, px + 30 + btn_w + 20,
                     btn_y + btn_ty, 200, 200, 200);
    }
  }

  int btw = font_get_text_width("Close");
  int btn_w = btw + 40; /* Slightly wider padding for the main close button */
  int btn_h = 40;
  int bx = px + (pw - btn_w) / 2;
  int by = py + ph - 80;

  material_draw_rounded_rect(bx, by, btn_w, btn_h, 8, (Color){50, 50, 50}, 255);
  int ty;
  font_get_text_center_offset("Close", 0, btn_h, NULL, &ty);
  font_draw_text(g_renderer, "Close", bx + (btn_w - btw) / 2, by + ty, 255, 255,
                 255);
}

/* ... fill_triangle helper ... */
/* ... render_progress_bar ... */
/* ... material_hit_test ... */

static void draw_aa_triangle(float x1, float y1, float x2, float y2, float x3,
                             float y3, Color c) {
  /* 1. Compute Centroid */
  float cx = (x1 + x2 + x3) / 3.0f;
  float cy = (y1 + y2 + y3) / 3.0f;

  SDL_Vertex verts[6];
  int indices[21];

  /* Inner vertices (Solid) */
  verts[0].position.x = x1;
  verts[0].position.y = y1;
  verts[0].color = (SDL_Color){c.r, c.g, c.b, 255};

  verts[1].position.x = x2;
  verts[1].position.y = y2;
  verts[1].color = (SDL_Color){c.r, c.g, c.b, 255};

  verts[2].position.x = x3;
  verts[2].position.y = y3;
  verts[2].color = (SDL_Color){c.r, c.g, c.b, 255};

  /* Outer vertices (Transparent) - Expand from centroid */
  float expand = 1.2f; /* 1.2px fringe */

  float dx, dy, len;

  /* V3 (Outer for V0) */
  dx = x1 - cx;
  dy = y1 - cy;
  len = sqrtf(dx * dx + dy * dy);
  if (len > 0.001f) {
    verts[3].position.x = x1 + (dx / len) * expand;
    verts[3].position.y = y1 + (dy / len) * expand;
  } else {
    verts[3].position = verts[0].position;
  }
  verts[3].color = (SDL_Color){c.r, c.g, c.b, 0};

  /* V4 (Outer for V1) */
  dx = x2 - cx;
  dy = y2 - cy;
  len = sqrtf(dx * dx + dy * dy);
  if (len > 0.001f) {
    verts[4].position.x = x2 + (dx / len) * expand;
    verts[4].position.y = y2 + (dy / len) * expand;
  } else {
    verts[4].position = verts[1].position;
  }
  verts[4].color = (SDL_Color){c.r, c.g, c.b, 0};

  /* V5 (Outer for V2) */
  dx = x3 - cx;
  dy = y3 - cy;
  len = sqrtf(dx * dx + dy * dy);
  if (len > 0.001f) {
    verts[5].position.x = x3 + (dx / len) * expand;
    verts[5].position.y = y3 + (dy / len) * expand;
  } else {
    verts[5].position = verts[2].position;
  }
  verts[5].color = (SDL_Color){c.r, c.g, c.b, 0};

  /* Indices */
  /* Center Tri */
  indices[0] = 0;
  indices[1] = 1;
  indices[2] = 2;

  /* Edge 0-1 (Quad: 0, 3, 4, 1) */
  indices[3] = 0;
  indices[4] = 3;
  indices[5] = 4;
  indices[6] = 0;
  indices[7] = 4;
  indices[8] = 1;

  /* Edge 1-2 (Quad: 1, 4, 5, 2) */
  indices[9] = 1;
  indices[10] = 4;
  indices[11] = 5;
  indices[12] = 1;
  indices[13] = 5;
  indices[14] = 2;

  /* Edge 2-0 (Quad: 2, 5, 3, 0) */
  indices[15] = 2;
  indices[16] = 5;
  indices[17] = 3;
  indices[18] = 2;
  indices[19] = 3;
  indices[20] = 0;

  SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
  SDL_RenderGeometry(g_renderer, NULL, verts, 6, indices, 21);
}

static void draw_icon_shuffle(int x, int y, int size, Color c) {
  SDL_SetRenderDrawColor(g_renderer, c.r, c.g, c.b, 255);

  // Crossed arrows, thick lines (2px)
  // Top-left to bottom-right
  SDL_RenderDrawLine(g_renderer, x, y, x + size, y + size);
  SDL_RenderDrawLine(g_renderer, x + 1, y, x + size, y + size - 1);
  SDL_RenderDrawLine(g_renderer, x, y + 1, x + size - 1, y + size);

  // Bottom-left to top-right (broken in middle)
  SDL_RenderDrawLine(g_renderer, x, y + size, x + size / 3,
                     y + size - size / 3);
  SDL_RenderDrawLine(g_renderer, x + 1, y + size, x + size / 3,
                     y + size - size / 3 + 1);
  SDL_RenderDrawLine(g_renderer, x, y + size - 1, x + size / 3 - 1,
                     y + size - size / 3);

  SDL_RenderDrawLine(g_renderer, x + size - size / 3, y + size / 3, x + size,
                     y);
  SDL_RenderDrawLine(g_renderer, x + size - size / 3 + 1, y + size / 3,
                     x + size, y + 1);
  SDL_RenderDrawLine(g_renderer, x + size - size / 3, y + size / 3 - 1,
                     x + size - 1, y);

  // Arrowheads
  draw_aa_triangle(x + size + 2, y + size + 2, x + size - 6, y + size - 2,
                   x + size - 2, y + size - 6, c);
  draw_aa_triangle(x + size + 2, y - 2, x + size - 6, y + 2, x + size - 2,
                   y + 6, c);
}

static void draw_icon_repeat(int x, int y, int size, Color c) {
  SDL_SetRenderDrawColor(g_renderer, c.r, c.g, c.b, 255);

  // Top arrow pointing right
  SDL_RenderDrawLine(g_renderer, x + 2, y, x + size - 2, y);
  SDL_RenderDrawLine(g_renderer, x + 2, y + 1, x + size - 2, y + 1);

  // Right side down
  SDL_RenderDrawLine(g_renderer, x + size - 2, y, x + size - 2, y + size / 2);
  SDL_RenderDrawLine(g_renderer, x + size - 3, y, x + size - 3, y + size / 2);

  // Bottom arrow pointing left
  SDL_RenderDrawLine(g_renderer, x + size - 2, y + size, x + 2, y + size);
  SDL_RenderDrawLine(g_renderer, x + size - 2, y + size - 1, x + 2,
                     y + size - 1);

  // Left side up
  SDL_RenderDrawLine(g_renderer, x + 2, y + size, x + 2, y + size / 2);
  SDL_RenderDrawLine(g_renderer, x + 3, y + size, x + 3, y + size / 2);

  // Arrowhead on top right
  draw_aa_triangle(x + size, y, x + size - 8, y - 5, x + size - 8, y + 5, c);
}

static void draw_icon_repeat_one(int x, int y, int size, Color c) {
  draw_icon_repeat(x, y, size, c);

  // Draw small "1" in center
  int cx = x + size / 2;
  int cy = y + size / 2;
  SDL_RenderDrawLine(g_renderer, cx, cy - 4, cx, cy + 4);
  SDL_RenderDrawLine(g_renderer, cx - 1, cy - 4, cx - 1, cy + 4);
  SDL_RenderDrawLine(g_renderer, cx - 3, cy - 1, cx, cy - 4);
  SDL_RenderDrawLine(g_renderer, cx - 3, cy, cx, cy - 3);
  SDL_RenderDrawLine(g_renderer, cx - 2, cy + 4, cx + 2, cy + 4);
  SDL_RenderDrawLine(g_renderer, cx - 2, cy + 3, cx + 2, cy + 3);
}

static void render_progress_bar(PlayerContext *ctx, WindowContext *layout,
                                int w, int h) {
  (void)w;
  (void)h;
  int bar_x = layout->progress_bar_rect.x;
  int bar_y = layout->progress_bar_rect.y;
  int bar_w = layout->progress_bar_rect.w;
  int bar_h = layout->progress_bar_rect.h;

  // Hit test
  int mx, my;
  SDL_GetMouseState(&mx, &my);
  bool hovering =
      (mx >= bar_x && mx <= bar_x + bar_w && my >= bar_y && my <= bar_y + 40);

  material_draw_rounded_rect(bar_x, bar_y, bar_w, bar_h, bar_h / 2,
                             (Color){60, 60, 60}, 100);

  if (ctx->duration > 0) {
    float progress = ctx->position / ctx->duration;
    if (progress > 1.0f)
      progress = 1.0f;
    int cur_w = (int)(bar_w * progress);
    if (cur_w > 0) {
      material_draw_rounded_rect(bar_x, bar_y, cur_w, bar_h, bar_h / 2,
                                 g_theme.primary, 255);
    }
  }

  // Dynamic seek/hover tooltip
  if (hovering) {
    char time_str[32];
    /* Calculate hover time */
    float hover_pct = (float)(mx - bar_x) / (float)bar_w;
    if (hover_pct < 0)
      hover_pct = 0;
    if (hover_pct > 1)
      hover_pct = 1;
    float hover_time = hover_pct * ctx->duration;

    int cur_min = (int)hover_time / 60;
    int cur_sec = (int)hover_time % 60;

    snprintf(time_str, sizeof(time_str), "%d:%02d", cur_min, cur_sec);

    int tw = font_get_text_width(time_str);
    int rect_w = tw + 20;
    int rect_h = 34;
    int rect_x = mx - rect_w / 2;
    int rect_y = bar_y - 40; /* Shift up above the bar */

    /* Clamp to screen */
    if (rect_x < 0)
      rect_x = 0;
    if (rect_x + rect_w > w)
      rect_x = w - rect_w;

    material_draw_rounded_rect(rect_x, rect_y, rect_w, rect_h, 6,
                               (Color){30, 30, 30}, 240);

    int off_x, off_y;
    font_get_text_center_offset(time_str, rect_w, rect_h, &off_x, &off_y);
    font_draw_text(g_renderer, time_str, rect_x + off_x, rect_y + off_y, 255,
                   255, 255);
  }
}

static void render_controls(PlayerContext *ctx, WindowContext *layout, int w,
                            int h) {
  /* Draw Control Bar Background */
  /* DEBUG: Print rect */
  /*
  char msg[128];
  snprintf(msg, sizeof(msg), "render_controls: x=%d y=%d w=%d h=%d",
           layout->control_bar_rect.x, layout->control_bar_rect.y,
           layout->control_bar_rect.w, layout->control_bar_rect.h);
  log_message("INFO", msg);

  snprintf(msg, sizeof(msg), "play_rect: x=%d y=%d w=%d h=%d state=%d",
            layout->play_button_rect.x, layout->play_button_rect.y,
            layout->play_button_rect.w, layout->play_button_rect.h, ctx->state);
  log_message("INFO", msg);
  */

  SDL_SetRenderDrawColor(g_renderer, 18, 18, 18, 255);
  SDL_RenderFillRect(g_renderer, &layout->control_bar_rect);

  render_progress_bar(ctx, layout, w, h);

  /* --- Album Art & Mini Player Toggle --- */
  /* Only show if we have a valid song and not in Now Playing scene (which has
   * large art) */
  if (ctx->current_scene != SCENE_NOW_PLAYING && ctx->current_index >= 0 &&
      (size_t)ctx->current_index < ctx->count) {
    Song *s = &ctx->songs[ctx->current_index];

    /* 1. Draw Art */
    if (s->art_path[0] != '\0') {
      SDL_Texture *art = get_or_load_icon(s->art_path);
      if (art) {
        SDL_RenderCopy(g_renderer, art, NULL, &layout->album_art_rect);
      } else {
        material_draw_rounded_rect(
            layout->album_art_rect.x, layout->album_art_rect.y,
            layout->album_art_rect.w, layout->album_art_rect.h, 8,
            (Color){40, 40, 40}, 255);
      }
    } else {
      material_draw_rounded_rect(
          layout->album_art_rect.x, layout->album_art_rect.y,
          layout->album_art_rect.w, layout->album_art_rect.h, 8,
          (Color){40, 40, 40}, 255);
    }

    /* 2. Hover Interaction for Mini Player */
    int mx, my;
    window_get_mouse_pos(&mx, &my);
    bool art_hover = material_hit_test_rect(
        layout->album_art_rect.x, layout->album_art_rect.y,
        layout->album_art_rect.w, layout->album_art_rect.h, mx, my);

    if (art_hover) {
      /* Dark Overlay */
      material_draw_rounded_rect(
          layout->album_art_rect.x, layout->album_art_rect.y,
          layout->album_art_rect.w, layout->album_art_rect.h, 8,
          (Color){0, 0, 0}, 150);

      /* Mini Player Button (Geometric) */
      /* Outer box */
      SDL_SetRenderDrawColor(g_renderer, 255, 255, 255, 255);
      SDL_Rect mp_outer = layout->mini_player_button_rect;
      SDL_RenderDrawRect(g_renderer, &mp_outer);

      /* Inner box (bottom right) */
      SDL_Rect mp_inner = {mp_outer.x + mp_outer.w / 2,
                           mp_outer.y + mp_outer.h / 2, mp_outer.w / 2 - 2,
                           mp_outer.h / 2 - 2};
      SDL_RenderFillRect(g_renderer, &mp_inner);

      /* Tooltip or status? For now just visual cue */
    }

    /* 3. Song Info (Title/Artist) in Info Area */
    if (layout->info_area_rect.w > 0) {
      int text_x = layout->info_area_rect.x;
      int center_y = layout->info_area_rect.y + layout->info_area_rect.h / 2;
      int max_w = layout->info_area_rect.w;
      uint32_t ticks = SDL_GetTicks();

      /* Title */
      int title_w = font_get_text_width(s->title);
      int title_y = center_y - 12;
      if (title_w > max_w) {
        /* Marquee */
        int scroll_w = title_w + 50;
        int offset = (ticks / 30) % scroll_w;
        SDL_Rect clip = {text_x, layout->info_area_rect.y, max_w,
                         layout->info_area_rect.h};
        SDL_RenderSetClipRect(g_renderer, &clip);
        font_draw_text(g_renderer, s->title, text_x - offset, title_y, 255, 255,
                       255);
        font_draw_text(g_renderer, s->title, text_x - offset + scroll_w,
                       title_y, 255, 255, 255);
        SDL_RenderSetClipRect(g_renderer, NULL);
      } else {
        font_draw_text(g_renderer, s->title, text_x, title_y, 255, 255, 255);
      }

      /* Artist */
      int artist_w = font_get_text_width(s->artist);
      int artist_y = center_y + 12;
      if (artist_w > max_w) {
        /* Marquee */
        int scroll_w = artist_w + 50;
        int offset = (ticks / 35) % scroll_w;
        SDL_Rect clip = {text_x, layout->info_area_rect.y, max_w,
                         layout->info_area_rect.h};
        SDL_RenderSetClipRect(g_renderer, &clip);
        font_draw_text(g_renderer, s->artist, text_x - offset, artist_y, 180,
                       180, 180);
        font_draw_text(g_renderer, s->artist, text_x - offset + scroll_w,
                       artist_y, 180, 180, 180);
        SDL_RenderSetClipRect(g_renderer, NULL);
      } else {
        font_draw_text(g_renderer, s->artist, text_x, artist_y, 180, 180, 180);
      }
    }
  }

  /* Play Button */
  if (ctx->state == PLAYER_STATE_PLAYING) {
    int cx = layout->play_button_rect.x + layout->play_button_rect.w / 2;
    int cy = layout->play_button_rect.y + layout->play_button_rect.h / 2;
    material_draw_rounded_rect(cx - 12, cy - 15, 8, 30, 2,
                               (Color){255, 255, 255}, 255);
    material_draw_rounded_rect(cx + 4, cy - 15, 8, 30, 2,
                               (Color){255, 255, 255}, 255);
  } else {
    /* Triangle */
    int cx = layout->play_button_rect.x + layout->play_button_rect.w / 2;
    int cy = layout->play_button_rect.y + layout->play_button_rect.h / 2;
    draw_aa_triangle((float)(cx - 10), (float)(cy - 15), (float)(cx + 15),
                     (float)cy, (float)(cx - 10), (float)(cy + 15),
                     (Color){255, 255, 255});
  }

  /* Previous Button */
  {
    int cx = layout->prev_button_rect.x + layout->prev_button_rect.w / 2;
    int cy = layout->prev_button_rect.y + layout->prev_button_rect.h / 2;
    draw_aa_triangle((float)(cx + 3), (float)(cy - 8), (float)(cx - 7),
                     (float)cy, (float)(cx + 3), (float)(cy + 8),
                     (Color){200, 200, 200});
    material_draw_rounded_rect(cx - 11, cy - 8, 3, 16, 1,
                               (Color){200, 200, 200}, 255);
  }

  /* Next Button */
  {
    int cx = layout->next_button_rect.x + layout->next_button_rect.w / 2;
    int cy = layout->next_button_rect.y + layout->next_button_rect.h / 2;
    draw_aa_triangle((float)(cx - 3), (float)(cy - 8), (float)(cx + 7),
                     (float)cy, (float)(cx - 3), (float)(cy + 8),
                     (Color){200, 200, 200});
    material_draw_rounded_rect(cx + 8, cy - 8, 3, 16, 1, (Color){200, 200, 200},
                               255);
  }

  /* Shuffle Button - Use cached rect */
  {
    /* Center icon in rect */
    int icon_sz = 20;
    int ix = layout->shuffle_button_rect.x +
             (layout->shuffle_button_rect.w - icon_sz) / 2;
    int iy = layout->shuffle_button_rect.y +
             (layout->shuffle_button_rect.h - icon_sz) / 2;

    Color shuf_col =
        ctx->shuffle_mode ? g_theme.primary : (Color){200, 200, 200};
    draw_icon_shuffle(ix, iy, icon_sz, shuf_col);

    if (ctx->shuffle_mode) {
      SDL_SetRenderDrawColor(g_renderer, shuf_col.r, shuf_col.g, shuf_col.b,
                             255);
      SDL_Rect ind = {ix + 2, iy + 26, 16, 2};
      SDL_RenderFillRect(g_renderer, &ind);
    }
  }

  /* Repeat Button - Use cached rect */
  {
    int icon_sz = 20;
    int ix = layout->repeat_button_rect.x +
             (layout->repeat_button_rect.w - icon_sz) / 2;
    int iy = layout->repeat_button_rect.y +
             (layout->repeat_button_rect.h - icon_sz) / 2;

    Color rpt_col = (ctx->repeat_mode != REPEAT_OFF) ? g_theme.primary
                                                     : (Color){200, 200, 200};

    if (ctx->repeat_mode == REPEAT_ONE) {
      draw_icon_repeat_one(ix, iy, icon_sz, rpt_col);
    } else {
      draw_icon_repeat(ix, iy, icon_sz, rpt_col);
    }

    if (ctx->repeat_mode != REPEAT_OFF) {
      SDL_SetRenderDrawColor(g_renderer, rpt_col.r, rpt_col.g, rpt_col.b, 255);
      SDL_Rect ind = {ix + 2, iy + 26, 16, 2};
      SDL_RenderFillRect(g_renderer, &ind);
    }
  }

  /* Volume Slider */
  material_draw_rounded_rect(
      layout->vol_slider_rect.x, layout->vol_slider_rect.y,
      layout->vol_slider_rect.w, layout->vol_slider_rect.h, 2,
      (Color){60, 60, 60}, 255);
  int cur_vol_w = (int)(layout->vol_slider_rect.w * ctx->volume);
  material_draw_rounded_rect(
      layout->vol_slider_rect.x, layout->vol_slider_rect.y, cur_vol_w,
      layout->vol_slider_rect.h, 2, g_theme.primary, 255);
}

/* =========================================================================
   EQ POPUP RENDERER
   ========================================================================= */
static void render_eq_popup(PlayerContext *ctx, int w, int h) {
  if (!ctx->eq_popup_open)
    return;

  /* --- Backdrop --- */
  SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 140);
  SDL_Rect screen = {0, 0, w, h};
  SDL_RenderFillRect(g_renderer, &screen);

  /* --- Popup dimensions --- */
  int bands = ctx->eq_band_count;
  /* Safety: Clamp bands to supported 5 or 10 */
  if (bands != 5 && bands != 10)
    bands = 5;

  int pw = (bands == 10) ? 760 : 520;
  int ph = 460;
  if (pw > w - 40)
    pw = w - 40;
  if (ph > h - 40)
    ph = h - 40;
  int px = (w - pw) / 2;
  int py = (h - ph) / 2;

  /* Shadow */
  SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 80);
  SDL_Rect sh = {px + 6, py + 6, pw, ph};
  SDL_RenderFillRect(g_renderer, &sh);

  /* Panel */
  material_draw_rounded_rect(px, py, pw, ph, 16, (Color){20, 20, 28}, 255);

  /* Subtle top gradient stripe */
  SDL_SetRenderDrawColor(g_renderer, g_theme.primary.r, g_theme.primary.g,
                         g_theme.primary.b, 30);
  SDL_Rect stripe = {px, py, pw, 60};
  SDL_RenderFillRect(g_renderer, &stripe);

  /* --- Mouse position for hover detection --- */
  int mx, my;
  window_get_mouse_pos(&mx, &my);

  /* -----------------------------------------------------------------------
     TOP ROW: Preset dropdown  +  5/10 band toggle
     ----------------------------------------------------------------------- */
  int top_y = py + 14;

  /* Dropdown button */
  int dd_x = px + 16;
  int dd_y = top_y;
  int dd_w = 200;
  int dd_h = 32;
  bool dd_hover = material_hit_test_rect(dd_x, dd_y, dd_w, dd_h, mx, my);
  Color dd_bg = dd_hover ? (Color){70, 70, 85} : (Color){38, 38, 50};
  material_draw_rounded_rect(dd_x, dd_y, dd_w, dd_h, 8, dd_bg, 255);
  /* Arrow */
  SDL_SetRenderDrawColor(g_renderer, 180, 180, 200, 255);
  int ax = dd_x + dd_w - 18;
  int ay = dd_y + dd_h / 2;
  SDL_RenderDrawLine(g_renderer, ax, ay - 3, ax + 6, ay + 3);
  SDL_RenderDrawLine(g_renderer, ax + 6, ay + 3, ax + 12, ay - 3);
  /* Label */
  int dd_ty;
  font_get_text_center_offset(ctx->eq_selected_preset_name, dd_w, dd_h, NULL,
                              &dd_ty);
  font_draw_text_limit(g_renderer, ctx->eq_selected_preset_name, dd_x + 10,
                       dd_y + dd_ty, 230, 230, 255, dd_w - 30);

  /* -----------------------------------------------------------------------
     FREQUENCY RESPONSE CURVE
     ----------------------------------------------------------------------- */
  int curve_x = px + 16;
  int curve_y = top_y + 46;
  int curve_w = pw - 32;
  int curve_h = 60;

  /* Grid background */
  material_draw_rounded_rect(curve_x, curve_y, curve_w, curve_h, 6,
                             (Color){14, 14, 20}, 255);
  /* Center line (0 dB) */
  SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(g_renderer, 60, 60, 80, 255);
  int center_y_curve = curve_y + curve_h / 2;
  SDL_RenderDrawLine(g_renderer, curve_x + 4, center_y_curve,
                     curve_x + curve_w - 4, center_y_curve);

  /* Frequency curve — connect gains as a polyline */
  if (bands > 1) {
    int prev_cx = -1, prev_cy = -1;
    for (int i = 0; i < bands; i++) {
      if (i >= EQ_BAND_COUNT_MAX)
        break;
      float t = (float)i / (float)(bands - 1);
      int cx = curve_x + 4 + (int)(t * (curve_w - 8));

      /* Safety: Ensure index is within eq_gains range */
      float gain = (i < EQ_BAND_COUNT_MAX) ? ctx->eq_gains[i] : 0.0f;
      float gain_norm = (gain + 12.0f) / 24.0f; /* 0..1 */
      int cy = curve_y + curve_h - 4 - (int)(gain_norm * (curve_h - 8));

      if (prev_cx >= 0) {
        SDL_SetRenderDrawColor(g_renderer, g_theme.primary.r, g_theme.primary.g,
                               g_theme.primary.b, 200);
        SDL_RenderDrawLine(g_renderer, prev_cx, prev_cy, cx, cy);
        /* Thicker line — draw adjacent lines */
        SDL_RenderDrawLine(g_renderer, prev_cx, prev_cy + 1, cx, cy + 1);
        SDL_RenderDrawLine(g_renderer, prev_cx, prev_cy - 1, cx, cy - 1);
      }
      /* Dot at each band */
      SDL_SetRenderDrawColor(g_renderer, 255, 255, 255, 230);
      SDL_Rect dot = {cx - 2, cy - 2, 5, 5};
      SDL_RenderFillRect(g_renderer, &dot);

      prev_cx = cx;
      prev_cy = cy;
    }
  }

  /* -----------------------------------------------------------------------
     BAND SLIDERS
     ----------------------------------------------------------------------- */
  int slider_area_y = curve_y + curve_h + 14;
  int slider_area_h = ph - (slider_area_y - py) - 70; /* space for bottom row */
  int band_slot_w = (pw - 32) / bands;
  int slider_h = slider_area_h - 30; /* room for label below */
  int slider_w_track = 6;
  int thumb_r = 8;

  /* Export slider geometry for command_dispatch Y→dB mapping */
  g_eq_slider_top = slider_area_y;
  g_eq_slider_h = slider_h;

  static int g_eq_dragging_band = -1; /* track which band thumb is held */

  for (int i = 0; i < bands; i++) {
    if (i >= EQ_BAND_COUNT_MAX)
      break;

    int slot_x = px + 16 + i * band_slot_w;
    int track_x = slot_x + (band_slot_w - slider_w_track) / 2;

    /* Track background */
    material_draw_rounded_rect(track_x, slider_area_y, slider_w_track, slider_h,
                               slider_w_track / 2, (Color){35, 35, 50}, 255);

    /* Filled portion from center (0 dB) to thumb */
    float gain = ctx->eq_gains[i];
    float gain_norm = (gain + 12.0f) / 24.0f; /* 0..1 */
    int thumb_y = slider_area_y + slider_h - (int)(gain_norm * slider_h);
    int center_y_s = slider_area_y + slider_h / 2;

    /* Fill from center to thumb */
    if (thumb_y < center_y_s) {
      /* boost — fill above center */
      material_draw_rounded_rect(track_x, thumb_y, slider_w_track,
                                 center_y_s - thumb_y, slider_w_track / 2,
                                 g_theme.primary, 255);
    } else {
      /* cut — fill below center (dimmer colour) */
      material_draw_rounded_rect(track_x, center_y_s, slider_w_track,
                                 thumb_y - center_y_s, slider_w_track / 2,
                                 (Color){(Uint8)(g_theme.primary.r / 2),
                                         (Uint8)(g_theme.primary.g / 2),
                                         (Uint8)(g_theme.primary.b / 2)},
                                 255);
    }

    /* Thumb */
    bool thumb_hover = material_hit_test_rect(
        track_x - thumb_r * 2, thumb_y - thumb_r, slider_w_track + thumb_r * 4,
        thumb_r * 2, mx, my);
    int tr = thumb_hover ? thumb_r + 2 : thumb_r;
    draw_filled_circle(track_x + slider_w_track / 2, thumb_y, tr,
                       (Color){255, 255, 255}, 255);

    /* dB label above thumb — always show when hover or non-zero */
    if (thumb_hover || fabsf(gain) > 0.1f) {
      char db_label[16];
      snprintf(db_label, sizeof(db_label), "%+.1f", gain);
      int lw = font_get_text_width(db_label);
      int label_y = thumb_y - 22;
      if (label_y < py + 2)
        label_y = py + 2;
      /* Small pill background */
      material_draw_rounded_rect(track_x + slider_w_track / 2 - lw / 2 - 4,
                                 label_y - 2, lw + 8, 18, 4,
                                 (Color){40, 40, 55}, 220);
      font_draw_text(g_renderer, db_label,
                     track_x + slider_w_track / 2 - lw / 2, label_y,
                     g_theme.primary.r, g_theme.primary.g, g_theme.primary.b);
    }

    /* Frequency label below track */
    static const char *labels_5[5] = {"60", "250", "1k", "4k", "12k"};
    static const char *labels_10[10] = {"31", "63", "125", "250", "500",
                                        "1k", "2k", "4k",  "8k",  "16k"};
    const char *lbl = (bands == 5) ? labels_5[i] : labels_10[i];
    int lbl_w = font_get_text_width(lbl);
    font_draw_text(g_renderer, lbl, track_x + slider_w_track / 2 - lbl_w / 2,
                   slider_area_y + slider_h + 6, 120, 120, 150);
  }

  /* -----------------------------------------------------------------------
     BOTTOM ACTION ROW
     ----------------------------------------------------------------------- */
  int btn_row_y = py + ph - 54;
  int btn_h = 34;

  /* EQ Enabled toggle pill — far left */
  int en_w = 70;
  Color en_bg = ctx->eq_enabled ? g_theme.primary : (Color){50, 50, 65};
  material_draw_rounded_rect(px + 16, btn_row_y, en_w, btn_h, btn_h / 2, en_bg,
                             255);
  int en_tw = font_get_text_width(ctx->eq_enabled ? "ON" : "OFF");
  int en_ty;
  font_get_text_center_offset("ON", en_w, btn_h, NULL, &en_ty);
  font_draw_text(g_renderer, ctx->eq_enabled ? "ON" : "OFF",
                 px + 16 + (en_w - en_tw) / 2, btn_row_y + en_ty, 255, 255,
                 255);

  /* Save + Delete — conditional on new/user-preset mode */
  int action_x = px + 16 + en_w + 12;

  if (ctx->eq_is_new_mode) {
    /* Name input field */
    int inp_w = 160;
    int ty_inp;
    font_get_text_center_offset("Preset", inp_w, btn_h, NULL, &ty_inp);
    material_draw_rounded_rect(action_x, btn_row_y, inp_w, btn_h, 6,
                               (Color){35, 35, 50}, 255);
    material_draw_rounded_rect(action_x + 1, btn_row_y + 1, inp_w - 2,
                               btn_h - 2, 5, (Color){20, 20, 32}, 255);
    if (ctx->eq_new_preset_name[0] != '\0') {
      font_draw_text_limit(g_renderer, ctx->eq_new_preset_name, action_x + 8,
                           btn_row_y + ty_inp, 220, 220, 255, inp_w - 16);
    } else {
      font_draw_text(g_renderer, "Preset name...", action_x + 8,
                     btn_row_y + ty_inp, 80, 80, 100);
    }
    /* Blinking cursor */
    if (ctx->eq_typing_preset_name && (SDL_GetTicks() / 500) % 2 == 0) {
      int cur_tw = ctx->eq_new_preset_name[0]
                       ? font_get_text_width(ctx->eq_new_preset_name)
                       : 0;
      SDL_SetRenderDrawColor(g_renderer, 200, 200, 255, 255);
      SDL_RenderDrawLine(g_renderer, action_x + 8 + cur_tw, btn_row_y + 8,
                         action_x + 8 + cur_tw, btn_row_y + btn_h - 8);
    }
    action_x += inp_w + 8;

    /* Save button */
    bool can_save = ctx->eq_new_preset_name[0] != '\0';
    int sv_tw = font_get_text_width("Save");
    int sv_w = sv_tw + 24;
    int sv_ty;
    font_get_text_center_offset("Save", sv_w, btn_h, NULL, &sv_ty);
    Color sv_bg = can_save ? (Color){40, 160, 80} : (Color){40, 50, 40};
    material_draw_rounded_rect(action_x, btn_row_y, sv_w, btn_h, 6, sv_bg, 255);
    font_draw_text(g_renderer, "Save", action_x + (sv_w - sv_tw) / 2,
                   btn_row_y + sv_ty, 255, 255, 255);
    action_x += sv_w + 8;
  } else if (ctx->eq_selected_preset_id >= 0) {
    /* Delete button for user-owned presets */
    int dl_tw = font_get_text_width("Delete");
    int dl_w = dl_tw + 24;
    int dl_ty;
    font_get_text_center_offset("Delete", dl_w, btn_h, NULL, &dl_ty);
    material_draw_rounded_rect(action_x, btn_row_y, dl_w, btn_h, 6,
                               (Color){160, 40, 40}, 255);
    font_draw_text(g_renderer, "Delete", action_x + (dl_w - dl_tw) / 2,
                   btn_row_y + dl_ty, 255, 255, 255);
    action_x += dl_w + 8;
  }

  /* Band Count Selection — center/right of action row */
  /* Close button — far right */
  int cl_tw = font_get_text_width("Close");
  int cl_w = cl_tw + 24;
  int cl_x = px + pw - cl_w - 16;
  int cl_ty;
  font_get_text_center_offset("Close", cl_w, btn_h, NULL, &cl_ty);
  bool cl_hov = material_hit_test_rect(cl_x, btn_row_y, cl_w, btn_h, mx, my);
  material_draw_rounded_rect(cl_x, btn_row_y, cl_w, btn_h, 6,
                             cl_hov ? (Color){80, 80, 95} : (Color){50, 50, 65},
                             255);
  font_draw_text(g_renderer, "Close", cl_x + (cl_w - cl_tw) / 2,
                 btn_row_y + cl_ty, 200, 200, 220);

  /* Band Count Selection — center/right of action row (placed next to Close) */
  {
    int toggle_w = 100;
    int toggle_h = 32;
    int toggle_x = cl_x - toggle_w - 24;
    int toggle_y = btn_row_y;

    material_draw_rounded_rect(toggle_x, toggle_y, toggle_w, toggle_h,
                               toggle_h / 2, (Color){40, 40, 55}, 255);

    int half_w = toggle_w / 2;
    if (ctx->eq_band_count == 5)
      material_draw_rounded_rect(toggle_x, toggle_y, half_w, toggle_h,
                                 toggle_h / 2, g_theme.primary, 255);
    if (ctx->eq_band_count == 10)
      material_draw_rounded_rect(toggle_x + half_w, toggle_y, half_w, toggle_h,
                                 toggle_h / 2, g_theme.primary, 255);

    int tw5 = font_get_text_width("5");
    int ty5;
    font_get_text_center_offset("5", half_w, toggle_h, NULL, &ty5);
    font_draw_text(g_renderer, "5", toggle_x + (half_w - tw5) / 2,
                   toggle_y + ty5, 255, 255, 255);
    int tw10 = font_get_text_width("10");
    int ty10;
    font_get_text_center_offset("10", half_w, toggle_h, NULL, &ty10);
    font_draw_text(g_renderer, "10", toggle_x + half_w + (half_w - tw10) / 2,
                   toggle_y + ty10, 255, 255, 255);
  }

  /* -----------------------------------------------------------------------
     PRESET DROPDOWN (rendered on top of everything else in the popup)
     ----------------------------------------------------------------------- */
  if (ctx->eq_dropdown_open) {
    static const char *builtin_names[] = {
        "Flat", "Bass Boost", "Treble", "Rock", "Pop", "Classical", "Vocal"};
    int n_builtin = 7;
    int n_user = (int)ctx->eq_preset_count;
    int n_items = n_builtin + n_user + 1; /* +1 for separator+New row */
    int item_h = 32;
    int dw = 220;
    int dh = n_items * item_h + 8;
    int dx = px + 16;
    int dy = top_y + dd_h + 4;
    if (dy + dh > py + ph)
      dy = top_y - dh - 4; /* flip up if off-screen */

    /* Shadow */
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 100);
    SDL_Rect dsh = {dx + 4, dy + 4, dw, dh};
    SDL_RenderFillRect(g_renderer, &dsh);
    material_draw_rounded_rect(dx, dy, dw, dh, 8, (Color){28, 28, 40}, 255);

    int iy = dy + 4;

    int dd_item_ty;
    font_get_text_center_offset("Flat", dw, item_h, NULL, &dd_item_ty);
    /* Built-in presets */
    for (int i = 0; i < n_builtin; i++) {
      bool hov = material_hit_test_rect(dx, iy, dw, item_h, mx, my);
      bool sel = (ctx->eq_selected_preset_id == -1 &&
                  strcmp(ctx->eq_selected_preset_name, builtin_names[i]) == 0);
      if (hov || sel) {
        SDL_SetRenderDrawColor(g_renderer, sel ? g_theme.primary.r : 50,
                               sel ? g_theme.primary.g : 50,
                               sel ? g_theme.primary.b : 65, 255);
        SDL_Rect hr = {dx, iy, dw, item_h};
        SDL_RenderFillRect(g_renderer, &hr);
      }
      font_draw_text(g_renderer, builtin_names[i], dx + 14, iy + dd_item_ty,
                     sel ? 255 : 210, sel ? 255 : 210, sel ? 255 : 225);
      iy += item_h;
    }

    /* User presets */
    if (ctx->eq_presets) {
      for (int i = 0; i < n_user; i++) {
        bool hov = material_hit_test_rect(dx, iy, dw, item_h, mx, my);
        bool sel = (ctx->eq_selected_preset_id == ctx->eq_presets[i].id);
        if (hov || sel) {
          SDL_SetRenderDrawColor(g_renderer, sel ? g_theme.primary.r : 50,
                                 sel ? g_theme.primary.g : 50,
                                 sel ? g_theme.primary.b : 65, 255);
          SDL_Rect hr = {dx, iy, dw, item_h};
          SDL_RenderFillRect(g_renderer, &hr);
        }
        font_draw_text(g_renderer, ctx->eq_presets[i].name, dx + 14,
                       iy + dd_item_ty, sel ? 255 : 210, sel ? 255 : 210,
                       sel ? 255 : 225);
        iy += item_h;
      }
    }

    /* Separator */
    SDL_SetRenderDrawColor(g_renderer, 55, 55, 70, 255);
    SDL_RenderDrawLine(g_renderer, dx + 10, iy + item_h / 2 - 1, dx + dw - 10,
                       iy + item_h / 2 - 1);

    /* New row */
    bool hov_new = material_hit_test_rect(dx, iy, dw, item_h, mx, my);
    if (hov_new) {
      SDL_SetRenderDrawColor(g_renderer, 50, 50, 65, 255);
      SDL_Rect hr = {dx, iy, dw, item_h};
      SDL_RenderFillRect(g_renderer, &hr);
    }
    font_draw_text(g_renderer, "+ New Preset", dx + 14, iy + dd_item_ty, 160,
                   210, 160);
  }

  (void)g_eq_dragging_band;
}

static void render_spotlight(PlayerContext *ctx, int w, int h) {
  if (!ctx->spotlight_active)
    return;

  float main_anim = ctx->spotlight_anim;          /* Lift anim (Phase 1/5) */
  float expand_anim = ctx->spotlight_expand_anim; /* Phase 4 anim */
  float flip_anim = ctx->spotlight_flip_anim;     /* Phase 3 anim */

  /* 1. Backdrop (Fades in during Phase 1) */
  SDL_Rect screen = {0, 0, w, h};
  SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, (Uint8)(200 * main_anim));
  SDL_RenderFillRect(g_renderer, &screen);

  /* 2. Calculate Base Metrics */
  int src_x = ctx->spotlight_source_rect.x;
  int src_y = ctx->spotlight_source_rect.y;
  int src_w = ctx->spotlight_source_rect.w;
  int src_h = ctx->spotlight_source_rect.h;

  /* Standard Focus Size (Phase 2/3) */
  int focus_w = 400;
  int focus_h = 550;
  int focus_x = (w - focus_w) / 2;
  int focus_y = (h - focus_h) / 2;

  /* Panel Size (Phase 4) */
  int panel_w = (int)(w * 0.8f);
  int panel_h = (int)(h * 0.8f);
  int panel_x = (w - panel_w) / 2;
  int panel_y = (h - panel_h) / 2;

  /* LERP: Phase 1/5 (Origin/Grid <-> Focus) */
  int cur_w = (int)(src_w + (focus_w - src_w) * main_anim);
  int cur_h = (int)(src_h + (focus_h - src_h) * main_anim);
  int cur_x = (int)(src_x + (focus_x - src_x) * main_anim);
  int cur_y = (int)(src_y + (focus_y - src_y) * main_anim);

  /* LERP: Phase 4 (Focus <-> Panel) */
  if (expand_anim > 0.001f) {
    cur_w = (int)(focus_w + (panel_w - focus_w) * expand_anim);
    cur_h = (int)(focus_h + (panel_h - focus_h) * expand_anim);
    cur_x = (int)(focus_x + (panel_x - focus_x) * expand_anim);
    cur_y = (int)(focus_y + (panel_y - focus_y) * expand_anim);
  }

  /* 3D Flip Scale (Phase 3) */
  float scale_x = 1.0f;
  if (ctx->spotlight_phase == SPOTLIGHT_PHASE_3_META || flip_anim > 0.001f) {
    scale_x = fabsf(cosf(flip_anim * 3.14159f));
  }
  int render_w = (int)(cur_w * scale_x);
  int render_x = cur_x + (cur_w - render_w) / 2;
  bool is_back = (flip_anim > 0.5f);

  /* 3. Render Card Surface */
  material_draw_rounded_rect(render_x - 4, cur_y + 4, render_w + 8, cur_h + 8,
                             12, (Color){0, 0, 0}, (Uint8)(100 * main_anim));
  material_draw_rounded_rect(render_x, cur_y, render_w, cur_h, 12,
                             g_theme.surface, (Uint8)(255 * main_anim));

  if (main_anim < 0.95f && !ctx->spotlight_is_closing) {
    /* Initialization (Lift) visuals: Just show art */
    Album *a = &ctx->library_albums[ctx->spotlight_album_idx];
    SDL_Texture *art = get_or_load_icon(a->art_filename);
    if (art) {
      SDL_Rect r = {render_x + 10, cur_y + 10, render_w - 20, render_w - 20};
      SDL_RenderCopy(g_renderer, art, NULL, &r);
    }
    return;
  }

  Album *a = &ctx->library_albums[ctx->spotlight_album_idx];
  const char *art_path = a->art_filename;
  if (ctx->spotlight_is_singular && ctx->spotlight_song_art_path[0] != '\0') {
    art_path = ctx->spotlight_song_art_path;
  }
  SDL_Texture *art = get_or_load_icon(art_path);

  /* Opacity Logic: Fade art to 0.1 during expansion */
  float art_opacity = 1.0f - (expand_anim * 0.9f);
  Uint8 art_v = (Uint8)(255 * art_opacity);

  /* 4. HUB Content (Phases 2-4) */
  if (!is_back) {
    /* Front Face */
    int art_sz = (expand_anim > 0.001f) ? (int)(focus_w * 0.8f) : 320;
    int ax = render_x + (render_w - art_sz) / 2;
    int ay = cur_y + 60;

    if (art) {
      SDL_SetTextureAlphaMod(art, art_v);
      SDL_Rect r = {ax, ay, art_sz, art_sz};
      SDL_RenderCopy(g_renderer, art, NULL, &r);
      SDL_SetTextureAlphaMod(art, 255);
    }

    if (expand_anim < 0.5f) {
      /* Center Play Button */
      int bx = ax + art_sz / 2;
      int by = ay + art_sz / 2;
      draw_play_icon(bx, by, 30, g_theme.primary);

      /* Info Icon Top Left */
      int ix = ax + 15;
      int iy = ay + 15;
      material_draw_rounded_rect(ix - 15, iy - 15, 30, 30, 8,
                                 (Color){30, 30, 30}, 180);
      font_draw_text(g_renderer, "i", ix - 3, iy + 8, 255, 255, 255);

      /* (+) Add to Playlist Icon Top Right */
      int px = ax + art_sz - 15;
      int py = ay + 15;
      material_draw_rounded_rect(px - 15, py - 15, 30, 30, 8,
                                 (Color){30, 30, 30}, 180);
      /* Draw Plus */
      SDL_SetRenderDrawColor(g_renderer, 255, 255, 255, 255);
      SDL_Rect p1 = {px - 8, py - 1, 16, 2};
      SDL_Rect p2 = {px - 1, py - 8, 2, 16};
      SDL_RenderFillRect(g_renderer, &p1);
      SDL_RenderFillRect(g_renderer, &p2);

      /* Text Labels */
      int ty = ay + art_sz + 40;
      const char *title_text = a->name;
      if (ctx->spotlight_is_singular && ctx->spotlight_song_title[0] != '\0') {
        title_text = ctx->spotlight_song_title;
      }

      font_draw_text(g_renderer, title_text,
                     render_x +
                         (render_w - font_get_text_width(title_text)) / 2,
                     ty, 255, 255, 255);

      /* Split artists by comma */
      int mx, my;
      window_get_mouse_pos(&mx, &my);

      char artist_buf[256];
      int artist_y = ty + 30;
      int total_artist_w = 0;

      /* First pass to calculate total width for centering */
      const char *artist_src = a->artist;
      if (ctx->spotlight_is_singular && ctx->spotlight_song_artist[0] != '\0') {
        artist_src = ctx->spotlight_song_artist;
      }

      char *temp_buf = strdup(artist_src);
      char *t = strtok(temp_buf, ",");
      while (t) {
        while (*t == ' ')
          t++;
        total_artist_w += font_get_text_width(t);
        t = strtok(NULL, ",");
        if (t)
          total_artist_w += font_get_text_width(", ");
      }
      free(temp_buf);

      int cur_artist_x = render_x + (render_w - total_artist_w) / 2;
      safe_strncpy(artist_buf, artist_src, sizeof(artist_buf));
      char *save_ptr;
      char *token = strtok_r(artist_buf, ",", &save_ptr);
      while (token) {
        while (*token == ' ')
          token++;
        int tw = font_get_text_width(token);

        /* Hover detection for individual artist */
        bool hovered = (mx >= cur_artist_x && mx <= cur_artist_x + tw &&
                        my >= artist_y - 20 && my <= artist_y + 10);
        if (hovered) {
          SDL_SetRenderDrawColor(g_renderer, 255, 255, 255, 40);
          SDL_Rect hr = {cur_artist_x - 4, artist_y - 18, tw + 8, 24};
          SDL_RenderFillRect(g_renderer, &hr);
          font_draw_text(g_renderer, token, cur_artist_x, artist_y, 255, 255,
                         255);
        } else {
          font_draw_text(g_renderer, token, cur_artist_x, artist_y, 180, 180,
                         180);
        }
        cur_artist_x += tw;

        token = strtok_r(NULL, ",", &save_ptr);
        if (token) {
          font_draw_text(g_renderer, ", ", cur_artist_x, artist_y, 180, 180,
                         180);
          cur_artist_x += font_get_text_width(", ");
        }
      }
    }
  } else {
    /* Back Face (Metadata List) */
    int list_x = render_x + 40;
    int list_y = cur_y + 40;
    if (expand_anim < 0.5f) {
      const char *details_label =
          ctx->spotlight_is_singular ? "Song Details" : "Album Details";
      font_draw_text(g_renderer, details_label, list_x, list_y, 200, 200, 200);

      char buf[512];
      int sy = list_y + 50;
      if (ctx->spotlight_is_singular) {
        snprintf(buf, sizeof(buf), "Title: %s", ctx->spotlight_song_title);
        font_draw_text(g_renderer, buf, list_x, sy, 180, 180, 180);
        sy += 35;
        snprintf(buf, sizeof(buf), "Artist: %s", ctx->spotlight_song_artist);
        font_draw_text(g_renderer, buf, list_x, sy, 180, 180, 180);
        sy += 35;
        snprintf(buf, sizeof(buf), "Album: %s", ctx->spotlight_song_album);
        font_draw_text(g_renderer, buf, list_x, sy, 180, 180, 180);
        sy += 35;
        snprintf(buf, sizeof(buf), "Genre: %s",
                 ctx->spotlight_song_genre[0] ? ctx->spotlight_song_genre
                                              : "Unknown");
        font_draw_text(g_renderer, buf, list_x, sy, 180, 180, 180);
        sy += 35;
        snprintf(buf, sizeof(buf), "Year: %d", ctx->spotlight_song_year);
        font_draw_text(g_renderer, buf, list_x, sy, 180, 180, 180);
        sy += 35;
        snprintf(buf, sizeof(buf), "Duration: %.2f",
                 ctx->spotlight_song_duration);
        font_draw_text(g_renderer, buf, list_x, sy, 180, 180, 180);
        sy += 35;
        snprintf(buf, sizeof(buf), "Path: %s", ctx->spotlight_song_path);
        font_draw_text_limit(g_renderer, buf, list_x, sy, 150, 150, 150,
                             render_w - 60);
      } else {
        snprintf(buf, sizeof(buf), "Genre: Electronic"); // Generic for album
        font_draw_text(g_renderer, buf, list_x, sy, 180, 180, 180);
        sy += 40;
        snprintf(buf, sizeof(buf), "Artist: %s", a->artist);
        font_draw_text(g_renderer, buf, list_x, sy, 180, 180, 180);
        sy += 40;
        snprintf(buf, sizeof(buf), "Path: %s", a->art_filename);
        font_draw_text_limit(g_renderer, buf, list_x, sy, 150, 150, 150,
                             render_w - 60);
      }

      /* Back Icon */
      material_draw_rounded_rect(render_x + 20, cur_y + cur_h - 60, 100, 40, 8,
                                 (Color){60, 60, 60}, 255);
      font_draw_text(g_renderer, "Return", render_x + 40, cur_y + cur_h - 32,
                     255, 255, 255);
    }
  }

  /* 5. Expansion Content (Phase 4 Tracks) */
  if (expand_anim > 0.5f) {
    int list_x = render_x + 40;
    int list_y = cur_y + 100;
    font_draw_text(g_renderer, "Track List", list_x, list_y, 255, 255, 255);

    /* Calculate max scroll based on total height */
    int row_h = 35;
    int visible_h = cur_h - 140;
    ctx->spotlight_max_scroll =
        (float)((int)ctx->spotlight_track_count * row_h - (visible_h - 40));
    if (ctx->spotlight_max_scroll < 0)
      ctx->spotlight_max_scroll = 0;

    SDL_Rect clip = {render_x + 20, cur_y + 130, render_w - 40, visible_h};
    SDL_RenderSetClipRect(g_renderer, &clip);

    for (size_t i = 0; i < ctx->spotlight_track_count; i++) {
      /* Request: "when enacting spotlight in now playing, make it that
       * singular song file." */
      if (ctx->spotlight_is_singular && ctx->spotlight_song_path[0] != '\0') {
        if (strcmp(ctx->spotlight_tracks[i].filepath,
                   ctx->spotlight_song_path) != 0) {
          continue;
        }
      }

      int ty = list_y + 50 + (int)i * row_h - (int)ctx->spotlight_scroll_y;
      if (ty + row_h < list_y + 50)
        continue;
      if (ty > cur_y + cur_h - 40)
        break;

      char buf[256];
      snprintf(buf, sizeof(buf), "%s", ctx->spotlight_tracks[i].title);
      font_draw_text(g_renderer, buf, list_x, ty, 220, 220, 220);
    }
    SDL_RenderSetClipRect(g_renderer, NULL);

    /* Back to Hub Button */
    material_draw_rounded_rect(render_x + 20, cur_y + 20, 100, 40, 8,
                               (Color){60, 60, 60}, 200);
    font_draw_text(g_renderer, "<- Hub", render_x + 35, cur_y + 48, 255, 255,
                   255);
  }
}

/* Info / Close Hint (Footer) */
/* Footer removed as requested */

static void render_splash_overlay(int w, int h) {
  if (!g_splash_initialized) {
    g_splash_start_time = SDL_GetTicks();
    g_splash_initialized = true;
  }

  Uint32 elapsed = SDL_GetTicks() - g_splash_start_time;
  Uint32 total_duration = SPLASH_HOLD_MS + SPLASH_FADE_MS;

  if (elapsed >= total_duration) {
    return;
  }

  Uint8 alpha = 255;
  if (elapsed > SPLASH_HOLD_MS) {
    float fade_progress =
        (float)(elapsed - SPLASH_HOLD_MS) / (float)SPLASH_FADE_MS;
    if (fade_progress > 1.0f)
      fade_progress = 1.0f;
    alpha = (Uint8)(255.0f * (1.0f - fade_progress));
  }

  if (alpha > 0) {
    /* Draw Full Screen Black Overlay */
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, alpha);
    SDL_Rect screen_rect = {0, 0, w, h};
    SDL_RenderFillRect(g_renderer, &screen_rect);

    /* Draw Logo Text */
    if (alpha > 5) {
      const char *text = "Harmony";
      int tw = font_get_text_width(text);
      font_set_global_alpha(alpha);
      font_draw_text(g_renderer, text, (w - tw) / 2, h / 2 - 20, 255, 255, 255);

      const char *sub = "Music Player";
      int sub_w = font_get_text_width(sub);
      font_draw_text(g_renderer, sub, (w - sub_w) / 2, h / 2 + 20, 180, 180,
                     180);

      font_set_global_alpha(255); /* Reset */
    }
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_NONE);
  }
}

void material_render(PlayerContext *ctx, WindowContext *layout) {
  static bool size_logged = false;
  if (!size_logged) {
    char size_msg[256];
    snprintf(
        size_msg, sizeof(size_msg),
        "material_renderer: sizeof(PlayerContext)=%zu, g_theme=%p, presets=%p",
        sizeof(PlayerContext), (void *)&g_theme, (void *)ctx->eq_presets);
    log_message("DEBUG", size_msg);

    size_logged = true;
  }

  int w, h;
  SDL_GetRendererOutputSize(g_renderer, &w, &h);

  update_animations(ctx);

  window_begin_frame();
  render_background(w, h);
  render_content(ctx, w, h);
  if (ctx->spotlight_anim > 0.001f) {
    render_spotlight(ctx, w, h);
  }

  render_sidebar_left(ctx, h);
  render_sidebar_right(ctx, w, h);
  render_flyout_buttons(ctx, w, h);
  render_controls(ctx, layout, w, h);
  render_settings_popup(ctx, w, h);
  render_eq_popup(ctx, w, h);

  /* Context Menu */
  if (ctx->context_menu.active) {
    int mx = ctx->context_menu.x;
    int my = ctx->context_menu.y;
    int mw = 200;
    int mh = (ctx->context_menu.show_playlists)
                 ? 300
                 : 100; /* Expanded height if playlists shown */

    /* Clamp to screen */
    if (mx + mw > w)
      mx = w - mw;
    if (my + mh > h)
      my = h - mh;

    /* Shadow */
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
    SDL_Rect sh = {mx + 4, my + 4, mw, mh};
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 100);
    SDL_RenderFillRect(g_renderer, &sh);

    /* Background */
    material_draw_rounded_rect(mx, my, mw, mh, 8, (Color){25, 25, 25}, 255);

    /* Items */
    int item_y = my + 10;

    if (!ctx->context_menu.show_playlists) {
      /* Item 1: Spotlight */
      int mx_mouse, my_mouse;
      window_get_mouse_pos(&mx_mouse, &my_mouse);

      bool hov1 = (mx_mouse >= mx && mx_mouse <= mx + mw &&
                   my_mouse >= item_y && my_mouse <= item_y + 36);
      if (hov1) {
        SDL_SetRenderDrawColor(g_renderer, 50, 50, 50, 255);
        SDL_Rect r = {mx, item_y, mw, 36};
        SDL_RenderFillRect(g_renderer, &r);
      }
      font_draw_text(g_renderer, "Spotlight", mx + 20, item_y + 8, 255, 255,
                     255);
      item_y += 40;

      /* Item 2: Add to Playlist */
      bool hov2 = (mx_mouse >= mx && mx_mouse <= mx + mw &&
                   my_mouse >= item_y && my_mouse <= item_y + 36);
      if (hov2) {
        SDL_SetRenderDrawColor(g_renderer, 50, 50, 50, 255);
        SDL_Rect r = {mx, item_y, mw, 36};
        SDL_RenderFillRect(g_renderer, &r);
      }
      font_draw_text(g_renderer, "Add to Playlist >", mx + 20, item_y + 8, 255,
                     255, 255);
    } else {
      /* Back */
      int mx_mouse, my_mouse;
      window_get_mouse_pos(&mx_mouse, &my_mouse);
      bool hov_back = (mx_mouse >= mx && mx_mouse <= mx + mw &&
                       my_mouse >= item_y && my_mouse <= item_y + 30);
      if (hov_back) {
        SDL_SetRenderDrawColor(g_renderer, 50, 50, 50, 255);
        SDL_Rect r = {mx, item_y, mw, 30};
        SDL_RenderFillRect(g_renderer, &r);
      }
      /* Draw stylized Back row */
      /* Arrow shape < */
      font_draw_text(g_renderer, "<", mx + 15, item_y + 5, 255, 255, 255);
      font_draw_text(g_renderer, "Back", mx + 35, item_y + 5, 200, 200, 200);
      item_y += 35;

      /* Playlist List */
      /* Fetch playlists if empty */
      if (!ctx->library_playlists) {
        size_t c = 0;
        db_get_playlists(NULL, &c);
        ctx->library_playlists = malloc(sizeof(Playlist) * (c + 1));
        ctx->library_playlist_count = c;
        db_get_playlists(ctx->library_playlists, &c);
      }

      for (size_t i = 0; i < ctx->library_playlist_count; i++) {
        if (item_y > my + mh - 30)
          break;
        int mx_mouse, my_mouse;
        window_get_mouse_pos(&mx_mouse, &my_mouse);

        bool hov = (mx_mouse >= mx && mx_mouse <= mx + mw &&
                    my_mouse >= item_y && my_mouse <= item_y + 30);
        if (hov) {
          SDL_SetRenderDrawColor(g_renderer, 50, 50, 50, 255);
          SDL_Rect r = {mx, item_y, mw, 30};
          SDL_RenderFillRect(g_renderer, &r);
        }
        font_draw_text(g_renderer, ctx->library_playlists[i].name, mx + 20,
                       item_y + 5, 255, 255, 255);
        item_y += 30;
      }
    }
  }

  mini_player_render(ctx);
  toast_render(g_renderer, w, h);
  render_splash_overlay(w, h);

  window_end_frame();
}

const char *material_hit_test(PlayerContext *ctx, WindowContext *layout, int mx,
                              int my, bool is_right_click) {
  int w, h;
  SDL_GetRendererOutputSize(g_renderer, &w, &h);

  if (ctx->eq_popup_open) {
    int bands = ctx->eq_band_count;
    int pw = (bands == 10) ? 760 : 520;
    int ph = 460;
    if (pw > w - 40)
      pw = w - 40;
    if (ph > h - 40)
      ph = h - 40;
    int px = (w - pw) / 2;
    int py = (h - ph) / 2;

    /* Dropdown item clicks (Highest priority if dropdown is open) */
    if (ctx->eq_dropdown_open) {
      int dd_x = px + 16;
      int dd_h = 32;
      int top_y = py + 14;
      int dw = 220;
      int item_h = 32;
      int n_builtin = 7;
      int n_user = (int)ctx->eq_preset_count;
      int n_items = n_builtin + n_user + 1;
      int dh = n_items * item_h + 8;
      int dy = top_y + dd_h + 4;
      if (dy + dh > py + ph)
        dy = top_y - dh - 4;

      if (mx >= dd_x && mx <= dd_x + dw && my >= dy && my <= dy + dh) {
        int iy = dy + 4;
        /* Built-in */
        for (int i = 0; i < n_builtin; i++) {
          if (my >= iy && my < iy + item_h) {
            static char cmd[64];
            snprintf(cmd, sizeof(cmd), "eq_preset_select_builtin_%d", i);
            return cmd;
          }
          iy += item_h;
        }
        /* User */
        if (ctx->eq_presets) {
          for (int i = 0; i < n_user; i++) {
            if (my >= iy && my < iy + item_h) {
              static char cmd[64];
              snprintf(cmd, sizeof(cmd), "eq_preset_select_%d", i);
              return cmd;
            }
            iy += item_h;
          }
        }
        /* Separator row? Just skip. New Preset at the bottom */
        iy += item_h;
        if (my >= iy - item_h && my < iy) {
          return "eq_preset_new";
        }
        return "blocked";
      }
      return "eq_dropdown_close";
    }

    /* Action Row Buttons */
    int btn_row_y = py + ph - 54;
    int cl_x = px + pw - (font_get_text_width("Close") + 24) - 16;

    /* Band Count Toggle in Action Row */
    if (material_hit_test_rect(cl_x - 100 - 24, btn_row_y, 100, 32, mx, my)) {
      return "eq_toggle_bands";
    }

    /* Slider drags */
    int curve_y = py + 14 + 46;
    int slider_area_y = curve_y + 60 + 14;
    int slider_area_h = ph - (slider_area_y - py) - 70;
    int band_slot_w = (pw - 32) / bands;
    int slider_h = slider_area_h - 30;

    if (my >= slider_area_y && my <= slider_area_y + slider_h) {
      int i = (mx - (px + 16)) / band_slot_w;
      if (i >= 0 && i < bands) {
        static char cmd[64];
        snprintf(cmd, sizeof(cmd), "eq_band_drag_%d", i);
        return cmd;
      }
    }

    /* Actions */
    /* btn_row_y is already defined above */
    int btn_h = 34;

    /* ON/OFF toggle */
    if (material_hit_test_rect(px + 16, btn_row_y, 70, btn_h, mx, my)) {
      return "eq_toggle_enabled";
    }

    /* Dropdown toggle */
    if (material_hit_test_rect(px + 16, py + 14, 200, 32, mx, my)) {
      return "eq_dropdown_open";
    }

    /* Save/Delete */
    int en_w = 70;
    int action_x = px + 16 + en_w + 12;
    if (ctx->eq_is_new_mode) {
      /* Name input field */
      if (material_hit_test_rect(action_x, btn_row_y, 160, btn_h, mx, my)) {
        return "eq_focus_name";
      }
      action_x += 160 + 8;
      /* Save */
      int sv_tw = font_get_text_width("Save");
      if (material_hit_test_rect(action_x, btn_row_y, sv_tw + 24, btn_h, mx,
                                 my)) {
        return "eq_preset_save";
      }
    } else if (ctx->eq_selected_preset_id >= 0) {
      int dl_tw = font_get_text_width("Delete");
      if (material_hit_test_rect(action_x, btn_row_y, dl_tw + 24, btn_h, mx,
                                 my)) {
        return "eq_preset_delete";
      }
    }

    /* Close */
    int cl_tw = font_get_text_width("Close");
    int cl_w = cl_tw + 24;
    if (material_hit_test_rect(px + pw - cl_w - 16, btn_row_y, cl_w, btn_h, mx,
                               my)) {
      return "close_eq";
    }

    if (!material_hit_test_rect(px, py, pw, ph, mx, my)) {
      return "close_eq";
    }
    return "blocked";
  }

  if (ctx->settings_popup_open) {
    int pw = (int)(w * 0.8f);
    int ph = (int)(h * 0.8f);
    if (pw < 600)
      pw = 600;
    if (pw > 900)
      pw = 900;
    if (ph < 400)
      ph = 400;
    if (ph > 700)
      ph = 700;
    if (pw > w - 40)
      pw = w - 40;
    if (ph > h - 40)
      ph = h - 40;

    int px = (w - pw) / 2;
    int py = (h - ph) / 2;

    int btn_w = 100;
    int btn_h = 40;
    int bx = px + (pw - btn_w) / 2;
    int by = py + ph - 80;

    if (material_hit_test_rect(bx, by, btn_w, btn_h, mx, my)) {
      return "close_settings";
    }

    /* Settings Tabs */
    int tab_w = pw / 3;
    if (mx >= px && mx <= px + pw && my >= py && my <= py + 40) {
      int tab = (mx - px) / tab_w;
      if (tab == 0)
        return "settings_tab_0";
      if (tab == 1)
        return "settings_tab_1";
      if (tab == 2)
        return "settings_tab_2";
    }

    /* Library Path Input */
    if (ctx->settings_active_tab == 0) {
      int content_y = py + 80;
      int cx = px + 30;
      int cw = pw - 60;

      int current_y = content_y + 40;
      for (size_t i = 0; i < ctx->library_path_count; i++) {
        int item_h = 40;
        int remove_w = font_get_text_width("Remove") + 20;
        int path_w = cw - remove_w - 10;
        int rx = cx + path_w + 10;

        if (material_hit_test_rect(rx, current_y, remove_w, item_h, mx, my)) {
          static char cmd[64];
          snprintf(cmd, sizeof(cmd), "settings_remove_folder_%zu", i);
          return cmd;
        }
        current_y += item_h + 10;
      }

      int btn_h = 40;
      int btw_browse = font_get_text_width("Browse");
      int btn_w_browse = btw_browse + 32;
      int btw_add = font_get_text_width("Add");
      int btn_w_add = btw_add + 32;
      int gap = 10;
      int input_w = cw - btn_w_browse - btn_w_add - gap * 2;

      if (material_hit_test_rect(cx, current_y, input_w, btn_h, mx, my)) {
        return "settings_focus_input";
      }

      int bx = cx + input_w + gap;
      if (material_hit_test_rect(bx, current_y, btn_w_browse, btn_h, mx, my)) {
        return "settings_browse";
      }

      int ax = bx + btn_w_browse + gap;
      if (material_hit_test_rect(ax, current_y, btn_w_add, btn_h, mx, my)) {
        return "settings_add_folder";
      }

      current_y += btn_h + 30;

      int btw_scan = font_get_text_width("Scan Library");
      int btn_w_scan = btw_scan + 32;
      if (material_hit_test_rect(cx, current_y, btn_w_scan, btn_h, mx, my)) {
        return "settings_scan_library";
      }

      int tx = cx + btn_w_scan + 20;
      int toggle_y = current_y + (btn_h - 20) / 2;
      if (material_hit_test_rect(tx, toggle_y, 20, 20, mx, my)) {
        return "settings_toggle_group";
      }

      /* Second toggle: Clean DB on Scan */
      int tw_group = font_get_text_width("Group by album name");
      int toggle2_tx = tx + 30 + tw_group + 30;
      if (material_hit_test_rect(toggle2_tx, toggle_y, 20, 20, mx, my)) {
        return "settings_toggle_clean_db";
      }

      int reset_y = current_y + 60;
      int btw_reset = font_get_text_width("Reset Database");
      int btn_w_reset = btw_reset + 32;
      if (material_hit_test_rect(cx, reset_y, btn_w_reset, btn_h, mx, my)) {
        return "settings_reset_database";
      }
    } else if (ctx->settings_active_tab == 1) {
      int content_y = py + 80;
      int cx = px + 30;
      int current_y = content_y;

      /* Master EQ Toggle */
      if (material_hit_test_rect(cx, current_y + (30 - 20) / 2, 20, 20, mx,
                                 my)) {
        return "eq_toggle_enabled";
      }
      current_y += 40;

      /* Open EQ Button */
      if (material_hit_test_rect(cx, current_y, 200, 40, mx, my)) {
        return "open_eq";
      }
      current_y += 60;

      /* Gapless Toggle */
      if (material_hit_test_rect(cx, current_y + (30 - 20) / 2, 20, 20, mx,
                                 my)) {
        return "settings_toggle_gapless";
      }
      current_y += 40;

      /* Normalization Toggle */
      if (material_hit_test_rect(cx, current_y + (30 - 20) / 2, 20, 20, mx,
                                 my)) {
        return "settings_toggle_normalization";
      }
    } else if (ctx->settings_active_tab == 2) {
      int tab_content_y = py + 80;
      int btn_w = 200;
      int btn_h = 40;
      int btn_y = tab_content_y + 80;
      if (material_hit_test_rect(px + 30, btn_y, btn_w, btn_h, mx, my)) {
        return "settings_check_updates";
      }
    }
    return "blocked";
  }

  if (ctx->context_menu.active) {
    int mx_menu = ctx->context_menu.x;
    int my_menu = ctx->context_menu.y;
    int mw = 200;
    int mh = (ctx->context_menu.show_playlists) ? 300 : 100;

    /* Clamp (must match render logic) */
    if (mx_menu + mw > w)
      mx_menu = w - mw;
    if (my_menu + mh > h)
      my_menu = h - mh;

    /* Check if click is OUTSIDE menu */
    if (!material_hit_test_rect(mx_menu, my_menu, mw, mh, mx, my)) {
      return "close_context_menu";
    }

    /* Check Items inside */
    int item_y = my_menu + 10;
    if (!ctx->context_menu.show_playlists) {
      /* Item 1: Spotlight */
      if (my >= item_y && my <= item_y + 36)
        return "context_action_spotlight";
      item_y += 40;
      /* Item 2: Add to Playlist */
      if (my >= item_y && my <= item_y + 36)
        return "context_action_add_playlist";
    } else {
      /* Back */
      if (my >= item_y && my <= item_y + 25)
        return "context_back";
      item_y += 30;

      /* Playlists */
      for (size_t i = 0; i < ctx->library_playlist_count; i++) {
        if (item_y > my_menu + mh - 30)
          break;
        if (my >= item_y && my <= item_y + 30) {
          static char cmd[64];
          snprintf(cmd, sizeof(cmd), "context_add_to_playlist_%d",
                   ctx->library_playlists[i].id);
          return cmd;
        }
        item_y += 30;
      }
    }
    return "blocked";
  }

  if (ctx->spotlight_active) {
    if (ctx->spotlight_anim < 0.5f && !ctx->spotlight_is_closing)
      return "blocked";

    /* Calculate dynamic metrics to match render_spotlight */
    float expand_anim = ctx->spotlight_expand_anim;

    int focus_w = 400;
    int focus_h = 550;
    int focus_x = (w - focus_w) / 2;
    int focus_y = (h - focus_h) / 2;

    int panel_w = (int)(w * 0.8f);
    int panel_h = (int)(h * 0.8f);
    int panel_x = (w - panel_w) / 2;
    int panel_y = (h - panel_h) / 2;

    int cur_w = (int)(focus_w + (panel_w - focus_w) * expand_anim);
    int cur_h = (int)(focus_h + (panel_h - focus_h) * expand_anim);
    int cur_x = (int)(focus_x + (panel_x - focus_x) * expand_anim);
    int cur_y = (int)(focus_y + (panel_y - focus_y) * expand_anim);

    /* Phase 4 Expansion Hits */
    if (expand_anim > 0.5f) {
      if (mx >= cur_x + 20 && mx <= cur_x + 120 && my >= cur_y + 20 &&
          my <= cur_y + 60)
        return "spotlight_expand_back";

      /* Track Hit Testing */
      int list_x = cur_x + 40;
      int list_y = cur_y + 100;
      int row_h = 35;

      if (mx >= list_x && mx <= cur_x + cur_w - 40 && my >= list_y + 40 &&
          my <= cur_y + cur_h - 40) {
        int relative_y = my - (list_y + 50) + (int)ctx->spotlight_scroll_y;
        int idx = relative_y / row_h;
        if (idx >= 0 && (size_t)idx < ctx->spotlight_track_count) {
          static char cmd[64];
          snprintf(cmd, sizeof(cmd), "spotlight_play_track_%d", idx);
          return cmd;
        }
      }

      if (mx < cur_x || mx > cur_x + cur_w || my < cur_y || my > cur_y + cur_h)
        return "spotlight_close";
      return "blocked";
    }

    /* Phase 2 Hub / Phase 3 Meta Hits */
    if (mx >= cur_x && mx <= cur_x + cur_w && my >= cur_y &&
        my <= cur_y + cur_h) {
      int art_sz = 320;
      int ax = cur_x + (cur_w - art_sz) / 2;
      int ay = cur_y + 60;

      if (ctx->spotlight_phase == SPOTLIGHT_PHASE_2_HUB) {
        /* Play Button (Center of Art) */
        int bx = ax + art_sz / 2;
        int by = ay + art_sz / 2;
        if (mx >= bx - 30 && mx <= bx + 30 && my >= by - 30 && my <= by + 30)
          return "spotlight_play";

        /* Info Icon (Top Left) */
        if (mx >= ax && mx <= ax + 45 && my >= ay && my <= ay + 45)
          return "spotlight_flip";

        /* Add Icon (Top Right) */
        if (mx >= ax + art_sz - 45 && mx <= ax + art_sz && my >= ay &&
            my <= ay + 45)
          return "spotlight_add_to_playlist";

        /* Album/Artist labels */
        int ty = ay + art_sz + 40;
        if (my >= ty - 10 && my <= ty + 30)
          return "spotlight_expand_album";

        if (my >= ty + 30 && my <= ty + 60) {
          if (ctx->spotlight_album_idx < 0 ||
              (size_t)ctx->spotlight_album_idx >= ctx->library_album_count)
            return "blocked";
          Album *a = &ctx->library_albums[ctx->spotlight_album_idx];
          /* Replicate split rendering logic to determine which artist was
           * clicked */
          char artist_buf[256];
          safe_strncpy(artist_buf, a->artist, sizeof(artist_buf));
          char *save_ptr;
          int total_artist_w = 0;

          /* Calculate total width to find the starting centered X */
          char *temp_buf = strdup(a->artist);
          char *t = strtok(temp_buf, ",");
          while (t) {
            while (*t == ' ')
              t++;
            total_artist_w += font_get_text_width(t) + 10;
            t = strtok(NULL, ",");
          }
          free(temp_buf);

          int cur_artist_x = cur_x + (cur_w - total_artist_w) / 2;
          safe_strncpy(artist_buf, a->artist, sizeof(artist_buf));
          char *token = strtok_r(artist_buf, ",", &save_ptr);
          while (token) {
            while (*token == ' ')
              token++;
            int tw = font_get_text_width(token);
            if (mx >= cur_artist_x && mx <= cur_artist_x + tw) {
              static char artist_cmd[256];
              snprintf(artist_cmd, sizeof(artist_cmd),
                       "spotlight_expand_artist_%s", token);
              return artist_cmd;
            }
            cur_artist_x += tw + 10;
            token = strtok_r(NULL, ",", &save_ptr);
          }
          /* Fallback if click was on row but not specific artist name */
          return "spotlight_expand_artist";
        }
      } else if (ctx->spotlight_phase == SPOTLIGHT_PHASE_3_META) {
        if (my >= cur_y + cur_h - 70)
          return "spotlight_flip";
      }
      return "blocked";
    } else {
      return "spotlight_close";
    }
  }

  /* Now Playing Hits */
  if (ctx->current_scene == SCENE_NOW_PLAYING) {
    int left_w = (int)(ctx->sidebar_left_anim * SIDEBAR_W);
    int right_w = (int)(ctx->sidebar_right_anim * SIDEBAR_W);
    int content_x = left_w;
    int content_w = w - left_w - right_w;

    int art_size = 350;
    if (art_size > content_w - 40)
      art_size = content_w - 40;

    /* Ensure hit box matches render_now_playing_art logic:
       art_x = x + (w - art_size) / 2;
       art_y = (h - CONTROL_BAR_H - art_size) / 2 - 20;
    */
    int art_x = content_x + (content_w - art_size) / 2;
    int art_y = (h - CONTROL_BAR_H - art_size) / 2 - 20;

    if (mx >= art_x && mx <= art_x + art_size && my >= art_y &&
        my <= art_y + art_size) {
      if (is_right_click) {
        ctx->spotlight_source_rect.x = art_x;
        ctx->spotlight_source_rect.y = art_y;
        ctx->spotlight_source_rect.w = art_size;
        ctx->spotlight_source_rect.h = art_size;
        return "now_playing_spotlight";
      }
    }
  }

  /* Control Bar Hits via Layout */
  if (mx >= layout->control_bar_rect.x && my >= layout->control_bar_rect.y &&
      mx <= layout->control_bar_rect.x + layout->control_bar_rect.w &&
      my <= layout->control_bar_rect.y + layout->control_bar_rect.h) {

    if (material_hit_test_rect(
            layout->play_button_rect.x, layout->play_button_rect.y,
            layout->play_button_rect.w, layout->play_button_rect.h, mx, my)) {
      return "play";
    }
    if (material_hit_test_rect(
            layout->next_button_rect.x, layout->next_button_rect.y,
            layout->next_button_rect.w, layout->next_button_rect.h, mx, my)) {
      return "next";
    }
    if (material_hit_test_rect(
            layout->prev_button_rect.x, layout->prev_button_rect.y,
            layout->prev_button_rect.w, layout->prev_button_rect.h, mx, my)) {
      return "prev";
    }

    /* Progress Bar (Reduced hit area to avoid overlaps) */
    if (mx >= layout->progress_bar_rect.x &&
        mx <= layout->progress_bar_rect.x + layout->progress_bar_rect.w &&
        my >= layout->progress_bar_rect.y - 5 &&
        my <= layout->progress_bar_rect.y + 10) {
      return "seek_bar";
    }

    /* Volume Slider (Reduced hit area) */
    if (mx >= layout->vol_slider_rect.x - 5 &&
        mx <= layout->vol_slider_rect.x + layout->vol_slider_rect.w + 5 &&
        my >= layout->vol_slider_rect.y - 5 &&
        my <= layout->vol_slider_rect.y + 10) {
      return "volume_slider";
    }

    /* Shuffle/Repeat */
    int cx = layout->play_button_rect.x + layout->play_button_rect.w / 2;
    int cy = layout->play_button_rect.y + layout->play_button_rect.h / 2;
    if (mx >= cx + 140 && mx <= cx + 170 && my >= cy - 20 && my <= cy + 20) {
      return "toggle_shuffle";
    }
    if (mx >= cx - 200 && mx <= cx - 170 && my >= cy - 20 && my <= cy + 20) {
      return "cycle_repeat";
    }

    /* Album Art Click → Mini Player */
    if (material_hit_test_rect(
            layout->album_art_rect.x, layout->album_art_rect.y,
            layout->album_art_rect.w, layout->album_art_rect.h, mx, my)) {
      return "toggle_mini_player";
    }

    if (is_right_click) {
      return "now_playing_spotlight";
    }
  }

  /* Sidebars */
  if (ctx->sidebar_left_open) {
    if (mx >= SIDEBAR_W - 30 && mx <= SIDEBAR_W && my >= 0 &&
        my < h - CONTROL_BAR_H)
      return "close_sidebar_left";

    /* Sidebar Playlists Hit Handling */
    int pl_base_y =
        100 + 60 + 60 + 40; /* Offset based on render logic approximate */
    if (ctx->library_playlist_count > 0 && mx >= 20 && mx <= 200 &&
        my >= pl_base_y) {
      int idx = (my - pl_base_y) / 30;
      if (idx >= 0 && idx < (int)ctx->library_playlist_count) {
        static char cmd[64];
        snprintf(cmd, sizeof(cmd), "open_playlist_%d",
                 ctx->library_playlists[idx].id);
        return cmd;
      }
    }
    /* Playlists Button Header */
    if (mx >= 20 && mx <= 260 && my >= 220 && my <= 270) {
      return "switch_view_playlists";
    }

    if (my > h - CONTROL_BAR_H - 70 && my < h - CONTROL_BAR_H - 30 && mx > 30 &&
        mx < 70)
      return "open_settings";
    if (mx <= SIDEBAR_W && my < h - CONTROL_BAR_H)
      return "blocked_sidebar";
  } else {
    int cy = (h - CONTROL_BAR_H) / 2;
    if (mx >= 0 && mx <= 24 && my >= cy - 30 && my <= cy + 30)
      return "toggle_sidebar_left";
  }

  if (ctx->sidebar_right_open) {
    int x_offset = w - SIDEBAR_W;

    /* Global Close areas */
    if (mx >= x_offset && mx <= x_offset + 30 && my >= 0 &&
        my < h - CONTROL_BAR_H)
      return "close_sidebar_right";

    int close_x = x_offset + 220;
    int close_y = 40;
    if (mx >= close_x && mx <= close_x + 30 && my >= close_y &&
        my <= close_y + 30)
      return "close_sidebar_right";

    if (ctx->current_scene == SCENE_VISUALIZER) {
      int pad = 40;
      int btn_w = 220;
      int header_y = 20;

      if (ctx->visualizer_show_settings) {
        /* Settings Mode Hit Testing */
        if (mx >= x_offset + pad && mx <= x_offset + pad + btn_w &&
            my >= header_y + 50 && my <= header_y + 90) {
          return "visualizer_settings_back";
        }
        const VisPlugin *active = visualizer_get_active();
        if (active && active->get_param_count && active->get_param) {
          int count = active->get_param_count();
          int current_sy = header_y + 120 - (int)ctx->visualizer_scroll_y;
          bool in_content_area = (my >= 60 && my < h - CONTROL_BAR_H);
          for (int i = 0; i < count; i++) {
            const VisParam *p = active->get_param(i);
            if (!p)
              continue;
            current_sy += 35;
            if (p->type == VIS_PARAM_FLOAT || p->type == VIS_PARAM_INT) {
              if (in_content_area && mx >= x_offset + pad &&
                  mx <= x_offset + pad + 200 && my >= current_sy &&
                  my <= current_sy + 20) {
                static char cmd[64];
                snprintf(cmd, sizeof(cmd), "visualizer_param_drag_%d", i);
                return cmd;
              }
              current_sy += 40;
            } else if (p->type == VIS_PARAM_ENUM || p->type == VIS_PARAM_BOOL) {
              if (in_content_area && mx >= x_offset + pad &&
                  mx <= x_offset + pad + 200 && my >= current_sy &&
                  my <= current_sy + 32) {
                static char cmd[64];
                snprintf(cmd, sizeof(cmd), "visualizer_param_toggle_%d", i);
                return cmd;
              }
              current_sy += 45;
            }
          }
        }
      } else if (ctx->visualizer_show_list) {
        /* List Mode Hit Testing */
        if (mx >= x_offset + pad && mx <= x_offset + pad + btn_w &&
            my >= header_y + 50 && my <= header_y + 90) {
          return "visualizer_list_back"; // Note: Need to handle this or just
                                         // toggle
        }
        int sy = header_y + 110;
        int active_idx = g_active_visualizer_index;
        /* Active */
        if (mx >= x_offset + pad && mx <= x_offset + pad + btn_w && my >= sy &&
            my <= sy + 40) {
          return "visualizer_list_back";
        }
        sy += 50;
        /* Others */
        for (int i = 0; i < g_visualizer_count; i++) {
          if (i == active_idx)
            continue;
          if (mx >= x_offset + pad && mx <= x_offset + pad + btn_w &&
              my >= sy && my <= sy + 40) {
            static char cmd[32];
            snprintf(cmd, sizeof(cmd), "set_visualizer_%d", i);
            return cmd;
          }
          sy += 50;
        }
      } else {
        /* Default Mode Hit Testing */
        /* Header at header_y is just display, not clickable */
        /* Change Visualizer at header_y + 50 */
        int sy = header_y + 50;
        if (mx >= x_offset + pad && mx <= x_offset + pad + btn_w && my >= sy &&
            my <= sy + 40)
          return "visualizer_list_open";
        /* Visualizer Settings at header_y + 100 */
        sy += 50;
        if (mx >= x_offset + pad && mx <= x_offset + pad + btn_w && my >= sy &&
            my <= sy + 40)
          return "visualizer_settings_open";
      }
    } else {
      bool has_art = false;
      if (ctx->sidebar_is_browsing) {
        has_art = (ctx->browse_context_art_path[0] != '\0');
      } else if (ctx->queue_type == QUEUE_TYPE_ALBUM) {
        has_art = (ctx->queue_context_art_path[0] != '\0');
      }

      /* Sidebar Header Art click */
      if (has_art) {
        int ax = x_offset + 50;
        int ay = 80;
        if (mx >= ax && mx <= ax + 180 && my >= ay && my <= ay + 180) {
          if (ctx->sidebar_is_browsing) {
            return "play_song_0";
          } else {
            return "open_album_context";
          }
        }
      }

      /* Queue Hit Tests */
      int start_y = has_art ? 280 : 100;
      if (mx >= x_offset + 40 && mx <= w && my >= start_y) {
        int idx = (my - start_y + (int)ctx->sidebar_right_scroll_y) / 50;
        size_t list_count = 0;

        if (ctx->sidebar_is_browsing) {
          list_count = ctx->browse_track_count;
        } else if (ctx->queue_type == QUEUE_TYPE_ALBUM ||
                   ctx->queue_type == QUEUE_TYPE_PLAYLIST) {
          list_count = ctx->count;
        } else {
          list_count = ctx->recents_count;
        }

        if (idx >= 0 && idx < (int)list_count) {
          ctx->spotlight_source_rect.x = x_offset + 40;
          ctx->spotlight_source_rect.y =
              start_y + idx * 50 - (int)ctx->sidebar_right_scroll_y;
          ctx->spotlight_source_rect.w = SIDEBAR_W - 40;
          ctx->spotlight_source_rect.h = 50;

          static char cmd[32];
          /* Request: "when clicking on a song, ... enact the spotlight
           * search" Update: Differentiate left/right click. Right Click ->
           * Spotlight Left Click -> Play
           */
          if (is_right_click) {
            snprintf(cmd, sizeof(cmd), "spotlight_song_%d", idx);
          } else {
            snprintf(cmd, sizeof(cmd), "play_song_%d", idx);
          }
          return cmd;
        }
      }
    }
    if (mx >= x_offset && my < h - CONTROL_BAR_H)
      return "blocked_sidebar";
  } else {
    int cy = (h - CONTROL_BAR_H) / 2;
    if (mx >= w - 24 && mx <= w && my >= cy - 30 && my <= cy + 30)
      return "toggle_sidebar_right";
  }

  /* Library Scene */
  if (ctx->current_scene == SCENE_LIBRARY) {
    int left_w = (int)(ctx->sidebar_left_anim * SIDEBAR_W);
    int right_w = (int)(ctx->sidebar_right_anim * SIDEBAR_W);
    int content_x = left_w;
    int content_w = w - left_w - right_w;

    if (mx >= content_x + 20 && mx <= content_x + content_w - 20 &&
        my >= 20 + (int)ctx->library_header_offset &&
        my <= 60 + (int)ctx->library_header_offset) {
      if (mx >= content_x + content_w - 100) {
        return "library_toggle_view";
      }
      if (ctx->library_view_mode != LIBRARY_VIEW_PLAYLISTS) {
        return "library_search_focus";
      }
    }

    /* Search Results Hit Test - PRIORITY OVER CHIPS/GRID */
    if (ctx->search_query[0] != '\0') {
      /* Sync with render_search_results layout */
      int cur_y =
          80 + (int)ctx->library_header_offset - (int)ctx->library_scroll_y;

      /* Albums Section */
      if (ctx->library_filtered_count > 0) {
        /* "Albums" header space */
        cur_y += 40;

        /* Check Album Row Hit */
        int row_start_y = cur_y;
        int row_height = 250;

        if (my >= row_start_y && my <= row_start_y + row_height) {
          for (size_t i = 0; i < ctx->library_filtered_count; i++) {
            int px = content_x + 20 + (int)i * 180 -
                     (int)ctx->library_search_album_scroll_x;
            if (mx >= px && mx <= px + 160) {
              if (is_right_click)
                return "blocked";
              static char cmd[32];
              snprintf(cmd, sizeof(cmd), "open_album_%zu",
                       ctx->library_filtered_indices[i]);
              return cmd;
            }
          }
        }
        cur_y += 250;
      }

      /* Songs Section */
      if (ctx->library_filtered_track_count > 0) {
        cur_y += 50; /* "Songs" header margin */

        /* List Hit Test */
        int item_h = 60;

        if (my >= cur_y) {
          int idx = (my - cur_y) / item_h;
          if (idx >= 0 && idx < (int)ctx->library_filtered_track_count) {
            int hx = content_x + 90;
            if (mx >= hx - 20 && mx <= hx + 20) {
              static char cmd[64];
              snprintf(cmd, sizeof(cmd), "toggle_favorite_track_%zu",
                       ctx->library_filtered_track_indices[idx]);
              return cmd;
            }
            static char cmd[64];
            snprintf(cmd, sizeof(cmd), "play_library_track_%zu",
                     ctx->library_filtered_track_indices[idx]);
            return cmd;
          }
        }
      }

      /* Block clicks on underlying elements if search is active */
      return "blocked";
    }

    /* Chips & Sort area */
    int chip_y = 75 + (int)ctx->library_header_offset;
    if (my >= chip_y && my <= chip_y + 32) {
      int cur_x = content_x + 20;
      const char *filters[] = {"All", "Favorites", "Recent"};
      for (int i = 0; i < 3; i++) {
        int tw = font_get_text_width(filters[i]);
        int cw = tw + 30;
        if (mx >= cur_x && mx <= cur_x + cw) {
          static char cmd[32];
          snprintf(cmd, sizeof(cmd), "library_filter_%d", i);
          return cmd;
        }
        cur_x += cw + 10;
      }
      /* Sort Area */
      if (mx >= content_x + content_w - 160) {
        return "library_cycle_sort";
      }
    }

    int grid_top = 120 + (int)ctx->library_header_offset;
    if (my >= grid_top && my < h - CONTROL_BAR_H) {
      if (ctx->search_query[0] != '\0') {
        /* Sync with render_search_results layout */
        int cur_y =
            80 + (int)ctx->library_header_offset - (int)ctx->library_scroll_y;

        /* Albums Section */
        if (ctx->library_filtered_count > 0) {
          /* "Albums" header space */
          cur_y += 40;

          /* Check Album Row Hit */
          /* render_search_album_row uses standard grid spacing roughly */
          /* Albums are drawn in a row: x += 180 */
          int row_start_y = cur_y;
          int row_height = 250;

          if (my >= row_start_y && my <= row_start_y + row_height) {
            for (size_t i = 0; i < ctx->library_filtered_count; i++) {
              int px = content_x + 20 + (int)i * 180 -
                       (int)ctx->library_search_album_scroll_x;
              /* Clip to visibility if needed, but simple check is okay */
              if (mx >= px && mx <= px + 160) {
                if (is_right_click) {
                  /* Not fully implemented for right click on search albums yet
                   */
                  return "blocked";
                }
                /* Standard Click -> Open Album */
                static char cmd[32];
                snprintf(cmd, sizeof(cmd), "open_album_%zu",
                         ctx->library_filtered_indices[i]);
                return cmd;
              }
            }
          }
          cur_y += 250;
        }

        /* Songs Section */
        if (ctx->library_filtered_track_count > 0) {
          cur_y += 50; /* "Songs" header margin */

          /* List Hit Test */
          /* render_search_song_list iterates tracks */
          int item_h = 60;

          if (my >= cur_y) {
            int idx = (my - cur_y) / item_h;
            if (idx >= 0 && idx < (int)ctx->library_filtered_track_count) {
              /* Check if we hit a track */
              /* We need to map this filtered index to...
                 play_library_track_INDEX assumes main library index if applied
                 to "all tracks". But play_library_track uses
                 `ctx->library_tracks[idx]`.

                 If we pass the *filtered* index, we point to WRONG track if
                 main library > filtered. We must pass the REAL track index.

                 We have `ctx->library_filtered_track_indices[idx]` which gives
                 us the real index.
              */

              /* Heart Check (Left ~90px +/- 15) */
              /* From render_search_song_list: heart is at x + 90 */
              int hx = content_x + 90;
              if (mx >= hx - 20 && mx <= hx + 20) {
                /* Toggle favorite */
                static char cmd[64];
                snprintf(cmd, sizeof(cmd), "toggle_favorite_track_%zu",
                         ctx->library_filtered_track_indices[idx]);
                return cmd;
              }

              static char cmd[64];
              snprintf(cmd, sizeof(cmd), "play_library_track_%zu",
                       ctx->library_filtered_track_indices[idx]);
              return cmd;
            }
          }
        }

        /* If we are in search mode and didn't hit anything, BLOCK interaction
           so we don't click through to the library underneath */
        return "blocked";
      } else {
        /* Generic View Hit Test (List or Grid) */
        int grid_top = 120 + (int)ctx->library_header_offset;
        int cur_y = grid_top - (int)ctx->library_scroll_y;

        if (ctx->library_view_mode == LIBRARY_VIEW_PLAYLISTS) {
          /* --- Playlist View Logic --- */
          int pad = 20;
          int cols = material_get_grid_cols(w);
          int cw = (w - (cols + 1) * pad) / cols;
          int ch = cw + 60;

          for (size_t i = 0; i < ctx->library_playlist_count; i++) {
            int col = i % cols;
            int row = i / cols;
            int item_x = content_x + pad + col * (cw + pad);
            int item_y = cur_y + row * (ch + pad);

            if (item_y + ch < grid_top)
              continue;
            if (item_y > h - CONTROL_BAR_H)
              break;

            if (mx >= item_x && mx <= item_x + cw && my >= item_y &&
                my <= item_y + ch) {
              static char cmd[64];
              snprintf(cmd, sizeof(cmd), "open_playlist_%d",
                       ctx->library_playlists[i].id);
              return cmd;
            }
          }

          /* FAB Hit Test */
          int fab_size = 56;
          int fab_x = content_x + content_w - fab_size - 30;
          /* Fixed position relative to screen, not scroll */
          int fab_y = h - CONTROL_BAR_H - fab_size - 30;

          /* Simple box check for FAB circle */
          if (mx >= fab_x && mx <= fab_x + fab_size && my >= fab_y &&
              my <= fab_y + fab_size) {
            return "create_playlist";
          }

          /* Dialog Input if open is handled by settings? No, custom dialog.
           */
          /* The dialog is modal, so we should check it earlier or assume
           * blocked. Actually, if is_creating_playlist is true, we should
           * block interaction with other things. Let's add that check at the
           * top later or assume simple non-blocking for now, but handling
           * click on it.
           */
          if (ctx->is_creating_playlist) {
            /* Dialog is centered */
            int dw = 400;
            int dh = 200;
            int dx = content_x + (content_w - dw) / 2;
            int dy = grid_top + (h - grid_top - dh) / 2;

            /* Cancel Button */
            /* Map Hit Test to New Coordinates */
            /* Cancel Button: x = dx + 30, w = 100 */
            /* Create Button: x = dx + dw - 30 - 100, w = 100 */
            /* Y = dy + dh - 60, h = 40 */

            int cancel_x = dx + 30;
            int create_x = dx + dw - 30 - 100;
            int btn_y = dy + dh - 60;

            if (my >= btn_y && my <= btn_y + 40) {
              if (mx >= cancel_x && mx <= cancel_x + 100)
                return "cancel_playlist_creation";
              if (mx >= create_x && mx <= create_x + 100)
                return "confirm_playlist_creation";
            }

            /* Input Focus */
            if (mx >= dx + 20 && mx <= dx + dw - 20 && my >= dy + 60 &&
                my <= dy + 100) {
              return "focus_playlist_input";
            }
            return "blocked";
          }

        } else if (ctx->library_view_mode == LIBRARY_VIEW_LIST) {
          /* --- List View Logic (Songs) --- */
          int item_h = 60; /* Updated height */

          for (size_t idx = 0; idx < ctx->library_filtered_track_count; idx++) {
            int py = cur_y + (int)idx * item_h;

            if (py > h - CONTROL_BAR_H)
              break;
            if (py + item_h < grid_top)
              continue;

            /* Check Bounds to prevent crash */
            if (idx >= ctx->library_filtered_track_count)
              break;
            size_t t_idx = ctx->library_filtered_track_indices[idx];
            if (t_idx >= ctx->library_track_count)
              continue;

            if (mx >= content_x + 20 && mx <= content_x + content_w - 20 &&
                my >= py && my <= py + item_h) {

              if (!is_right_click) {
                /* Art/Play Hit Area */
                int art_sz = 50;
                int art_x = content_x + 20;
                int art_y = py + (item_h - art_sz) / 2;

                if (mx >= art_x && mx <= art_x + art_sz && my >= art_y &&
                    my <= art_y + art_sz) {
                  static char cmd[64];
                  snprintf(cmd, sizeof(cmd), "play_library_track_%zu", t_idx);
                  return cmd;
                }

                /* Heart: x + 90, +/- 15px */
                int hx = content_x + 90;
                if (mx >= hx - 15 && mx <= hx + 15) {
                  static char cmd[64];
                  snprintf(cmd, sizeof(cmd), "toggle_favorite_track_%zu",
                           t_idx);
                  return cmd;
                }
              } else {
                /* Right Click -> Context Menu */
                static char cmd[64];
                snprintf(cmd, sizeof(cmd), "context_track_%zu", t_idx);
                return cmd;
              }

              /* Anywhere else -> Play Song */

              /* Anywhere else -> Play Song? Or just select?
                 User request: "library view needs to have a play option over
                 a album cover..." "clicking white space results in a crash"
                 -> Ensure we don't return valid cmd for empty space if using
                 strict hit test. But row click is usually play. */

              /* Let's keep row click as play for now, but prevent heart
                 conflict. If we are here, we missed art and heart. */
              static char cmd[64];
              snprintf(cmd, sizeof(cmd), "play_library_track_%zu", t_idx);
              return cmd;
            }
          }
        } else {
          /* --- Grid View Logic (Fix: Explicit World Space) --- */
          int base_card_w = 180;
          int padding = 20;

          int cols = material_get_grid_cols(content_w);

          int content_w_area = content_w - 40;
          int card_w = (cols > 0)
                           ? (content_w_area - (cols - 1) * padding) / cols
                           : base_card_w;
          int card_h = 260 + (card_w - base_card_w);

          /* Header + "Albums" Text Offset */
          int grid_top = 120 + (int)ctx->library_header_offset;
          int items_start_y = grid_top + 40;

          if (my >= items_start_y && my <= h - CONTROL_BAR_H) {
            /* Transform Screen Mouse -> World Mouse */
            /* WorldMouseY = ScreenMouseY - WindowOffsetY + ScrollOffsetY */
            int world_mouse_y = my - items_start_y + (int)ctx->library_scroll_y;
            int world_mouse_x = mx - (content_x + 20);

            if (world_mouse_y >= 0 && world_mouse_x >= 0) {
              /* Grid Index Calculation */
              int row = world_mouse_y / (card_h + padding);
              int col = world_mouse_x / (card_w + padding);

              if (col < cols && (world_mouse_x % (card_w + padding) < card_w) &&
                  (world_mouse_y % (card_h + padding) < card_h)) {

                size_t idx = (size_t)(row * cols + col);

                if (idx < ctx->library_filtered_count) {
                  int px = content_x + 20 + col * (card_w + padding);
                  int py = items_start_y - (int)ctx->library_scroll_y +
                           row * (card_h + padding);

                  ctx->spotlight_source_rect.x = px;
                  ctx->spotlight_source_rect.y = py;
                  ctx->spotlight_source_rect.w = card_w;
                  ctx->spotlight_source_rect.h = card_h;

                  if (!is_right_click) {
                    /* Check Heart (Top Right) */
                    int hx = px + card_w - 25;
                    int hy = py + 25;

                    /* Merged Heart Catch Area & Safe Zone (35px) */
                    if (mx >= hx - 35 && mx <= hx + 35 && my >= hy - 35 &&
                        my <= hy + 35) {
                      static char cmd[64];
                      snprintf(cmd, sizeof(cmd), "toggle_favorite_album_%zu",
                               ctx->library_filtered_indices[idx]);
                      return cmd;
                    }

                    /* Check Play (Hover Overlay on Art) - Box Logic */
                    if (mx >= px + 10 && mx <= px + 10 + card_w - 20 &&
                        my >= py + 10 && my <= py + 10 + card_w - 20) {
                      static char cmd[64];
                      snprintf(cmd, sizeof(cmd), "play_album_%zu",
                               ctx->library_filtered_indices[idx]);
                      return cmd;
                    }

                    /* Otherwise -> Open Album */
                    static char cmd[64];
                    snprintf(cmd, sizeof(cmd), "open_album_%zu",
                             ctx->library_filtered_indices[idx]);
                    return cmd;
                  } else {
                    /* Right Click -> Revert to Open Album (Spotlight) */
                    static char cmd[64];
                    snprintf(cmd, sizeof(cmd), "open_album_%zu",
                             ctx->library_filtered_indices[idx]);
                    return cmd;
                  }
                }
              }
            }
          }
        }
      }
    }
  }

  return NULL;
}
