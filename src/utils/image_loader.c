#define STB_IMAGE_IMPLEMENTATION
#include "vendor/stb_image.h"

#include "image_loader.h"
#include "logging.h"
#include <stdlib.h>
#include <string.h>

/* Apply Gaussian-like blur using 3-pass horizontal/vertical box blur
 * This is much smoother than a single box blur pass.
 */
static void apply_gaussian_blur(unsigned char *data, int w, int h, int radius) {
  if (radius <= 0 || !data)
    return;

  int size = w * h * 4;
  unsigned char *temp = (unsigned char *)malloc(size);
  if (!temp) {
    log_message("ERROR", "apply_gaussian_blur: Failed to allocate memory");
    return;
  }

  /* 3 passes of box blur as an approximation of Gaussian blur */
  for (int pass = 0; pass < 3; pass++) {
    memcpy(temp, data, size);

    /* Horizontal pass */
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        int r = 0, g = 0, b = 0, count = 0;
        int start = x - radius;
        int end = x + radius;

        for (int nx = start; nx <= end; nx++) {
          if (nx >= 0 && nx < w) {
            int idx = (y * w + nx) * 4;
            r += temp[idx];
            g += temp[idx + 1];
            b += temp[idx + 2];
            count++;
          }
        }

        int idx = (y * w + x) * 4;
        data[idx] = r / count;
        data[idx + 1] = g / count;
        data[idx + 2] = b / count;
      }
    }

    memcpy(temp, data, size);

    /* Vertical pass */
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        int r = 0, g = 0, b = 0, count = 0;
        int start = y - radius;
        int end = y + radius;

        for (int ny = start; ny <= end; ny++) {
          if (ny >= 0 && ny < h) {
            int idx = (ny * w + x) * 4;
            r += temp[idx];
            g += temp[idx + 1];
            b += temp[idx + 2];
            count++;
          }
        }

        int idx = (y * w + x) * 4;
        data[idx] = r / count;
        data[idx + 1] = g / count;
        data[idx + 2] = b / count;
      }
    }
  }

  free(temp);
}

SDL_Texture *load_texture_from_file(SDL_Renderer *renderer, const char *path,
                                    Color *out_dominant_color,
                                    Color *out_secondary_color) {
  int w, h, channels;
  unsigned char *data;
  SDL_Surface *surface;
  SDL_Texture *texture;
  long r_sum = 0, g_sum = 0, b_sum = 0;
  int pixel_count;
  int i;
  Color dom = {0, 0, 0};

  /* P10 Rule 5: Assertions */
  if (!c_assert(renderer != NULL))
    return NULL;
  if (!c_assert(path != NULL))
    return NULL;

  data = stbi_load(path, &w, &h, &channels, 4); /* Force RGBA */
  if (data == NULL) {
    char err[512];
    snprintf(err, sizeof(err), "Failed to load image file: %s", path);
    log_message("WARN", err);
    return NULL;
  }

  /* Calculate Colors */
  if (out_dominant_color != NULL || out_secondary_color != NULL) {
    pixel_count = w * h;

    /* 1. Calculate Dominant Color */
    int count_used = 0;
    for (i = 0; i < pixel_count * 4; i += 4) {
      if (data[i + 3] > 10) { /* Ignore transparent pixels */
        r_sum += data[i];
        g_sum += data[i + 1];
        b_sum += data[i + 2];
        count_used++;
      }
    }

    if (count_used > 0) {
      dom.r = (uint8_t)(r_sum / count_used);
      dom.g = (uint8_t)(g_sum / count_used);
      dom.b = (uint8_t)(b_sum / count_used);
    } else {
      dom.r = 100;
      dom.g = 100;
      dom.b = 255;
    }

    if (out_dominant_color) {
      *out_dominant_color = dom;
      char log_msg[128];
      snprintf(log_msg, sizeof(log_msg), "Dominant Color: %d, %d, %d", dom.r,
               dom.g, dom.b);
      log_message("INFO", log_msg);
    }

    /* 2. Calculate Secondary Color */
    if (out_secondary_color) {
      long r2_sum = 0, g2_sum = 0, b2_sum = 0;
      int count2 = 0;
      int dist_threshold_sq = 60 * 60; /* Distance > 60 */

      for (i = 0; i < pixel_count * 4; i += 4) {
        if (data[i + 3] > 10) {
          int dr = data[i] - dom.r;
          int dg = data[i + 1] - dom.g;
          int db = data[i + 2] - dom.b;
          int dist_sq = dr * dr + dg * dg + db * db;

          if (dist_sq > dist_threshold_sq) {
            r2_sum += data[i];
            g2_sum += data[i + 1];
            b2_sum += data[i + 2];
            count2++;
          }
        }
      }

      if (count2 > 0) {
        out_secondary_color->r = (uint8_t)(r2_sum / count2);
        out_secondary_color->g = (uint8_t)(g2_sum / count2);
        out_secondary_color->b = (uint8_t)(b2_sum / count2);
      } else {
        /* Fallback: Inverse or Offset or just white for contrast */
        out_secondary_color->r = 255 - dom.r;
        out_secondary_color->g = 255 - dom.g;
        out_secondary_color->b = 255 - dom.b;
      }
    }
  }

  /* Create SDL Surface from data */
  surface = SDL_CreateRGBSurfaceWithFormatFrom(data, w, h, 32, 4 * w,
                                               SDL_PIXELFORMAT_RGBA32);
  if (surface == NULL) {
    log_message("ERROR", "Failed to create surface.");
    stbi_image_free(data);
    return NULL;
  }

  /* Chroma Key: Magenta (255, 0, 255) for UI assets fallback */
  SDL_SetColorKey(surface, SDL_TRUE, SDL_MapRGB(surface->format, 255, 0, 255));

  texture = SDL_CreateTextureFromSurface(renderer, surface);
  if (texture) {
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
  }

  SDL_FreeSurface(surface);
  stbi_image_free(data); /* P10: Free STB memory immediately */

  return texture;
}

SDL_Texture *load_texture_with_theme(SDL_Renderer *renderer, const char *path,
                                     ThemeColors *out_theme) {
  int w, h, channels;
  unsigned char *data;
  SDL_Surface *surface;
  SDL_Texture *texture;

  /* P10 Rule 5: Assertions */
  if (!c_assert(renderer != NULL))
    return NULL;
  if (!c_assert(path != NULL))
    return NULL;

  data = stbi_load(path, &w, &h, &channels, 4); /* Force RGBA */
  if (data == NULL) {
    char err[512];
    snprintf(err, sizeof(err), "Failed to load image file: %s", path);
    log_message("WARN", err);
    return NULL;
  }

  /* Extract theme colors if requested */
  if (out_theme != NULL) {
    extract_theme_colors(data, w, h, out_theme);
  }

  /* Create SDL Surface from data */
  surface = SDL_CreateRGBSurfaceWithFormatFrom(data, w, h, 32, 4 * w,
                                               SDL_PIXELFORMAT_RGBA32);
  if (surface == NULL) {
    log_message("ERROR", "Failed to create surface.");
    stbi_image_free(data);
    return NULL;
  }

  /* Chroma Key: Magenta (255, 0, 255) */
  SDL_SetColorKey(surface, SDL_TRUE, SDL_MapRGB(surface->format, 255, 0, 255));

  texture = SDL_CreateTextureFromSurface(renderer, surface);
  if (texture) {
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
  }

  SDL_FreeSurface(surface);
  stbi_image_free(data); /* P10: Free STB memory immediately */

  return texture;
}

SDL_Texture *load_texture_with_blurred_bg(SDL_Renderer *renderer,
                                          const char *path,
                                          ThemeColors *out_theme,
                                          SDL_Texture **out_blurred_bg) {
  int w, h, channels;
  unsigned char *data, *blurred_data;
  SDL_Surface *surface;
  SDL_Texture *texture, *blurred_texture;

  /* P10 Rule 5: Assertions */
  if (!c_assert(renderer != NULL))
    return NULL;
  if (!c_assert(path != NULL))
    return NULL;

  data = stbi_load(path, &w, &h, &channels, 4); /* Force RGBA */
  if (data == NULL) {
    log_message("ERROR", "Failed to load image file.");
    return NULL;
  }

  /* Extract theme colors if requested */
  if (out_theme != NULL) {
    extract_theme_colors(data, w, h, out_theme);
  }

  /* Create sharp texture for center display */
  surface = SDL_CreateRGBSurfaceWithFormatFrom(data, w, h, 32, 4 * w,
                                               SDL_PIXELFORMAT_RGBA32);
  if (surface == NULL) {
    log_message("ERROR", "Failed to create surface.");
    stbi_image_free(data);
    return NULL;
  }

  /* Chroma Key: Magenta (255, 0, 255) */
  SDL_SetColorKey(surface, SDL_TRUE, SDL_MapRGB(surface->format, 255, 0, 255));

  texture = SDL_CreateTextureFromSurface(renderer, surface);
  if (texture) {
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
  }
  SDL_FreeSurface(surface);

  /* Create blurred texture for background if requested */
  if (out_blurred_bg != NULL) {
    int size = w * h * 4;
    blurred_data = (unsigned char *)malloc(size);
    if (blurred_data) {
      memcpy(blurred_data, data, size);

      /* Apply heavy Gaussian blur */
      apply_gaussian_blur(blurred_data, w, h, 15);

      surface = SDL_CreateRGBSurfaceWithFormatFrom(
          blurred_data, w, h, 32, 4 * w, SDL_PIXELFORMAT_RGBA32);
      if (surface) {
        blurred_texture = SDL_CreateTextureFromSurface(renderer, surface);
        if (blurred_texture) {
          SDL_SetTextureBlendMode(blurred_texture, SDL_BLENDMODE_BLEND);
        }
        SDL_FreeSurface(surface);
        *out_blurred_bg = blurred_texture;
      }

      free(blurred_data);
    }
  }

  stbi_image_free(data); /* P10: Free STB memory immediately */

  return texture;
}

SDL_Texture *create_color_texture(SDL_Renderer *renderer, Color c) {
  SDL_Surface *surface;
  SDL_Texture *texture;
  uint32_t pixel;

  /* Create 1x1 surface */
  surface = SDL_CreateRGBSurface(0, 1, 1, 32, 0xFF000000, 0x00FF0000,
                                 0x0000FF00, 0x000000FF);
  if (!surface)
    return NULL;

  pixel = SDL_MapRGBA(surface->format, c.r, c.g, c.b, 255);
  SDL_FillRect(surface, NULL, pixel);

  texture = SDL_CreateTextureFromSurface(renderer, surface);
  if (texture) {
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
  }
  SDL_FreeSurface(surface);
  return texture;
}

void image_extract_theme(const char *path, ThemeColors *out_theme) {
  int w, h, channels;
  unsigned char *data;

  if (!path || !out_theme) return;

  data = stbi_load(path, &w, &h, &channels, 4);
  if (data) {
    extract_theme_colors(data, w, h, out_theme);
    stbi_image_free(data);
  }
}
