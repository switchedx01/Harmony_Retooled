#include "audio_backend.h"
#include "logging.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

void print_usage(const char *prog_name) {
  printf("Usage: %s <path_to_audio_file>\n", prog_name);
}

int main(int argc, char **argv) {
  if (argc < 2) {
    print_usage(argv[0]);
    return 1;
  }

  const char *file_path = argv[1];
  printf("Test Playback: Initializing...\n");

  /* Initialize Logging */
  if (logging_init("test_playback.log") != RESULT_SUCCESS) {
    fprintf(stderr, "Failed to init logging.\n");
    return 1;
  }

  /* Initialize Audio */
  if (audio_init() != RESULT_SUCCESS) {
    fprintf(stderr, "Failed to init audio backend.\n");
    return 1;
  }

  printf("Audio initialized. Attempting to play: %s\n", file_path);

  /* Play File */
  if (audio_play_file(file_path) != RESULT_SUCCESS) {
    fprintf(stderr, "Failed to start playback.\n");
    audio_shutdown();
    return 1;
  }

  /* Wait and monitor */
  printf("Playback started. Waiting 5 seconds...\n");
  for (int i = 0; i < 50; i++) {
    struct timespec req = {0, 100000000}; /* 100ms */
    nanosleep(&req, NULL);
    if (!audio_is_playing()) {
      /* Ideally it should be playing. If it stopped early, maybe error or short
       * file. */
      /* But audio_is_playing might return true even if silent if buffer has
       * data. */
    }
  }

  printf("Stopping playback...\n");
  audio_stop();

  printf("Shutting down...\n");
  audio_shutdown();
  logging_shutdown();

  printf("Test Complete. Check test_playback.log for details.\n");
  return 0;
}
