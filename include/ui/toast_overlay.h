#ifndef TOAST_OVERLAY_H
#define TOAST_OVERLAY_H

#include <SDL2/SDL.h>
#include <stdbool.h>

typedef enum { TOAST_INFO, TOAST_ERROR, TOAST_PROGRESS } ToastType;

/* Initialize the toast system */
void toast_init(void);

/* Show a new toast notification.
 * Overwrites any existing toast for now (single toast channel).
 * Duration is in milliseconds. Use -1 for infinite (until manually dismissed or
 * replaced).
 */
void toast_show(const char *message, ToastType type, int duration_ms);

/* Update the progress of the current toast if it is of type TOAST_PROGRESS.
 * Progress is 0.0 to 1.0.
 */
void toast_update_progress(float progress, const char *message);

/* Render the active toast overlay.
 * Should be called at the end of the main render loop.
 */
void toast_render(SDL_Renderer *renderer, int window_w, int window_h);

/* Check if a specific type of toast is currently active */
bool toast_is_active(ToastType type);

#endif /* TOAST_OVERLAY_H */
