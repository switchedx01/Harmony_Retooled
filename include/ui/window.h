#ifndef WINDOW_H
#define WINDOW_H

#include "common.h"
#include <SDL2/SDL.h>

typedef enum {
  EVENT_NONE,
  EVENT_QUIT,
  EVENT_MOUSEDOWN,
  EVENT_DROPFILE,
  EVENT_TEXTINPUT,
  EVENT_KEYDOWN,
  EVENT_KEYUP,
  EVENT_MOUSEUP,
  EVENT_MOUSEMOTION,
  EVENT_MOUSEWHEEL,
  EVENT_RESIZED,
  EVENT_WINDOW /* Generic window event pass-through (for mini player, etc.) */
} EventType;

typedef struct {
  EventType type;
  char dropped_file[256];
  char text[32];
  int key;
  uint16_t mod;
  struct {
    int x;
    int y;
  } wheel;
  struct {
    int w;
    int h;
  } resize;
  int button;          /* 1=Left, 2=Middle, 3=Right */
  SDL_Event sdl_event; /* Raw event for advanced handling */
} WindowEvent;

/* Initialize Window and Graphics Subsystem */
Result window_init(const char *title, int width, int height);

/* Shutdown Window and Graphics */
void window_shutdown(void);

/* Poll for events. */
WindowEvent window_poll_event(void);

/* Begin Frame (Clear) */
void window_begin_frame(void);

/* End Frame (Present) */
void window_end_frame(void);

/* Draw a simple rectangle (primitive UI for MVP) */
void window_draw_rect(int x, int y, int w, int h, uint8_t r, uint8_t g,
                      uint8_t b);

/* Check if a point is inside a rect (for button clicks) */
bool is_point_in_rect(int px, int py, int x, int y, int w, int h);
void window_get_mouse_pos(int *x, int *y);
bool window_is_mouse_down(void);
bool window_is_mouse_held(void);

/* Get raw SDL Renderer for font drawing */
void *window_get_renderer(void);

/* Get raw SDL Window */
void *window_get_sdl_window(void);

/* Get current window size */
void window_get_size(int *w, int *h);

#endif /* WINDOW_H */
