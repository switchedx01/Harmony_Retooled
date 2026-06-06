#include "player.h"
#include "audio_backend.h"
#include "hub_client.h"
#include "material_renderer.h"
#include "logging.h"
#include "string_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void player_send_status_to_hub(PlayerContext *ctx, const char *event_name) {
  if (!ctx || ctx->current_index < 0 || (size_t)ctx->current_index >= ctx->count) return;

  Song *current = &ctx->songs[ctx->current_index];
  ThemeColors *theme = material_get_theme();
  
  char state_str[16];
  if (ctx->state == PLAYER_STATE_PLAYING) strcpy(state_str, "playing");
  else if (ctx->state == PLAYER_STATE_PAUSED) strcpy(state_str, "paused");
  else strcpy(state_str, "stopped");

  char color_str[10] = "#000000";
  if (theme) {
      snprintf(color_str, sizeof(color_str), "#%02x%02x%02x", 
               theme->primary.r, theme->primary.g, theme->primary.b);
  }

  char hub_msg[2048];
  snprintf(hub_msg, sizeof(hub_msg), 
           "{\"event\": \"%s\", \"title\": \"%s\", \"artist\": \"%s\", "
           "\"album\": \"%s\", \"art_path\": \"%s\", \"filepath\": \"%s\", "
           "\"position\": %.2f, \"duration\": %.2f, \"state\": \"%s\", \"theme_color\": \"%s\"}",
           event_name,
           current->title, current->artist, current->album, current->art_path, current->path,
           ctx->position, ctx->duration, state_str, color_str);

  send_to_hub(hub_msg);
}

Result player_init(PlayerContext *ctx) {
  if (!c_assert(ctx != NULL))
    return RESULT_ERROR_NULL_POINTER;

  ctx->count = 0;
  ctx->current_index = -1;
  ctx->volume = 1.0f;
  ctx->state = PLAYER_STATE_STOPPED;
  ctx->position = 0.0f;
  ctx->duration = 0.0f;

  /* Context defaults */
  ctx->queue_type = QUEUE_TYPE_RECENTS;
  ctx->queue_context_name[0] = '\0';
  ctx->queue_context_art_path[0] = '\0';
  ctx->recents_count = 0;

  /* UI defaults: Menu closed by default */
  ctx->sidebar_left_open = false;
  ctx->sidebar_right_open = false;
  ctx->sidebar_left_anim = 0.0f;
  ctx->sidebar_right_anim = 0.0f;
  ctx->settings_popup_open = false;
  ctx->settings_popup_anim = 0.0f;
  ctx->settings_active_tab = 0;
  memset(ctx->library_input_buffer, 0, 512);
  memset(ctx->input_clipboard_buffer, 0, 1024);
  ctx->is_typing_library_path = false;
  ctx->setting_group_albums = true; /* Default to true as per user preference */
  ctx->setting_clean_db_on_scan = false; /* Default to false */
  ctx->library_scroll_y = 0.0f;
  ctx->library_max_scroll = 0.0f;
  ctx->library_last_scroll_y = 0.0f;
  ctx->library_header_offset = 0.0f;

  ctx->library_albums = NULL;
  ctx->library_album_count = 0;
  ctx->library_filtered_indices = NULL;
  ctx->library_filtered_count = 0;

  ctx->library_tracks = NULL;
  ctx->library_track_count = 0;
  ctx->library_filtered_track_indices = NULL;
  ctx->library_filtered_track_count = 0;

  ctx->library_paths = NULL;
  ctx->library_path_count = 0;

  ctx->library_needs_filter = true;
  ctx->library_search_album_scroll_x = 0.0f;

  ctx->sidebar_right_scroll_y = 0.0f;
  ctx->sidebar_right_max_scroll = 0.0f;

  ctx->repeat_mode = REPEAT_OFF;
  ctx->shuffle_mode = false;
  ctx->shuffle_indices = NULL;

  ctx->library_playlists = NULL;
  ctx->library_playlist_count = 0;
  ctx->is_creating_playlist = false;
  ctx->new_playlist_name[0] = '\0';

  ctx->visualizer_drag_param_index = -1;
  ctx->visualizer_show_settings = false;
  ctx->visualizer_link_colors = true;

  /* Confirmation Dialog */
  ctx->confirm_dialog_open = false;
  ctx->confirm_dialog_message[0] = '\0';
  ctx->confirm_dialog_action[0] = '\0';

  /* Queue Drag-to-Reorder */
  ctx->queue_drag_index = -1;
  ctx->queue_is_dragging = false;
  ctx->queue_drag_target_index = -1;

  /* Tooltip */
  ctx->tooltip_text[0] = '\0';
  ctx->tooltip_timer = 0.0f;

  /* EQ defaults */
  ctx->eq_popup_open = false;
  ctx->eq_enabled = true;
  ctx->eq_band_count = 5;
  for (int i = 0; i < EQ_BAND_COUNT_MAX; i++)
    ctx->eq_gains[i] = 0.0f;
  ctx->eq_selected_preset_id = -1;
  strncpy(ctx->eq_selected_preset_name, "Flat", sizeof(ctx->eq_selected_preset_name) - 1);
  ctx->eq_is_new_mode = false;
  ctx->eq_new_preset_name[0] = '\0';
  ctx->eq_typing_preset_name = false;
  ctx->eq_dropdown_open = false;
  ctx->eq_presets = NULL;
  ctx->eq_preset_count = 0;

  char addr_msg[512];
  snprintf(addr_msg, sizeof(addr_msg), 
           "player_init: ctx=%p, songs=%zu, count=%zu, presets=%zu, size=%zu", 
           (void*)ctx,
           (size_t)((char*)&ctx->songs - (char*)ctx),
           (size_t)((char*)&ctx->count - (char*)ctx),
           (size_t)((char*)&ctx->eq_presets - (char*)ctx),
           sizeof(PlayerContext));
  log_message("DEBUG", addr_msg);

  ctx->setting_normalization = false;



  ctx->setting_gapless = false;

  log_message("INFO", "Player initialized.");
  return RESULT_SUCCESS;
}

void player_add_to_recents(PlayerContext *ctx, const Song *song) {
  if (!ctx || !song)
    return;

  /* Check if already in recents */
  for (size_t i = 0; i < ctx->recents_count; i++) {
    if (strcmp(ctx->recents[i].path, song->path) == 0) {
      /* Move to front */
      Song tmp = ctx->recents[i];
      for (size_t j = i; j > 0; j--) {
        ctx->recents[j] = ctx->recents[j - 1];
      }
      ctx->recents[0] = tmp;
      return;
    }
  }

  /* Shift others right */
  size_t to_move = (ctx->recents_count < 10) ? ctx->recents_count : 9;
  for (size_t i = to_move; i > 0; i--) {
    ctx->recents[i] = ctx->recents[i - 1];
  }
  ctx->recents[0] = *song;
  if (ctx->recents_count < 10)
    ctx->recents_count++;
}

Result player_add_song(PlayerContext *ctx, const char *title,
                       const char *artist, const char *album, const char *path,
                       const char *art_path) {
  Song *song;
  Result res;

  if (!c_assert(ctx != NULL))
    return RESULT_ERROR_NULL_POINTER;
  if (!c_assert(title != NULL))
    return RESULT_ERROR_NULL_POINTER;
  if (!c_assert(artist != NULL))
    return RESULT_ERROR_NULL_POINTER;
  if (!c_assert(path != NULL))
    return RESULT_ERROR_NULL_POINTER;

  if (ctx->count >= MAX_PLAYLIST_SIZE) {
    log_message("WARN", "Playlist full.");
    return RESULT_ERROR_BUFFER_OVERFLOW;
  }

  song = &ctx->songs[ctx->count];

  res = safe_strncpy(song->title, title, MAX_SONG_TITLE);
  if (res != RESULT_SUCCESS)
    return res;

  res = safe_strncpy(song->artist, artist, MAX_SONG_TITLE);
  if (res != RESULT_SUCCESS)
    return res;

  if (album) {
    res = safe_strncpy(song->album, album, MAX_SONG_TITLE);
    if (res != RESULT_SUCCESS)
      return res;
  } else {
    song->album[0] = '\0';
  }

  res = safe_strncpy(song->path, path, MAX_PATH_LENGTH);
  if (res != RESULT_SUCCESS)
    return res;

  if (art_path) {
    res = safe_strncpy(song->art_path, art_path, MAX_PATH_LENGTH);
    if (res != RESULT_SUCCESS)
      return res;
  } else {
    song->art_path[0] = '\0';
  }

  ctx->count++;

  if (ctx->current_index == -1) {
    ctx->current_index = 0;
  }

  return RESULT_SUCCESS;
}

Result player_play(PlayerContext *ctx) {
  if (!c_assert(ctx != NULL))
    return RESULT_ERROR_NULL_POINTER;

  if (ctx->count == 0) {
    log_message("WARN", "No songs to play.");
    return RESULT_ERROR_GENERIC;
  }

  if (ctx->current_index < 0 || (size_t)ctx->current_index >= ctx->count) {
    ctx->current_index = 0;
  }

  ctx->state = PLAYER_STATE_PLAYING;
  log_message("INFO", ctx->songs[ctx->current_index].title);

  player_send_status_to_hub(ctx, "song_started");

  return RESULT_SUCCESS;
}

Result player_pause(PlayerContext *ctx) {
  if (!c_assert(ctx != NULL))
    return RESULT_ERROR_NULL_POINTER;

  if (ctx->state == PLAYER_STATE_PLAYING) {
    ctx->state = PLAYER_STATE_PAUSED;
    log_message("INFO", "Playback paused.");
    player_send_status_to_hub(ctx, "playback_paused");
  } else if (ctx->state == PLAYER_STATE_PAUSED) {
    ctx->state = PLAYER_STATE_PLAYING;
    log_message("INFO", "Playback resumed.");
    player_send_status_to_hub(ctx, "playback_resumed");
  }

  return RESULT_SUCCESS;
}

Result player_stop(PlayerContext *ctx) {
  if (!c_assert(ctx != NULL))
    return RESULT_ERROR_NULL_POINTER;

  ctx->state = PLAYER_STATE_STOPPED;
  log_message("INFO", "Playback stopped.");
  player_send_status_to_hub(ctx, "playback_stopped");
  return RESULT_SUCCESS;
}

/* Helper: Fisher-Yates Shuffle */
static void shuffle_playlist(PlayerContext *ctx) {
  if (!ctx || ctx->count == 0)
    return;

  /* Allocate if needed */
  if (!ctx->shuffle_indices) {
    ctx->shuffle_indices = malloc(sizeof(int) * MAX_PLAYLIST_SIZE);
  }

  /* Init 0..N */
  for (int i = 0; i < (int)ctx->count; i++) {
    ctx->shuffle_indices[i] = i;
  }

  /* Shuffle */
  for (int i = (int)ctx->count - 1; i > 0; i--) {
    int j = rand() % (i + 1);
    int temp = ctx->shuffle_indices[i];
    ctx->shuffle_indices[i] = ctx->shuffle_indices[j];
    ctx->shuffle_indices[j] = temp;
  }

  /* Ensure current playing song is first? Optional.
     For now, just random. */
}

void player_set_shuffle(PlayerContext *ctx, bool enable) {
  if (!ctx)
    return;
  ctx->shuffle_mode = enable;
  if (enable) {
    shuffle_playlist(ctx);
  } else {
    if (ctx->shuffle_indices) {
      free(ctx->shuffle_indices);
      ctx->shuffle_indices = NULL;
    }
  }
}

void player_set_repeat(PlayerContext *ctx, RepeatMode mode) {
  if (!ctx)
    return;
  ctx->repeat_mode = mode;
}

Result player_next(PlayerContext *ctx) {
  if (!c_assert(ctx != NULL))
    return RESULT_ERROR_NULL_POINTER;

  if (ctx->count == 0)
    return RESULT_SUCCESS;

  /* Check Repeat One */
  if (ctx->repeat_mode == REPEAT_ONE) {
    audio_seek(0);
    /* Resume if paused? logic usually implies 'next' button press forces play
     */
    if (ctx->state != PLAYER_STATE_PLAYING) {
      player_play(ctx);
    }
    return RESULT_SUCCESS;
  }

  int next_index = -1;

  if (ctx->shuffle_mode && ctx->shuffle_indices) {
    /* Find current visual index in shuffle list */
    int list_pos = -1;
    for (int i = 0; i < (int)ctx->count; i++) {
      if (ctx->shuffle_indices[i] == ctx->current_index) {
        list_pos = i;
        break;
      }
    }

    if (list_pos != -1) {
      if (list_pos + 1 < (int)ctx->count) {
        next_index = ctx->shuffle_indices[list_pos + 1];
      } else {
        /* End of list */
        if (ctx->repeat_mode == REPEAT_ALL) {
          next_index = ctx->shuffle_indices[0]; /* Loop */
        } else {
          /* Stop at end */
          next_index = -1;
        }
      }
    } else {
      /* Current not found (maybe added?), play random or first */
      next_index = ctx->shuffle_indices[0];
    }
  } else {
    /* Normal */
    if (ctx->current_index + 1 < (int)ctx->count) {
      next_index = ctx->current_index + 1;
    } else {
      if (ctx->repeat_mode == REPEAT_ALL) {
        next_index = 0;
      } else {
        next_index = -1;
      }
    }
  }

  if (next_index != -1) {

    ctx->current_index = next_index;

    /* If we were stopped, we might want to stay stopped?
       But usually Next implies Play.
       Let main.c handle the audio_play_file trigger based on index change. */

    /* Note: main.c checks `if (g_player_ctx.current_index >= 0)` then plays. */
  } else {
    /* End of playlist, stop */
    player_stop(ctx);
    /* Invalidate index so main.c doesn't replay last song?
       Currently main.c: if (current_index >= 0) play.
       If we set it to -1, it stops. */
    // ctx->current_index = 0; // Don't reset to 0 if stopping, or maybe do?
    // Standard behavior: Stop.
  }

  return RESULT_SUCCESS;
}

Result player_prev(PlayerContext *ctx) {
  if (!c_assert(ctx != NULL))
    return RESULT_ERROR_NULL_POINTER;

  if (ctx->count == 0)
    return RESULT_SUCCESS;

  /* Check Play Position: if > 3 seconds, replay current */
  if (audio_get_position() > 3.0f) {
    audio_seek(0);
    return RESULT_SUCCESS;
  }

  int prev_index = -1;

  if (ctx->shuffle_mode && ctx->shuffle_indices) {
    int list_pos = -1;
    for (int i = 0; i < (int)ctx->count; i++) {
      if (ctx->shuffle_indices[i] == ctx->current_index) {
        list_pos = i;
        break;
      }
    }
    if (list_pos > 0) {
      prev_index = ctx->shuffle_indices[list_pos - 1];
    } else {
      if (ctx->repeat_mode == REPEAT_ALL) {
        prev_index = ctx->shuffle_indices[ctx->count - 1];
      } else {
        prev_index = 0; // Stay at start
      }
    }
  } else {
    if (ctx->current_index > 0) {
      prev_index = ctx->current_index - 1;
    } else {
      if (ctx->repeat_mode == REPEAT_ALL) {
        prev_index = ctx->count - 1;
      } else {
        prev_index = 0;
      }
    }
  }

  if (prev_index != -1) {
    ctx->current_index = prev_index;
  }

  return RESULT_SUCCESS;
}

Result player_play_at(PlayerContext *ctx, int index) {
  if (!c_assert(ctx != NULL))
    return RESULT_ERROR_NULL_POINTER;
  if (index < 0 || (size_t)index >= ctx->count)
    return RESULT_ERROR_GENERIC;

  ctx->current_index = index;
  return player_play(ctx);
}
