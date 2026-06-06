#ifndef METADATA_PARSER_H
#define METADATA_PARSER_H

#include <stdbool.h>

#include "database.h"

/* Attempt to read metadata from file and populate Track struct.
   Returns true if successful, false otherwise. */
bool get_metadata(const char *path, Track *out_track, char *out_artist,
                  char *out_album);

#endif
