#ifndef MATERIAL_RENDERER_H
#define MATERIAL_RENDERER_H

#include "color_extractor.h" /* For ThemeColors */
#include "image_loader.h"
#include "layout.h"
#include "player.h"
#include "window.h" /* For EventType if needed, but mostly for renderer ptr */

/* Initialize resources (fonts, etc if we added them) */
void material_init(SDL_Renderer *renderer);

/* Clean up resources (textures, etc) on shutdown or when going headless */
void material_shutdown(void);

/* Update the background texture from a new image file */
void material_set_background(const char *image_path);

/* Render the full Material Design UI for the current frame */
void material_render(PlayerContext *ctx, WindowContext *layout);

/* Hit testing for the new layout. Returns command string or NULL */
const char *material_hit_test(PlayerContext *ctx, WindowContext *layout, int mx,
                              int my, bool is_right_click);

/* Helper to expose the current theme to other UI elements (like Toasts) */
ThemeColors *material_get_theme(void);

/* Handle input events for debug shortcuts (e.g. F3) */
void material_handle_input(SDL_Event *e);

/* Helper to draw rounded, alpha-blended rectangles.
 * Exposed for reuse by overlays/toasts.
 */
void material_draw_rounded_rect(int x, int y, int w, int h, int r, Color c,
                                Uint8 alpha);

#endif /* MATERIAL_RENDERER_H */
