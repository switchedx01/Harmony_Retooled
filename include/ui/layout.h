#ifndef LAYOUT_H
#define LAYOUT_H

#include <SDL2/SDL_rect.h>
#include <stdbool.h>

typedef enum { STATE_NORMAL, STATE_MINI_TRIGGERED } WindowLayoutState;

typedef struct {
  int width;
  int height;
  bool is_maximized;
  WindowLayoutState layout_state;

  /* Cached Layout Rects */
  SDL_Rect content_area;

  /* Control Bar Elements */
  SDL_Rect control_bar_rect;
  SDL_Rect play_button_rect;
  SDL_Rect prev_button_rect;
  SDL_Rect next_button_rect;
  SDL_Rect progress_bar_rect;
  SDL_Rect vol_slider_rect;
  SDL_Rect vol_icon_rect;

  /* Explicit Control Bar Elements (New) */
  SDL_Rect shuffle_button_rect;
  SDL_Rect repeat_button_rect;
  SDL_Rect info_area_rect;
  SDL_Rect album_art_rect;
  SDL_Rect mini_player_button_rect;

  /* Sidebars (Calculated positions, though render might animate them) */
  SDL_Rect sidebar_left_rect;
  SDL_Rect sidebar_right_rect;
} WindowContext;

/* Initialize layout context with defaults */
void layout_init(WindowContext *ctx, int width, int height);

/* Check and update the layout state based on dimensions */
void check_layout_state(WindowContext *ctx);

/* Recalculate all UI element positions based on current width/height */
void recalculate_layout(WindowContext *ctx);

#endif /* LAYOUT_H */
