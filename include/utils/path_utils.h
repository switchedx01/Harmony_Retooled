#ifndef PATH_UTILS_H
#define PATH_UTILS_H

#include "common.h"

/* Returns the path to the standard data directory for the player (e.g. ~/.local/share/harmony_player).
   It will create the directory if it does not exist.
   The resulting path is stored in 'buffer'. Returns RESULT_SUCCESS on success. */
Result get_data_path(char *buffer, size_t max_len);

/* Resolves a relative path (e.g. "harmony_v2.db" or "assets/fonts/font.ttf") 
   against the standard data directory and stores the full path in 'buffer'. */
Result resolve_data_path(const char *rel_path, char *buffer, size_t max_len);

/* Resolves a relative path to an existing asset. Checks the standard data directory first. 
   If the file exists there, stores the path in 'buffer' and returns RESULT_SUCCESS. 
   Otherwise, copies the rel_path to buffer as a fallback and returns RESULT_ERROR_NOT_FOUND,
   or another error code on failure. */
Result resolve_asset_path(const char *rel_path, char *buffer, size_t max_len);

#endif /* PATH_UTILS_H */
