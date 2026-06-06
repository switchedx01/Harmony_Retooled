#include "library_manager.h"
#include "common.h"
#include "database.h"
#include "logging.h"
#include "metadata_parser.h"
#include <dirent.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "album_art.h"

static size_t count_recursive(const char *path) {
  size_t count = 0;
  DIR *dir = opendir(path);
  if (!dir)
    return 0;

  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;

    char full_path[MAX_PATH_LENGTH];
    snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

    struct stat st;
    if (stat(full_path, &st) == 0) {
      if (S_ISDIR(st.st_mode)) {
        count += count_recursive(full_path);
      } else {
        const char *ext = strrchr(entry->d_name, '.');
        if (ext && (strncasecmp(ext, ".mp3", 4) == 0 ||
                    strncasecmp(ext, ".wav", 4) == 0 ||
                    strncasecmp(ext, ".flac", 5) == 0 ||
                    strncasecmp(ext, ".m4a", 4) == 0)) {
          count++;
        }
      }
    }
  }
  closedir(dir);
  return count;
}

static void scan_recursive(const char *path,
                           void (*progress_cb)(const char *, const char *,
                                               float),
                           size_t *current_count, size_t total_count) {
  DIR *dir = opendir(path);
  if (!dir)
    return;

  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;

    char full_path[MAX_PATH_LENGTH];
    snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

    struct stat st;
    if (stat(full_path, &st) == 0) {
      if (S_ISDIR(st.st_mode)) {
        scan_recursive(full_path, progress_cb, current_count, total_count);
      } else {
        const char *ext = strrchr(entry->d_name, '.');
        if (ext && (strncasecmp(ext, ".mp3", 4) == 0 ||
                    strncasecmp(ext, ".wav", 4) == 0 ||
                    strncasecmp(ext, ".flac", 5) == 0 ||
                    strncasecmp(ext, ".m4a", 4) == 0)) {
          Track t = {0};
          strncpy(t.filepath, full_path, MAX_PATH_LENGTH - 1);

          char artist_name[256] = {0};
          char album_name[256] = {0};

          /* Extract Metadata */
          get_metadata(full_path, &t, artist_name, album_name);

          /* Fallback: Title */
          if (t.title[0] == '\0') {
            const char *fname = strrchr(full_path, '/');
            strncpy(t.title, fname ? fname + 1 : full_path, MAX_SONG_TITLE - 1);
          }
          /* Fallback: Artist */
          if (artist_name[0] == '\0') {
            strncpy(artist_name, "Unknown Artist", 255);
          }
          /* Fallback: Album (use parent folder name) */
          if (album_name[0] == '\0') {
            const char *last_slash = strrchr(path, '/');
            strncpy(album_name, last_slash ? last_slash + 1 : "Unknown Album",
                    255);
          }

          if (progress_cb) {
            float pct = 0.0f;
            if (total_count > 0)
              pct = (float)(*current_count) / (float)total_count;
            progress_cb(t.title, artist_name, pct);
          }
          (*current_count)++;

          t.artist_id = db_get_or_create_artist(artist_name);
          t.album_id = db_get_or_create_album(album_name, t.artist_id, t.year);

          /* Try to extract and cache album art */
          char *art_tmp = extract_album_art(full_path);
          if (art_tmp) {
            db_update_album_art(t.album_id, art_tmp);
            free(art_tmp);
          }
          db_add_track(&t);
        }
      }
    }
  }
  closedir(dir);
}

Result library_init(void) { return db_init("harmony_v2.db"); }

Result library_scan(const char *folder_path,
                    void (*progress_cb)(const char *title, const char *artist,
                                        float percent)) {
  log_message("INFO", "Starting library scan...");

  /* Resolve absolute path to ensure DB stores consistent paths */
  char absolute_path[MAX_PATH_LENGTH];
  if (realpath(folder_path, absolute_path) == NULL) {
    char msg[512];
    snprintf(msg, sizeof(msg), "Failed to resolve path: %s", folder_path);
    log_message("ERROR", msg);
    /* Fallback to provided path if resolution fails (e.g. permission?) */
    strncpy(absolute_path, folder_path, MAX_PATH_LENGTH - 1);
  } else {
    /* realpath might return path without trailing slash, but our recursive
       function adds it via %s/%s. However, if realpath returns just "/" (root),
       we might get "//file". This is usually fine in unix. */
  }

  size_t total = count_recursive(absolute_path);
  size_t current = 0;

  db_begin_transaction();
  scan_recursive(absolute_path, progress_cb, &current, total);
  db_commit();
  log_message("INFO", "Library scan finished.");
  return RESULT_SUCCESS;
}

Result library_reset(void) {
  log_message("INFO", "Resetting library database...");
  return db_clear_library();
}
