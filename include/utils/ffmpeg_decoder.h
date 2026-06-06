#ifndef FFMPEG_DECODER_H
#define FFMPEG_DECODER_H

#include "common.h"
#include <stdbool.h>

/* Check if file is M4A/AAC format */
bool is_m4a_file(const char *filepath);

/* Decode M4A file to temporary WAV file for playback */
/* Returns path to temporary WAV file, or NULL on failure */
/* Caller must free the returned string */
char *decode_m4a_to_wav(const char *m4a_path);

/* Clean up temporary WAV file */
void cleanup_temp_wav(const char *wav_path);

#endif /* FFMPEG_DECODER_H */
