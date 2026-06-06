#include "album_art.h"
#include "logging.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static unsigned int djb2_hash(const char *str) {
  unsigned int hash = 5381;
  int c;
  while ((c = *str++))
    hash = ((hash << 5) + hash) + c;
  return hash;
}

char *extract_album_art(const char *audio_path) {
  if (!audio_path)
    return NULL;

  /* Generate cache path */
  const char *home = getenv("HOME");
  if (!home)
    return NULL;

  char cache_dir[256];
  snprintf(cache_dir, sizeof(cache_dir), "%s/.cache/harmony/art", home);

  /* Ensure directory exists */
  char mkdir_cmd[512];
  snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p \"%s\"", cache_dir);
  system(mkdir_cmd);

  char *temp_path = malloc(512);
  if (!temp_path)
    return NULL;

  snprintf(temp_path, 512, "%s/%u.jpg", cache_dir, djb2_hash(audio_path));

  /* Check cache first */
  FILE *existing = fopen(temp_path, "rb");
  if (existing) {
    fseek(existing, 0, SEEK_END);
    long size = ftell(existing);
    fclose(existing);
    if (size > 100) {
      log_message("INFO", "Using cached album art");
      return temp_path;
    }
  }

  log_message("INFO", "Extracting album art...");

  /* Fork and exec ffmpeg directly */
  pid_t pid = fork();
  if (pid == 0) {
    /* Child - detach from terminal, redirect output */
    setsid();
    int devnull = open("/dev/null", O_RDWR);
    if (devnull >= 0) {
      dup2(devnull, STDIN_FILENO);
      dup2(devnull, STDOUT_FILENO);
      dup2(devnull, STDERR_FILENO);
      if (devnull > 2)
        close(devnull);
    }

    execlp("ffmpeg", "ffmpeg", "-y", "-loglevel", "error", "-i", audio_path,
           "-an", "-vcodec", "copy", temp_path, (char *)NULL);
    _exit(1);
  } else if (pid > 0) {
    /* Parent - wait for completion */
    int status;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
      /* Verify file */
      FILE *f = fopen(temp_path, "rb");
      if (f) {
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fclose(f);
        if (size > 100) {
          log_message("INFO", "Album art extracted");
          return temp_path;
        }
      }
    }
  }

  free(temp_path);
  return NULL;
}

void cleanup_album_art(const char *art_path) {
  if (art_path) {
    unlink(art_path);
  }
}
