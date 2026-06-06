#include "layout.h"
#include "common.h"
#include "logging.h"

#define MIN_WIDTH_THRESHOLD 500

void layout_init(WindowContext *ctx, int width, int height) {
  if (!ctx)
    return;

  ctx->width = width;
  ctx->height = height;
  ctx->is_maximized =
      false; /* Initial assumption, will be updated by window events */
  ctx->layout_state = STATE_NORMAL;

  recalculate_layout(ctx);
}

void check_layout_state(WindowContext *ctx) {
  if (!ctx)
    return;

  WindowLayoutState old_state = ctx->layout_state;

  if (ctx->width < MIN_WIDTH_THRESHOLD) {
    ctx->layout_state = STATE_MINI_TRIGGERED;
  } else {
    ctx->layout_state = STATE_NORMAL;
  }

  if (ctx->layout_state != old_state) {
    if (ctx->layout_state == STATE_MINI_TRIGGERED) {

    } else {
    }
  }
}

void recalculate_layout(WindowContext *ctx) {
  if (!ctx)
    return;

  int w = ctx->width;
  int h = ctx->height;

  /* 1. Main Content Area */
  /* Content fills the space above the control bar */
  ctx->content_area.x = 0;
  ctx->content_area.y = 0;
  ctx->content_area.w = w;
  ctx->content_area.h = h - CONTROL_BAR_H;
  if (ctx->content_area.h < 0)
    ctx->content_area.h = 0;

  /* 2. Control Bar */
  ctx->control_bar_rect.x = 0;
  ctx->control_bar_rect.y = h - CONTROL_BAR_H;
  ctx->control_bar_rect.w = w;
  ctx->control_bar_rect.h = CONTROL_BAR_H;

  /* 3. Centered Play Controls */
  /* Center point */
  int cx = w / 2;
  int cy = h - (CONTROL_BAR_H / 2);

  /* Play Button (60x60) */
  int play_size = 60;
  ctx->play_button_rect.x = cx - (play_size / 2);
  ctx->play_button_rect.y = cy - (play_size / 2);
  ctx->play_button_rect.w = play_size;
  ctx->play_button_rect.h = play_size;

  /* Prev/Next Buttons (40x40) - Spaced out */
  int nav_size = 40;
  int spacing = 60; /* Distance from center of play button */

  /* Basic scaling logic: if width is large, increase spacing slightly?
     User req: "scale elements... stretch the space between them" if
     maximized/grows. Let's add dynamic spacing factor. */
  /* Fixed spacing to keep controls grouped */
  /* if (w > 1200) { spacing += (w - 1200) / 20; } */

  ctx->prev_button_rect.x = cx - (play_size / 2) - spacing - nav_size;
  ctx->prev_button_rect.y = cy - (nav_size / 2);
  ctx->prev_button_rect.w = nav_size;
  ctx->prev_button_rect.h = nav_size;

  ctx->next_button_rect.x = cx + (play_size / 2) + spacing;
  ctx->next_button_rect.y = cy - (nav_size / 2);
  ctx->next_button_rect.w = nav_size;
  ctx->next_button_rect.h = nav_size;

  /* 4. Anchored Right Elements (Volume) */
  int vol_w = 100;
  int vol_icon_w = 24;
  int pad_right = 40;

  ctx->vol_slider_rect.x = w - vol_w - pad_right;
  ctx->vol_slider_rect.y =
      cy - 2; /* Centered vertically roughly, slider is thin */
  ctx->vol_slider_rect.w = vol_w;
  ctx->vol_slider_rect.h = 4; /* Visual height */

  ctx->vol_icon_rect.x = ctx->vol_slider_rect.x - vol_icon_w - 10;
  ctx->vol_icon_rect.y = cy - (vol_icon_w / 2);
  ctx->vol_icon_rect.w = vol_icon_w;
  ctx->vol_icon_rect.h = vol_icon_w;

  /* 5. Progress Bar (Top of control bar) */
  /* Spans full width for now, or maybe padded?
     Design doc usually implies full width stripe or partial.
     Material renderer implementation at ~line 800+ (not fully seen) likely has
     it at the top of control bar. */
  ctx->progress_bar_rect.x = 20;
  ctx->progress_bar_rect.y =
      h - CONTROL_BAR_H - 3; /* Slightly above or at top edge */
  ctx->progress_bar_rect.w = w - 40;
  ctx->progress_bar_rect.h = 6;

  /* 6. Sidebars */
  ctx->sidebar_left_rect.x = 0;
  ctx->sidebar_left_rect.y = 0;
  ctx->sidebar_left_rect.w = SIDEBAR_W;
  ctx->sidebar_left_rect.h = h - CONTROL_BAR_H;

  ctx->sidebar_right_rect.x = w - SIDEBAR_W;
  ctx->sidebar_right_rect.y = 0;
  ctx->sidebar_right_rect.w = SIDEBAR_W;
  ctx->sidebar_right_rect.h = h - CONTROL_BAR_H;

  /* 7. New Control Bar Elements */
  /* Album Art - Left aligned in control bar */
  int art_size = CONTROL_BAR_H - 20; /* 10px padding top/bottom */
  ctx->album_art_rect.x = 20;
  ctx->album_art_rect.y = h - CONTROL_BAR_H + 10;
  ctx->album_art_rect.w = art_size;
  ctx->album_art_rect.h = art_size;

  /* Mini Player Button - Centered in Album Art */
  int mini_sz = art_size / 2;
  ctx->mini_player_button_rect.x =
      ctx->album_art_rect.x + (art_size - mini_sz) / 2;
  ctx->mini_player_button_rect.y =
      ctx->album_art_rect.y + (art_size - mini_sz) / 2;
  ctx->mini_player_button_rect.w = mini_sz;
  ctx->mini_player_button_rect.h = mini_sz;

  /* Shuffle & Repeat - Flanking the main controls */
  /* Shuffle to the left of Prev */
  int secondary_size = 30;
  int sec_spacing = 40;

  ctx->repeat_button_rect.x =
      ctx->prev_button_rect.x - sec_spacing - secondary_size;
  ctx->repeat_button_rect.y = cy - (secondary_size / 2);
  ctx->repeat_button_rect.w = secondary_size;
  ctx->repeat_button_rect.h = secondary_size;

  /* Shuffle to the right of Next */
  ctx->shuffle_button_rect.x =
      ctx->next_button_rect.x + ctx->next_button_rect.w + sec_spacing;
  ctx->shuffle_button_rect.y = cy - (secondary_size / 2);
  ctx->shuffle_button_rect.w = secondary_size;
  ctx->shuffle_button_rect.h = secondary_size;

  /* Info Area - Between Art and Repeat Button */
  /* Start after art + padding */
  int info_start_x = ctx->album_art_rect.x + ctx->album_art_rect.w + 20;
  /* End before repeat button - padding */
  int info_end_x = ctx->repeat_button_rect.x - 20;

  ctx->info_area_rect.x = info_start_x;
  ctx->info_area_rect.y = h - CONTROL_BAR_H + 10;
  ctx->info_area_rect.w =
      (info_end_x > info_start_x) ? (info_end_x - info_start_x) : 0;
  ctx->info_area_rect.h = CONTROL_BAR_H - 20;
}
