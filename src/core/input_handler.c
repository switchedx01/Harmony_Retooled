#include "input_handler.h"
#include "audio_backend.h"
#include "command_dispatch.h"
#include "database.h"
#include "material_renderer.h" /* For material_handle_input (F3 debug) */
#include "toast_overlay.h"
#include <SDL2/SDL.h>
#include <string.h>

/* Helper: check if user is currently typing in any text field */
static bool is_typing(PlayerContext *p) {
  return p->is_typing_library_path || p->is_creating_playlist ||
         p->is_typing_search || p->eq_typing_preset_name;
}

void input_handle_event(AppContext *ctx, WindowEvent *evt) {
  PlayerContext *p = ctx->player;

  if (evt->type == EVENT_KEYDOWN) {
    /* Pass to material renderer for global shortcuts like F3 */
    material_handle_input(&evt->sdl_event);

    /* --- Global shortcuts (only when NOT typing in a text field) --- */
    if (!is_typing(p)) {
      /* Playback Controls */
      if (evt->key == SDLK_SPACE) {
        dispatch_command(ctx, "play", 0);
        return;
      }
      if (evt->key == SDLK_RIGHT && (evt->mod & KMOD_SHIFT)) {
        /* Shift+Right: seek forward 30s */
        float target = p->position + 30.0f;
        if (target > p->duration)
          target = p->duration;
        p->position = target;
        audio_seek(target);
        return;
      }
      if (evt->key == SDLK_LEFT && (evt->mod & KMOD_SHIFT)) {
        /* Shift+Left: seek backward 30s */
        float target = p->position - 30.0f;
        if (target < 0.0f)
          target = 0.0f;
        p->position = target;
        audio_seek(target);
        return;
      }
      if (evt->key == SDLK_RIGHT) {
        dispatch_command(ctx, "next", 0);
        return;
      }
      if (evt->key == SDLK_LEFT) {
        dispatch_command(ctx, "prev", 0);
        return;
      }
      if (evt->key == SDLK_UP) {
        p->volume += 0.05f;
        if (p->volume > 1.0f)
          p->volume = 1.0f;
        audio_set_volume(p->volume);
        return;
      }
      if (evt->key == SDLK_DOWN) {
        p->volume -= 0.05f;
        if (p->volume < 0.0f)
          p->volume = 0.0f;
        audio_set_volume(p->volume);
        return;
      }

      /* Mute Toggle */
      if (evt->key == SDLK_m) {
        static float pre_mute_volume = 1.0f;
        if (p->volume > 0.0f) {
          pre_mute_volume = p->volume;
          p->volume = 0.0f;
        } else {
          p->volume = pre_mute_volume;
        }
        audio_set_volume(p->volume);
        return;
      }

      /* Shuffle / Repeat */
      if (evt->key == SDLK_s) {
        dispatch_command(ctx, "toggle_shuffle", 0);
        return;
      }
      if (evt->key == SDLK_r) {
        dispatch_command(ctx, "cycle_repeat", 0);
        return;
      }

      /* Global Search: Ctrl+F or / */
      if ((evt->key == SDLK_f && (evt->mod & KMOD_CTRL)) ||
          evt->key == SDLK_SLASH) {
        dispatch_command(ctx, "library_search_focus", 0);
        return;
      }

      /* Media Keys (SDL2 maps XF86Audio* to these) */
      if (evt->key == SDLK_AUDIOPLAY) {
        dispatch_command(ctx, "play", 0);
        return;
      }
      if (evt->key == SDLK_AUDIONEXT) {
        dispatch_command(ctx, "next", 0);
        return;
      }
      if (evt->key == SDLK_AUDIOPREV) {
        dispatch_command(ctx, "prev", 0);
        return;
      }
      if (evt->key == SDLK_AUDIOSTOP) {
        dispatch_command(ctx, "play", 0); /* toggle pause */
        return;
      }
    }

    /* --- Text field shortcuts (active when typing) --- */
    if (evt->key == SDLK_BACKSPACE) {
      if (p->is_typing_library_path) {
        size_t len = strlen(p->library_input_buffer);
        if (len > 0)
          p->library_input_buffer[len - 1] = '\0';
      }
      if (p->is_creating_playlist) {
        size_t len = strlen(p->new_playlist_name);
        if (len > 0)
          p->new_playlist_name[len - 1] = '\0';
      }
      if (p->is_typing_search) {
        size_t len = strlen(p->search_query);
        if (len > 0) {
          p->search_query[len - 1] = '\0';
          p->library_needs_filter = true;
        }
      }
      if (p->eq_typing_preset_name) {
        size_t len = strlen(p->eq_new_preset_name);
        if (len > 0)
          p->eq_new_preset_name[len - 1] = '\0';
      }
    }
    if (evt->key == SDLK_RETURN) {
      if (p->is_typing_library_path) {
        p->is_typing_library_path = false;
        SDL_StopTextInput();
      }
      if (p->is_creating_playlist) {
        if (strlen(p->new_playlist_name) > 0) {
          db_create_playlist(p->new_playlist_name);
          if (p->library_playlists) {
            free(p->library_playlists);
            p->library_playlists = NULL;
          }
          p->library_playlist_count = 0;

          p->is_creating_playlist = false;
          SDL_StopTextInput();
          toast_show("Playlist Created", TOAST_INFO, 3000);
        }
      }
      if (p->is_typing_search) {
        p->is_typing_search = false;
        SDL_StopTextInput();
      }
      if (p->eq_typing_preset_name) {
        dispatch_command(ctx, "eq_preset_save", 0);
      }
    }
    if (evt->key == SDLK_ESCAPE) {
      p->is_creating_playlist = false;
      p->is_typing_library_path = false;
      p->is_typing_search = false;
      p->eq_typing_preset_name = false;
      p->context_menu.active = false;
      p->spotlight_active = false;
      SDL_StopTextInput();
    }
  }

  if (evt->type == EVENT_TEXTINPUT) {
    if (p->is_typing_library_path) {
      if (strlen(p->library_input_buffer) <
          sizeof(p->library_input_buffer) - 1) {
        strcat(p->library_input_buffer, evt->text);
      }
    } else if (p->is_creating_playlist) {
      if (strlen(p->new_playlist_name) < sizeof(p->new_playlist_name) - 1) {
        strcat(p->new_playlist_name, evt->text);
      }
    } else if (p->is_typing_search) {
      if (strlen(p->search_query) < sizeof(p->search_query) - 1) {
        strcat(p->search_query, evt->text);
        p->library_needs_filter = true;
      }
    } else if (p->eq_typing_preset_name) {
      if (strlen(p->eq_new_preset_name) < sizeof(p->eq_new_preset_name) - 1) {
        strcat(p->eq_new_preset_name, evt->text);
      }
    }
  }
}
