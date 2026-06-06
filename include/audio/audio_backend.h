#ifndef AUDIO_BACKEND_H
#define AUDIO_BACKEND_H

#include "common.h"

/* Maximum number of EQ bands supported */
#define EQ_BAND_COUNT_MAX 10

/* Initialize the audio subsystem */
Result audio_init(void);

/* Shutdown the audio subsystem */
void audio_shutdown(void);

/* Play a file from path */
Result audio_play_file(const char *filepath);

/* Play/Pause/Stop controls for the current engine */
void audio_stop(void);
void audio_pause(void);
void audio_resume(void);

/* Check if currently playing */
bool audio_is_playing(void);

/* Set Volume (0.0 to 1.0) */
void audio_set_volume(float volume);

/* Get total duration in seconds */
float audio_get_duration(void);

/* Get current position in seconds */
float audio_get_position(void);

/* Seek to position in seconds */
void audio_seek(float seconds);

/* Check if current track has finished playing */
bool audio_is_finished(void);

/* =========================================================================
   EQUALIZER API
   ========================================================================= */

/* Set gain (in dB, range -12.0 to +12.0) for a specific EQ band.
   band_index: 0..9 (5-band mode uses 0..4, 10-band uses 0..9)
   band_count: 5 or 10 — controls which center frequency table is used. */
void audio_set_eq_band(int band_index, float gain_db, int band_count);

/* Enable or disable the entire EQ chain (bypass when disabled). */
void audio_set_eq_enabled(bool enabled);

/* Reset all EQ bands to 0 dB (flat). */
void audio_eq_reset(void);

#endif /* AUDIO_BACKEND_H */
