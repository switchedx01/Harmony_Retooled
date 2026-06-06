#include "ffmpeg_decoder.h"
#include "logging.h"
#include <fcntl.h>
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

bool is_m4a_file(const char *filepath) {
  if (!filepath)
    return false;

  const char *ext = strrchr(filepath, '.');
  if (!ext)
    return false;

  return (strcasecmp(ext, ".m4a") == 0 || strcasecmp(ext, ".aac") == 0);
}

char *decode_m4a_to_wav(const char *m4a_path) {
  if (!m4a_path)
    return NULL;

  /* Generate cache path */
  char *temp_path = malloc(256);
  if (!temp_path)
    return NULL;

  snprintf(temp_path, 256, "/tmp/harmony_wav_%u.wav", djb2_hash(m4a_path));

  /* Check cache first */
  FILE *existing = fopen(temp_path, "rb");
  if (existing) {
    fseek(existing, 0, SEEK_END);
    long size = ftell(existing);
    fclose(existing);
    if (size > 44) {
      log_message("INFO", "Using cached WAV");
      return temp_path;
    }
  }

  log_message("INFO", "Converting M4A to WAV...");

  /* Fork and exec ffmpeg directly */
  pid_t pid = fork();
  if (pid == 0) {
    /* Child - detach from terminal */
    setsid();
    int devnull = open("/dev/null", O_RDWR);
    if (devnull >= 0) {
      dup2(devnull, STDIN_FILENO);
      dup2(devnull, STDOUT_FILENO);
      /* dup2(devnull, STDERR_FILENO); */
      if (devnull > 2)
        close(devnull);
    }

    execlp("ffmpeg", "ffmpeg", "-y", "-loglevel", "warning", "-i", m4a_path,
           "-acodec", "pcm_s16le", "-ar", "44100", "-ac", "2", temp_path,
           (char *)NULL);
    _exit(1);
  } else if (pid > 0) {
    /* Parent - wait for completion */
    int status;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
      log_message("INFO", "M4A conversion complete");
      return temp_path;
    }
  }

  log_message("WARN", "M4A conversion failed");
  free(temp_path);
  return NULL;
}

void cleanup_temp_wav(const char *wav_path) {
  if (wav_path) {
    unlink(wav_path);
  }
}
