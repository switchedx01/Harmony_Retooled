/*
 * mini_player.c — External floating borderless window mini player
 *
 * Creates a small, always-on-top, borderless SDL2 window that
 * acts as a standalone mini player. Shares the audio pipeline
 * with the main application via PlayerContext.
 *
 * - Collapsed: small pill showing play/pause state
 * - Expanded:  shows track info, controls, seek bar
 * - Draggable via SDL_SetWindowHitTest (Wayland + X11 compatible)
 * - Hides the main window when active (iTunes-style)
 */

#include "mini_player.h"
#include "audio_backend.h"
#include "color_extractor.h"
#include "common.h"
#include "font_renderer.h"
#include "image_loader.h"
#include "material_renderer.h"
#include "player.h"
#include "window.h"
#include "path_utils.h"

#include <SDL2/SDL_shape.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ── Window Dimensions ──────────────────────────────── */
#define MP_COLLAPSED_W 76
#define MP_COLLAPSED_H 76
#define MP_FLYOUT_W (MP_COLLAPSED_W + 200) /* fixed window width in collapsed mode */
/* No longer using expanded geometry */

#define MP_CONTAINER_W 600
#define MP_CONTAINER_H 400

/* ── Internal State ─────────────────────────────────── */
static struct {
  /* SDL objects — owned by mini player */
  SDL_Window *window;
  SDL_Renderer *renderer;
  Uint32 window_id;

  bool visible;
  int click_count;

  /* Font texture for this renderer */
  SDL_Texture *font_texture;

  /* Screen bounds */
  int screen_x;
  int screen_y;
  int screen_w;
  int screen_h;

  /* Animation state */
  bool animating;
  Uint32 anim_start;
  int start_x, start_y, start_w, start_h;
  int target_x, target_y, target_w, target_h;

  /* Flyout State */
  bool flyout_active;
  bool flyout_expanding;
  Uint32 flyout_anim_start;
  int flyout_start_w;
  int flyout_target_w;
  int flyout_anim_x_offset;
  int flyout_anim_y_offset;
  int flyout_current_w;
  Uint32 flyout_timer;
  int flyout_direction; /* 1 for expand right, -1 for expand left */
  int last_index; /* Track changes to trigger flyout */
  float flyout_scroll_offset; /* marquee scroll position in pixels */
  Uint32 flyout_scroll_start; /* tick when scrolling delay started */

  /* Restoration coordinates */
  int pill_x, pill_y;

  /* Bounds snapping */
  int dock_x, dock_y;
  Uint32 last_move_time;

  /* Artwork cache */
  SDL_Texture *art_texture;
  SDL_Texture *blurred_bg_texture;
  char current_art_path[MAX_PATH_LENGTH];

  /* Shaped window mask */
  SDL_Surface *shape_surface;

  /* Interaction Timers */
  bool pending_expand;
  Uint32 last_click_time;
  int click_origin_x, click_origin_y;

  /* Dragging */
  bool is_dragging;
  int drag_start_global_x, drag_start_global_y;
  int drag_start_pill_x, drag_start_pill_y;
  int win_start_x, win_start_y;
  bool drag_moved;
  /* Tuck State */
  bool is_tucked;
  int tuck_direction;

} g_mp = {0};

/* ── Font (re-baked for our renderer) ───────────────── */

#include "vendor/stb_truetype.h"

#define MP_BITMAP_W 512
#define MP_BITMAP_H 512

static stbtt_bakedchar g_mp_cdata[96];
static bool g_mp_font_ready = false;

static void mp_font_init(SDL_Renderer *renderer) {
  char font_path[MAX_PATH_LENGTH];
  FILE *f = NULL;
  resolve_asset_path("assets/fonts/Roboto-Regular.ttf", font_path, sizeof(font_path));
  f = fopen(font_path, "rb");
  if (!f) {
      f = fopen("assets/fonts/Roboto-Regular.ttf", "rb");
  }
  if (!f)
    return;

  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);

  unsigned char *font_buf = (unsigned char *)malloc(size);
  if (!font_buf) {
    fclose(f);
    return;
  }
  fread(font_buf, 1, size, f);
  fclose(f);

  unsigned char *bitmap = (unsigned char *)calloc(MP_BITMAP_W * MP_BITMAP_H, 1);
  if (!bitmap) {
    free(font_buf);
    return;
  }

  int offset = stbtt_GetFontOffsetForIndex(font_buf, 0);
  if (offset < 0)
    offset = 0;

  stbtt_BakeFontBitmap(font_buf, offset, 20.0f, bitmap, MP_BITMAP_W,
                       MP_BITMAP_H, 32, 96, g_mp_cdata);
  free(font_buf);

  g_mp.font_texture =
      SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                        SDL_TEXTUREACCESS_STATIC, MP_BITMAP_W, MP_BITMAP_H);

  if (g_mp.font_texture) {
    uint32_t *pixels = (uint32_t *)malloc(MP_BITMAP_W * MP_BITMAP_H * 4);
    if (pixels) {
      for (int i = 0; i < MP_BITMAP_W * MP_BITMAP_H; i++) {
        uint8_t a = bitmap[i];
        uint8_t *p = (uint8_t *)&pixels[i];
        p[0] = 255;
        p[1] = 255;
        p[2] = 255;
        p[3] = a;
      }
      SDL_UpdateTexture(g_mp.font_texture, NULL, pixels, MP_BITMAP_W * 4);
      SDL_SetTextureBlendMode(g_mp.font_texture, SDL_BLENDMODE_BLEND);
      free(pixels);
    }
  }

  free(bitmap);
  g_mp_font_ready = true;
}

static int mp_draw_text(const char *text, int x, int y, uint8_t r, uint8_t g,
                        uint8_t b) {
  if (!g_mp_font_ready || !g_mp.font_texture || !g_mp.renderer)
    return 0;

  float cur_x = (float)x;
  float cur_y = (float)y;

  SDL_SetTextureColorMod(g_mp.font_texture, r, g, b);
  SDL_SetTextureAlphaMod(g_mp.font_texture, 255);

  for (const char *p = text; *p; p++) {
    unsigned char c = (unsigned char)*p;
    if (c >= 32 && c < 128) {
      stbtt_aligned_quad q;
      stbtt_GetBakedQuad(g_mp_cdata, MP_BITMAP_W, MP_BITMAP_H, c - 32, &cur_x,
                         &cur_y, &q, 1);
      SDL_Rect src = {(int)(q.s0 * MP_BITMAP_W), (int)(q.t0 * MP_BITMAP_H),
                      (int)((q.s1 - q.s0) * MP_BITMAP_W),
                      (int)((q.t1 - q.t0) * MP_BITMAP_H)};
      SDL_Rect dst = {(int)q.x0, (int)q.y0, (int)(q.x1 - q.x0),
                      (int)(q.y1 - q.y0)};
      SDL_RenderCopy(g_mp.renderer, g_mp.font_texture, &src, &dst);
    }
  }
  return (int)cur_x - x;
}

static int mp_text_width(const char *text) {
  if (!g_mp_font_ready)
    return 0;
  float x = 0, y = 0;
  for (const char *p = text; *p; p++) {
    unsigned char c = (unsigned char)*p;
    if (c >= 32 && c < 128) {
      stbtt_aligned_quad q;
      stbtt_GetBakedQuad(g_mp_cdata, MP_BITMAP_W, MP_BITMAP_H, c - 32, &x, &y,
                         &q, 1);
    }
  }
  return (int)x;
}

/* ── Draw Helpers ───────────────────────────────────── */

static int get_bounds_offset_x(int current_w) {
  if (g_mp.animating && (g_mp.target_w != g_mp.start_w || g_mp.target_h != g_mp.start_h)) {
    return (MP_CONTAINER_W - current_w) / 2;
  }
  if (g_mp.flyout_active && g_mp.flyout_direction == -1) {
    return (MP_CONTAINER_W - MP_COLLAPSED_W) / 2 + MP_COLLAPSED_W - current_w;
  }
  return (MP_CONTAINER_W - MP_COLLAPSED_W) / 2;
}

static int get_bounds_offset_y(int current_h) {
  if (g_mp.animating && (g_mp.target_w != g_mp.start_w || g_mp.target_h != g_mp.start_h)) {
    return (MP_CONTAINER_H - current_h) / 2;
  }
  return (MP_CONTAINER_H - MP_COLLAPSED_H) / 2;
}

static void mp_fill_rect(int x, int y, int w, int h, uint8_t r, uint8_t g,
                         uint8_t b, uint8_t a) {
  SDL_SetRenderDrawBlendMode(g_mp.renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(g_mp.renderer, r, g, b, a);
  SDL_Rect rect = {x, y, w, h};
  SDL_RenderFillRect(g_mp.renderer, &rect);
}

static void mp_fill_circle(int cx, int cy, int rad, uint8_t r, uint8_t g,
                           uint8_t b, uint8_t a) {
  SDL_SetRenderDrawBlendMode(g_mp.renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(g_mp.renderer, r, g, b, a);
  for (int dy = -rad; dy <= rad; dy++) {
    int dx = (int)sqrtf((float)(rad * rad - dy * dy));
    SDL_RenderDrawLine(g_mp.renderer, cx - dx, cy + dy, cx + dx, cy + dy);
  }
}

static void draw_aa_textured_circle(int cx, int cy, int rad, SDL_Texture *tex,
                                    float angle) {
  if (!tex)
    return;

  SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);

  int segments = 64;
  /* Center + inner ring + outer ring */
  int vert_count = 1 + segments + segments;
  SDL_Vertex verts[64 * 2 + 1];
  /* Inner triangles (segments * 3) + Outer quads (segments * 6) */
  int idx_count = segments * 3 + segments * 6;
  int indices[64 * 9];

  // Center vertex
  verts[0].position.x = (float)cx;
  verts[0].position.y = (float)cy;
  verts[0].color = (SDL_Color){255, 255, 255, 255};
  verts[0].tex_coord.x = 0.5f;
  verts[0].tex_coord.y = 0.5f;

  float start_angle = angle * (float)M_PI / 180.0f;
  float aa_fringe = 1.0f; /* 1 pixel soft edge */

  for (int i = 0; i < segments; i++) {
    float a = start_angle + (float)i / (float)segments * 2.0f * (float)M_PI;
    float c = cosf(a);
    float s = sinf(a);

    float local_a = (float)i / (float)segments * 2.0f * (float)M_PI;
    float lc = cosf(local_a);
    float ls = sinf(local_a);
    float tx = 0.5f + lc * 0.5f;
    float ty = 0.5f + ls * 0.5f;

    /* Inner Ring (Solid) */
    int in_idx = 1 + i;
    verts[in_idx].position.x = (float)cx + c * rad;
    verts[in_idx].position.y = (float)cy + s * rad;
    verts[in_idx].color = (SDL_Color){255, 255, 255, 255};
    verts[in_idx].tex_coord.x = tx;
    verts[in_idx].tex_coord.y = ty;

    /* Outer Ring (Transparent) */
    int out_idx = 1 + segments + i;
    verts[out_idx].position.x = (float)cx + c * (rad + aa_fringe);
    verts[out_idx].position.y = (float)cy + s * (rad + aa_fringe);
    verts[out_idx].color = (SDL_Color){255, 255, 255, 0};
    verts[out_idx].tex_coord.x = tx;
    verts[out_idx].tex_coord.y = ty;
  }

  int idx_ptr = 0;
  for (int i = 0; i < segments; i++) {
    int next_i = (i + 1) % segments;

    /* Inner solid triangle */
    indices[idx_ptr++] = 0;
    indices[idx_ptr++] = 1 + i;
    indices[idx_ptr++] = 1 + next_i;

    /* Outer AA fringe quad */
    indices[idx_ptr++] = 1 + i;
    indices[idx_ptr++] = 1 + segments + i;
    indices[idx_ptr++] = 1 + segments + next_i;

    indices[idx_ptr++] = 1 + i;
    indices[idx_ptr++] = 1 + segments + next_i;
    indices[idx_ptr++] = 1 + next_i;
  }

  SDL_RenderGeometry(g_mp.renderer, tex, verts, vert_count, indices, idx_count);
}

static bool point_in_rect(int px, int py, int rx, int ry, int rw, int rh) {
  return (px >= rx && px <= rx + rw && py >= ry && py <= ry + rh);
}

/* ── Shaped Window Mask ────────────────────────────── */

static void fill_circle_on_surface(SDL_Surface *surf, int cx, int cy, int rad) {
  Uint32 white = SDL_MapRGBA(surf->format, 255, 255, 255, 255);
  for (int dy = -rad; dy <= rad; dy++) {
    int dx = (int)sqrtf((float)(rad * rad - dy * dy));
    int py = cy + dy;
    if (py < 0 || py >= surf->h) continue;
    int x0 = cx - dx;
    int x1 = cx + dx;
    if (x0 < 0) x0 = 0;
    if (x1 >= surf->w) x1 = surf->w - 1;
    Uint32 *row = (Uint32 *)((Uint8 *)surf->pixels + py * surf->pitch);
    for (int x = x0; x <= x1; x++) {
      row[x] = white;
    }
  }
}

static void fill_rect_on_surface(SDL_Surface *surf, int rx, int ry, int rw, int rh) {
  Uint32 white = SDL_MapRGBA(surf->format, 255, 255, 255, 255);
  for (int py = ry; py < ry + rh; py++) {
    if (py < 0 || py >= surf->h) continue;
    Uint32 *row = (Uint32 *)((Uint8 *)surf->pixels + py * surf->pitch);
    for (int px = rx; px < rx + rw; px++) {
      if (px < 0 || px >= surf->w) continue;
      row[px] = white;
    }
  }
}

static void update_window_shape(void) {
  if (!g_mp.window) return;

  int w = MP_CONTAINER_W;
  int h = MP_CONTAINER_H;

  /* Determine layout coordinates inside the container */
  int current_w = g_mp.flyout_active ? g_mp.flyout_current_w : MP_COLLAPSED_W;
  int current_h = MP_COLLAPSED_H;
  
  if (g_mp.animating) {
    current_w = g_mp.start_w + (int)((g_mp.target_w - g_mp.start_w) * (1.0f - powf(1.0f - (float)(SDL_GetTicks() - g_mp.anim_start) / 150.0f, 2.0f)));
    current_h = g_mp.start_h + (int)((g_mp.target_h - g_mp.start_h) * (1.0f - powf(1.0f - (float)(SDL_GetTicks() - g_mp.anim_start) / 150.0f, 2.0f)));
  }

  /* Cache check to prevent spamming SDL_SetWindowShape and crashing the OS window manager */
  static int last_w = -1, last_h = -1, last_dir = 0;
  
  if (g_mp.shape_surface && 
      last_w == current_w && last_h == current_h &&
      last_dir == g_mp.flyout_direction) {
    return;
  }
  
  last_w = current_w;
  last_h = current_h;
  last_dir = g_mp.flyout_direction;

  /* Recreate shape surface if size changed */
  if (g_mp.shape_surface && (g_mp.shape_surface->w != w || g_mp.shape_surface->h != h)) {
    SDL_FreeSurface(g_mp.shape_surface);
    g_mp.shape_surface = NULL;
  }

  if (!g_mp.shape_surface) {
    g_mp.shape_surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
    if (!g_mp.shape_surface) return;
  }

  /* Clear to fully transparent */
  SDL_FillRect(g_mp.shape_surface, NULL, SDL_MapRGBA(g_mp.shape_surface->format, 0, 0, 0, 0));

  /* CD / collapsed offset logic */
  int offset_x = get_bounds_offset_x(current_w);
  int offset_y = get_bounds_offset_y(current_h);

  if (g_mp.animating && (g_mp.target_w != g_mp.start_w || g_mp.target_h != g_mp.start_h)) {
    /* Full rectangle for mode transitions (not used anymore typically) */
    SDL_Rect r = {offset_x, offset_y, current_w, current_h};
    SDL_FillRect(g_mp.shape_surface, &r, SDL_MapRGBA(g_mp.shape_surface->format, 255, 255, 255, 255));
  } else if (g_mp.flyout_active || g_mp.animating) {
    int cd_radius = MP_COLLAPSED_W / 2;
    int cd_cy = offset_y + MP_COLLAPSED_H / 2;
    int flyout_w = current_w - MP_COLLAPSED_W;
    int cd_cx = offset_x + cd_radius;
    if (g_mp.flyout_direction == -1) {
      cd_cx += flyout_w;
    }

    /* CD circle */
    fill_circle_on_surface(g_mp.shape_surface, cd_cx, cd_cy, cd_radius + 1);

    if (flyout_w > 0) {
      /* Pill body */
      int end_cx, rect_x;
      if (g_mp.flyout_direction == 1) {
        end_cx = cd_cx + flyout_w;
        rect_x = cd_cx;
      } else {
        end_cx = cd_cx - flyout_w;
        rect_x = end_cx;
      }
      fill_circle_on_surface(g_mp.shape_surface, end_cx, cd_cy, cd_radius);
      fill_rect_on_surface(g_mp.shape_surface, rect_x, cd_cy - cd_radius, flyout_w, cd_radius * 2);
    }
  } else {
    /* Collapsed: just the CD circle */
    int cd_radius = MP_COLLAPSED_W / 2;
    fill_circle_on_surface(g_mp.shape_surface, offset_x + MP_COLLAPSED_W / 2, offset_y + MP_COLLAPSED_H / 2, cd_radius + 1);
  }

  SDL_WindowShapeMode mode;
  mode.mode = ShapeModeDefault;
  mode.parameters.binarizationCutoff = 128;
  SDL_SetWindowShape(g_mp.window, g_mp.shape_surface, &mode);
}

static SDL_HitTestResult SDLCALL mp_hit_test_callback(SDL_Window *win,
                                                      const SDL_Point *area,
                                                      void *data) {
  (void)win; (void)data;
  int mx = area->x; int my = area->y;
  int current_w = g_mp.flyout_active ? g_mp.flyout_current_w : MP_COLLAPSED_W;
  int current_h = MP_COLLAPSED_H;
  int offset_x = get_bounds_offset_x(current_w);
  int offset_y = get_bounds_offset_y(current_h);

  /* Ensure within window logical area */
  if (mx < offset_x || mx > offset_x + current_w || my < offset_y || my > offset_y + current_h) {
    return SDL_HITTEST_NORMAL;
  }

  int cd_radius = MP_COLLAPSED_W / 2;
  int cd_cx = offset_x + cd_radius;
  if (g_mp.flyout_active && g_mp.flyout_direction == -1) {
    cd_cx += (current_w - MP_COLLAPSED_W);
  }
  int cd_cy = offset_y + cd_radius;

  int dx = mx - cd_cx;
  int dy = my - cd_cy;
  int dist_sq = dx * dx + dy * dy;

  /* If inside the CD itself */
  if (dist_sq <= cd_radius * cd_radius) {
    /* If the user clicks the inner 25px radius (50px diameter), it's a CLICK. */
    if (dist_sq <= 25 * 25) {
      return SDL_HITTEST_NORMAL;
    }
  }

  /* Any other part of the mini player (the CD's outer edge OR the flyout body) is naturally draggable */
  return SDL_HITTEST_DRAGGABLE;
}

/* ── Window Management ──────────────────────────────── */

static void update_screen_bounds(void) {
  int display_idx = 0;
  if (g_mp.window) {
    display_idx = SDL_GetWindowDisplayIndex(g_mp.window);
    if (display_idx < 0)
      display_idx = 0;
  }
  SDL_Rect bounds;
  if (SDL_GetDisplayUsableBounds(display_idx, &bounds) == 0) {
    g_mp.screen_x = bounds.x;
    g_mp.screen_y = bounds.y;
    g_mp.screen_w = bounds.w;
    g_mp.screen_h = bounds.h;
  }
}

static void enforce_bounds(void) {
  if (g_mp.animating || !g_mp.window)
    return;

  if (g_mp.is_dragging)
    return;

  int global_mx, global_my;
  Uint32 mouse_state = SDL_GetGlobalMouseState(&global_mx, &global_my);
  if (mouse_state & SDL_BUTTON(SDL_BUTTON_LEFT))
    return;

  /* Prevent snapping while the user is actively dragging it over bounds.
     Wait 150ms after the last move event before enforcing bounds. */
  if (SDL_GetTicks() - g_mp.last_move_time < 150)
    return;

  int wx, wy, w, h;
  SDL_GetWindowPosition(g_mp.window, &wx, &wy);
  SDL_GetWindowSize(g_mp.window, &w, &h);

  int display_idx = SDL_GetWindowDisplayIndex(g_mp.window);
  if (display_idx < 0)
    return;

  SDL_Rect bounds;
  if (SDL_GetDisplayUsableBounds(display_idx, &bounds) < 0)
    return;



  int cw = MP_COLLAPSED_W;
  int ch = MP_COLLAPSED_H;

  int offset_x = get_bounds_offset_x(cw);
  int offset_y = get_bounds_offset_y(ch);

  int visual_x = wx + offset_x;
  int visual_y = wy + offset_y;

  /* Determine if window is currently hovered using exact circular hit test. */
  bool is_hovered = false;
  
  if (g_mp.flyout_active) {
    is_hovered = true; /* Prevent tucking while flyout is active */
  } else {
    int local_mx = global_mx - visual_x;
    int local_my = global_my - visual_y;
    int cd_radius = MP_COLLAPSED_W / 2;
    int dx = local_mx - cd_radius;
    int dy = local_my - cd_radius;
    if (dx * dx + dy * dy <= cd_radius * cd_radius) {
      is_hovered = true;
    }
    
    /* Edge stickiness: keep hovered if mouse rests on the screen edge next to the player */
    if (visual_x < bounds.x + 20 && global_mx <= bounds.x && 
        global_my >= visual_y && global_my <= visual_y + ch) {
      is_hovered = true;
    }
    if (visual_x + cw > bounds.x + bounds.w - 20 && global_mx >= bounds.x + bounds.w - 1 && 
        global_my >= visual_y && global_my <= visual_y + ch) {
      is_hovered = true;
    }
  }

  int desired_x = visual_x;
  int desired_y = visual_y;

  /* X snapping and tucking */
  if (visual_x < bounds.x + 20) {
    if (is_hovered) {
      desired_x = bounds.x;
      g_mp.tuck_direction = -1;
      g_mp.is_tucked = false;
    } else {
      desired_x = bounds.x - cw / 2;
      g_mp.tuck_direction = -1;
      g_mp.is_tucked = true;
    }
  } else if (visual_x + cw > bounds.x + bounds.w - 20) {
    if (is_hovered) {
      desired_x = bounds.x + bounds.w - cw;
      g_mp.tuck_direction = 1;
      g_mp.is_tucked = false;
    } else {
      desired_x = bounds.x + bounds.w - cw / 2;
      g_mp.tuck_direction = 1;
      g_mp.is_tucked = true;
    }
  } else {
    g_mp.tuck_direction = 0;
    g_mp.is_tucked = false;
  }

  /* Y snapping */
  if (visual_y < bounds.y) {
    desired_y = bounds.y;
  } else if (visual_y + ch > bounds.y + bounds.h) {
    desired_y = bounds.y + bounds.h - ch;
  }

  if (desired_x != visual_x || desired_y != visual_y) {
    g_mp.start_x = wx;
    g_mp.start_y = wy;
    g_mp.start_w = w;
    g_mp.start_h = h;
    g_mp.target_w = w;
    g_mp.target_h = h;
    g_mp.target_x = desired_x - offset_x;
    g_mp.target_y = desired_y - offset_y;

    g_mp.animating = true;
    g_mp.anim_start = SDL_GetTicks();
  } else {
    g_mp.dock_x = wx;
    g_mp.dock_y = wy;
  }
}

static void create_window(void) {
  if (g_mp.window)
    return;

  /* Position: near bottom-right of primary display */
  update_screen_bounds();
  int visual_x = g_mp.screen_x + g_mp.screen_w - MP_COLLAPSED_W - 40;
  int visual_y = g_mp.screen_y + g_mp.screen_h - MP_COLLAPSED_H - 80;
  
  int wx = visual_x - get_bounds_offset_x(MP_COLLAPSED_W);
  int wy = visual_y - get_bounds_offset_y(MP_COLLAPSED_H);
  
  g_mp.pill_x = wx;
  g_mp.pill_y = wy;
  g_mp.dock_x = wx;
  g_mp.dock_y = wy;

  g_mp.window = SDL_CreateShapedWindow(
      "Harmony Mini Player", wx, wy, MP_CONTAINER_W, MP_CONTAINER_H,
      SDL_WINDOW_ALWAYS_ON_TOP | SDL_WINDOW_SKIP_TASKBAR);

  if (!g_mp.window)
    return;

  /* Force borderless — shaped windows should be borderless but some WMs add decorations */
  SDL_SetWindowBordered(g_mp.window, SDL_FALSE);

  /* Set up compositor-native drag via hit test */
  SDL_SetWindowHitTest(g_mp.window, mp_hit_test_callback, NULL);

  g_mp.renderer = SDL_CreateRenderer(
      g_mp.window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

  if (!g_mp.renderer) {
    g_mp.renderer = SDL_CreateRenderer(g_mp.window, -1, SDL_RENDERER_SOFTWARE);
  }

  if (!g_mp.renderer) {
    SDL_DestroyWindow(g_mp.window);
    g_mp.window = NULL;
    return;
  }

  g_mp.window_id = SDL_GetWindowID(g_mp.window);

  /* Init font for this renderer */
  mp_font_init(g_mp.renderer);

  /* Slight transparency for polish */
  SDL_SetWindowOpacity(g_mp.window, 0.95f);

  /* Force always-on-top (some compositors need this after creation) */
#if SDL_VERSION_ATLEAST(2, 0, 16)
  SDL_SetWindowAlwaysOnTop(g_mp.window, SDL_TRUE);
#endif
}

static void destroy_window(void) {
  if (g_mp.shape_surface) {
    SDL_FreeSurface(g_mp.shape_surface);
    g_mp.shape_surface = NULL;
  }
  if (g_mp.art_texture) {
    SDL_DestroyTexture(g_mp.art_texture);
    g_mp.art_texture = NULL;
  }
  if (g_mp.blurred_bg_texture) {
    SDL_DestroyTexture(g_mp.blurred_bg_texture);
    g_mp.blurred_bg_texture = NULL;
  }
  g_mp.current_art_path[0] = '\0';

  if (g_mp.font_texture) {
    SDL_DestroyTexture(g_mp.font_texture);
    g_mp.font_texture = NULL;
  }
  g_mp_font_ready = false;

  if (g_mp.renderer) {
    SDL_DestroyRenderer(g_mp.renderer);
    g_mp.renderer = NULL;
  }
  if (g_mp.window) {
    SDL_DestroyWindow(g_mp.window);
    g_mp.window = NULL;
  }
  g_mp.window_id = 0;
}

/* ── Main Window Visibility ─────────────────────────── */

static void hide_main_window(void) {
  SDL_Window *main_win = (SDL_Window *)window_get_sdl_window();
  if (main_win) {
    SDL_HideWindow(main_win);
  }
}

static void show_main_window(void) {
  SDL_Window *main_win = (SDL_Window *)window_get_sdl_window();
  if (main_win) {
    SDL_ShowWindow(main_win);
    SDL_RestoreWindow(main_win);
    SDL_RaiseWindow(main_win);

    /* Force a resize event so the layout recalculates */
    int w, h;
    SDL_GetWindowSize(main_win, &w, &h);
    SDL_Event resize_evt;
    resize_evt.type = SDL_WINDOWEVENT;
    resize_evt.window.event = SDL_WINDOWEVENT_SIZE_CHANGED;
    resize_evt.window.data1 = w;
    resize_evt.window.data2 = h;
    resize_evt.window.windowID = SDL_GetWindowID(main_win);
    SDL_PushEvent(&resize_evt);
  }
}

/* trigger_expand and trigger_collapse removed as expanded mode is deprecated */

/* ── Public API ─────────────────────────────────────── */

void mini_player_init(void) {
  memset(&g_mp, 0, sizeof(g_mp));
  g_mp.visible = false;
}

void mini_player_toggle(void) {
  if (g_mp.visible) {
    /* Close mini player, show main window */
    g_mp.visible = false;
    destroy_window();
    show_main_window();
  } else {
    /* Open mini player, hide main window */
    g_mp.visible = true;
    create_window();
    hide_main_window();
  }
}

bool mini_player_is_active(void) { return g_mp.visible && g_mp.window != NULL; }

void mini_player_shutdown(void) {
  if (g_mp.visible) {
    destroy_window();
    show_main_window();
  }
  g_mp.visible = false;
}

static void start_flyout(void) {
  g_mp.flyout_active = true;
  g_mp.flyout_expanding = true;
  g_mp.flyout_anim_start = SDL_GetTicks();
  g_mp.flyout_timer = SDL_GetTicks() + 3000;

  g_mp.flyout_start_w = MP_COLLAPSED_W;
  g_mp.flyout_target_w = MP_FLYOUT_W;
  g_mp.flyout_current_w = MP_COLLAPSED_W;
  g_mp.flyout_scroll_offset = 0.0f;
  g_mp.flyout_scroll_start = SDL_GetTicks();

  update_screen_bounds();
  
  int center_x = g_mp.pill_x + get_bounds_offset_x(MP_COLLAPSED_W) + MP_COLLAPSED_W / 2;
  if (center_x > g_mp.screen_x + g_mp.screen_w / 2) {
    g_mp.flyout_direction = -1; /* left */
  } else {
    g_mp.flyout_direction = 1; /* right */
  }

  /* Force the window mask to instantly expand to full logical width so 
     we ONLY animate pixels, not the compositor masks 60 times a second. */
  update_window_shape();
}

    /* ── Event Handling ─────────────────────────────────── */

bool mini_player_handle_event(SDL_Event *e, PlayerContext *ctx) {
  if (!g_mp.visible || !g_mp.window)
    return false;

  if (g_mp.animating) {
    if (g_mp.start_w != g_mp.target_w || g_mp.start_h != g_mp.target_h)
      return true; /* Block interaction during animation bounds transitions */
  }

  /* Determine which window the event belongs to */
  Uint32 eid = 0;
  if (e->type == SDL_MOUSEBUTTONDOWN || e->type == SDL_MOUSEBUTTONUP)
    eid = e->button.windowID;
  else if (e->type == SDL_MOUSEMOTION)
    eid = e->motion.windowID;
  else if (e->type == SDL_WINDOWEVENT)
    eid = e->window.windowID;
  else if (e->type == SDL_MOUSEWHEEL)
    eid = e->wheel.windowID;

  /* Only handle events for our window */
  if (eid != 0 && eid != g_mp.window_id)
    return false;

  /* Window events */
  if (e->type == SDL_WINDOWEVENT && eid == g_mp.window_id) {
    if (e->window.event == SDL_WINDOWEVENT_CLOSE) {
      mini_player_toggle();
      return true;
    }
    if (e->window.event == SDL_WINDOWEVENT_MOVED) {
      g_mp.last_move_time = SDL_GetTicks();
      
      /* Sync structural coordinates if user dragged the window natively */
      if (!g_mp.animating && !g_mp.pending_expand) {
        int wx = e->window.data1;
        int wy = e->window.data2;
        if (wx != g_mp.pill_x) g_mp.pill_x = wx;
        g_mp.pill_y = wy;
      }

      if (g_mp.pending_expand) {
        /* Cancel expand if window moved significantly */
        g_mp.pending_expand = false;
      }
    }
    /* Unpin / window focus logic not needed for expanded view collapse anymore */
    /* Mouse left the window — start collapsing flyout after a short grace */
    if (e->window.event == SDL_WINDOWEVENT_LEAVE && g_mp.flyout_active && g_mp.flyout_expanding) {
      g_mp.flyout_timer = SDL_GetTicks() + 400; /* 400ms grace before collapse */
    }
  }

  /* Flyout Hover Logic */
  if (eid == g_mp.window_id && !g_mp.is_dragging && !g_mp.animating) {
    if (e->type == SDL_MOUSEMOTION) {
      int current_w = g_mp.flyout_active ? g_mp.flyout_current_w : MP_COLLAPSED_W;
      int offset_x = get_bounds_offset_x(current_w);
      int cd_x = offset_x + ((g_mp.flyout_active && g_mp.flyout_direction == -1) ? (current_w - MP_COLLAPSED_W) : 0);

      if (e->motion.x >= cd_x && e->motion.x <= cd_x + MP_COLLAPSED_W) {
        if (!g_mp.flyout_active) {
          if (!g_mp.is_tucked) {
            start_flyout();
          }
        } else if (g_mp.flyout_expanding) {
          g_mp.flyout_timer = SDL_GetTicks() + 3000; /* reset timer on hover */
        }
      }
    }
  }

  /* Global Scroll to adjust volume inside mini player */
  if (e->type == SDL_MOUSEWHEEL && (eid == g_mp.window_id || eid == 0)) {
    float scroll_y = (float)e->wheel.y;
#if SDL_VERSION_ATLEAST(2, 0, 18)
    if (e->wheel.preciseY != 0.0f) scroll_y = e->wheel.preciseY;
#endif
    ctx->volume += (scroll_y > 0) ? 0.05f : -0.05f;
    if (ctx->volume < 0.0f)
      ctx->volume = 0.0f;
    if (ctx->volume > 1.0f)
      ctx->volume = 1.0f;
    audio_set_volume(ctx->volume);
    return true;
  }

  if ((e->type == SDL_MOUSEBUTTONDOWN || e->type == SDL_MOUSEBUTTONUP) && eid == g_mp.window_id) {
    int raw_mx = e->button.x;
    int raw_my = e->button.y;
    int current_w = g_mp.flyout_active ? g_mp.flyout_current_w : MP_COLLAPSED_W;
    int current_h = MP_COLLAPSED_H;
    int offset_x = get_bounds_offset_x(current_w);
    int offset_y = get_bounds_offset_y(current_h);
    int mx = raw_mx - offset_x;
    int my = raw_my - offset_y;

    if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_RIGHT) {
      /* Only process right clicks inside the local bounds */
      if (mx >= 0 && mx <= current_w && my >= 0 && my <= current_h) {
        if (ctx->state == PLAYER_STATE_PLAYING) {
          player_pause(ctx);
          audio_pause();
        } else if (ctx->state == PLAYER_STATE_PAUSED) {
          player_play(ctx);
          audio_resume();
        } else if (ctx->current_index >= 0 &&
                   (size_t)ctx->current_index < ctx->count) {
          Song *s = &ctx->songs[ctx->current_index];
          audio_play_file(s->path);
          player_play(ctx);
        }
        return true;
      }
    } else if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_LEFT) {
      /* We only care about mousedown on left clicks if WM returns NORMAL for hit test,
         we receive MOUSEBUTTONDOWN for the inner 25px hole perfectly. */
      int flyout_w = current_w - MP_COLLAPSED_W;
      int cd_local_x = 0;
      if (g_mp.flyout_active && g_mp.flyout_direction == -1) {
        cd_local_x += flyout_w;
      }
      
      /* Check if we clicked the CD part */
      if (mx >= cd_local_x && mx <= cd_local_x + MP_COLLAPSED_W) {
        int cd_cx_local = cd_local_x + MP_COLLAPSED_W / 2;
        int cd_cy_local = MP_COLLAPSED_H / 2;
        int dx = mx - cd_cx_local;
        int dy = my - cd_cy_local;
        int dist_sq = dx * dx + dy * dy;

        /* Verify it hit the inner target */
        if (dist_sq <= 25 * 25) {
            Uint32 now = SDL_GetTicks();
            if (now - g_mp.last_click_time < 350) {
              g_mp.click_count++;
            } else {
              g_mp.click_count = 1;
            }
            g_mp.last_click_time = now;
            
            if (g_mp.click_count == 2) {
              /* Double click registered! Skip Track */
              player_next(ctx);
              if (ctx->state == PLAYER_STATE_PLAYING ||
                  ctx->state == PLAYER_STATE_PAUSED) {
                Song *s = &ctx->songs[ctx->current_index];
                audio_play_file(s->path);
                player_play(ctx);
              }
            } else if (g_mp.click_count == 3) {
              /* Triple click registered! Undo the skip track and go back */
              player_prev(ctx);
              player_prev(ctx);
              if (ctx->state == PLAYER_STATE_PLAYING ||
                  ctx->state == PLAYER_STATE_PAUSED) {
                Song *s = &ctx->songs[ctx->current_index];
                audio_play_file(s->path);
                player_play(ctx);
              }
              g_mp.click_count = 0; /* reset */
            }
            return true;
        }
      }
    } else if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_MIDDLE) {
      if (mx >= 0 && mx <= current_w && my >= 0 && my <= current_h) {
        mini_player_toggle();
        return true;
      }
    }
  }

  return false;
}

/* ── Rendering ──────────────────────────────────────── */

void mini_player_render(PlayerContext *ctx) {
  if (!g_mp.visible || !g_mp.renderer || !g_mp.window)
    return;

  enforce_bounds();

  /* Set logical size BEFORE handling flyout so coordinate mapping is 1:1 with window pixels */
  ThemeColors *theme = material_get_theme();
  int w = MP_CONTAINER_W;
  int h = MP_CONTAINER_H;
  SDL_RenderSetLogicalSize(g_mp.renderer, w, h);

  /* Track change detection for Flyout */
  if (ctx->count > 0 && ctx->current_index >= 0 &&
      ctx->current_index < (int)ctx->count) {
    if (g_mp.last_index != ctx->current_index) {
      g_mp.last_index = ctx->current_index;
      
      if (!g_mp.flyout_active) {
        start_flyout();
      } else if (g_mp.flyout_expanding) {
        g_mp.flyout_timer = SDL_GetTicks() + 3000;
      }
    }
  }

  /* Flyout Animation & Auto-Collapse */
  if (g_mp.flyout_active && !g_mp.is_dragging && !g_mp.animating) {
    Uint32 now = SDL_GetTicks();
    
    if (g_mp.flyout_expanding) {
      float t = (float)(now - g_mp.flyout_anim_start) / 350.0f;
      if (t >= 1.0f) {
        t = 1.0f;
        g_mp.flyout_current_w = g_mp.flyout_target_w;
      } else {
        float ease = 1.0f - powf(1.0f - t, 3.0f); /* cubic ease out */
        g_mp.flyout_current_w = g_mp.flyout_start_w + (int)((g_mp.flyout_target_w - g_mp.flyout_start_w) * ease);
      }
      
      if (t >= 1.0f) {
        if (SDL_GetMouseFocus() != g_mp.window) {
          if (now >= g_mp.flyout_timer) {
            g_mp.flyout_expanding = false;
            g_mp.flyout_anim_start = now;
            g_mp.flyout_start_w = g_mp.flyout_current_w;
            g_mp.flyout_target_w = MP_COLLAPSED_W;
          }
        } else {
          g_mp.flyout_timer = now + 400;
        }
      }
    } else {
      /* Collapsing */
      float t = (float)(now - g_mp.flyout_anim_start) / 250.0f;
      if (t >= 1.0f) {
        t = 1.0f;
        g_mp.flyout_current_w = MP_COLLAPSED_W;
        g_mp.flyout_active = false;
        if (g_mp.flyout_direction == -1) {
          SDL_SetWindowPosition(g_mp.window, g_mp.pill_x, g_mp.pill_y);
        }
      } else {
        float ease = t * t * t; /* cubic ease in */
        g_mp.flyout_current_w = g_mp.flyout_start_w + (int)((g_mp.flyout_target_w - g_mp.flyout_start_w) * ease);
      }
    }
  }

  /* Handle resizing animation bounds (expanding/collapsing) */
  if (g_mp.animating) {
    Uint32 now = SDL_GetTicks();
    float t = (float)(now - g_mp.anim_start) / 150.0f; /* 150ms bounce animation */
    
    if (t >= 1.0f) {
      t = 1.0f;
      g_mp.animating = false;
      if (g_mp.target_w == g_mp.start_w) {
        g_mp.pill_x = g_mp.target_x + ((g_mp.flyout_active && g_mp.flyout_direction == -1) ? (g_mp.target_w - MP_COLLAPSED_W) : 0);
        g_mp.pill_y = g_mp.target_y;
      }
      /* Sync internal offsets for tracking boundaries (to align dragging) */
      SDL_SetWindowPosition(g_mp.window, g_mp.target_x, g_mp.target_y);
      
      /* If we just finished untucking, check if we should start the flyout */
      if (!g_mp.is_tucked && !g_mp.flyout_active) {
        int global_mx, global_my;
        SDL_GetGlobalMouseState(&global_mx, &global_my);
        int wx, wy;
        SDL_GetWindowPosition(g_mp.window, &wx, &wy);
        int visual_x = wx + get_bounds_offset_x(MP_COLLAPSED_W);
        int visual_y = wy + get_bounds_offset_y(MP_COLLAPSED_H);
        int local_mx = global_mx - visual_x;
        int local_my = global_my - visual_y;
        int cd_radius = MP_COLLAPSED_W / 2;
        int dx = local_mx - cd_radius;
        int dy = local_my - cd_radius;
        if (dx * dx + dy * dy <= cd_radius * cd_radius) {
          start_flyout();
        }
      }
    } else {
      float ease = 1.0f - (1.0f - t) * (1.0f - t); /* simple ease-out */
      int cur_x = g_mp.start_x + (int)((g_mp.target_x - g_mp.start_x) * ease);
      int cur_y = g_mp.start_y + (int)((g_mp.target_y - g_mp.start_y) * ease);
      
      if (g_mp.target_w == g_mp.start_w) {
        g_mp.pill_x = cur_x + ((g_mp.flyout_active && g_mp.flyout_direction == -1) ? (g_mp.target_w - MP_COLLAPSED_W) : 0);
        g_mp.pill_y = cur_y;
      }
      SDL_SetWindowPosition(g_mp.window, cur_x, cur_y);
    }
  }

  /* Since the layout lives *inside* the 400x400 container, establish offset */
  int current_w = g_mp.flyout_active ? g_mp.flyout_current_w : MP_COLLAPSED_W;
  int current_h = MP_COLLAPSED_H;

  if (g_mp.animating) {
    current_w = g_mp.start_w + (int)((g_mp.target_w - g_mp.start_w) * (1.0f - powf(1.0f - (float)(SDL_GetTicks() - g_mp.anim_start) / 150.0f, 2.0f)));
    current_h = g_mp.start_h + (int)((g_mp.target_h - g_mp.start_h) * (1.0f - powf(1.0f - (float)(SDL_GetTicks() - g_mp.anim_start) / 150.0f, 2.0f)));
  }

  int offset_x = get_bounds_offset_x(current_w);
  int offset_y = get_bounds_offset_y(current_h);

  /* Pure container clear */
  SDL_SetRenderDrawColor(g_mp.renderer, 0, 0, 0, 0);
  SDL_RenderClear(g_mp.renderer);

  /* ── CD and Flyout rendering ────────────────────────── */
  {
    Song *s = NULL;
    if (ctx->count > 0 && ctx->current_index >= 0 &&
        ctx->current_index < (int)ctx->count) {
      s = &ctx->songs[ctx->current_index];
    }

    if (s && s->art_path[0] != '\0') {
      if (strncmp(g_mp.current_art_path, s->art_path, MAX_PATH_LENGTH) != 0) {
        if (g_mp.art_texture) {
          SDL_DestroyTexture(g_mp.art_texture);
          g_mp.art_texture = NULL;
        }
        if (g_mp.blurred_bg_texture) {
          SDL_DestroyTexture(g_mp.blurred_bg_texture);
          g_mp.blurred_bg_texture = NULL;
        }

        g_mp.art_texture = load_texture_with_blurred_bg(
            g_mp.renderer, s->art_path, NULL, &g_mp.blurred_bg_texture);

        if (g_mp.art_texture) {
          SDL_SetTextureScaleMode(g_mp.art_texture, SDL_ScaleModeLinear);
        }

        strncpy(g_mp.current_art_path, s->art_path, MAX_PATH_LENGTH - 1);
        g_mp.current_art_path[MAX_PATH_LENGTH - 1] = '\0';
      }
    } else {
      if (g_mp.art_texture) {
        SDL_DestroyTexture(g_mp.art_texture);
        g_mp.art_texture = NULL;
      }
      if (g_mp.blurred_bg_texture) {
        SDL_DestroyTexture(g_mp.blurred_bg_texture);
        g_mp.blurred_bg_texture = NULL;
      }
      g_mp.current_art_path[0] = '\0';
    }

    /* Flyout rendering layer (under CD) */
    if (g_mp.flyout_active) {
      int pr = theme->primary.r;
      int pg = theme->primary.g;
      int pb = theme->primary.b;
      int flyout_w = g_mp.flyout_current_w - MP_COLLAPSED_W;
      if (flyout_w > 0) {
        int cd_cx_static = (g_mp.flyout_direction == -1) ? (offset_x + current_w - MP_COLLAPSED_W / 2) : (offset_x + MP_COLLAPSED_W / 2);
        int cd_radius = MP_COLLAPSED_W / 2;
        int cd_cy = offset_y + MP_COLLAPSED_H / 2;
        int start_cx, end_cx, rect_x;
        
        if (g_mp.flyout_direction == 1) {
          start_cx = cd_cx_static;
          end_cx = start_cx + flyout_w;
          rect_x = start_cx;
        } else {
          start_cx = cd_cx_static;
          end_cx = start_cx - flyout_w;
          rect_x = end_cx;
        }
        
        /* Shadow */
        mp_fill_circle(end_cx, cd_cy, cd_radius + 2, 0, 0, 0, 40);
        mp_fill_rect(rect_x, cd_cy - (cd_radius + 2), flyout_w, (cd_radius + 2) * 2, 0, 0, 0, 40);
        
        /* Pill body (matches CD radius) — single clean surface, no dark inner box */
        mp_fill_circle(end_cx, cd_cy, cd_radius - 1, pr, pg, pb, 230);
        mp_fill_rect(rect_x, cd_cy - (cd_radius - 1), flyout_w, (cd_radius - 1) * 2, pr, pg, pb, 230);

        /* Text Rendering with Marquee Scroll */
        int inn = cd_radius - 3;
        if (flyout_w > 30 && s) {
          /* Clip to flyout bounds */
          SDL_Rect clip;
          int text_area_w;
          if (g_mp.flyout_direction == 1) {
            clip.x = rect_x + cd_radius;
            clip.y = cd_cy - inn;
            clip.w = flyout_w - cd_radius + inn - 10;
            clip.h = inn * 2;
          } else {
            clip.x = rect_x - inn + 10;
            clip.y = cd_cy - inn;
            clip.w = flyout_w - cd_radius + inn - 10;
            clip.h = inn * 2;
          }
          if (clip.w < 0) clip.w = 0;
          text_area_w = clip.w;
          SDL_RenderSetClipRect(g_mp.renderer, &clip);

          /* Compute per-line scroll: only scroll lines that actually overflow */
          int title_w = mp_text_width(s->title);
          int artist_w = mp_text_width(s->artist);

          float title_scroll = 0.0f;
          float artist_scroll = 0.0f;

          if (g_mp.flyout_expanding) {
            Uint32 now = SDL_GetTicks();
            int elapsed = (int)(now - g_mp.flyout_scroll_start);
            if (elapsed > 1000) { /* 1s delay before scrolling starts */
              float scroll_time = (float)(elapsed - 1000) / 1000.0f;
              float raw_scroll = scroll_time * 30.0f; /* 30px per second */

              if (title_w > text_area_w) {
                float title_overflow = (float)(title_w - text_area_w + 20);
                title_scroll = raw_scroll;
                if (title_scroll > title_overflow) title_scroll = title_overflow;
              }
              if (artist_w > text_area_w) {
                float artist_overflow = (float)(artist_w - text_area_w + 20);
                artist_scroll = raw_scroll;
                if (artist_scroll > artist_overflow) artist_scroll = artist_overflow;
              }
            }
          }

          if (g_mp.flyout_direction == 1) {
            int base_x = start_cx + cd_radius + 10;
            mp_draw_text(s->title, base_x - (int)title_scroll, cd_cy - 2, 255, 255, 255);
            mp_draw_text(s->artist, base_x - (int)artist_scroll, cd_cy + 18, 180, 180, 180);
          } else {
            int origin_x = start_cx - cd_radius - 10;
            int title_x = origin_x - title_w + (int)title_scroll;
            int artist_x = origin_x - artist_w + (int)artist_scroll;
            if (title_x < clip.x) title_x = clip.x;
            if (artist_x < clip.x) artist_x = clip.x;
            mp_draw_text(s->title, title_x, cd_cy - 2, 255, 255, 255);
            mp_draw_text(s->artist, artist_x, cd_cy + 18, 180, 180, 180);
          }

          SDL_RenderSetClipRect(g_mp.renderer, NULL);
        }
      }
    }

    if (g_mp.art_texture) {
      float angle = ctx->position * 45.0f; /* 45 degrees per second */

      /* Outer edge shadow ring to give depth underneath the CD */
      int flyout_w = current_w - MP_COLLAPSED_W;
      
      int cd_cx = offset_x + MP_COLLAPSED_W / 2;
      if (g_mp.flyout_direction == -1) cd_cx += flyout_w;
      
      int cd_cy = offset_y + MP_COLLAPSED_H / 2;
      mp_fill_circle(cd_cx, cd_cy, 38, 0, 0, 0, 40);

      /* Draw the CD using Anti-Aliased Geometry */
      draw_aa_textured_circle(cd_cx, cd_cy, 37, g_mp.art_texture, angle);

      /* CD Cutout / Shading */
      mp_fill_circle(cd_cx, cd_cy, 10, 0, 0, 0, 100);
      mp_fill_circle(cd_cx, cd_cy, 9, theme->primary.r, theme->primary.g, theme->primary.b, 255);
      mp_fill_circle(cd_cx, cd_cy, 6, 24, 24, 24, 255);
    } else {
      int flyout_w = current_w - MP_COLLAPSED_W;
      int cd_cx = offset_x + MP_COLLAPSED_W / 2;
      if (g_mp.flyout_direction == -1) cd_cx += flyout_w;
      
      int cd_cy = offset_y + MP_COLLAPSED_H / 2;
      /* Fallback to themed circle if no art */
      mp_fill_circle(cd_cx, cd_cy, 38, theme->primary.r, theme->primary.g,
                     theme->primary.b, 255);

      /* Play/Pause icon */
      if (ctx->state == PLAYER_STATE_PLAYING) {
        /* Pause bars */
        mp_fill_rect(cd_cx - 9, cd_cy - 11, 6, 22, 255, 255, 255, 240);
        mp_fill_rect(cd_cx + 3, cd_cy - 11, 6, 22, 255, 255, 255, 240);
      } else {
        /* Play triangle (scanline fill) */
        int cx = cd_cx + 3;
        int cy = cd_cy;
        SDL_SetRenderDrawBlendMode(g_mp.renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(g_mp.renderer, 255, 255, 255, 240);
        for (int dy = -11; dy <= 11; dy++) {
          float t = (float)(dy + 11) / 22.0f;
          int x_right;
          if (t <= 0.5f)
            x_right = cx - 6 + (int)(18.0f * t * 2.0f);
          else
            x_right = cx - 6 + (int)(18.0f * (1.0f - t) * 2.0f);
          SDL_RenderDrawLine(g_mp.renderer, cx - 6, cy + dy, x_right, cy + dy);
        }
      }
    }

    update_window_shape();
    SDL_RenderPresent(g_mp.renderer);
    return;
  }

  /* ── Expanded mode ─────────────────────────── */

  /* Get mouse for hover effects */
  int mx, my;
  SDL_GetMouseState(&mx, &my);
  
  /* Translating hover state */
  mx -= offset_x;
  my -= offset_y;

  /* Top accent bar */
  mp_fill_rect(offset_x, offset_y, current_w, 3, theme->primary.r, theme->primary.g, theme->primary.b,
               120);

  /* Close button (top-left: X mark) */
  {
    bool hover = point_in_rect(mx, my, 5, 5, 30, 30);
    if (hover) {
      SDL_SetRenderDrawColor(g_mp.renderer, 250, 80, 80, 255);
    } else {
      SDL_SetRenderDrawColor(g_mp.renderer, 160, 160, 160, 220);
    }
    /* X shape */
    SDL_RenderDrawLine(g_mp.renderer, offset_x + 12, offset_y + 12, offset_x + 22, offset_y + 22);
    SDL_RenderDrawLine(g_mp.renderer, offset_x + 22, offset_y + 12, offset_x + 12, offset_y + 22);
    SDL_RenderDrawLine(g_mp.renderer, offset_x + 13, offset_y + 12, offset_x + 23, offset_y + 22);
    SDL_RenderDrawLine(g_mp.renderer, offset_x + 23, offset_y + 12, offset_x + 13, offset_y + 22);
  }

  Song *s = NULL;
  if (ctx->count > 0 && ctx->current_index >= 0 &&
      ctx->current_index < (int)ctx->count) {
    s = &ctx->songs[ctx->current_index];
  }

  /* ── Track Info ─────────────────────────────── */
  if (s) {
    int max_w = current_w - 60;

    /* Title */
    int tw = mp_text_width(s->title);
    if (tw > max_w) {
      char buf[128];
      snprintf(buf, sizeof(buf), "%.40s...", s->title);
      mp_draw_text(buf, offset_x + 16, offset_y + 40, 255, 255, 255);
    } else {
      mp_draw_text(s->title, offset_x + 16, offset_y + 40, 255, 255, 255);
    }

    /* Artist */
    int aw = mp_text_width(s->artist);
    if (aw > max_w) {
      char buf[128];
      snprintf(buf, sizeof(buf), "%.40s...", s->artist);
      mp_draw_text(buf, offset_x + 16, offset_y + 65, 160, 160, 160);
    } else {
      mp_draw_text(s->artist, offset_x + 16, offset_y + 65, 160, 160, 160);
    }
  } else {
    mp_draw_text("No Track", offset_x + 16, offset_y + 40, 100, 100, 100);
  }

  /* ── Seek Bar ───────────────────────────────── */
  {
    int bar_x = offset_x + 16;
    int bar_y = offset_y + 90;
    int bar_w = current_w - 32;
    int bar_h = 4;

    /* Track */
    mp_fill_rect(bar_x, bar_y, bar_w, bar_h, 50, 50, 50, 200);

    /* Progress */
    float progress = 0.0f;
    if (ctx->duration > 0.0f) {
      progress = ctx->position / ctx->duration;
      if (progress > 1.0f)
        progress = 1.0f;
    }
    int fill_w = (int)(bar_w * progress);
    if (fill_w > 0) {
      mp_fill_rect(bar_x, bar_y, fill_w, bar_h, theme->primary.r,
                   theme->primary.g, theme->primary.b, 230);
    }

    /* Time text */
    int pos_min = (int)ctx->position / 60;
    int pos_sec = (int)ctx->position % 60;
    int dur_min = (int)ctx->duration / 60;
    int dur_sec = (int)ctx->duration % 60;
    char time_buf[32];
    snprintf(time_buf, sizeof(time_buf), "%d:%02d / %d:%02d", pos_min, pos_sec,
             dur_min, dur_sec);
    mp_draw_text(time_buf, bar_x, bar_y + 22, 100, 100, 100);
  }

  /* No extra UI rendered in collapsed form */

  update_window_shape();
  SDL_RenderPresent(g_mp.renderer);
}
