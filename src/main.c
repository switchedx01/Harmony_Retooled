#include "app_context.h"
#include "audio_backend.h"
#include "command_dispatch.h"
#include "init.h"
#include "input_handler.h"
#include "layout.h"
#include "logging.h"
#include "material_renderer.h"
#include "metadata_parser.h"
#include "mini_player.h"
#include "mpris_service.h"
#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
  /* Force X11 backend to allow programmatic window positioning (fixes Wayland
   * clamp bugs) */
  setenv("SDL_VIDEODRIVER", "x11", 0);

  AppContext *app = app_get_context();

  /* Process CLI Arguments first so app_init knows about headless mode */
  if (argc > 1) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--headless") == 0) {
            app->headless = true;
            log_message("INFO", "Running in headless (lull) mode.");
            continue;
        }
        
        // If it's not a flag, assume it's a file path to play
        const char *target_file = argv[i];
        Track parsed_track = {0};
        char artist_buf[MAX_SONG_TITLE] = {0};
        char album_buf[MAX_SONG_TITLE] = {0};

        // Forward declare the header manually to avoid missing include issues if
        // not present globally
        extern bool get_metadata(const char *path, Track *out_track,
                                 char *out_artist, char *out_album);

        if (get_metadata(target_file, &parsed_track, artist_buf, album_buf)) {
          /* Add to player queue and play */
          player_add_song(app->player, parsed_track.title, artist_buf, album_buf,
                          target_file, parsed_track.art_filename);
          player_play(app->player);

          /* Optional: Update UI to show now playing if we want that to be the
           * default view */
          app->player->current_scene = SCENE_NOW_PLAYING;

          log_message("INFO", "Loaded CLI file successfully.");
        } else {
          log_message("ERROR", "Failed to parse metadata for CLI file.");
        }
    }
  }

  /* Initialize everything via consolidated init module */
  if (app_init(app) != RESULT_SUCCESS) {
    return -1;
  }

  bool layout_needs_update = true;
  WindowEvent evt;

  /* Initialize mini player overlay */
  mini_player_init();

  while (app->running) {
    extern void poll_hub_commands(void);
    poll_hub_commands();

    /* Update Audio State */
    if (!app->player->is_typing_search && !app->is_dragging_seek) {
      app->player->position = audio_get_position();
    }
    app->player->duration = audio_get_duration();

    /* Auto-advance: check if current track has finished */
    if (audio_is_finished() && app->player->state == PLAYER_STATE_PLAYING) {
      dispatch_command(app, "next", 0);
    }

    /* Periodic Hub Updates (every 500ms for smoother progress) */
    static uint32_t last_hub_update = 0;
    uint32_t current_ticks = SDL_GetTicks();
    if (app->player->state == PLAYER_STATE_PLAYING && (current_ticks - last_hub_update > 500)) {
        player_send_status_to_hub(app->player, "time_update");
        last_hub_update = current_ticks;
    }

    /* Safety: release stuck drag if mouse button is no longer held */
    if (app->is_dragging_seek && !window_is_mouse_held()) {
      int mx_rel, my_rel;
      window_get_mouse_pos(&mx_rel, &my_rel);
      dispatch_command(app, "seek_bar_force", mx_rel);
      app->is_dragging_seek = false;
    }
    if (app->is_dragging_vol && !window_is_mouse_held()) {
      app->is_dragging_vol = false;
    }
    if (app->is_dragging_eq_band && !window_is_mouse_held()) {
      app->is_dragging_eq_band = false;
    }
    if (app->is_dragging_vis_param && !window_is_mouse_held()) {
      app->is_dragging_vis_param = false;
    }

    if (layout_needs_update) {
      check_layout_state(app->layout);
      recalculate_layout(app->layout);
      layout_needs_update = false;
    }

    /* Pump MPRIS DBus events */
    mpris_process();

    /* Process Events */
    while (true) {
      evt = window_poll_event();
      if (evt.type == EVENT_NONE)
        break;

      if (evt.type == EVENT_QUIT) {
        bool launched_from_engine = false;
        const char *env_engine = getenv("HARMONY_ENGINE");
        if (env_engine && strcmp(env_engine, "1") == 0) {
            launched_from_engine = true;
        }

        if (launched_from_engine) {
            if (!app->headless) {
                log_message("INFO", "Window closed. Reverting to headless mode.");
                app->headless = true;
                material_shutdown();
                window_shutdown();
            } else {
                app->running = false;
            }
        } else {
            log_message("INFO", "Window closed. Shutting down standalone player.");
            app->running = false;
        }
      } else if (evt.type == EVENT_RESIZED) {
        app->layout->width = evt.resize.w;
        app->layout->height = evt.resize.h;
        layout_needs_update = true;
      }

      /* Let mini player handle events first (drag, expand, buttons) */
      if (mini_player_is_active() &&
          mini_player_handle_event(&evt.sdl_event, app->player)) {
        continue; /* Event consumed by mini player */
      }

      if (evt.type == EVENT_MOUSEDOWN) {
        int mx, my;
        window_get_mouse_pos(&mx, &my);
        bool is_right_click = (evt.button == SDL_BUTTON_RIGHT);

        /* Reset all drag states on new click to prevent stuck drags */
        app->is_dragging_seek = false;
        app->is_dragging_vol = false;
        app->is_dragging_eq_band = false;
        app->is_dragging_vis_param = false;

        /* Hit test returns a command string */
        const char *cmd =
            material_hit_test(app->player, app->layout, mx, my, is_right_click);
        if (cmd) {
          if (strcmp(cmd, "seek_bar") == 0) {
            app->is_dragging_seek = true;
          } else if (strcmp(cmd, "volume_slider") == 0) {
            app->is_dragging_vol = true;
          } else if (strncmp(cmd, "eq_band_drag_", 13) == 0) {
            app->is_dragging_eq_band = true;
            app->dragging_eq_band_idx = atoi(cmd + 13);
          } else if (strncmp(cmd, "visualizer_param_drag_", 22) == 0) {
            app->is_dragging_vis_param = true;
            app->dragging_vis_param_idx = atoi(cmd + 22);
          }

          if (strcmp(cmd, "library_search_focus") != 0 &&
              strcmp(cmd, "settings_focus_input") != 0) {
            app->player->is_typing_search = false;
            SDL_StopTextInput();
          }

          /* Dispatch command via new module */
          dispatch_command(app, cmd, mx);
        } else {
          app->player->is_typing_search = false;
          SDL_StopTextInput();
        }
      } else if (evt.type == EVENT_MOUSEUP) {
        if (app->is_dragging_seek) {
          int mx_up, my_up;
          window_get_mouse_pos(&mx_up, &my_up);
          dispatch_command(app, "seek_bar_force", mx_up);
        }
        app->is_dragging_seek = false;
        app->is_dragging_vol = false;
        app->is_dragging_eq_band = false;
        app->is_dragging_vis_param = false;
      } else if (evt.type == EVENT_MOUSEMOTION) {
        int mx, my;
        window_get_mouse_pos(&mx, &my);
        if (app->is_dragging_seek) {
          dispatch_command(app, "seek_bar", mx);
        } else if (app->is_dragging_vol) {
          dispatch_command(app, "volume_slider", mx);
        } else if (app->is_dragging_eq_band) {
          static char cmd_buf[64];
          snprintf(cmd_buf, sizeof(cmd_buf), "eq_band_drag_%d", app->dragging_eq_band_idx);
          dispatch_command(app, cmd_buf, my); /* Pass raw Y in mx_context */
        } else if (app->is_dragging_vis_param) {
          static char cmd_buf[64];
          snprintf(cmd_buf, sizeof(cmd_buf), "visualizer_param_drag_%d", app->dragging_vis_param_idx);
          dispatch_command(app, cmd_buf, mx); /* Pass raw X in mx_context */
        }
      } else if (evt.type == EVENT_MOUSEWHEEL) {
        int mx_w, my_w;
        window_get_mouse_pos(&mx_w, &my_w);

        /* Priority: mouse-wheel volume when hovering volume slider area */
        SDL_Rect vol_r = app->layout->vol_slider_rect;
        bool over_vol =
            (mx_w >= vol_r.x - 20 && mx_w <= vol_r.x + vol_r.w + 20 &&
             my_w >= vol_r.y - 15 && my_w <= vol_r.y + vol_r.h + 15);

        if (over_vol) {
          app->player->volume += evt.wheel.y * 0.05f;
          if (app->player->volume > 1.0f)
            app->player->volume = 1.0f;
          if (app->player->volume < 0.0f)
            app->player->volume = 0.0f;
          audio_set_volume(app->player->volume);
        } else {
          float *scroll_y = NULL;
          float *max_scroll = NULL;
          float speed = 40.0f; /* px per scroll tick */

          if (app->player->spotlight_active) {
            scroll_y = &app->player->spotlight_scroll_y;
            max_scroll = &app->player->spotlight_max_scroll;
          } else if (app->player->sidebar_right_open && mx_w >= app->layout->sidebar_right_rect.x) {
            scroll_y = &app->player->sidebar_right_scroll_y;
            max_scroll = &app->player->sidebar_right_max_scroll;
          } else if (app->player->current_scene == SCENE_LIBRARY ||
                     app->player->current_scene == SCENE_PLAYLISTS) {
            scroll_y = &app->player->library_scroll_y;
            max_scroll = &app->player->library_max_scroll;
          } else if (app->player->current_scene == SCENE_VISUALIZER) {
            scroll_y = &app->player->visualizer_scroll_y;
            max_scroll = &app->player->visualizer_max_scroll;
          }

          if (scroll_y && max_scroll) {
            *scroll_y -= evt.wheel.y * speed;
            if (*scroll_y < 0.0f)
              *scroll_y = 0.0f;
            if (*scroll_y > *max_scroll)
              *scroll_y = *max_scroll;
          }
        }
      } else if (evt.type == EVENT_KEYDOWN || evt.type == EVENT_TEXTINPUT) {
        /* Handle keyboard/text input here or move to another helper */
        input_handle_event(app, &evt);
      } else if (evt.type == EVENT_DROPFILE) {
        /* Drag-and-drop: parse file and start playback */
        Track parsed_track = {0};
        char artist_buf[MAX_SONG_TITLE] = {0};
        char album_buf[MAX_SONG_TITLE] = {0};

        extern bool get_metadata(const char *path, Track *out_track,
                                 char *out_artist, char *out_album);

        if (get_metadata(evt.dropped_file, &parsed_track, artist_buf,
                         album_buf)) {
          app->player->count = 0;
          player_add_song(app->player, parsed_track.title, artist_buf,
                          album_buf, evt.dropped_file,
                          parsed_track.art_filename);
          app->player->current_index = 0;
          dispatch_command(app, "play_song_0", 0);
        }
      }
    }

    /* Render Frame */
    if (app->headless) {
        SDL_Delay(100); // Low CPU lull state when no GUI is needed
        continue;
    }

    if (!mini_player_is_active()) {
      material_render(app->player, app->layout);
    } else {
      /* Main window is hidden — only render mini player */
      mini_player_render(app->player);
      SDL_Delay(16); /* ~60fps cap when main window is hidden */
    }
  }

  app_shutdown(app);
  return 0;
}
