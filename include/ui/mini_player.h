#ifndef MINI_PLAYER_H
#define MINI_PLAYER_H

#include "player.h"
#include <SDL2/SDL.h>
#include <stdbool.h>

/* Initialize the mini player (does NOT create the window yet) */
void mini_player_init(void);

/* Toggle visibility: creates/destroys the external window */
void mini_player_toggle(void);

/* Query if mini player window is currently visible */
bool mini_player_is_active(void);

/* Process SDL events targeted at the mini player window.
 * Returns true if the event was consumed. */
bool mini_player_handle_event(SDL_Event *e, PlayerContext *ctx);

/* Render one frame of the mini player window */
void mini_player_render(PlayerContext *ctx);

/* Destroy the mini player window and free resources */
void mini_player_shutdown(void);

#endif /* MINI_PLAYER_H */
