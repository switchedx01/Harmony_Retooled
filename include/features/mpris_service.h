#ifndef MPRIS_SERVICE_H
#define MPRIS_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

/* Callbacks for player control actions */
typedef struct {
  void (*on_play)(void);
  void (*on_pause)(void);
  void (*on_next)(void);
  void (*on_prev)(void);
  void (*on_seek)(int64_t offset_us);
  void (*on_set_position)(int64_t position_us);
  void (*on_raise)(void);
  void (*on_quit)(void);
  void (*on_open_uri)(const char *uri);
} MprisCallbacks;

/* Initialize MPRIS service. Returns true on success. */
bool mpris_init(const MprisCallbacks *callbacks);

/* Clean up MPRIS service */
void mpris_shutdown(void);

/* Process pending DBus events. Call this in the main loop. */
void mpris_process(void);

/* Update metadata */
void mpris_update_metadata(const char *title, const char *artist,
                           const char *album, const char *art_url,
                           int64_t duration_us);

/* Update playback status */
void mpris_update_playback_status(bool is_playing);

/* Update position */
void mpris_update_position(int64_t position_us);

#endif /* MPRIS_SERVICE_H */
