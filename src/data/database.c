#include "database.h"
#include "common.h"
#include "logging.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "string_utils.h"

static sqlite3 *g_db = NULL;

Result db_init(const char *db_path) {
  int rc = sqlite3_open(db_path, &g_db);
  if (rc) {
    log_message("ERROR", "Can't open database");
    return RESULT_ERROR_GENERIC;
  }

  /* Set busy timeout to 5 seconds to handle locking */
  sqlite3_busy_timeout(g_db, 5000);

  /* Enable WAL mode for better concurrency */
  if (sqlite3_exec(g_db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL) !=
      SQLITE_OK) {
    log_message("WARN", "Failed to set WAL mode");
  }
  if (sqlite3_exec(g_db, "PRAGMA synchronous=NORMAL;", NULL, NULL, NULL) !=
      SQLITE_OK) {
    log_message("WARN", "Failed to set synchronous mode");
  }

  char *err_msg = NULL;
  /* Create Tables */
  const char *sql = "CREATE TABLE IF NOT EXISTS artists ("
                    "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                    "name TEXT UNIQUE);"
                    "CREATE TABLE IF NOT EXISTS albums ("
                    "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                    "name TEXT, "
                    "artist_id INTEGER, "
                    "year INTEGER, "
                    "art_filename TEXT, "
                    "is_favorite INTEGER DEFAULT 0, "
                    "UNIQUE(name, artist_id));"
                    "CREATE TABLE IF NOT EXISTS tracks ("
                    "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                    "filepath TEXT UNIQUE, "
                    "title TEXT, "
                    "album_id INTEGER, "
                    "artist_id INTEGER, "
                    "duration REAL, "
                    "track_number INTEGER, "
                    "disc_number INTEGER, "
                    "year INTEGER, "
                    "genre TEXT);"
                    "CREATE TABLE IF NOT EXISTS library_paths ("
                    "path TEXT UNIQUE);"
                    "CREATE TABLE IF NOT EXISTS visualizer_settings ("
                    "visualizer_name TEXT NOT NULL, "
                    "param_name TEXT NOT NULL, "
                    "value_type INTEGER NOT NULL, "
                    "value_float REAL, "
                    "value_int INTEGER, "
                    "PRIMARY KEY (visualizer_name, param_name));"
                    "CREATE TABLE IF NOT EXISTS playlists ("
                    "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                    "name TEXT UNIQUE);"
                    "CREATE TABLE IF NOT EXISTS playlist_tracks ("
                    "playlist_id INTEGER, "
                    "track_id INTEGER, "
                    "sequence INTEGER, "
                    "FOREIGN KEY(playlist_id) REFERENCES playlists(id), "
                    "FOREIGN KEY(track_id) REFERENCES tracks(id));"
                    "CREATE TABLE IF NOT EXISTS settings ("
                    "key TEXT PRIMARY KEY, "
                    "value TEXT);"
                    "CREATE TABLE IF NOT EXISTS eq_presets ("
                    "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                    "name TEXT UNIQUE, "
                    "gains TEXT NOT NULL, "
                    "band_count INTEGER NOT NULL DEFAULT 5);";

  rc = sqlite3_exec(g_db, sql, 0, 0, &err_msg);
  if (rc != SQLITE_OK) {
    char msg[512];
    snprintf(msg, sizeof(msg), "Database init error: %s", err_msg);
    log_message("ERROR", msg);
    sqlite3_free(err_msg);
    return RESULT_ERROR_GENERIC;
  }

  /* Migration: Add is_favorite column if it doesn't exist */
  sqlite3_exec(g_db,
               "ALTER TABLE albums ADD COLUMN is_favorite INTEGER DEFAULT 0;",
               NULL, NULL, NULL);
  /* Migration: Add is_favorite column to tracks if it doesn't exist */
  sqlite3_exec(g_db,
               "ALTER TABLE tracks ADD COLUMN is_favorite INTEGER DEFAULT 0;",
               NULL, NULL, NULL);

  /* Migration: Add band_count column to eq_presets if it doesn't exist */
  sqlite3_exec(g_db,
               "ALTER TABLE eq_presets ADD COLUMN band_count INTEGER NOT NULL DEFAULT 5;",
               NULL, NULL, NULL);

  log_message("INFO", "Database initialized successfully");
  return RESULT_SUCCESS;
}

Result db_close(void) {
  if (g_db) {
    sqlite3_close(g_db);
    g_db = NULL;
  }
  return RESULT_SUCCESS;
}

/* ... (other functions would be here, but I must provide FULL file logic) ...
 */
/* Using what I saw in previous views to reconstruct */

int db_get_or_create_artist(const char *name) {
  if (!c_assert(name != NULL) || !c_assert(g_db != NULL))
    return -1;

  sqlite3_stmt *stmt;
  const char *sql_sel = "SELECT id FROM artists WHERE name = ?;";
  int rc = sqlite3_prepare_v2(g_db, sql_sel, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    log_message("ERROR", "Failed to prepare select artist statement");
    return -1;
  }

  rc = sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
  if (!c_assert(rc == SQLITE_OK)) {
    sqlite3_finalize(stmt);
    return -1;
  }

  int id = -1;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    id = sqlite3_column_int(stmt, 0);
  }
  sqlite3_finalize(stmt);

  if (id != -1)
    return id;

  const char *sql_ins = "INSERT INTO artists (name) VALUES (?);";
  rc = sqlite3_prepare_v2(g_db, sql_ins, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    log_message("ERROR", "Failed to prepare insert artist statement");
    return -1;
  }

  rc = sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
  if (!c_assert(rc == SQLITE_OK)) {
    sqlite3_finalize(stmt);
    return -1;
  }

  if (sqlite3_step(stmt) == SQLITE_DONE) {
    id = (int)sqlite3_last_insert_rowid(g_db);
  }
  sqlite3_finalize(stmt);
  return id;
}

int db_get_or_create_album(const char *name, int artist_id, int year) {
  if (!c_assert(name != NULL) || !c_assert(g_db != NULL))
    return -1;

  sqlite3_stmt *stmt;
  const char *sql_sel =
      "SELECT id FROM albums WHERE name = ? AND artist_id = ?;";

  if (sqlite3_prepare_v2(g_db, sql_sel, -1, &stmt, NULL) != SQLITE_OK)
    return -1;

  sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 2, artist_id);

  int id = -1;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    id = sqlite3_column_int(stmt, 0);
  }
  sqlite3_finalize(stmt);

  if (id != -1)
    return id;

  const char *sql_ins =
      "INSERT INTO albums (name, artist_id, year, art_filename) "
      "VALUES (?, ?, ?, '');";
  if (sqlite3_prepare_v2(g_db, sql_ins, -1, &stmt, NULL) != SQLITE_OK)
    return -1;

  sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 2, artist_id);
  sqlite3_bind_int(stmt, 3, year);
  if (sqlite3_step(stmt) == SQLITE_DONE) {
    id = (int)sqlite3_last_insert_rowid(g_db);
  }
  sqlite3_finalize(stmt);
  return id;
}

Result db_update_album_art(int album_id, const char *art_filename) {
  if (!g_db)
    return RESULT_ERROR_GENERIC;
  const char *sql = "UPDATE albums SET art_filename = ? WHERE id = ?;";
  sqlite3_stmt *stmt;
  sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
  sqlite3_bind_text(stmt, 1, art_filename, -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 2, album_id);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  return RESULT_SUCCESS;
}

Result db_toggle_album_favorite(int album_id, bool *out_is_favorite) {
  if (!g_db)
    return RESULT_ERROR_GENERIC;

  sqlite3_stmt *stmt;
  const char *sql_sel = "SELECT is_favorite FROM albums WHERE id = ?;";
  if (sqlite3_prepare_v2(g_db, sql_sel, -1, &stmt, NULL) != SQLITE_OK)
    return RESULT_ERROR_GENERIC;

  sqlite3_bind_int(stmt, 1, album_id);
  int is_fav = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    is_fav = sqlite3_column_int(stmt, 0);
  }
  sqlite3_finalize(stmt);

  is_fav = !is_fav;
  const char *sql_upd = "UPDATE albums SET is_favorite = ? WHERE id = ?;";
  if (sqlite3_prepare_v2(g_db, sql_upd, -1, &stmt, NULL) != SQLITE_OK)
    return RESULT_ERROR_GENERIC;

  sqlite3_bind_int(stmt, 1, is_fav);
  sqlite3_bind_int(stmt, 2, album_id);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (out_is_favorite)
    *out_is_favorite = (bool)is_fav;
  return RESULT_SUCCESS;
}

Result db_toggle_track_favorite(int track_id, bool *out_is_favorite) {
  if (!g_db)
    return RESULT_ERROR_GENERIC;

  sqlite3_stmt *stmt;
  const char *sql_sel = "SELECT is_favorite FROM tracks WHERE id = ?;";
  if (sqlite3_prepare_v2(g_db, sql_sel, -1, &stmt, NULL) != SQLITE_OK)
    return RESULT_ERROR_GENERIC;

  sqlite3_bind_int(stmt, 1, track_id);
  int is_fav = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    is_fav = sqlite3_column_int(stmt, 0);
  }
  sqlite3_finalize(stmt);

  is_fav = !is_fav;
  const char *sql_upd = "UPDATE tracks SET is_favorite = ? WHERE id = ?;";
  if (sqlite3_prepare_v2(g_db, sql_upd, -1, &stmt, NULL) != SQLITE_OK)
    return RESULT_ERROR_GENERIC;

  sqlite3_bind_int(stmt, 1, is_fav);
  sqlite3_bind_int(stmt, 2, track_id);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (out_is_favorite)
    *out_is_favorite = (bool)is_fav;
  return RESULT_SUCCESS;
}

Result db_add_track(const Track *track) {
  if (!c_assert(g_db != NULL) || !c_assert(track != NULL))
    return RESULT_ERROR_GENERIC;

  const char *sql = "INSERT OR REPLACE INTO tracks (filepath, title, album_id, "
                    "artist_id, duration, track_number, disc_number, year, "
                    "genre) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);";
  sqlite3_stmt *stmt;
  if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    log_message("ERROR", "db_add_track: Failed to prepare statement");
    return RESULT_ERROR_GENERIC;
  }

  /* P10 Rule 7: Check return values. Although for binding, we usually assume
     success if prepare worked, strict compliance requires validation. We can
     chain them or macro checking, but here explicitly checking is slightly
     verbose. We will check the first one and step. */

  if (sqlite3_bind_text(stmt, 1, track->filepath, -1, SQLITE_STATIC) !=
      SQLITE_OK)
    goto error;
  sqlite3_bind_text(stmt, 2, track->title, -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 3, track->album_id);
  sqlite3_bind_int(stmt, 4, track->artist_id);
  sqlite3_bind_double(stmt, 5, track->duration);
  sqlite3_bind_int(stmt, 6, track->track_number);
  sqlite3_bind_int(stmt, 7, track->disc_number);
  sqlite3_bind_int(stmt, 8, track->year);
  sqlite3_bind_text(stmt, 9, track->genre, -1, SQLITE_STATIC);

  int rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    char msg[512];
    snprintf(msg, sizeof(msg), "db_add_track: Failed to step: %s (path: %s)",
             sqlite3_errmsg(g_db), track->filepath);
    log_message("ERROR", msg);
    goto error;
  }
  sqlite3_finalize(stmt);
  return RESULT_SUCCESS;

error:
  sqlite3_finalize(stmt);
  return RESULT_ERROR_GENERIC;
}

Result db_get_all_albums(Album *out_albums, size_t *count, bool consolidate) {
  if (!g_db) {
    log_message("ERROR", "db_get_all_albums: Database not initialized");
    return RESULT_ERROR_GENERIC;
  }

  /* Count first if out_albums is NULL */
  if (!out_albums) {
    const char *sql_count = consolidate
                                ? "SELECT COUNT(DISTINCT name) FROM albums;"
                                : "SELECT COUNT(*) FROM albums;";
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(g_db, sql_count, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
      log_message("ERROR", "db_get_all_albums: Failed to prepare count query");
      return RESULT_ERROR_GENERIC;
    }
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      *count = (size_t)sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    char msg[64];
    snprintf(msg, sizeof(msg),
             "db_get_all_albums: found %zu albums (grouped: %d)", *count,
             consolidate);
    log_message("DEBUG", msg);
    return RESULT_SUCCESS;
  }

  /* Get Data: Left join to get artist name */
  const char *sql_normal = "SELECT al.id, al.name, al.artist_id, ar.name, "
                           "al.art_filename, al.year, al.is_favorite FROM "
                           "albums al LEFT JOIN artists ar "
                           "ON al.artist_id = ar.id ORDER BY al.name;";

  const char *sql_consolidated =
      "SELECT MIN(al.id), al.name, -1, "
      "CASE WHEN COUNT(DISTINCT al.artist_id) > 1 THEN 'Various Artists' "
      "ELSE MAX(ar.name) END, "
      "(SELECT art_filename FROM albums WHERE name = al.name AND "
      "art_filename IS NOT NULL AND art_filename <> '' LIMIT 1), "
      "MAX(al.year), MAX(al.is_favorite) FROM albums al LEFT JOIN artists ar "
      "ON al.artist_id = ar.id GROUP BY al.name ORDER BY al.name;";

  const char *sql = consolidate ? sql_consolidated : sql_normal;
  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    char msg[512];
    snprintf(msg, sizeof(msg), "db_get_all_albums: Failed to prepare: %s",
             sqlite3_errmsg(g_db));
    log_message("ERROR", msg);
    return RESULT_ERROR_GENERIC;
  }

  size_t capacity = *count;
  *count = 0;

  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    if (*count < capacity) {
      Album *a = &out_albums[*count];
      a->id = sqlite3_column_int(stmt, 0);

      const unsigned char *name = sqlite3_column_text(stmt, 1);
      if (name)
        strncpy(a->name, (const char *)name, sizeof(a->name) - 1);
      else
        a->name[0] = '\0';

      a->artist_id = sqlite3_column_int(stmt, 2);

      const unsigned char *artist = sqlite3_column_text(stmt, 3);
      if (artist)
        strncpy(a->artist, (const char *)artist, sizeof(a->artist) - 1);
      else
        a->artist[0] = '\0';

      const unsigned char *art = sqlite3_column_text(stmt, 4);
      if (art)
        strncpy(a->art_filename, (const char *)art,
                sizeof(a->art_filename) - 1);
      else
        a->art_filename[0] = '\0';

      a->year = sqlite3_column_int(stmt, 5);
      a->is_favorite = (bool)sqlite3_column_int(stmt, 6);
    }
    (*count)++;
  }

  if (rc != SQLITE_DONE) {
    char msg[512];
    snprintf(msg, sizeof(msg), "db_get_all_albums: error during fetch: %s",
             sqlite3_errmsg(g_db));
    log_message("ERROR", msg);
    sqlite3_finalize(stmt);
    return RESULT_ERROR_GENERIC;
  }

  sqlite3_finalize(stmt);
  return RESULT_SUCCESS;
}

Result db_get_tracks_by_album_name(const char *album_name, Track *out_tracks,
                                   size_t *count) {
  if (!g_db)
    return RESULT_ERROR_GENERIC;

  if (!out_tracks) {
    const char *sql = "SELECT COUNT(*) FROM tracks t JOIN albums al ON "
                      "t.album_id = al.id WHERE al.name = ?;";
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, album_name, -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      *count = (size_t)sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return RESULT_SUCCESS;
  }

  const char *sql =
      "SELECT t.id, t.filepath, t.title, t.track_number, "
      "t.disc_number, t.duration, ar.name, t.is_favorite FROM tracks t "
      "JOIN albums al ON t.album_id = al.id "
      "LEFT JOIN artists ar ON t.artist_id = ar.id "
      "WHERE al.name = ? "
      "ORDER BY t.disc_number, t.track_number;";
  sqlite3_stmt *stmt;
  sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
  sqlite3_bind_text(stmt, 1, album_name, -1, SQLITE_STATIC);

  size_t capacity = *count;
  *count = 0;
  while (sqlite3_step(stmt) == SQLITE_ROW && *count < capacity) {
    Track *t = &out_tracks[*count];
    t->id = sqlite3_column_int(stmt, 0);
    strncpy(t->filepath, (const char *)sqlite3_column_text(stmt, 1),
            MAX_PATH_LENGTH - 1);
    strncpy(t->title, (const char *)sqlite3_column_text(stmt, 2),
            MAX_SONG_TITLE - 1);
    t->track_number = sqlite3_column_int(stmt, 3);
    t->disc_number = sqlite3_column_int(stmt, 4);
    t->duration = (float)sqlite3_column_double(stmt, 5);
    const char *artist_ptr = (const char *)sqlite3_column_text(stmt, 6);
    strncpy(t->artist, artist_ptr ? artist_ptr : "Unknown Artist",
            MAX_SONG_TITLE - 1);
    (*count)++;
  }
  sqlite3_finalize(stmt);
  return RESULT_SUCCESS;
}

Result db_get_tracks_by_album(int album_id, Track *out_tracks, size_t *count) {
  if (!g_db)
    return RESULT_ERROR_GENERIC;

  if (!out_tracks) {
    const char *sql = "SELECT COUNT(*) FROM tracks WHERE album_id = ?;";
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, album_id);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      *count = (size_t)sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return RESULT_SUCCESS;
  }

  const char *sql =
      "SELECT t.id, t.filepath, t.title, t.track_number, "
      "t.disc_number, t.duration, ar.name, t.is_favorite FROM tracks t "
      "LEFT JOIN artists ar ON t.artist_id = ar.id "
      "WHERE t.album_id = ? "
      "ORDER BY t.disc_number, t.track_number;";
  sqlite3_stmt *stmt;
  sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
  sqlite3_bind_int(stmt, 1, album_id);

  size_t capacity = *count;
  *count = 0;
  while (sqlite3_step(stmt) == SQLITE_ROW && *count < capacity) {
    Track *t = &out_tracks[*count];
    t->id = sqlite3_column_int(stmt, 0);
    strncpy(t->filepath, (const char *)sqlite3_column_text(stmt, 1),
            MAX_PATH_LENGTH - 1);
    strncpy(t->title, (const char *)sqlite3_column_text(stmt, 2),
            MAX_SONG_TITLE - 1);
    t->track_number = sqlite3_column_int(stmt, 3);
    t->disc_number = sqlite3_column_int(stmt, 4);
    t->duration = (float)sqlite3_column_double(stmt, 5);
    const char *artist_ptr = (const char *)sqlite3_column_text(stmt, 6);
    strncpy(t->artist, artist_ptr ? artist_ptr : "Unknown Artist",
            MAX_SONG_TITLE - 1);
    (*count)++;
  }
  sqlite3_finalize(stmt);
  return RESULT_SUCCESS;
}

Result db_get_tracks_by_artist_deep(const char *artist_name, Track *out_tracks,
                                    size_t *count) {
  if (!g_db)
    return RESULT_ERROR_GENERIC;

  char pattern[256];
  snprintf(pattern, sizeof(pattern), "%%%s%%", artist_name);

  if (!out_tracks) {
    const char *sql = "SELECT COUNT(*) FROM tracks t JOIN artists ar ON "
                      "t.artist_id = ar.id WHERE ar.name LIKE ?;";
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_STATIC);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      *count = (size_t)sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return RESULT_SUCCESS;
  }

  const char *sql =
      "SELECT t.id, t.filepath, t.title, t.track_number, "
      "t.disc_number, t.duration, ar.name, t.is_favorite FROM tracks t "
      "LEFT JOIN artists ar ON t.artist_id = ar.id "
      "WHERE ar.name LIKE ? "
      "ORDER BY t.title;";
  sqlite3_stmt *stmt;
  sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
  sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_STATIC);

  size_t capacity = *count;
  *count = 0;
  while (sqlite3_step(stmt) == SQLITE_ROW && *count < capacity) {
    Track *t = &out_tracks[*count];
    t->id = sqlite3_column_int(stmt, 0);
    strncpy(t->filepath, (const char *)sqlite3_column_text(stmt, 1),
            MAX_PATH_LENGTH - 1);
    strncpy(t->title, (const char *)sqlite3_column_text(stmt, 2),
            MAX_SONG_TITLE - 1);
    t->track_number = sqlite3_column_int(stmt, 3);
    t->disc_number = sqlite3_column_int(stmt, 4);
    t->duration = (float)sqlite3_column_double(stmt, 5);
    const char *artist_ptr = (const char *)sqlite3_column_text(stmt, 6);
    strncpy(t->artist, artist_ptr ? artist_ptr : "Unknown Artist",
            MAX_SONG_TITLE - 1);
    (*count)++;
  }
  sqlite3_finalize(stmt);
  return RESULT_SUCCESS;
}

Result db_get_track_by_path(const char *path, Track *out_track) {
  if (!g_db || !path || !out_track)
    return RESULT_ERROR_GENERIC;

  const char *sql =
      "SELECT t.id, t.filepath, t.title, t.track_number, "
      "t.disc_number, t.duration, ar.name, t.is_favorite, al.name, t.genre, "
      "t.year FROM tracks t "
      "LEFT JOIN artists ar ON t.artist_id = ar.id "
      "LEFT JOIN albums al ON t.album_id = al.id "
      "WHERE t.filepath = ?;";
  sqlite3_stmt *stmt;
  if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK)
    return RESULT_ERROR_GENERIC;

  sqlite3_bind_text(stmt, 1, path, -1, SQLITE_STATIC);

  Result res = RESULT_ERROR_GENERIC;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    out_track->id = sqlite3_column_int(stmt, 0);
    strncpy(out_track->filepath, (const char *)sqlite3_column_text(stmt, 1),
            MAX_PATH_LENGTH - 1);
    strncpy(out_track->title, (const char *)sqlite3_column_text(stmt, 2),
            MAX_SONG_TITLE - 1);
    out_track->track_number = sqlite3_column_int(stmt, 3);
    out_track->disc_number = sqlite3_column_int(stmt, 4);
    out_track->duration = (float)sqlite3_column_double(stmt, 5);
    const char *artist_ptr = (const char *)sqlite3_column_text(stmt, 6);
    strncpy(out_track->artist, artist_ptr ? artist_ptr : "Unknown Artist",
            MAX_SONG_TITLE - 1);
    out_track->is_favorite = (bool)sqlite3_column_int(stmt, 7);
    /* Note: Track struct has album_id/artist_id, but here we might just want
       strings for spotlight. However, to stay consistent with Track struct: */
    // strncpy(out_track->album_name, ...) // Track struct doesn't have album
    // name string!
    const char *genre_ptr = (const char *)sqlite3_column_text(stmt, 9);
    strncpy(out_track->genre, genre_ptr ? genre_ptr : "Unknown", 63);
    out_track->year = sqlite3_column_int(stmt, 10);

    res = RESULT_SUCCESS;
  }
  sqlite3_finalize(stmt);
  return res;
}

Result db_remove_stale_tracks(void) {
  if (!g_db)
    return RESULT_ERROR_GENERIC;

  /* Fetch all (id, filepath) pairs */
  const char *sql = "SELECT id, filepath FROM tracks;";
  sqlite3_stmt *stmt;
  if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK)
    return RESULT_ERROR_GENERIC;

  /* Collect stale IDs first to avoid modifying during iteration */
  int stale_ids[4096];
  int stale_count = 0;

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    int id = sqlite3_column_int(stmt, 0);
    const char *path = (const char *)sqlite3_column_text(stmt, 1);
    if (path) {
      struct stat st;
      if (stat(path, &st) != 0) {
        /* File does not exist */
        if (stale_count < 4096)
          stale_ids[stale_count++] = id;
      }
    }
  }
  sqlite3_finalize(stmt);

  if (stale_count == 0)
    return RESULT_SUCCESS;

  /* Delete stale tracks */
  db_begin_transaction();
  for (int i = 0; i < stale_count; i++) {
    const char *del_sql = "DELETE FROM tracks WHERE id = ?;";
    sqlite3_stmt *del_stmt;
    if (sqlite3_prepare_v2(g_db, del_sql, -1, &del_stmt, NULL) == SQLITE_OK) {
      sqlite3_bind_int(del_stmt, 1, stale_ids[i]);
      sqlite3_step(del_stmt);
      sqlite3_finalize(del_stmt);
    }
  }
  db_commit();

  /* Also clean up orphaned albums/artists */
  const char *cleanup =
      "DELETE FROM albums WHERE id NOT IN (SELECT DISTINCT album_id FROM tracks);"
      "DELETE FROM artists WHERE id NOT IN (SELECT DISTINCT artist_id FROM tracks);";
  char *err_msg = NULL;
  sqlite3_exec(g_db, cleanup, NULL, NULL, &err_msg);
  if (err_msg) sqlite3_free(err_msg);

  char msg[64];
  snprintf(msg, sizeof(msg), "Removed %d stale track(s) from database.", stale_count);
  log_message("INFO", msg);
  return RESULT_SUCCESS;
}

Result db_begin_transaction(void) {
  char *err_msg = NULL;
  int rc = sqlite3_exec(g_db, "BEGIN TRANSACTION;", 0, 0, &err_msg);
  if (rc != SQLITE_OK) {
    log_message("ERROR", err_msg);
    sqlite3_free(err_msg);
    return RESULT_ERROR_GENERIC;
  }
  return RESULT_SUCCESS;
}

Result db_commit(void) {
  char *err_msg = NULL;
  int rc = sqlite3_exec(g_db, "COMMIT;", 0, 0, &err_msg);
  if (rc != SQLITE_OK) {
    log_message("ERROR", err_msg);
    sqlite3_free(err_msg);
    return RESULT_ERROR_GENERIC;
  }
  return RESULT_SUCCESS;
}

/* ========================================
 *       Library Path Management
 * ======================================== */

Result db_add_library_path(const char *path) {
  if (!c_assert(g_db != NULL) || !c_assert(path != NULL))
    return RESULT_ERROR_GENERIC;

  const char *sql = "INSERT OR IGNORE INTO library_paths (path) VALUES (?);";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    log_message("ERROR", "db_add_library_path: Failed to prepare statement");
    return RESULT_ERROR_GENERIC;
  }

  if (sqlite3_bind_text(stmt, 1, path, -1, SQLITE_STATIC) != SQLITE_OK) {
    sqlite3_finalize(stmt);
    return RESULT_ERROR_GENERIC;
  }

  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    log_message("ERROR", "db_add_library_path: Failed to insert path");
    return RESULT_ERROR_GENERIC;
  }

  return RESULT_SUCCESS;
}

Result db_get_library_paths(LibraryPath *out_paths, size_t *count) {
  if (!c_assert(g_db != NULL) || !c_assert(count != NULL))
    return RESULT_ERROR_GENERIC;

  /* Count first if out_paths is NULL */
  if (!out_paths) {
    const char *sql_count = "SELECT COUNT(*) FROM library_paths;";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(g_db, sql_count, -1, &stmt, NULL) != SQLITE_OK)
      return RESULT_ERROR_GENERIC;

    if (sqlite3_step(stmt) == SQLITE_ROW) {
      *count = (size_t)sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return RESULT_SUCCESS;
  }

  /* Get actual paths */
  const char *sql = "SELECT path FROM library_paths;";
  sqlite3_stmt *stmt;
  if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK)
    return RESULT_ERROR_GENERIC;

  size_t capacity = *count;
  *count = 0;

  while (sqlite3_step(stmt) == SQLITE_ROW && *count < capacity) {
    const unsigned char *path = sqlite3_column_text(stmt, 0);
    if (path) {
      strncpy(out_paths[*count].path, (const char *)path,
              sizeof(out_paths[*count].path) - 1);
      out_paths[*count].path[sizeof(out_paths[*count].path) - 1] = '\0';
    }
    (*count)++;
  }
  sqlite3_finalize(stmt);
  return RESULT_SUCCESS;
}

Result db_remove_library_path(const char *path) {
  if (!c_assert(g_db != NULL) || !c_assert(path != NULL))
    return RESULT_ERROR_GENERIC;

  const char *sql = "DELETE FROM library_paths WHERE path = ?;";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    log_message("ERROR", "db_remove_library_path: Failed to prepare statement");
    return RESULT_ERROR_GENERIC;
  }

  if (sqlite3_bind_text(stmt, 1, path, -1, SQLITE_STATIC) != SQLITE_OK) {
    sqlite3_finalize(stmt);
    return RESULT_ERROR_GENERIC;
  }

  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    log_message("ERROR", "db_remove_library_path: Failed to delete path");
    return RESULT_ERROR_GENERIC;
  }

  return RESULT_SUCCESS;
}
Result db_get_all_tracks(Track *out_tracks, size_t *count) {
  if (!g_db)
    return RESULT_ERROR_GENERIC;

  const char *sql =
      "SELECT t.id, t.filepath, t.title, ar.name, t.album_id, t.artist_id, "
      "t.duration, t.track_number, t.disc_number, t.year, t.genre, "
      "al.art_filename, t.is_favorite "
      "FROM tracks t "
      "LEFT JOIN artists ar ON t.artist_id = ar.id "
      "LEFT JOIN albums al ON t.album_id = al.id "
      "ORDER BY t.title;";
  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    log_message("ERROR", "db_get_all_tracks: Failed to prepare statement");
    return RESULT_ERROR_GENERIC;
  }

  if (out_tracks == NULL) {
    *count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW)
      (*count)++;
    sqlite3_finalize(stmt);
    return RESULT_SUCCESS;
  }

  size_t capacity = *count;
  *count = 0;
  while (sqlite3_step(stmt) == SQLITE_ROW && *count < capacity) {
    Track *t = &out_tracks[*count];
    t->id = sqlite3_column_int(stmt, 0);

    const unsigned char *path = sqlite3_column_text(stmt, 1);
    if (path)
      strncpy(t->filepath, (const char *)path, MAX_PATH_LENGTH - 1);

    const unsigned char *title = sqlite3_column_text(stmt, 2);
    if (title)
      strncpy(t->title, (const char *)title, MAX_SONG_TITLE - 1);

    const unsigned char *artist = sqlite3_column_text(stmt, 3);
    if (artist)
      strncpy(t->artist, (const char *)artist, MAX_SONG_TITLE - 1);

    t->album_id = sqlite3_column_int(stmt, 4);
    t->artist_id = sqlite3_column_int(stmt, 5);
    t->duration = (float)sqlite3_column_double(stmt, 6);
    t->track_number = sqlite3_column_int(stmt, 7);
    t->disc_number = sqlite3_column_int(stmt, 8);
    t->year = sqlite3_column_int(stmt, 9);

    const unsigned char *genre = sqlite3_column_text(stmt, 10);
    if (genre)
      strncpy(t->genre, (const char *)genre, 63);

    const unsigned char *art = sqlite3_column_text(stmt, 11);
    if (art)
      strncpy(t->art_filename, (const char *)art, MAX_PATH_LENGTH - 1);
    else
      t->art_filename[0] = '\0';

    t->is_favorite = (bool)sqlite3_column_int(stmt, 12);

    (*count)++;
  }

  sqlite3_finalize(stmt);
  return RESULT_SUCCESS;
}


/* ========================================
 *       Visualizer Settings Persistence
 * ======================================== */

Result db_save_visualizer_param(const char *vis_name, const char *param_name,
                                int value_type, float value_float,
                                int value_int) {
  if (!c_assert(g_db != NULL) || !c_assert(vis_name != NULL) ||
      !c_assert(param_name != NULL))
    return RESULT_ERROR_GENERIC;

  const char *sql =
      "INSERT OR REPLACE INTO visualizer_settings "
      "(visualizer_name, param_name, value_type, value_float, value_int) "
      "VALUES (?, ?, ?, ?, ?);";

  sqlite3_stmt *stmt;
  if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    log_message("ERROR", "db_save_visualizer_param: Failed to prepare");
    return RESULT_ERROR_GENERIC;
  }

  sqlite3_bind_text(stmt, 1, vis_name, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, param_name, -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 3, value_type);
  sqlite3_bind_double(stmt, 4, (double)value_float);
  sqlite3_bind_int(stmt, 5, value_int);

  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    log_message("ERROR", "db_save_visualizer_param: Failed to execute");
    return RESULT_ERROR_GENERIC;
  }

  return RESULT_SUCCESS;
}

Result db_load_visualizer_params(const char *vis_name,
                                 void (*callback)(const char *param_name,
                                                  int value_type,
                                                  float value_float,
                                                  int value_int)) {
  if (!c_assert(g_db != NULL) || !c_assert(vis_name != NULL) ||
      !c_assert(callback != NULL))
    return RESULT_ERROR_GENERIC;

  const char *sql = "SELECT param_name, value_type, value_float, value_int "
                    "FROM visualizer_settings WHERE visualizer_name = ?;";

  sqlite3_stmt *stmt;
  if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    log_message("ERROR", "db_load_visualizer_params: Failed to prepare");
    return RESULT_ERROR_GENERIC;
  }

  sqlite3_bind_text(stmt, 1, vis_name, -1, SQLITE_STATIC);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const char *param_name = (const char *)sqlite3_column_text(stmt, 0);
    int value_type = sqlite3_column_int(stmt, 1);
    float value_float = (float)sqlite3_column_double(stmt, 2);
    int value_int = sqlite3_column_int(stmt, 3);

    if (param_name) {
      callback(param_name, value_type, value_float, value_int);
    }
  }

  sqlite3_finalize(stmt);
  return RESULT_SUCCESS;
}

Result db_clear_library(void) {
  if (!g_db)
    return RESULT_ERROR_GENERIC;

  const char *sql_clear = "DELETE FROM tracks;"
                          "DELETE FROM albums;"
                          "DELETE FROM artists;"
                          "DELETE FROM sqlite_sequence WHERE name='tracks';"
                          "DELETE FROM sqlite_sequence WHERE name='albums';"
                          "DELETE FROM sqlite_sequence WHERE name='artists';";

  char *err_msg = NULL;
  int rc = sqlite3_exec(g_db, sql_clear, NULL, NULL, &err_msg);
  if (rc != SQLITE_OK) {
    if (strstr(err_msg, "no such table: sqlite_sequence") != NULL) {
      /* Ignore missing sqlite_sequence table, it means no autoincrements yet */
    } else {
      log_message("ERROR", err_msg);
      sqlite3_free(err_msg);
      return RESULT_ERROR_GENERIC;
    }
  }

  return RESULT_SUCCESS;
}

Result db_create_playlist(const char *name) {
  if (!g_db || !name)
    return RESULT_ERROR_GENERIC;

  sqlite3_stmt *stmt;
  const char *sql = "INSERT OR IGNORE INTO playlists (name) VALUES (?);";
  if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK)
    return RESULT_ERROR_GENERIC;

  sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc == SQLITE_DONE)
    return RESULT_SUCCESS;
  return RESULT_ERROR_GENERIC; /* Likely duplicate name if IGNORE didn't work as
                                  expected or generic fail */
}

Result db_get_playlists(Playlist *out_playlists, size_t *count) {
  if (!g_db)
    return RESULT_ERROR_GENERIC;

  if (!out_playlists) {
    const char *sql = "SELECT COUNT(*) FROM playlists;";
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      *count = (size_t)sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return RESULT_SUCCESS;
  }

  /* Fetch with track count */
  const char *sql =
      "SELECT p.id, p.name, (SELECT COUNT(*) FROM playlist_tracks WHERE "
      "playlist_id=p.id) FROM playlists p ORDER BY p.name;";
  sqlite3_stmt *stmt;
  sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);

  size_t capacity = *count;
  *count = 0;
  while (sqlite3_step(stmt) == SQLITE_ROW && *count < capacity) {
    Playlist *p = &out_playlists[*count];
    p->id = sqlite3_column_int(stmt, 0);
    strncpy(p->name, (const char *)sqlite3_column_text(stmt, 1),
            63); /* Assuming 64 char limit in struct */
    p->track_count = sqlite3_column_int(stmt, 2);
    (*count)++;
  }
  sqlite3_finalize(stmt);
  return RESULT_SUCCESS;
}

Result db_add_track_to_playlist(int playlist_id, int track_id) {
  if (!g_db)
    return RESULT_ERROR_GENERIC;

  /* Get next sequence */
  int seq = 0;
  sqlite3_stmt *seq_stmt;
  const char *sql_seq =
      "SELECT MAX(sequence) FROM playlist_tracks WHERE playlist_id=?;";
  sqlite3_prepare_v2(g_db, sql_seq, -1, &seq_stmt, NULL);
  sqlite3_bind_int(seq_stmt, 1, playlist_id);
  if (sqlite3_step(seq_stmt) == SQLITE_ROW) {
    seq = sqlite3_column_int(seq_stmt, 0) + 1;
  }
  sqlite3_finalize(seq_stmt);

  const char *sql = "INSERT INTO playlist_tracks (playlist_id, track_id, "
                    "sequence) VALUES (?, ?, ?);";
  sqlite3_stmt *stmt;
  if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK)
    return RESULT_ERROR_GENERIC;

  sqlite3_bind_int(stmt, 1, playlist_id);
  sqlite3_bind_int(stmt, 2, track_id);
  sqlite3_bind_int(stmt, 3, seq);
  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  return (rc == SQLITE_DONE) ? RESULT_SUCCESS : RESULT_ERROR_GENERIC;
}

Result db_get_playlist_tracks(int playlist_id, Track *out_tracks,
                              size_t *count) {
  if (!g_db)
    return RESULT_ERROR_GENERIC;

  if (!out_tracks) {
    const char *sql =
        "SELECT COUNT(*) FROM playlist_tracks WHERE playlist_id=?;";
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, playlist_id);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      *count = (size_t)sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return RESULT_SUCCESS;
  }

  /* Fetch Tracks joined */
  const char *sql =
      "SELECT t.id, t.filepath, t.title, t.track_number, t.disc_number, "
      "t.duration, ar.name, t.is_favorite, al.art_filename "
      "FROM playlist_tracks pt "
      "JOIN tracks t ON pt.track_id = t.id "
      "LEFT JOIN artists ar ON t.artist_id = ar.id "
      "LEFT JOIN albums al ON t.album_id = al.id "
      "WHERE pt.playlist_id = ? "
      "ORDER BY pt.sequence;";

  sqlite3_stmt *stmt;
  sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL);
  sqlite3_bind_int(stmt, 1, playlist_id);

  size_t capacity = *count;
  *count = 0;
  while (sqlite3_step(stmt) == SQLITE_ROW && *count < capacity) {
    Track *t = &out_tracks[*count];
    t->id = sqlite3_column_int(stmt, 0);
    strncpy(t->filepath, (const char *)sqlite3_column_text(stmt, 1),
            MAX_PATH_LENGTH - 1);
    strncpy(t->title, (const char *)sqlite3_column_text(stmt, 2),
            MAX_SONG_TITLE - 1);
    t->track_number = sqlite3_column_int(stmt, 3);
    t->disc_number = sqlite3_column_int(stmt, 4);
    t->duration = (float)sqlite3_column_double(stmt, 5);
    const char *artist = (const char *)sqlite3_column_text(stmt, 6);
    strncpy(t->artist, artist ? artist : "Unknown", MAX_SONG_TITLE - 1);
    t->is_favorite = sqlite3_column_int(stmt, 7);
    const char *art = (const char *)sqlite3_column_text(stmt, 8);
    strncpy(t->art_filename, art ? art : "", MAX_PATH_LENGTH - 1);

    (*count)++;
  }
  sqlite3_finalize(stmt);
  return RESULT_SUCCESS;
}

/* ========================================
 *       Settings Persistence
 * ======================================== */

Result db_save_setting(const char *key, const char *value) {
  if (!g_db || !key || !value)
    return RESULT_ERROR_GENERIC;

  const char *sql =
      "INSERT OR REPLACE INTO settings (key, value) VALUES (?, ?);";
  sqlite3_stmt *stmt;
  if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK)
    return RESULT_ERROR_GENERIC;

  sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, value, -1, SQLITE_STATIC);
  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  return (rc == SQLITE_DONE) ? RESULT_SUCCESS : RESULT_ERROR_GENERIC;
}

Result db_get_setting(const char *key, char *out_value, size_t max_len) {
  if (!g_db || !key || !out_value)
    return RESULT_ERROR_GENERIC;

  const char *sql = "SELECT value FROM settings WHERE key = ?;";
  sqlite3_stmt *stmt;
  if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK)
    return RESULT_ERROR_GENERIC;

  sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);

  Result res = RESULT_ERROR_GENERIC;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    const char *val = (const char *)sqlite3_column_text(stmt, 0);
    if (val) {
      strncpy(out_value, val, max_len - 1);
      out_value[max_len - 1] = '\0';
      res = RESULT_SUCCESS;
    }
  }
  sqlite3_finalize(stmt);
  return res;
}

/* ========================================
 *       Playlist Management
 * ======================================== */

Result db_delete_playlist(int playlist_id) {
  if (!g_db)
    return RESULT_ERROR_GENERIC;

  /* Delete tracks from playlist first */
  const char *sql1 = "DELETE FROM playlist_tracks WHERE playlist_id = ?;";
  sqlite3_stmt *stmt;
  if (sqlite3_prepare_v2(g_db, sql1, -1, &stmt, NULL) == SQLITE_OK) {
    sqlite3_bind_int(stmt, 1, playlist_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
  }

  /* Delete playlist itself */
  const char *sql2 = "DELETE FROM playlists WHERE id = ?;";
  if (sqlite3_prepare_v2(g_db, sql2, -1, &stmt, NULL) != SQLITE_OK)
    return RESULT_ERROR_GENERIC;

  sqlite3_bind_int(stmt, 1, playlist_id);
  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  return (rc == SQLITE_DONE) ? RESULT_SUCCESS : RESULT_ERROR_GENERIC;
}

Result db_rename_playlist(int playlist_id, const char *new_name) {
  if (!g_db || !new_name)
    return RESULT_ERROR_GENERIC;

  const char *sql = "UPDATE playlists SET name = ? WHERE id = ?;";
  sqlite3_stmt *stmt;
  if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK)
    return RESULT_ERROR_GENERIC;

  sqlite3_bind_text(stmt, 1, new_name, -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 2, playlist_id);
  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  return (rc == SQLITE_DONE) ? RESULT_SUCCESS : RESULT_ERROR_GENERIC;
}

Result db_remove_track_from_playlist(int playlist_id, int track_id) {
  if (!g_db)
    return RESULT_ERROR_GENERIC;

  const char *sql =
      "DELETE FROM playlist_tracks WHERE playlist_id = ? AND track_id = ?;";
  sqlite3_stmt *stmt;
  if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK)
    return RESULT_ERROR_GENERIC;

  sqlite3_bind_int(stmt, 1, playlist_id);
  sqlite3_bind_int(stmt, 2, track_id);
  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  return (rc == SQLITE_DONE) ? RESULT_SUCCESS : RESULT_ERROR_GENERIC;
}

/* =========================================================================
   EQ Preset Persistence
   ========================================================================= */

Result db_save_eq_preset(const char *name, const float *gains, int band_count) {
  if (!g_db || !name || !gains)
    return RESULT_ERROR_GENERIC;

  /* Serialize gains to comma-separated string (always EQ_BAND_COUNT_MAX values) */
  char gains_str[256];
  int offset = 0;
  for (int i = 0; i < EQ_BAND_COUNT_MAX; i++) {
    int written = snprintf(gains_str + offset, sizeof(gains_str) - (size_t)offset,
                           "%.3f%s", gains[i], (i < EQ_BAND_COUNT_MAX - 1) ? "," : "");
    if (written < 0 || (size_t)(offset + written) >= sizeof(gains_str))
      break;
    offset += written;
  }

  const char *sql =
      "INSERT OR REPLACE INTO eq_presets (name, gains, band_count) "
      "VALUES (?, ?, ?);";
  sqlite3_stmt *stmt;
  if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK)
    return RESULT_ERROR_GENERIC;

  sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 2, gains_str, -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 3, band_count);

  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  return (rc == SQLITE_DONE) ? RESULT_SUCCESS : RESULT_ERROR_GENERIC;
}

Result db_delete_eq_preset(int id) {
  if (!g_db)
    return RESULT_ERROR_GENERIC;

  const char *sql = "DELETE FROM eq_presets WHERE id = ?;";
  sqlite3_stmt *stmt;
  if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK)
    return RESULT_ERROR_GENERIC;

  sqlite3_bind_int(stmt, 1, id);
  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  return (rc == SQLITE_DONE) ? RESULT_SUCCESS : RESULT_ERROR_GENERIC;
}

Result db_get_eq_presets(EqPreset *out, size_t *count) {
  if (!g_db || !count)
    return RESULT_ERROR_GENERIC;

  if (!out) {
    /* Count only */
    const char *sql = "SELECT COUNT(*) FROM eq_presets;";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK)
      return RESULT_ERROR_GENERIC;
    if (sqlite3_step(stmt) == SQLITE_ROW)
      *count = (size_t)sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return RESULT_SUCCESS;
  }

  const char *sql = "SELECT id, name, gains, band_count FROM eq_presets ORDER BY name;";
  log_message("DEBUG", "database: Preparing SQL for EQ presets");
  sqlite3_stmt *stmt;
  if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK) {
    log_message("ERROR", "database: sqlite3_prepare_v2 failed for EQ presets");
    return RESULT_ERROR_GENERIC;
  }

  size_t capacity = *count;
  *count = 0;
  log_message("DEBUG", "database: Starting SQLite step loop for EQ presets");

  while (sqlite3_step(stmt) == SQLITE_ROW && *count < capacity) {
    EqPreset *p = &out[*count];
    memset(p, 0, sizeof(EqPreset));

    p->id = sqlite3_column_int(stmt, 0);

    const char *name = (const char *)sqlite3_column_text(stmt, 1);
    if (name) {
      safe_strncpy(p->name, name, sizeof(p->name));
    } else {
      p->name[0] = '\0';
    }

    /* Parse comma-separated gains */
    const char *gains_str = (const char *)sqlite3_column_text(stmt, 2);
    if (gains_str) {
      const char *ptr = gains_str;
      for (int i = 0; i < EQ_BAND_COUNT_MAX && *ptr != '\0'; i++) {
        p->gains[i] = (float)atof(ptr);
        ptr = strchr(ptr, ',');
        if (!ptr) break;
        ptr++; /* skip comma */
      }
    }

    p->band_count = sqlite3_column_int(stmt, 3);
    /* Validation: Only allow 5 or 10 bands for now */
    if (p->band_count != 5 && p->band_count != 10) {
      p->band_count = 5;
    }

    (*count)++;
  }

  log_message("DEBUG", "database: Finalizing SQLite statement");
  sqlite3_finalize(stmt);
  log_message("DEBUG", "database: db_get_eq_presets complete");
  return RESULT_SUCCESS;
}


