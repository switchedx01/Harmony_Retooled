#ifndef ALBUM_ART_H
#define ALBUM_ART_H

#include "common.h"
#include <stdbool.h>

/* Extract album art from audio file to temporary image file */
/* Returns path to temporary image file, or NULL on failure */
/* Caller must free the returned string */
char *extract_album_art(const char *audio_path);

/* Clean up temporary album art file */
void cleanup_album_art(const char *art_path);

#endif /* ALBUM_ART_H */
