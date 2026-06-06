#ifndef IMAGE_LOADER_H
#define IMAGE_LOADER_H

#include "color_extractor.h"
#include "common.h"
#include <SDL2/SDL.h>

/* Load an image from path and return SDL Texture */
/* Also optionally returns the dominant and secondary colors if they are not
 * null */
SDL_Texture *load_texture_from_file(SDL_Renderer *renderer, const char *path,
                                    Color *out_dominant_color,
                                    Color *out_secondary_color);

/* Load texture and extract full theme colors */
SDL_Texture *load_texture_with_theme(SDL_Renderer *renderer, const char *path,
                                     ThemeColors *out_theme);

/* Load texture with separate blurred version for background */
SDL_Texture *load_texture_with_blurred_bg(SDL_Renderer *renderer,
                                          const char *path,
                                          ThemeColors *out_theme,
                                          SDL_Texture **out_blurred_bg);

/* Generate a 1x1 pixel texture of a specific color (fallback) */
SDL_Texture *create_color_texture(SDL_Renderer *renderer, Color c);

/* Headless theme extraction from image file */
void image_extract_theme(const char *path, ThemeColors *out_theme);

#endif /* IMAGE_LOADER_H */
