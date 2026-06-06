#ifndef FONT_RENDERER_H
#define FONT_RENDERER_H

#include "common.h"
#include <SDL2/SDL.h>

/* Initialize Font System with TTF file */
Result font_init(SDL_Renderer *renderer, const char *font_path,
                 float font_size);
void font_shutdown(void);

/* Draw text at position with color. Returns width of string. */
int font_draw_text(SDL_Renderer *renderer, const char *text, int x, int y,
                   uint8_t r, uint8_t g, uint8_t b);

/* Calculate width of string */
int font_get_text_width(const char *text);

/* Draw text with character limit/truncation. Returns width. */
int font_draw_text_limit(SDL_Renderer *renderer, const char *text, int x, int y,
                         uint8_t r, uint8_t g, uint8_t b, int max_w);

/* Calculate X/Y offsets to center text within a rect of w, h */
void font_get_text_center_offset(const char *text, int w, int h, int *out_x,
                                 int *out_y);

#endif /* FONT_RENDERER_H */
