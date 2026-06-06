#ifndef DATABASE_H
#define DATABASE_H

#include "common.h"
#include <sqlite3.h>
#include <stddef.h>

/* Must match EQ_BAND_COUNT_MAX in audio_backend.h */
#define EQ_BAND_COUNT_MAX 10

typedef struct {
  int id;
  char name[256];
} Artist;

typedef struct Album {
  int id;
  char name[256];
  int artist_id;
  char artist[256];
  char art_filename[256];
  int year;
  bool is_favorite;
} Album;

typedef struct {
  int id;
  char filepath[MAX_PATH_LENGTH];
  char title[MAX_SONG_TITLE];
  char artist[MAX_SONG_TITLE];
  int album_id;
  int artist_id;
  float duration;
  int track_number;
  int disc_number;
  int year;
  char genre[64];
  char art_filename[MAX_PATH_LENGTH];
  bool is_favorite;
} Track;

typedef struct {
  char path[MAX_PATH_LENGTH];
} LibraryPath;

typedef struct {
  int id;
  char name[256];
  int track_count;
} Playlist;

Result db_init(const char *db_path);
Result db_close(void);

/* Library Paths */
Result db_add_library_path(const char *path);
Result db_get_library_paths(LibraryPath *out_paths, size_t *count);
Result db_remove_library_path(const char *path);
Result db_clear_library(void);

/* Playlist Management */
Result db_create_playlist(const char *name);
Result db_add_track_to_playlist(int playlist_id, int track_id);
Result db_get_playlists(Playlist *out_playlists, size_t *count);
Result db_get_playlist_tracks(int playlist_id, Track *out_tracks,
                              size_t *count);

/* Library queries */
Result db_add_track(const Track *track);
Result db_get_all_tracks(Track *out_tracks, size_t *count);
Result db_get_all_albums(Album *out_albums, size_t *count, bool consolidate);
Result db_get_tracks_by_album_name(const char *album_name, Track *out_tracks,
                                   size_t *count);
Result db_get_tracks_by_album(int album_id, Track *out_tracks, size_t *count);
Result db_get_tracks_by_artist_deep(const char *artist_name, Track *out_tracks,
                                    size_t *count);
Result db_get_track_by_path(const char *path, Track *out_track);
Result db_remove_stale_tracks(void); /* Remove tracks whose files no longer exist */

/* Artist/Album Resolution */
int db_get_or_create_artist(const char *name);
int db_get_or_create_album(const char *name, int artist_id, int year);
Result db_update_album_art(int album_id, const char *art_filename);
Result db_toggle_album_favorite(int album_id, bool *out_is_favorite);
Result db_toggle_track_favorite(int track_id, bool *out_is_favorite);

/* Transaction Support */
Result db_begin_transaction(void);
Result db_commit(void);

/* Visualizer Settings Persistence */
Result db_save_visualizer_param(const char *vis_name, const char *param_name,
                                int value_type, float value_float,
                                int value_int);
Result db_load_visualizer_params(const char *vis_name,
                                 void (*callback)(const char *param_name,
                                                  int value_type,
                                                  float value_float,
                                                  int value_int));

/* Settings Persistence */
Result db_save_setting(const char *key, const char *value);
Result db_get_setting(const char *key, char *out_value, size_t max_len);

/* Playlist Management */
Result db_delete_playlist(int playlist_id);
Result db_rename_playlist(int playlist_id, const char *new_name);
Result db_remove_track_from_playlist(int playlist_id, int track_id);

/* =========================================================================
   EQ Preset Persistence
   Gains are stored for EQ_BAND_COUNT_MAX bands even in 5-band mode
   (unused bands are stored as 0.0).
   ========================================================================= */

typedef struct {
  int id;
  char name[64];
  float gains[EQ_BAND_COUNT_MAX]; /* dB per band, -12..+12 */
  int band_count;                 /* 5 or 10 */
} EqPreset;

Result db_save_eq_preset(const char *name, const float *gains, int band_count);
Result db_delete_eq_preset(int id);
Result db_get_eq_presets(EqPreset *out, size_t *count);

#endif /* DATABASE_H */
