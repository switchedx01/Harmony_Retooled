#include "command_dispatch.h"
#include "album_art.h"
#include "audio_backend.h"
#include "database.h"
#include "library_manager.h"
#include "logging.h"
#include "material_renderer.h"
#include "metadata_parser.h"
#include "mini_player.h"
#include "mpris_service.h"
#include "player.h"
#include "string_utils.h"
#include "toast_overlay.h"
#include "visualizer_loader.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> // for strcasecmp

/* Helper forward declarations */
static void start_playback_helper(PlayerContext *ctx);
static void on_scan_progress(const char *title, const char *artist,
                             float percent);

static int updater_thread_func(void *data) {
    PlayerContext *ctx = (PlayerContext *)data;
    char cmd_buf[256];
    snprintf(cmd_buf, sizeof(cmd_buf), "python3 scripts/updater.py \"%s\"", HARMONY_VERSION);
    FILE *fp = popen(cmd_buf, "r");
    if (!fp) {
        strncpy(ctx->update_status_msg, "Failed to start updater script", sizeof(ctx->update_status_msg) - 1);
        return 0;
    }
    char line[128];
    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
        if (len > 0) {
            strncpy(ctx->update_status_msg, line, sizeof(ctx->update_status_msg) - 1);
        }
    }
    pclose(fp);
    return 0;
}

static void try_load_cover_art_helper(Song *s) {
  char *art_path = extract_album_art(s->path);
  if (art_path) {
    safe_strncpy(s->art_path, art_path, MAX_PATH_LENGTH);
    material_set_background(art_path);
    free(art_path);
  } else {
    s->art_path[0] = '\0';
    material_set_background("placeholder.png");
  }
}

static void start_playback_helper(PlayerContext *ctx) {
  if (ctx->current_index >= 0 && (size_t)ctx->current_index < ctx->count) {
    Song *s = &ctx->songs[ctx->current_index];
    audio_play_file(s->path);
    player_play(ctx);
    try_load_cover_art_helper(s);
    player_add_to_recents(ctx, s);

    char *art_url = NULL;
    if (s->art_path[0] != '\0') {
      art_url = s->art_path;
    }
    mpris_update_metadata(s->title, s->artist, s->album, art_url, 0);
    mpris_update_playback_status(true);
  }
}

static void on_scan_progress(const char *title, const char *artist,
                             float percent) {
  AppContext *app = app_get_context();
  static Uint32 last_update_time = 0;
  Uint32 current_time = SDL_GetTicks();

  if (current_time - last_update_time < 50 && percent < 1.0f) {
    return;
  }
  last_update_time = current_time;

  char msg[512];
  if (artist && artist[0] != '\0') {
    snprintf(msg, sizeof(msg), "Scanning: %s - %s", title, artist);
  } else {
    snprintf(msg, sizeof(msg), "Scanning: %s", title);
  }

  if (!toast_is_active(TOAST_PROGRESS)) {
    toast_show(msg, TOAST_PROGRESS, -1);
  }
  toast_update_progress(percent, msg);

  /* Force Event Pump & Render */
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
  }
  material_render(app->player, app->layout);
}

bool dispatch_command(AppContext *ctx, const char *cmd, int mx_context) {
  if (!ctx || !cmd)
    return false;
  
  static bool size_logged = false;
  if (!size_logged) {
    char size_msg[128];
    snprintf(size_msg, sizeof(size_msg), "command_dispatch: sizeof(PlayerContext)=%zu", sizeof(PlayerContext));
    log_message("DEBUG", size_msg);
    size_logged = true;
  }

  PlayerContext *g_player_ctx = ctx->player;

  WindowContext *g_layout_ctx = ctx->layout;

  if (strcmp(cmd, "play") == 0) {
    if (g_player_ctx->state == PLAYER_STATE_PLAYING) {
      player_pause(g_player_ctx);
      audio_pause();
      mpris_update_playback_status(false);
    } else if (g_player_ctx->state == PLAYER_STATE_PAUSED) {
      player_pause(g_player_ctx);
      audio_resume();
      mpris_update_playback_status(true);
    } else {
      if (g_player_ctx->current_index >= 0) {
        start_playback_helper(g_player_ctx);
      }
    }
  } else if (strcmp(cmd, "play_force") == 0) {
    if (g_player_ctx->state != PLAYER_STATE_PLAYING) {
        if (g_player_ctx->state == PLAYER_STATE_PAUSED) {
            player_pause(g_player_ctx);
            audio_resume();
            mpris_update_playback_status(true);
        } else if (g_player_ctx->current_index >= 0) {
            start_playback_helper(g_player_ctx);
        }
    }
  } else if (strcmp(cmd, "pause_force") == 0) {
    if (g_player_ctx->state == PLAYER_STATE_PLAYING) {
      player_pause(g_player_ctx);
      audio_pause();
      mpris_update_playback_status(false);
    }
  } else if (strcmp(cmd, "next") == 0) {
    player_next(g_player_ctx);
    start_playback_helper(g_player_ctx);
  } else if (strcmp(cmd, "prev") == 0) {
    player_prev(g_player_ctx);
    start_playback_helper(g_player_ctx);
  } else if (strcmp(cmd, "toggle_shuffle") == 0) {
    player_set_shuffle(g_player_ctx, !g_player_ctx->shuffle_mode);
  } else if (strcmp(cmd, "cycle_repeat") == 0) {
    RepeatMode next_mode = REPEAT_OFF;
    if (g_player_ctx->repeat_mode == REPEAT_OFF)
      next_mode = REPEAT_ALL;
    else if (g_player_ctx->repeat_mode == REPEAT_ALL)
      next_mode = REPEAT_ONE;
    else
      next_mode = REPEAT_OFF;
    player_set_repeat(g_player_ctx, next_mode);
  } else if (strncmp(cmd, "play_song_", 10) == 0 ||
             strncmp(cmd, "spotlight_song_", 15) == 0) {
    bool is_spotlight = (strncmp(cmd, "spotlight_song_", 15) == 0) ||
                        (strstr(cmd, "_right") != NULL);
    int index =
        atoi(cmd + (strncmp(cmd, "spotlight_song_", 15) == 0 ? 15 : 10));

    if (is_spotlight) {
      Song *s = NULL;
      if (g_player_ctx->queue_type == QUEUE_TYPE_ALBUM ||
          g_player_ctx->count > 1) {
        if (index >= 0 && (size_t)index < g_player_ctx->count)
          s = &g_player_ctx->songs[index];
      } else if (index >= 0 && (size_t)index < g_player_ctx->recents_count) {
        s = &g_player_ctx->recents[index];
      }

      if (s) {
        int album_idx = -1;
        for (size_t i = 0; i < g_player_ctx->library_album_count; i++) {
          if (strcasecmp(g_player_ctx->library_albums[i].name, s->album) == 0) {
            album_idx = (int)i;
            break;
          }
        }
        if (album_idx != -1) {
          g_player_ctx->spotlight_active = true;
          g_player_ctx->spotlight_album_idx = album_idx;
          g_player_ctx->spotlight_is_singular = true;
          strncpy(g_player_ctx->spotlight_song_path, s->path,
                  MAX_PATH_LENGTH - 1);
          strncpy(g_player_ctx->spotlight_song_title, s->title,
                  MAX_SONG_TITLE - 1);
          strncpy(g_player_ctx->spotlight_song_artist, s->artist,
                  MAX_SONG_TITLE - 1);
          strncpy(g_player_ctx->spotlight_song_art_path, s->art_path,
                  MAX_PATH_LENGTH - 1);

          Track db_track;
          if (db_get_track_by_path(s->path, &db_track) == RESULT_SUCCESS) {
            strncpy(g_player_ctx->spotlight_song_album, db_track.title,
                    MAX_SONG_TITLE - 1);
            strncpy(g_player_ctx->spotlight_song_album, s->album,
                    MAX_SONG_TITLE - 1);
            strncpy(g_player_ctx->spotlight_song_genre, db_track.genre, 63);
            g_player_ctx->spotlight_song_year = db_track.year;
            g_player_ctx->spotlight_song_duration = db_track.duration;
          } else {
            strncpy(g_player_ctx->spotlight_song_album, s->album,
                    MAX_SONG_TITLE - 1);
            g_player_ctx->spotlight_song_genre[0] = '\0';
            g_player_ctx->spotlight_song_year = 0;
            g_player_ctx->spotlight_song_duration = 0;
          }

          g_player_ctx->queue_type = QUEUE_TYPE_ALBUM;
          strncpy(g_player_ctx->queue_context_name, s->album,
                  MAX_SONG_TITLE - 1);
          Album *a = &g_player_ctx->library_albums[album_idx];
          strncpy(g_player_ctx->queue_context_art_path, a->art_filename,
                  MAX_PATH_LENGTH - 1);

          g_player_ctx->spotlight_phase = SPOTLIGHT_PHASE_1_INIT;
          g_player_ctx->spotlight_anim = 0;
          g_player_ctx->spotlight_flip_anim = 0;
          g_player_ctx->spotlight_scroll_y = 0;
          int mx, my;
          SDL_GetMouseState(&mx, &my);
          g_player_ctx->spotlight_source_rect.x = mx;
          g_player_ctx->spotlight_source_rect.y = my;
          g_player_ctx->spotlight_source_rect.w = 1;
          g_player_ctx->spotlight_source_rect.h = 1;
        }
      }
      return true;
    }

    Song *target = NULL;
    if (g_player_ctx->sidebar_is_browsing && !is_spotlight) {
      g_player_ctx->count = 0;
      for (size_t i = 0;
           i < g_player_ctx->browse_track_count && i < MAX_PLAYLIST_SIZE; i++) {
        Track *t = &g_player_ctx->browse_tracks[i];
        player_add_song(g_player_ctx, t->title, t->artist,
                        g_player_ctx->browse_context_name, t->filepath,
                        g_player_ctx->browse_context_art_path);
      }
      g_player_ctx->queue_type = QUEUE_TYPE_ALBUM;
      strncpy(g_player_ctx->queue_context_name,
              g_player_ctx->browse_context_name, MAX_SONG_TITLE - 1);
      strncpy(g_player_ctx->queue_context_art_path,
              g_player_ctx->browse_context_art_path, MAX_PATH_LENGTH - 1);

      g_player_ctx->current_index = index;
      if (index >= 0 && (size_t)index < g_player_ctx->count) {
        target = &g_player_ctx->songs[index];
      }
      g_player_ctx->sidebar_is_browsing = false;
    } else {
      /* Fallback: try to play from the main song list if count > 0 */
      if (index >= 0 && (size_t)index < g_player_ctx->count) {
        g_player_ctx->current_index = index;
        target = &g_player_ctx->songs[index];
      } else if (index >= 0 && (size_t)index < g_player_ctx->recents_count) {
        target = &g_player_ctx->recents[index];
      }
    }

    if (target) {
      start_playback_helper(g_player_ctx);
    }
  } else if (strcmp(cmd, "toggle_sidebar_left") == 0) {
    g_player_ctx->sidebar_left_open = !g_player_ctx->sidebar_left_open;
  } else if (strcmp(cmd, "close_sidebar_left") == 0) {
    g_player_ctx->sidebar_left_open = false;
  } else if (strcmp(cmd, "toggle_sidebar_right") == 0) {
    g_player_ctx->sidebar_right_open = !g_player_ctx->sidebar_right_open;
    if (!g_player_ctx->sidebar_right_open) {
      g_player_ctx->sidebar_is_browsing = false;
    }
  } else if (strcmp(cmd, "close_sidebar_right") == 0) {
    g_player_ctx->sidebar_right_open = false;
    g_player_ctx->sidebar_is_browsing = false;
  } else if (strcmp(cmd, "open_settings") == 0) {
    g_player_ctx->settings_popup_open = true;
  } else if (strcmp(cmd, "close_settings") == 0) {
    g_player_ctx->settings_popup_open = false;
  } else if (strcmp(cmd, "set_volume") == 0) {
    float new_vol = (float)mx_context / 100.0f;
    if (new_vol < 0.0f) new_vol = 0.0f;
    if (new_vol > 1.0f) new_vol = 1.0f;
    g_player_ctx->volume = new_vol;
    audio_set_volume(new_vol);
  } else if (strcmp(cmd, "volume_slider") == 0) {
    int vol_x = g_layout_ctx->vol_slider_rect.x;
    int vol_w = g_layout_ctx->vol_slider_rect.w;
    int mx = mx_context;
    if (mx < vol_x)
      mx = vol_x;
    if (mx > vol_x + vol_w)
      mx = vol_x + vol_w;
    float new_vol = (float)(mx - vol_x) / (float)vol_w;
    g_player_ctx->volume = new_vol;
    audio_set_volume(new_vol);
    char msg[64];
    /* Debounce volume logging to avoid spam */
    static uint32_t last_vol_log = 0;
    if (SDL_GetTicks() - last_vol_log > 500) {
      snprintf(msg, sizeof(msg), "Volume set to: %.2f", new_vol);
      log_message("INFO", msg);
      last_vol_log = SDL_GetTicks();
    }
  } else if (strcmp(cmd, "seek_bar") == 0 ||
             strcmp(cmd, "seek_bar_force") == 0) {
    bool force = (strcmp(cmd, "seek_bar_force") == 0);
    int bar_x = g_layout_ctx->progress_bar_rect.x;
    int bar_w = g_layout_ctx->progress_bar_rect.w;
    int mx = mx_context;
    if (mx < bar_x)
      mx = bar_x;
    if (mx > bar_x + bar_w)
      mx = bar_x + bar_w;
    float pct = (float)(mx - bar_x) / (float)bar_w;
    float target_time = pct * g_player_ctx->duration;

    /* Always update UI position immediately for responsiveness */
    g_player_ctx->position = target_time;

    /* Debounce actual audio seeking to prevent audio stutter/choppiness */
    static uint32_t last_seek_time = 0;
    if (force || (SDL_GetTicks() - last_seek_time > 100)) {
      audio_seek(target_time);
      last_seek_time = SDL_GetTicks();
    }
  } else if (strcmp(cmd, "seek_to") == 0) {
    /* mx_context is passed as absolute seconds * 100 to preserve some precision in int */
    float target_time = (float)mx_context / 100.0f;
    if (target_time < 0) target_time = 0;
    if (target_time > g_player_ctx->duration) target_time = g_player_ctx->duration;
    
    g_player_ctx->position = target_time;
    audio_seek(target_time);
  } else if (strncmp(cmd, "settings_tab_", 13) == 0) {
    int tab = atoi(cmd + 13);
    g_player_ctx->settings_active_tab = tab;
  } else if (strcmp(cmd, "settings_focus_input") == 0) {
    g_player_ctx->is_typing_library_path = true;
    SDL_StartTextInput();
  } else if (strcmp(cmd, "settings_add_folder") == 0) {
    if (strlen(g_player_ctx->library_input_buffer) > 0) {
      db_add_library_path(g_player_ctx->library_input_buffer);
      
      /* Refresh paths */
      if (g_player_ctx->library_paths) free(g_player_ctx->library_paths);
      db_get_library_paths(NULL, &g_player_ctx->library_path_count);
      g_player_ctx->library_paths = malloc(sizeof(LibraryPath) * g_player_ctx->library_path_count);
      db_get_library_paths(g_player_ctx->library_paths, &g_player_ctx->library_path_count);
      
      toast_show("Counting files...", TOAST_PROGRESS, -1);
      material_render(g_player_ctx, g_layout_ctx);
      library_scan(g_player_ctx->library_input_buffer, on_scan_progress);
      
      toast_show("Folder Added and Scanned", TOAST_INFO, 4000);
      
      /* Clear input */
      g_player_ctx->library_input_buffer[0] = '\0';
      g_player_ctx->is_typing_library_path = false;
      
      /* Reload tracks & albums */
      g_player_ctx->library_album_count = 0;
      db_get_all_albums(NULL, &g_player_ctx->library_album_count, g_player_ctx->setting_group_albums);
      if (g_player_ctx->library_album_count > 0) {
        if (g_player_ctx->library_albums) free(g_player_ctx->library_albums);
        g_player_ctx->library_albums = malloc(sizeof(Album) * g_player_ctx->library_album_count);
        db_get_all_albums(g_player_ctx->library_albums, &g_player_ctx->library_album_count, g_player_ctx->setting_group_albums);
      }
      g_player_ctx->library_track_count = 0;
      db_get_all_tracks(NULL, &g_player_ctx->library_track_count);
      if (g_player_ctx->library_track_count > 0) {
        if (g_player_ctx->library_tracks) free(g_player_ctx->library_tracks);
        g_player_ctx->library_tracks = malloc(sizeof(Track) * g_player_ctx->library_track_count);
        db_get_all_tracks(g_player_ctx->library_tracks, &g_player_ctx->library_track_count);
      }
      g_player_ctx->library_needs_filter = true;
    }
  } else if (strncmp(cmd, "settings_remove_folder_", 23) == 0) {
    int idx = atoi(cmd + 23);
    if (idx >= 0 && idx < (int)g_player_ctx->library_path_count) {
      db_remove_library_path(g_player_ctx->library_paths[idx].path);
      
      /* Refresh paths */
      if (g_player_ctx->library_paths) free(g_player_ctx->library_paths);
      db_get_library_paths(NULL, &g_player_ctx->library_path_count);
      if (g_player_ctx->library_path_count > 0) {
        g_player_ctx->library_paths = malloc(sizeof(LibraryPath) * g_player_ctx->library_path_count);
        db_get_library_paths(g_player_ctx->library_paths, &g_player_ctx->library_path_count);
      } else {
        g_player_ctx->library_paths = NULL;
      }
      toast_show("Folder Removed (Full Scan Recommended)", TOAST_INFO, 4000);
    }
  } else if (strcmp(cmd, "settings_scan_library") == 0) {
    if (g_player_ctx->library_path_count > 0) {
      toast_show("Counting files...", TOAST_PROGRESS, -1);
      material_render(g_player_ctx, g_layout_ctx);
      for (size_t i = 0; i < g_player_ctx->library_path_count; i++) {
        library_scan(g_player_ctx->library_paths[i].path, on_scan_progress);
      }

      /* Clean up stale entries if setting is enabled */
      if (g_player_ctx->setting_clean_db_on_scan) {
        int removed = 0;
        (void)removed;
        db_remove_stale_tracks();
        toast_show("Scan Complete. Stale tracks removed.", TOAST_INFO, 4000);
      } else {
        toast_show("Library Scan Complete", TOAST_INFO, 4000);
      }
      
      g_player_ctx->library_album_count = 0;
      db_get_all_albums(NULL, &g_player_ctx->library_album_count, g_player_ctx->setting_group_albums);
      if (g_player_ctx->library_album_count > 0) {
        if (g_player_ctx->library_albums) free(g_player_ctx->library_albums);
        g_player_ctx->library_albums = malloc(sizeof(Album) * g_player_ctx->library_album_count);
        db_get_all_albums(g_player_ctx->library_albums, &g_player_ctx->library_album_count, g_player_ctx->setting_group_albums);
      }
      g_player_ctx->library_track_count = 0;
      db_get_all_tracks(NULL, &g_player_ctx->library_track_count);
      if (g_player_ctx->library_track_count > 0) {
        if (g_player_ctx->library_tracks) free(g_player_ctx->library_tracks);
        g_player_ctx->library_tracks = malloc(sizeof(Track) * g_player_ctx->library_track_count);
        db_get_all_tracks(g_player_ctx->library_tracks, &g_player_ctx->library_track_count);
      }
      g_player_ctx->library_needs_filter = true;
    }
  } else if (strcmp(cmd, "settings_browse") == 0) {
#ifdef __linux__
    FILE *fp = popen("zenity --file-selection --directory", "r");
    if (fp) {
      char path[512];
      if (fgets(path, sizeof(path), fp) != NULL) {
        size_t len = strlen(path);
        if (len > 0 && path[len - 1] == '\n')
          path[len - 1] = '\0';
        strncpy(g_player_ctx->library_input_buffer, path, 511);
      }
      pclose(fp);
    }
#endif
  } else if (strcmp(cmd, "settings_toggle_group") == 0) {
    g_player_ctx->setting_group_albums = !g_player_ctx->setting_group_albums;
  } else if (strcmp(cmd, "settings_toggle_clean_db") == 0) {
    g_player_ctx->setting_clean_db_on_scan = !g_player_ctx->setting_clean_db_on_scan;
  } else if (strcmp(cmd, "settings_toggle_gapless") == 0) {
    g_player_ctx->setting_gapless = !g_player_ctx->setting_gapless;
  } else if (strcmp(cmd, "settings_toggle_normalization") == 0) {
    g_player_ctx->setting_normalization = !g_player_ctx->setting_normalization;
  } else if (strcmp(cmd, "settings_check_updates") == 0) {
    /* Set update status message showing next release info */
    strncpy(g_player_ctx->update_status_msg, 
            "Starting updater...", 
            sizeof(g_player_ctx->update_status_msg) - 1);
            
    SDL_CreateThread(updater_thread_func, "UpdaterThread", g_player_ctx);
    
  } else if (strcmp(cmd, "settings_restart_app") == 0) {
    system("./harmony_player &");
    exit(0);
  } else if (strcmp(cmd, "settings_reset_database") == 0) {
    /* Show confirmation dialog instead of resetting immediately */
    g_player_ctx->confirm_dialog_open = true;
    strncpy(g_player_ctx->confirm_dialog_message,
            "Reset the entire music database? This cannot be undone.",
            sizeof(g_player_ctx->confirm_dialog_message) - 1);
    strncpy(g_player_ctx->confirm_dialog_action,
            "settings_reset_database_confirmed",
            sizeof(g_player_ctx->confirm_dialog_action) - 1);
  } else if (strcmp(cmd, "settings_reset_database_confirmed") == 0) {
    library_reset();
    toast_show("Database Cleared. Rescan required.", TOAST_INFO, 4000);
    if (g_player_ctx->library_albums) {
      free(g_player_ctx->library_albums);
      g_player_ctx->library_albums = NULL;
    }
    if (g_player_ctx->library_tracks) {
      free(g_player_ctx->library_tracks);
      g_player_ctx->library_tracks = NULL;
    }
    g_player_ctx->library_album_count = 0;
    g_player_ctx->library_track_count = 0;
    g_player_ctx->library_needs_filter = true;
  } else if (strcmp(cmd, "confirm_yes") == 0) {
    g_player_ctx->confirm_dialog_open = false;
    if (strlen(g_player_ctx->confirm_dialog_action) > 0) {
      dispatch_command(ctx, g_player_ctx->confirm_dialog_action, 0);
      g_player_ctx->confirm_dialog_action[0] = '\0';
    }
  } else if (strcmp(cmd, "confirm_no") == 0) {
    g_player_ctx->confirm_dialog_open = false;
    g_player_ctx->confirm_dialog_action[0] = '\0';
  } else if (strncmp(cmd, "queue_reorder_", 14) == 0) {
    /* Format: queue_reorder_FROM_TO */
    int from_idx = -1, to_idx = -1;
    if (sscanf(cmd + 14, "%d_%d", &from_idx, &to_idx) == 2) {
      if (from_idx >= 0 && from_idx < (int)g_player_ctx->count && to_idx >= 0 &&
          to_idx < (int)g_player_ctx->count && from_idx != to_idx) {
        Song temp = g_player_ctx->songs[from_idx];
        if (from_idx < to_idx) {
          for (int i = from_idx; i < to_idx; i++)
            g_player_ctx->songs[i] = g_player_ctx->songs[i + 1];
        } else {
          for (int i = from_idx; i > to_idx; i--)
            g_player_ctx->songs[i] = g_player_ctx->songs[i - 1];
        }
        g_player_ctx->songs[to_idx] = temp;
        /* Update current_index if affected */
        if (g_player_ctx->current_index == from_idx) {
          g_player_ctx->current_index = to_idx;
        } else if (from_idx < to_idx) {
          if (g_player_ctx->current_index > from_idx &&
              g_player_ctx->current_index <= to_idx)
            g_player_ctx->current_index--;
        } else {
          if (g_player_ctx->current_index >= to_idx &&
              g_player_ctx->current_index < from_idx)
            g_player_ctx->current_index++;
        }
      }
    }
  } else if (strncmp(cmd, "delete_playlist_", 16) == 0) {
    int pid = atoi(cmd + 16);
    db_delete_playlist(pid);
    /* Refresh playlist cache */
    if (g_player_ctx->library_playlists) {
      free(g_player_ctx->library_playlists);
      g_player_ctx->library_playlists = NULL;
    }
    g_player_ctx->library_playlist_count = 0;
    toast_show("Playlist Deleted", TOAST_INFO, 3000);
  } else if (strncmp(cmd, "play_library_track_", 19) == 0) {
    size_t idx = (size_t)atoll(cmd + 19);
    if (idx < g_player_ctx->library_track_count) {
      Track *t = &g_player_ctx->library_tracks[idx];
      g_player_ctx->count = 0;
      char art_path[MAX_PATH_LENGTH] = {0};
      if (t->album_id > 0) {
        for (size_t ai = 0; ai < g_player_ctx->library_album_count; ai++) {
          if (g_player_ctx->library_albums[ai].id == t->album_id) {
            strncpy(art_path, g_player_ctx->library_albums[ai].art_filename,
                    MAX_PATH_LENGTH - 1);
            break;
          }
        }
      }
      if (art_path[0] == '\0') {
        char *temp = extract_album_art(t->filepath);
        if (temp) {
          strncpy(art_path, temp, MAX_PATH_LENGTH - 1);
          free(temp);
        }
      }
      player_add_song(g_player_ctx, t->title, t->artist, NULL, t->filepath,
                      art_path);
      start_playback_helper(g_player_ctx);
      material_set_background(art_path);
    }
  } else if (strncmp(cmd, "toggle_favorite_album_", 22) == 0) {
    size_t album_idx = (size_t)atoll(cmd + 22);
    if (album_idx < g_player_ctx->library_album_count) {
      Album *album = &g_player_ctx->library_albums[album_idx];
      bool is_fav;
      if (db_toggle_album_favorite(album->id, &is_fav) == RESULT_SUCCESS) {
        album->is_favorite = is_fav;
        g_player_ctx->library_needs_filter = true;
      }
    }
  } else if (strncmp(cmd, "library_filter_", 15) == 0) {
    int filter = atoi(cmd + 15);
    g_player_ctx->library_filter_mode = (LibraryFilterMode)filter;
    g_player_ctx->library_needs_filter = true;
  } else if (strcmp(cmd, "library_cycle_sort") == 0) {
    g_player_ctx->library_sort_mode = (g_player_ctx->library_sort_mode + 1) % 4;
    g_player_ctx->library_needs_filter = true;
  } else if (strcmp(cmd, "library_toggle_view") == 0) {
    g_player_ctx->library_view_mode =
        (g_player_ctx->library_view_mode == LIBRARY_VIEW_GRID)
            ? LIBRARY_VIEW_LIST
            : LIBRARY_VIEW_GRID;
    g_player_ctx->library_needs_filter = true;
  } else if (strncmp(cmd, "open_album_", 11) == 0) {
    int idx = atoi(cmd + 11);
    if (idx >= 0 && idx < (int)g_player_ctx->library_album_count) {
      Album *album = &g_player_ctx->library_albums[idx];
      if (g_player_ctx->browse_tracks) {
        free(g_player_ctx->browse_tracks);
        g_player_ctx->browse_tracks = NULL;
      }
      g_player_ctx->browse_track_count = 0;
      if (g_player_ctx->setting_group_albums) {
        db_get_tracks_by_album_name(album->name, NULL,
                                    &g_player_ctx->browse_track_count);
      } else {
        db_get_tracks_by_album(album->id, NULL,
                               &g_player_ctx->browse_track_count);
      }
      if (g_player_ctx->browse_track_count > 0) {
        g_player_ctx->browse_tracks =
            malloc(sizeof(Track) * g_player_ctx->browse_track_count);
        if (g_player_ctx->setting_group_albums) {
          db_get_tracks_by_album_name(album->name, g_player_ctx->browse_tracks,
                                      &g_player_ctx->browse_track_count);
        } else {
          db_get_tracks_by_album(album->id, g_player_ctx->browse_tracks,
                                 &g_player_ctx->browse_track_count);
        }
        strncpy(g_player_ctx->browse_context_name, album->name,
                MAX_SONG_TITLE - 1);
        strncpy(g_player_ctx->browse_context_art_path, album->art_filename,
                MAX_PATH_LENGTH - 1);
        g_player_ctx->sidebar_is_browsing = true;
        g_player_ctx->sidebar_right_open = true;
        g_player_ctx->sidebar_right_scroll_y = 0;
      }
    }
  } else if (strncmp(cmd, "play_album_", 11) == 0) {
    size_t idx = (size_t)atoll(cmd + 11);
    if (idx < g_player_ctx->library_album_count) {
      Album *album = &g_player_ctx->library_albums[idx];
      g_player_ctx->count = 0;
      size_t track_count = 0;
      Track *tracks = NULL;

      /* Identify tracks */
      if (g_player_ctx->setting_group_albums) {
        db_get_tracks_by_album_name(album->name, NULL, &track_count);
      } else {
        db_get_tracks_by_album(album->id, NULL, &track_count);
      }

      if (track_count > 0) {
        tracks = malloc(sizeof(Track) * track_count);
        if (g_player_ctx->setting_group_albums) {
          db_get_tracks_by_album_name(album->name, tracks, &track_count);
        } else {
          db_get_tracks_by_album(album->id, tracks, &track_count);
        }

        /* Populate Queue */
        for (size_t i = 0; i < track_count && i < MAX_PLAYLIST_SIZE; i++) {
          player_add_song(g_player_ctx, tracks[i].title, tracks[i].artist,
                          album->name, tracks[i].filepath, album->art_filename);
        }
        free(tracks);

        /* Play first song */
        g_player_ctx->queue_type = QUEUE_TYPE_ALBUM;
        strncpy(g_player_ctx->queue_context_name, album->name,
                MAX_SONG_TITLE - 1);
        strncpy(g_player_ctx->queue_context_art_path, album->art_filename,
                MAX_PATH_LENGTH - 1);
        g_player_ctx->current_index = 0;
        start_playback_helper(g_player_ctx);
      }
    }
  } else if (strcmp(cmd, "visualizer_settings_open") == 0) {
    g_player_ctx->visualizer_show_settings = true;
    g_player_ctx->visualizer_show_list = false;
  } else if (strcmp(cmd, "visualizer_list_open") == 0) {
    g_player_ctx->visualizer_show_list = true;
    g_player_ctx->visualizer_show_settings = false;
  } else if (strcmp(cmd, "visualizer_settings_back") == 0 ||
             strcmp(cmd, "visualizer_list_back") == 0) {
    g_player_ctx->visualizer_show_settings = false;
    g_player_ctx->visualizer_show_list = false;
  } else if (strncmp(cmd, "visualizer_param_drag_", 22) == 0) {
    int param_idx = atoi(cmd + 22);
    const VisPlugin *active = visualizer_get_active();
    if (active && active->get_param) {
      const VisParam *p = active->get_param(param_idx);
      if (p && (p->type == VIS_PARAM_FLOAT || p->type == VIS_PARAM_INT)) {
        int pad = 40;
        int x_offset = g_layout_ctx->width - 280; /* SIDEBAR_W = 280 */
        int slider_x = x_offset + pad;
        int slider_w = 200;
        int mx = mx_context;
        if (mx < slider_x) mx = slider_x;
        if (mx > slider_x + slider_w) mx = slider_x + slider_w;
        float pct = (float)(mx - slider_x) / (float)slider_w;
        
        if (p->type == VIS_PARAM_FLOAT && p->value_ptr) {
          float *val = (float *)p->value_ptr;
          *val = p->min + (p->max - p->min) * pct;
        } else if (p->type == VIS_PARAM_INT && p->value_ptr) {
          int *val = (int *)p->value_ptr;
          *val = (int)(p->min + (p->max - p->min) * pct);
        }
      }
    }
  } else if (strncmp(cmd, "visualizer_param_toggle_", 24) == 0) {
    int param_idx = atoi(cmd + 24);
    const VisPlugin *active = visualizer_get_active();
    if (active && active->get_param) {
      const VisParam *p = active->get_param(param_idx);
      if (p && p->type == VIS_PARAM_BOOL && p->value_ptr) {
        bool *val = (bool *)p->value_ptr;
        *val = !(*val);
      } else if (p && p->type == VIS_PARAM_ENUM && p->value_ptr && p->options) {
        int *val = (int *)p->value_ptr;
        int max_opt = 0;
        while (p->options[max_opt] != NULL)
          max_opt++;
        if (max_opt > 0) {
          *val = (*val + 1) % max_opt;
        }
      }
    }
  } else if (strncmp(cmd, "set_visualizer_", 15) == 0) {
    int vis_idx = atoi(cmd + 15);
    visualizer_set_active(vis_idx);
    g_player_ctx->visualizer_show_list = false;
  } else if (strcmp(cmd, "toggle_mini_player") == 0) {
    mini_player_toggle();
  } else if (strcmp(cmd, "library_search_focus") == 0) {
    g_player_ctx->is_typing_search = true;
    g_player_ctx->current_scene = SCENE_LIBRARY;
    SDL_StartTextInput();

  /* =========================================================================
     EQ COMMANDS
     ========================================================================= */
  } else if (strcmp(cmd, "open_eq") == 0) {
    log_message("DEBUG", "command_dispatch: Received open_eq");
    g_player_ctx->eq_popup_open = true;
    g_player_ctx->eq_dropdown_open = false;
    char addr_msg[512];
    unsigned char *base = (unsigned char*)&g_player_ctx->eq_presets;
    char hex[128];
    snprintf(hex, sizeof(hex), "hex: %02x %02x %02x %02x %02x %02x %02x %02x",
             base[0], base[1], base[2], base[3], base[4], base[5], base[6], base[7]);

    snprintf(addr_msg, sizeof(addr_msg), 
             "command_dispatch: ctx=%p, presets=%zu, val=%p, %s", 
             (void*)g_player_ctx, 
             (size_t)((char*)&g_player_ctx->eq_presets - (char*)g_player_ctx),
             (void*)g_player_ctx->eq_presets,
             hex);
    log_message("DEBUG", addr_msg);




    log_message("DEBUG", "command_dispatch: Clearing old EQ presets");

    if (g_player_ctx->eq_presets) {
      log_message("DEBUG", "command_dispatch: free()ing old presets");
      free(g_player_ctx->eq_presets);
      g_player_ctx->eq_presets = NULL;
    }

    g_player_ctx->eq_preset_count = 0;
    
    if (db_get_eq_presets(NULL, &g_player_ctx->eq_preset_count) == RESULT_SUCCESS) {
      char msg[128];
      snprintf(msg, sizeof(msg), "command_dispatch: Found %zu EQ presets in DB", g_player_ctx->eq_preset_count);
      log_message("DEBUG", msg);
      if (g_player_ctx->eq_preset_count > 0) {

        /* Use calloc to ensure the allocated memory is zeroed */
        g_player_ctx->eq_presets = calloc(g_player_ctx->eq_preset_count, sizeof(EqPreset));
        if (g_player_ctx->eq_presets) {
          log_message("DEBUG", "command_dispatch: Fetching preset details");
          if (db_get_eq_presets(g_player_ctx->eq_presets, &g_player_ctx->eq_preset_count) != RESULT_SUCCESS) {

             /* If second call fails, clean up */
             free(g_player_ctx->eq_presets);
             g_player_ctx->eq_presets = NULL;
             g_player_ctx->eq_preset_count = 0;
          }
        } else {
          /* Handle allocation failure gracefully */
          g_player_ctx->eq_preset_count = 0;
          log_message("ERROR", "Failed to allocate memory for EQ presets cache.");
        }
      }
    } else {
      log_message("ERROR", "command_dispatch: db_get_eq_presets(NULL) failed");
      g_player_ctx->eq_preset_count = 0;
    }
    log_message("DEBUG", "command_dispatch: open_eq complete");



  } else if (strcmp(cmd, "close_eq") == 0) {
    g_player_ctx->eq_popup_open = false;
    g_player_ctx->eq_dropdown_open = false;
    g_player_ctx->eq_is_new_mode = false;
    if (g_player_ctx->eq_typing_preset_name) {
      SDL_StopTextInput();
      g_player_ctx->eq_typing_preset_name = false;
    }

  } else if (strcmp(cmd, "eq_toggle_enabled") == 0) {
    g_player_ctx->eq_enabled = !g_player_ctx->eq_enabled;
    audio_set_eq_enabled(g_player_ctx->eq_enabled);

  } else if (strcmp(cmd, "eq_toggle_bands") == 0) {
    int new_count = (g_player_ctx->eq_band_count == 5) ? 10 : 5;
    g_player_ctx->eq_band_count = new_count;
    /* Re-apply all current gains. 
       Note: audio_set_eq_band is now safe and will fallback to 10-band freqs for indices >= 5. */
    for (int i = 0; i < EQ_BAND_COUNT_MAX; i++) {
      float gain = (i < new_count) ? g_player_ctx->eq_gains[i] : 0.0f;
      audio_set_eq_band(i, gain, new_count);
    }

  } else if (strcmp(cmd, "eq_dropdown_open") == 0) {
    g_player_ctx->eq_dropdown_open = true;

  } else if (strcmp(cmd, "eq_dropdown_close") == 0) {
    g_player_ctx->eq_dropdown_open = false;

  } else if (strcmp(cmd, "eq_preset_new") == 0) {
    g_player_ctx->eq_dropdown_open = false;
    g_player_ctx->eq_is_new_mode = true;
    g_player_ctx->eq_selected_preset_id = -1;
    strncpy(g_player_ctx->eq_selected_preset_name, "New Preset",
            sizeof(g_player_ctx->eq_selected_preset_name) - 1);
    /* Start text input for preset name */
    g_player_ctx->eq_new_preset_name[0] = '\0';
    g_player_ctx->eq_typing_preset_name = true;
    SDL_StartTextInput();

  } else if (strncmp(cmd, "eq_preset_select_builtin_", 25) == 0) {
    /* Built-in presets: gains packed as index into a static table */
    static const float builtin_gains[7][5] = {
      { 0.0f,  0.0f,  0.0f,  0.0f,  0.0f }, /* Flat       */
      { 6.0f,  4.0f,  0.0f,  0.0f,  0.0f }, /* Bass Boost */
      { 0.0f,  0.0f,  0.0f,  4.0f,  6.0f }, /* Treble     */
      { 4.0f,  2.0f, -1.0f,  2.0f,  4.0f }, /* Rock       */
      {-1.0f,  2.0f,  4.0f,  2.0f, -1.0f }, /* Pop        */
      { 0.0f,  0.0f,  0.0f, -2.0f, -4.0f }, /* Classical  */
      {-2.0f,  0.0f,  4.0f,  3.0f, -1.0f }, /* Vocal      */
    };
    static const char *builtin_names[] = {
      "Flat","Bass Boost","Treble","Rock","Pop","Classical","Vocal"
    };
    int idx = atoi(cmd + 25);
    if (idx >= 0 && idx < 7) {
      for (int i = 0; i < EQ_BAND_COUNT_MAX; i++) {
        float g = (i < 5) ? builtin_gains[idx][i] : 0.0f;
        g_player_ctx->eq_gains[i] = g;
        audio_set_eq_band(i, g, g_player_ctx->eq_band_count);
      }
      g_player_ctx->eq_selected_preset_id = -1;
      safe_strncpy(g_player_ctx->eq_selected_preset_name, builtin_names[idx],
                   sizeof(g_player_ctx->eq_selected_preset_name));

      g_player_ctx->eq_is_new_mode = false;
      g_player_ctx->eq_dropdown_open = false;
      if (!g_player_ctx->eq_enabled) {
        g_player_ctx->eq_enabled = true;
        audio_set_eq_enabled(true);
      }
    }

  } else if (strncmp(cmd, "eq_preset_select_", 17) == 0) {
    /* User preset: index into eq_presets cache */
    int idx = atoi(cmd + 17);
    if (idx >= 0 && idx < (int)g_player_ctx->eq_preset_count) {
      EqPreset *p = &g_player_ctx->eq_presets[idx];
      for (int i = 0; i < EQ_BAND_COUNT_MAX; i++) {
        g_player_ctx->eq_gains[i] = p->gains[i];
        audio_set_eq_band(i, p->gains[i], g_player_ctx->eq_band_count);
      }
      g_player_ctx->eq_selected_preset_id = p->id;
      safe_strncpy(g_player_ctx->eq_selected_preset_name, p->name,
                   sizeof(g_player_ctx->eq_selected_preset_name));

      g_player_ctx->eq_is_new_mode = false;
      g_player_ctx->eq_dropdown_open = false;
      if (!g_player_ctx->eq_enabled) {
        g_player_ctx->eq_enabled = true;
        audio_set_eq_enabled(true);
      }
    }

  } else if (strcmp(cmd, "eq_focus_name") == 0) {
    g_player_ctx->eq_typing_preset_name = true;
    SDL_StartTextInput();

  } else if (strcmp(cmd, "eq_preset_save") == 0) {
    if (g_player_ctx->eq_new_preset_name[0] != '\0') {
      db_save_eq_preset(g_player_ctx->eq_new_preset_name,
                        g_player_ctx->eq_gains,
                        g_player_ctx->eq_band_count);
      safe_strncpy(g_player_ctx->eq_selected_preset_name,
                   g_player_ctx->eq_new_preset_name,
                   sizeof(g_player_ctx->eq_selected_preset_name));

      g_player_ctx->eq_is_new_mode = false;
      g_player_ctx->eq_typing_preset_name = false;
      SDL_StopTextInput();
      /* Reload preset cache */
      if (g_player_ctx->eq_presets) { free(g_player_ctx->eq_presets); g_player_ctx->eq_presets = NULL; }
      g_player_ctx->eq_preset_count = 0;
      db_get_eq_presets(NULL, &g_player_ctx->eq_preset_count);
      if (g_player_ctx->eq_preset_count > 0) {
        g_player_ctx->eq_presets = calloc(g_player_ctx->eq_preset_count, sizeof(EqPreset));
        if (g_player_ctx->eq_presets) {
          db_get_eq_presets(g_player_ctx->eq_presets, &g_player_ctx->eq_preset_count);
          /* Find and select the newly saved preset */
          for (size_t i = 0; i < g_player_ctx->eq_preset_count; i++) {
            if (strcmp(g_player_ctx->eq_presets[i].name, g_player_ctx->eq_new_preset_name) == 0) {
              g_player_ctx->eq_selected_preset_id = g_player_ctx->eq_presets[i].id;
              break;
            }
          }
        } else {
          g_player_ctx->eq_preset_count = 0;
        }
      }

      toast_show("EQ Preset Saved", TOAST_INFO, 3000);
    }

  } else if (strcmp(cmd, "eq_preset_delete") == 0) {
    if (g_player_ctx->eq_selected_preset_id >= 0) {
      db_delete_eq_preset(g_player_ctx->eq_selected_preset_id);
      g_player_ctx->eq_selected_preset_id = -1;
      safe_strncpy(g_player_ctx->eq_selected_preset_name, "Flat",
                   sizeof(g_player_ctx->eq_selected_preset_name));

      /* Reload preset cache */
      if (g_player_ctx->eq_presets) { free(g_player_ctx->eq_presets); g_player_ctx->eq_presets = NULL; }
      g_player_ctx->eq_preset_count = 0;
      db_get_eq_presets(NULL, &g_player_ctx->eq_preset_count);
      if (g_player_ctx->eq_preset_count > 0) {
        g_player_ctx->eq_presets = calloc(g_player_ctx->eq_preset_count, sizeof(EqPreset));
        if (g_player_ctx->eq_presets) {
          db_get_eq_presets(g_player_ctx->eq_presets, &g_player_ctx->eq_preset_count);
        } else {
          g_player_ctx->eq_preset_count = 0;
        }
      }

      toast_show("EQ Preset Deleted", TOAST_INFO, 3000);
    }

  } else if (strncmp(cmd, "eq_band_drag_", 13) == 0) {
    /* Format: eq_band_drag_N  — mx_context carries the raw Y pixel from main.c */
    int band = atoi(cmd + 13);
    if (band >= 0 && band < g_player_ctx->eq_band_count) {
      /* The Y coordinate is passed as mx_context.  Slider geometry is
         resolved in material_renderer.c at render time, but here we need to
         reverse-map it to a gain.  We store the slider top/height in a static
         shared with the renderer via eq_slider_top / eq_slider_h. */
      extern int g_eq_slider_top;
      extern int g_eq_slider_h;
      int my = mx_context;
      float t = 1.0f - (float)(my - g_eq_slider_top) / (float)(g_eq_slider_h > 0 ? g_eq_slider_h : 1);
      if (t < 0.0f) t = 0.0f;
      if (t > 1.0f) t = 1.0f;
      float gain = t * 24.0f - 12.0f; /* maps 0..1 → -12..+12 dB */
      g_player_ctx->eq_gains[band] = gain;
      audio_set_eq_band(band, gain, g_player_ctx->eq_band_count);
      if (!g_player_ctx->eq_enabled) {
        g_player_ctx->eq_enabled = true;
        audio_set_eq_enabled(true);
      }
    }

  } else if (strncmp(cmd, "eq_band_reset_", 14) == 0) {
    int band = atoi(cmd + 14);
    if (band >= 0 && band < EQ_BAND_COUNT_MAX) {
      g_player_ctx->eq_gains[band] = 0.0f;
      audio_set_eq_band(band, 0.0f, g_player_ctx->eq_band_count);
    }

  } else {
    return false;
  }
  return true;
}
