#define STB_TRUETYPE_IMPLEMENTATION
#include "font_renderer.h"
#include "logging.h"
#include "vendor/stb_truetype.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BITMAP_W 1024
#define BITMAP_H 1024

static stbtt_bakedchar g_cdata[96]; /* ASCII 32..126 */
static SDL_Texture *g_font_texture = NULL;
static float g_ascent = 0;
static float g_descent = 0;
static float g_line_gap = 0;
static float g_scale = 0;
static uint8_t g_global_alpha = 255;

void font_set_global_alpha(uint8_t alpha) { g_global_alpha = alpha; }

Result font_init(SDL_Renderer *renderer, const char *font_path,
                 float font_size) {
  long size;
  unsigned char *font_buffer;
  unsigned char *bitmap;
  int res;
  FILE *f;

  f = fopen(font_path, "rb");
  if (!f) {
    log_message("ERROR", "Failed to open font file.");
    return RESULT_ERROR_FILE_IO;
  }

  fseek(f, 0, SEEK_END);
  size = ftell(f);
  fseek(f, 0, SEEK_SET);

  font_buffer = (unsigned char *)malloc(size);
  if (!font_buffer) {
    fclose(f);
    log_message("ERROR", "Failed to allocate font buffer.");
    return RESULT_ERROR_GENERIC;
  }

  size_t read_res = fread(font_buffer, 1, size, f);
  fclose(f);

  if (read_res != (size_t)size) {
    log_message("ERROR", "Failed to read full font file.");
    free(font_buffer);
    return RESULT_ERROR_FILE_IO;
  }

  char msg[128];
  snprintf(msg, sizeof(msg), "Font file read size: %ld", size);
  log_message("INFO", msg);

  /* Log header for debug */
  log_message("INFO", "Inspecting font header...");
  char hex_msg[64];
  snprintf(hex_msg, sizeof(hex_msg), "Header: %02X %02X %02X %02X",
           font_buffer[0], font_buffer[1], font_buffer[2], font_buffer[3]);
  log_message("INFO", hex_msg);

  bitmap = (unsigned char *)calloc(BITMAP_W * BITMAP_H, 1);
  if (!bitmap) {
    free(font_buffer);
    return RESULT_ERROR_GENERIC;
  }

  /* Removed complex offset lookup which was failing with -1.
     For standard TTF files, offset 0 is usually correct. */

  /* Attempt to find correct font offset */
  int offset = stbtt_GetFontOffsetForIndex(font_buffer, 0);

  if (offset < 0) {
    /* Try hardcoded 0 as backup */
    log_message("WARNING", "stbtt_GetFontOffsetForIndex failed, using 0.");
    offset = 0;
  }

  /* Log offset */
  char off_msg[64];
  snprintf(off_msg, sizeof(off_msg), "Font Offset: %d", offset);
  log_message("INFO", off_msg);

  /* Bake font into bitmap (Alpha values 0..255) */
  res = stbtt_BakeFontBitmap(font_buffer, offset, font_size, bitmap, BITMAP_W,
                             BITMAP_H, 32, 96, g_cdata);

  /* Get Metrics for Centering */
  stbtt_fontinfo info;
  if (stbtt_InitFont(&info, font_buffer, offset)) {
    int asc, desc, lg;
    stbtt_GetFontVMetrics(&info, &asc, &desc, &lg);
    g_scale = stbtt_ScaleForPixelHeight(&info, font_size);
    g_ascent = asc * g_scale;
    g_descent = desc * g_scale;
    g_line_gap = lg * g_scale;

    char m_msg[128];
    snprintf(m_msg, sizeof(m_msg),
             "Font Metrics - Asc: %.2f, Desc: %.2f, Scale: %.4f", g_ascent,
             g_descent, g_scale);
    log_message("INFO", m_msg);
  } else {
    log_message("WARNING", "Failed to init font info for metrics.");
  }

  free(font_buffer);

  if (res <= 0) {
    char msg[128];
    snprintf(msg, sizeof(msg), "Font bake failed. Res: %d. Bitmap: %dx%d.", res,
             BITMAP_W, BITMAP_H);
    log_message("ERROR", msg);
  } else {
    char msg[128];
    snprintf(msg, sizeof(msg), "Font baked height: %d", res);
    log_message("INFO", msg);
  }

  /* Create texture with explicit format RGBA32 (8,8,8,8) */
  g_font_texture =
      SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                        SDL_TEXTUREACCESS_STATIC, BITMAP_W, BITMAP_H);

  if (g_font_texture) {
    uint32_t *pixels = (uint32_t *)malloc(BITMAP_W * BITMAP_H * 4);
    if (pixels) {
      int i;
      for (i = 0; i < BITMAP_W * BITMAP_H; i++) {
        uint8_t a = bitmap[i];
        uint8_t *p = (uint8_t *)&pixels[i];
        /* R=255, G=255, B=255, A=a */
        p[0] = 255;
        p[1] = 255;
        p[2] = 255;
        p[3] = a;
      }
      SDL_UpdateTexture(g_font_texture, NULL, pixels, BITMAP_W * 4);
      SDL_SetTextureBlendMode(g_font_texture, SDL_BLENDMODE_BLEND);
      free(pixels);
    } else {
      log_message("ERROR", "Failed to allocate pixel buffer for font.");
    }
  } else {
    log_message("ERROR", "Failed to create font texture.");
  }

  free(bitmap);
  log_message("INFO", "Font initialized (Safe Mode).");
  return RESULT_SUCCESS;
}

void font_shutdown(void) {
  if (g_font_texture) {
    SDL_DestroyTexture(g_font_texture);
    g_font_texture = NULL;
    log_message("INFO", "Font system shutdown.");
  }
}

int font_draw_text(SDL_Renderer *renderer, const char *text, int x, int y,
                   uint8_t r, uint8_t g, uint8_t b) {
  float cur_x = (float)x;
  float cur_y = (float)y;
  const char *p;

  if (!g_font_texture)
    return 0;

  SDL_SetTextureColorMod(g_font_texture, r, g, b);
  SDL_SetTextureAlphaMod(g_font_texture, g_global_alpha);

  for (p = text; *p; p++) {
    unsigned char c = (unsigned char)*p;
    if (c >= 32 && c < 128) {
      stbtt_aligned_quad q;
      stbtt_GetBakedQuad(g_cdata, BITMAP_W, BITMAP_H, c - 32, &cur_x, &cur_y,
                         &q, 1);

      /* NOTE: stbtt_GetBakedQuad assumes row-major texture but returns coords.
         The UVs (s0,t0) are 0..1 if OpenGL is assumed?
         Wait, check source of stb_truetype:
         "s0,t0,s1,t1 are texture coordinates"
         Usually normalization happens if you pass dim.
         Actually, stbtt_BakeFontBitmap doc says:
         "return value is >0 if all characters fit, <=0 if not...
         ...coordinates are relative to the top-left of the bitmap."

         BUT, q.s0 etc are dependent on how GetBakedQuad is called.
         We pass BITMAP_W, BITMAP_H.
         If we pass floats, it normalizes?
         Let's assume normalized behavior from previous usage patterns,
         so multiplying by BITMAP_W/H is correct for integer pixel rects in SDL.
      */

      SDL_Rect src_r = {(int)(q.s0 * BITMAP_W), (int)(q.t0 * BITMAP_H),
                        (int)((q.s1 - q.s0) * BITMAP_W),
                        (int)((q.t1 - q.t0) * BITMAP_H)};

      SDL_Rect dst = {(int)q.x0, (int)q.y0, (int)(q.x1 - q.x0),
                      (int)(q.y1 - q.y0)};

      SDL_RenderCopy(renderer, g_font_texture, &src_r, &dst);
    }
  }
  return (int)cur_x - x;
}

int font_get_text_width(const char *text) {
  float x = 0;
  float y = 0;
  const char *p;
  if (!g_font_texture)
    return 0;
  for (p = text; *p; p++) {
    unsigned char c = (unsigned char)*p;
    if (c >= 32 && c < 128) {
      stbtt_aligned_quad q;
      stbtt_GetBakedQuad(g_cdata, BITMAP_W, BITMAP_H, c - 32, &x, &y, &q, 1);
    }
  }
  return (int)x;
}

int font_draw_text_limit(SDL_Renderer *renderer, const char *text, int x, int y,
                         uint8_t r, uint8_t g, uint8_t b, int max_w) {
  int full_w = font_get_text_width(text);
  if (full_w <= max_w) {
    return font_draw_text(renderer, text, x, y, r, g, b);
  }

  int ellipsis_w = font_get_text_width("...");
  int target_w = max_w - ellipsis_w;

  /* Find how many chars fit in target_w */
  char *temp = strdup(text);
  if (!temp)
    return 0;

  int i = 0;
  for (i = 0; temp[i]; i++) {
    char saved = temp[i + 1];
    temp[i + 1] = '\0';
    int cur_w = font_get_text_width(temp);
    if (cur_w > target_w) {
      temp[i] = '\0'; /* Cut here */
      break;
    }
    temp[i + 1] = saved;
  }

  int drawn_w = font_draw_text(renderer, temp, x, y, r, g, b);
  drawn_w += font_draw_text(renderer, "...", x + drawn_w, y, r, g, b);

  free(temp);
  return drawn_w;
}

void font_get_text_center_offset(const char *text, int w, int h, int *out_x,
                                 int *out_y) {
  int text_w = font_get_text_width(text);

  if (out_x) {
    *out_x = (w - text_w) / 2;
  }

  if (out_y) {
    /* Vertical Center = (BoxHeight - TextHeight) / 2 */
    /* TextHeight is typically Ascent - Descent (Descent is negative) */
    /* However, we draw from Baseline. */
    /* So we want Baseline Y such that the middle of the text is at the middle
     * of the box. */
    /* Middle of Box = h / 2 */
    /* Middle of Text relative to Baseline = (Ascent + Descent) / 2 */
    /* Baseline Y = Middle of Box + (Ascent - Descent)/2 - Ascent? No. */

    /* Let's think:
       Top of text = Baseline - Ascent
       Bottom of text = Baseline - Descent (Descent is negative, so + |Descent|)

       Center of text = (Top + Bottom) / 2
                      = (Baseline - Ascent + Baseline - Descent) / 2
                      = Baseline - (Ascent + Descent) / 2

       We want Center of Text = Center of Box (h / 2)
       h / 2 = Baseline - (Ascent + Descent) / 2
       Baseline = h / 2 + (Ascent + Descent) / 2
    */

    /* Note: Ascent is positive, Descent is negative. */
    float center_offset = (g_ascent + g_descent) / 2.0f;
    *out_y = (int)((h / 2.0f) + center_offset);
  }
}
