#include "toast_overlay.h"
#include "font_renderer.h"
#include "material_renderer.h"
#include "window.h" /* For window dimensions if needed */
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>

/* Toast State */
typedef struct {
  bool active;
  char message[256];
  ToastType type;

  Uint32 start_time;
  int duration_ms; /* -1 for infinite */

  float progress; /* 0.0 - 1.0 */
  bool show_progress;

  /* Animation state */
  float anim_val; /* 0.0 (hidden) -> 1.0 (fully visible) */
} ToastState;

static ToastState g_toast = {0};

void toast_init(void) { memset(&g_toast, 0, sizeof(ToastState)); }

void toast_show(const char *message, ToastType type, int duration_ms) {
  /* If a toast is currently animating out, or we want to overwrite it */
  g_toast.active = true;
  snprintf(g_toast.message, sizeof(g_toast.message), "%s", message);
  g_toast.type = type;
  g_toast.start_time = SDL_GetTicks();
  g_toast.duration_ms = duration_ms;
  g_toast.progress = 0.0f;
  g_toast.show_progress = (type == TOAST_PROGRESS);

  /* If it was hidden, start animation from 0. If already visible, keep 1 or
     animate to it? For simplicity, if we are swapping content, we stay at 1.0
     if we were active. But if we want a "pop" effect, we could reset anim_val
     slightly. Let's just keep it simple. */
  if (g_toast.anim_val < 0.01f) {
    g_toast.anim_val = 0.0f;
  }
}

void toast_update_progress(float progress, const char *message) {
  if (g_toast.active && g_toast.type == TOAST_PROGRESS) {
    g_toast.progress = progress;
    if (g_toast.progress > 1.0f)
      g_toast.progress = 1.0f;
    if (g_toast.progress < 0.0f)
      g_toast.progress = 0.0f;

    if (message) {
      snprintf(g_toast.message, sizeof(g_toast.message), "%s", message);
    }

    /* Reset start time to keep it alive while updating */
    g_toast.start_time = SDL_GetTicks();
  }
}

void toast_render(SDL_Renderer *renderer, int window_w, int window_h) {
  /* Handle Lifecycle */
  if (g_toast.active) {
    /* Check duration */
    if (g_toast.duration_ms != -1) {
      Uint32 elapsed = SDL_GetTicks() - g_toast.start_time;
      if ((int)elapsed > g_toast.duration_ms) {
        g_toast.active = false; /* Trigger hide animation */
      }
    }
  }

  /* Animation Logic */
  float speed = 0.1f;
  if (g_toast.active) {
    if (g_toast.anim_val < 1.0f) {
      g_toast.anim_val += speed;
      if (g_toast.anim_val > 1.0f)
        g_toast.anim_val = 1.0f;
    }
  } else {
    if (g_toast.anim_val > 0.0f) {
      g_toast.anim_val -= speed;
      if (g_toast.anim_val < 0.0f)
        g_toast.anim_val = 0.0f;
    }
  }

  if (g_toast.anim_val <= 0.001f)
    return; /* Nothing to draw */

  /* Layout Dimensions */
  int toast_w = 320;       /* Reduced from 360 */
  int toast_h = g_toast.show_progress ? 64 : 50; /* Taller if progress bar exists */
  int margin_bottom = 120; /* Above control bar */

  /* Calculate Y position with slide-up animation */
  /* Target Y is (window_h - margin_bottom - toast_h) */
  /* Start Y (hidden) is (window_h - margin_bottom + 50) */
  int target_y = window_h - margin_bottom - toast_h;
  int start_y = window_h - margin_bottom + 20;

  int current_y = (int)(start_y + (target_y - start_y) * g_toast.anim_val);
  int current_x = (window_w - toast_w) / 2;

  /* Get Theme for Colors */
  ThemeColors *theme = material_get_theme();
  Color bg_col = theme->primary;
  Color text_col = {255, 255, 255};
  Color accent_col = theme->tertiary;

  /* Override for Error */
  if (g_toast.type == TOAST_ERROR) {
    bg_col = (Color){200, 60, 60}; /* Redish */
    accent_col = (Color){255, 255, 255};
  } else if (!theme) {
    /* Fallback if no theme yet */
    bg_col = (Color){40, 40, 40};
    accent_col = (Color){100, 200, 255};
  }

  /* Draw Background (Squircle) */
  /* Alpha fades in */
  Uint8 alpha = (Uint8)(g_toast.anim_val * 240);
  material_draw_rounded_rect(current_x, current_y, toast_w, toast_h, 16, bg_col,
                             alpha);

  /* Border - Defined Boundary */
  /* Use a darker, semi-opaque border to define edges clearly against any
   * background */
  material_draw_rounded_rect(current_x - 1, current_y - 1, toast_w + 2,
                             toast_h + 2, 16, (Color){20, 20, 20},
                             (Uint8)(alpha * 0.5f));

  /* Inner Highlight */
  material_draw_rounded_rect(current_x + 1, current_y + 1, toast_w - 2,
                             toast_h - 2, 14, (Color){255, 255, 255},
                             (Uint8)(alpha * 0.15f));

  /* Render Content */
  /* Icon area */
  /* TODO: Render icon based on type (Error X, Info i, etc) - Skipping for now,
   * just text */

  /* Text */
  int label_x = current_x + 30;
  int label_y;

  if (g_toast.show_progress) {
    /* Center text in the upper portion */
    int ty;
    font_get_text_center_offset(g_toast.message, 0, 36, NULL, &ty);
    label_y = current_y + ty;
    
    font_draw_text_limit(renderer, g_toast.message, label_x, label_y,
                         text_col.r, text_col.g, text_col.b, toast_w - 60);

    /* Draw Progress Bar */
    int bar_w = toast_w - 60;
    int bar_h = 4; /* Slightly thicker */
    int bar_x = current_x + 30;
    int bar_y = current_y + 46;

    /* Track */
    material_draw_rounded_rect(bar_x, bar_y, bar_w, bar_h, 2, (Color){0, 0, 0},
                               (Uint8)(alpha * 0.3f));

    /* Fill */
    if (g_toast.progress > 0.0f) {
      int fill_w = (int)(bar_w * g_toast.progress);
      if (fill_w < 6)
        fill_w = 6; /* min width for roundedness */
      material_draw_rounded_rect(bar_x, bar_y, fill_w, bar_h, 2, accent_col,
                                 alpha);
    }

  } else {
    /* Centered text vertically inside the entire toast_h */
    int ty;
    font_get_text_center_offset(g_toast.message, 0, toast_h, NULL, &ty);
    label_y = current_y + ty;
    font_draw_text_limit(renderer, g_toast.message, label_x, label_y,
                         text_col.r, text_col.g, text_col.b, toast_w - 60);
  }
}

bool toast_is_active(ToastType type) {
  return g_toast.active && g_toast.type == type;
}
