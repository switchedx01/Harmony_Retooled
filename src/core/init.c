#include "init.h"
#include "audio_backend.h"
#include "common.h"
#include "database.h"
#include "layout.h"
#include "library_manager.h"
#include "logging.h"
#include "material_renderer.h"
#include "metadata_parser.h"
#include "mpris_service.h"
#include "player.h"
#include "toast_overlay.h"
#include "visualizer_loader.h"
#include "window.h"
#include "hub_client.h"
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void on_mpris_play(void) {
  PlayerContext *ctx = app_get_player();
  if (ctx->state == PLAYER_STATE_PAUSED) {
    player_pause(ctx);
    audio_resume();
    mpris_update_playback_status(true);
  }
}

static void on_mpris_pause(void) {
  PlayerContext *ctx = app_get_player();
  if (ctx->state == PLAYER_STATE_PLAYING) {
    player_pause(ctx);
    audio_pause();
    mpris_update_playback_status(false);
  }
}

static void on_mpris_next(void) { player_next(app_get_player()); }

static void on_mpris_prev(void) { player_prev(app_get_player()); }

static void on_mpris_raise(void) {
  // SDL_RaiseWindow(window_get_sdl_window()); // Need to expose window getter
}

static void on_mpris_quit(void) {
  AppContext *app = app_get_context();
  app->running = false;
}

static void on_mpris_open_uri(const char *uri) {
  log_message("DEBUG", "on_mpris_open_uri called");
  if (!uri || strncmp(uri, "file://", 7) != 0) {
    log_message("WARN",
                "MPRIS OpenUri received unsupported URI scheme or null");
    return;
  }
  char log_buf[512];
  snprintf(log_buf, sizeof(log_buf), "uri: %s", uri);
  log_message("DEBUG", log_buf);

  /* Decode file:// uri properly using GLib */
  gchar *filepath = g_filename_from_uri(uri, NULL, NULL);
  if (!filepath) {
    log_message("ERROR", "Failed to decode URI to local path");
    return;
  }

  snprintf(log_buf, sizeof(log_buf), "filepath decoded: %s", filepath);
  log_message("DEBUG", log_buf);

  Track parsed_track = {0};
  char artist_buf[MAX_SONG_TITLE] = {0};
  char album_buf[MAX_SONG_TITLE] = {0};

  AppContext *app = app_get_context();
  if (!app) {
    log_message("ERROR", "app_context was null in mpris_open_uri");
    return;
  }
  log_message("DEBUG", "calling get_metadata...");
  if (get_metadata(filepath, &parsed_track, artist_buf, album_buf)) {
    log_message("DEBUG", "adding song to player...");
    player_add_song(app->player, parsed_track.title, artist_buf, album_buf,
                    filepath, parsed_track.art_filename);
    log_message("DEBUG", "playing song...");
    player_play(app->player);
    app->player->current_scene = SCENE_NOW_PLAYING;
    log_message("INFO",
                "MPRIS OpenUri successfully loaded and started playback.");
  } else {
    log_message("ERROR", "MPRIS OpenUri failed to parse file metadata.");
  }

  g_free(filepath);
}

static void load_library_initial(PlayerContext *ctx) {
  size_t path_count = 0;
  db_get_library_paths(NULL, &path_count);
  if (path_count == 0) {
    log_message("INFO",
                "No library paths found. Auto-adding current directory.");
    db_add_library_path(".");
    db_get_library_paths(NULL, &path_count);
  }

  ctx->library_path_count = path_count;
  if (path_count > 0) {
    ctx->library_paths = malloc(sizeof(LibraryPath) * path_count);
    db_get_library_paths(ctx->library_paths, &path_count);
  }

  /* Load Albums */
  ctx->library_album_count = 0;
  db_get_all_albums(NULL, &ctx->library_album_count, ctx->setting_group_albums);
  if (ctx->library_album_count > 0) {
    ctx->library_albums = malloc(sizeof(Album) * ctx->library_album_count);
    db_get_all_albums(ctx->library_albums, &ctx->library_album_count,
                      ctx->setting_group_albums);
  }

  /* Load Tracks */
  ctx->library_track_count = 0;
  db_get_all_tracks(NULL, &ctx->library_track_count);
  if (ctx->library_track_count > 0) {
    ctx->library_tracks = malloc(sizeof(Track) * ctx->library_track_count);
    db_get_all_tracks(ctx->library_tracks, &ctx->library_track_count);
  }

  ctx->library_needs_filter = true;
  char msg[64];
  snprintf(msg, sizeof(msg), "Library loaded: %zu albums",
           ctx->library_album_count);
  log_message("INFO", msg);
}

Result app_init(AppContext *ctx) {
  if (logging_init("harmony.log") != RESULT_SUCCESS)
    return RESULT_ERROR_GENERIC;
    
  if (ctx->headless) {
      log_message("INFO", "Initializing audio-only backend (Headless).");
      // Still need SDL_Init for audio if not already done by window_init
      SDL_Init(SDL_INIT_AUDIO); 
  } else {
      if (window_init("Harmony Retooled (Material)", 1000, 700) != RESULT_SUCCESS)
        return RESULT_ERROR_GENERIC;
  }

  init_hub_connection();

  layout_init(ctx->layout, 1000, 700);

  if (audio_init() != RESULT_SUCCESS)
    return RESULT_ERROR_GENERIC;

  player_init(ctx->player);

  if (library_init() != RESULT_SUCCESS) {
    log_message("ERROR", "Failed to initialize library.");
  } else {
    load_library_initial(ctx->player);
  }

  /* Load persisted settings (must be after library_init which opens DB) */
  {
    char buf[64];
    if (db_get_setting("volume", buf, sizeof(buf)) == RESULT_SUCCESS) {
      ctx->player->volume = strtof(buf, NULL);
      audio_set_volume(ctx->player->volume);
    }
    if (db_get_setting("shuffle", buf, sizeof(buf)) == RESULT_SUCCESS) {
      ctx->player->shuffle_mode = (atoi(buf) != 0);
    }
    if (db_get_setting("repeat", buf, sizeof(buf)) == RESULT_SUCCESS) {
      ctx->player->repeat_mode = (RepeatMode)atoi(buf);
    }
    
    if (db_get_setting("eq_enabled", buf, sizeof(buf)) == RESULT_SUCCESS) {
      ctx->player->eq_enabled = (atoi(buf) != 0);
      audio_set_eq_enabled(ctx->player->eq_enabled);
    }
    if (db_get_setting("eq_band_count", buf, sizeof(buf)) == RESULT_SUCCESS) {
      ctx->player->eq_band_count = atoi(buf);
    }
    if (db_get_setting("eq_selected_preset_name", buf, sizeof(buf)) == RESULT_SUCCESS) {
      strncpy(ctx->player->eq_selected_preset_name, buf, sizeof(ctx->player->eq_selected_preset_name) - 1);
      ctx->player->eq_selected_preset_name[sizeof(ctx->player->eq_selected_preset_name) - 1] = '\0';
    }
    for (int i = 0; i < EQ_BAND_COUNT_MAX; i++) {
      char key[32];
      snprintf(key, sizeof(key), "eq_gain_%d", i);
      if (db_get_setting(key, buf, sizeof(buf)) == RESULT_SUCCESS) {
        ctx->player->eq_gains[i] = strtof(buf, NULL);
        audio_set_eq_band(i, ctx->player->eq_gains[i], ctx->player->eq_band_count);
      }
    }
  }

  if (!ctx->headless) {
    material_init((SDL_Renderer *)window_get_renderer());
    toast_init();
    visualizer_system_init();
  }

  MprisCallbacks mpris_cb = {.on_play = on_mpris_play,
                             .on_pause = on_mpris_pause,
                             // .on_next = on_mpris_next,
                             // .on_prev = on_mpris_prev,
                             // .on_raise = on_mpris_raise,
                             .on_quit = on_mpris_quit};
  /* Since I defined on_mpris_next above, I should use them within the struct
   * initialiser for clarity, or assign */
  mpris_cb.on_next = on_mpris_next;
  mpris_cb.on_prev = on_mpris_prev;
  mpris_cb.on_raise = on_mpris_raise;
  mpris_cb.on_open_uri = on_mpris_open_uri;

  mpris_init(&mpris_cb);

  return RESULT_SUCCESS;
}

void app_shutdown(AppContext *ctx) {
  /* Save persisted settings */
  {
    char buf[64];
    snprintf(buf, sizeof(buf), "%.2f", ctx->player->volume);
    db_save_setting("volume", buf);
    snprintf(buf, sizeof(buf), "%d", ctx->player->shuffle_mode ? 1 : 0);
    db_save_setting("shuffle", buf);
    snprintf(buf, sizeof(buf), "%d", (int)ctx->player->repeat_mode);
    db_save_setting("repeat", buf);
    
    snprintf(buf, sizeof(buf), "%d", ctx->player->eq_enabled ? 1 : 0);
    db_save_setting("eq_enabled", buf);
    snprintf(buf, sizeof(buf), "%d", ctx->player->eq_band_count);
    db_save_setting("eq_band_count", buf);
    db_save_setting("eq_selected_preset_name", ctx->player->eq_selected_preset_name);
    for (int i = 0; i < EQ_BAND_COUNT_MAX; i++) {
      char key[32];
      snprintf(key, sizeof(key), "eq_gain_%d", i);
      snprintf(buf, sizeof(buf), "%.2f", ctx->player->eq_gains[i]);
      db_save_setting(key, buf);
    }
  }

  close_hub_connection();
  if (!ctx->headless) {
    material_shutdown();
    window_shutdown();
  }
  logging_shutdown();
  SDL_Quit();
}
void app_enable_gui(AppContext *ctx) {
  if (!ctx->headless) return;
  
  log_message("INFO", "Enabling GUI from headless state.");
  if (window_init("Harmony Retooled (Material)", 1000, 700) == RESULT_SUCCESS) {
    ctx->headless = false;
    material_init((SDL_Renderer *)window_get_renderer());
    toast_init();
    visualizer_system_init();
    
    // Resume visuals for current song
    if (ctx->player->current_index >= 0) {
      Song *s = &ctx->player->songs[ctx->player->current_index];
      if (s->art_path[0] != '\0') {
        material_set_background(s->art_path);
      }
    }
  }
}
