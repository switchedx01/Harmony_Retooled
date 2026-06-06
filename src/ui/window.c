#include "window.h"
#include "logging.h"
#include "string_utils.h"
#include <SDL2/SDL.h>

static SDL_Window *g_window = NULL;
static SDL_Renderer *g_renderer = NULL;
static bool g_mouse_down = false;
static int g_mouse_x = 0;
static int g_mouse_y = 0;

Result window_init(const char *title, int width, int height) {
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    log_message("ERROR", "SDL_Init failed.");
    return RESULT_ERROR_GENERIC;
  }

  g_window = SDL_CreateWindow(
      title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height,
      SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);

  if (!c_assert(g_window != NULL)) {
    log_message("ERROR", SDL_GetError());
    return RESULT_ERROR_GENERIC;
  }

  /* Set Minimum Window Size to prevent breakage */
  SDL_SetWindowMinimumSize(g_window, 400, 300);

  g_renderer = SDL_CreateRenderer(
      g_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (g_renderer == NULL) {
    g_renderer = SDL_CreateRenderer(g_window, -1, SDL_RENDERER_SOFTWARE);
  }

  if (!c_assert(g_renderer != NULL)) {
    log_message("ERROR", SDL_GetError());
    return RESULT_ERROR_GENERIC;
  }

  log_message("INFO", "Window system initialized.");
  return RESULT_SUCCESS;
}

void window_shutdown(void) {
  if (g_renderer)
    SDL_DestroyRenderer(g_renderer);
  if (g_window)
    SDL_DestroyWindow(g_window);
  SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
  g_renderer = NULL;
  g_window = NULL;
  log_message("INFO", "Window system shutdown.");
}

WindowEvent window_poll_event(void) {
  SDL_Event e;
  WindowEvent we = {.type = EVENT_NONE, .text = ""};

  g_mouse_down = false;

  while (SDL_PollEvent(&e)) {
    /* Copy raw event for pass-through */
    we.sdl_event = e;

    if (e.type == SDL_QUIT) {
      we.type = EVENT_QUIT;
      return we;
    } else if (e.type == SDL_WINDOWEVENT) {
      if (e.window.event == SDL_WINDOWEVENT_RESIZED ||
          e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
        we.type = EVENT_RESIZED;
        we.resize.w = e.window.data1;
        we.resize.h = e.window.data2;
        /* Return immediately so core can re-layout */
        return we;
      }
      /* Pass through all other window events (focus, close, etc.)
       * so the mini player and other handlers can see them. */
      we.type = EVENT_WINDOW;
      return we;
    } else if (e.type == SDL_MOUSEBUTTONDOWN) {
      g_mouse_x = e.button.x;
      g_mouse_y = e.button.y;
      g_mouse_down = true;
      we.type = EVENT_MOUSEDOWN;
      we.button = (int)e.button.button;
    } else if (e.type == SDL_MOUSEBUTTONUP) {
      g_mouse_x = e.button.x;
      g_mouse_y = e.button.y;
      g_mouse_down = false;
      we.type = EVENT_MOUSEUP;
      we.button = (int)e.button.button;
    } else if (e.type == SDL_MOUSEMOTION) {
      g_mouse_x = e.motion.x;
      g_mouse_y = e.motion.y;
      we.type = EVENT_MOUSEMOTION;
      // You might want to pass x/y in the event struct if needed contextually,
      // but main usually polls window_get_mouse_pos anyway.
      // But for completeness let's rely on global state update above
      // and just return the event type.
      return we;
    } else if (e.type == SDL_DROPFILE) {
      we.type = EVENT_DROPFILE;
      safe_strncpy(we.dropped_file, e.drop.file, sizeof(we.dropped_file));
      SDL_free(e.drop.file);
      return we;
    } else if (e.type == SDL_TEXTINPUT) {
      we.type = EVENT_TEXTINPUT;
      safe_strncpy(we.text, e.text.text, sizeof(we.text));
      return we;
    } else if (e.type == SDL_KEYDOWN) {
      we.type = EVENT_KEYDOWN;
      we.key = e.key.keysym.sym;
      we.mod = SDL_GetModState();
      return we;
    } else if (e.type == SDL_KEYUP) {
      we.type = EVENT_KEYUP;
      we.key = e.key.keysym.sym;
      return we;
    } else if (e.type == SDL_MOUSEWHEEL) {
      we.type = EVENT_MOUSEWHEEL;
      we.wheel.x = e.wheel.x;
      we.wheel.y = e.wheel.y;
      return we;
    }
  }
  return we;
}

void window_begin_frame(void) {
  SDL_SetRenderDrawColor(g_renderer, 18, 18, 18, 255);
  SDL_RenderClear(g_renderer);
}

void window_end_frame(void) { SDL_RenderPresent(g_renderer); }

void window_draw_rect(int x, int y, int w, int h, uint8_t r, uint8_t g,
                      uint8_t b) {
  SDL_Rect rect = {x, y, w, h};
  SDL_SetRenderDrawColor(g_renderer, r, g, b, 255);
  SDL_RenderFillRect(g_renderer, &rect);
}

bool is_point_in_rect(int px, int py, int x, int y, int w, int h) {
  return (px >= x && px <= x + w && py >= y && py <= y + h);
}

void window_get_mouse_pos(int *x, int *y) {
  if (x)
    *x = g_mouse_x;
  if (y)
    *y = g_mouse_y;
}

void window_get_size(int *w, int *h) {
  if (g_window) {
    SDL_GetWindowSize(g_window, w, h);
  } else {
    if (w)
      *w = 0;
    if (h)
      *h = 0;
  }
}

bool window_is_mouse_down(void) { return g_mouse_down; }

bool window_is_mouse_held(void) {
  return (SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(SDL_BUTTON_LEFT));
}

void *window_get_renderer(void) { return g_renderer; }

void *window_get_sdl_window(void) { return g_window; }
