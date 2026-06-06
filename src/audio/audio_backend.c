#define STB_VORBIS_HEADER_ONLY
#include "vendor/stb_vorbis.c"

#define MINIAUDIO_IMPLEMENTATION
/* Enable MP3, FLAC, WAV (Default) + Vorbis via stb + M4A/AAC */
#include "vendor/miniaudio.h"

#include "audio_backend.h"
#include "ffmpeg_decoder.h"
#include "logging.h"
#include "ring_buffer.h" /* Now in include/ */
#include "visualizer_buffer.h"

/* Re-include stb_vorbis implementation to be compiled here */
#undef STB_VORBIS_HEADER_ONLY
#include "vendor/stb_vorbis.c"

#include <stdatomic.h>
#include <string.h>

#define RING_BUFFER_SIZE (1024 * 512) /* 512 KB buffer */

/* Global State */
static ma_device g_device;
static ma_decoder g_decoder;
static RingBuffer g_rb;
static atomic_bool g_is_initialized = false;
static atomic_bool g_sound_loaded = false;
static atomic_bool g_decoder_running = false;
static atomic_bool g_seek_requested = false;
static _Atomic float g_seek_target = 0.0f;
static ma_thread g_decoder_thread;
static atomic_bool g_stop_thread = false;
static atomic_bool g_cancel_decode = false;
static atomic_bool g_is_paused = false;
static atomic_bool g_track_finished = false;
static atomic_bool g_decoder_at_end = false;

/* Volume Control (Atomic Float for Thread Safety) */
/* C11 _Atomic float is supported by GCC/Clang */
static _Atomic float g_volume = 1.0f;

/* =========================================================================
   EQ DSP State (Thread-Safe via Atomic Reinit Flags)
   The main thread updates g_eq_biquad_configs[] and sets g_eq_reinit_pending.
   The audio callback checks the flag and calls ma_biquad_reinit() safely.
   All biquad processing happens exclusively on the audio thread.
   ========================================================================= */

/* Center frequencies for 5-band and 10-band modes */
static const double g_eq_freqs_5band[5]  = {60.0, 250.0, 1000.0, 4000.0, 12000.0};
static const double g_eq_freqs_10band[10] = {31.0, 63.0, 125.0, 250.0, 500.0,
                                              1000.0, 2000.0, 4000.0, 8000.0, 16000.0};

static ma_biquad          g_eq_biquads[EQ_BAND_COUNT_MAX];      /* Filter instances */
static ma_biquad_config   g_eq_biquad_configs[EQ_BAND_COUNT_MAX]; /* Pending configs */
static atomic_bool        g_eq_reinit_pending[EQ_BAND_COUNT_MAX]; /* Per-band flag */
static atomic_bool        g_eq_enabled = false;                    /* Master bypass */
static atomic_bool        g_eq_initialized = false;               /* Filters ready? */

/* Build a peaking EQ biquad config for one band.
   Uses the Audio EQ Cookbook formula for peaking EQ filters.
   gain_db: dB boost/cut  freq: center Hz  q: resonance (~0.7 for gentle slope)
   sampleRate / channels: match the playback device config */
static ma_biquad_config eq_make_peaking_config(double gain_db, double freq,
                                               double q, ma_uint32 sampleRate,
                                               ma_uint32 channels) {
  double A  = pow(10.0, gain_db / 40.0); /* sqrt(10^(gain/20)) */
  double w0 = 2.0 * MA_PI * freq / (double)sampleRate;
  double alpha = sin(w0) / (2.0 * q);

  double b0 =  1.0 + alpha * A;
  double b1 = -2.0 * cos(w0);
  double b2 =  1.0 - alpha * A;
  double a0 =  1.0 + alpha / A;
  double a1 = -2.0 * cos(w0);
  double a2 =  1.0 - alpha / A;

  return ma_biquad_config_init(ma_format_f32, channels, b0, b1, b2, a0, a1, a2);
}

/* Initialize all EQ biquads to flat (0 dB gain).  Called from audio_init(). */
static void eq_init_flat(ma_uint32 sampleRate, ma_uint32 channels) {
  for (int i = 0; i < EQ_BAND_COUNT_MAX; i++) {
    /* 0 dB peaking = identity biquad */
    g_eq_biquad_configs[i] = eq_make_peaking_config(0.0,
        g_eq_freqs_10band[i], 0.707, sampleRate, channels);
    ma_biquad_init(&g_eq_biquad_configs[i], NULL, &g_eq_biquads[i]);
    atomic_store(&g_eq_reinit_pending[i], false);
  }
  atomic_store(&g_eq_initialized, true);
}

/* Temporary path for M4A conversion */
static char *g_temp_wav_path = NULL;
static char g_current_filepath[2048] = {0};
static atomic_bool g_new_file_requested = false;

/* Forward Declarations */
static void *decoder_thread_entry(void *pUserData);

/* =========================================================================
   AUDIO CALLBACK (High Priority, Real-Time)
   CRITICAL: No Mutexes, No Malloc, No Printf, No File I/O
   ========================================================================= */
static void data_callback(ma_device *pDevice, void *pOutput, const void *pInput,
                          ma_uint32 frameCount) {
  float *out = (float *)pOutput;

  /* Calculate bytes needed: frameCount * channels * sizeof(float) */
  /* Device is configured for F32, 2 Channels */
  size_t bytes_per_frame = sizeof(float) * pDevice->playback.channels;
  size_t bytes_requested = frameCount * bytes_per_frame;

  /* Read from Ring Buffer */
  if (atomic_load(&g_is_paused)) {
    /* Instant Silence on Pause */
    memset(out, 0, bytes_requested);
  } else if (atomic_load(&g_sound_loaded)) {
    size_t bytes_read = rb_read(&g_rb, out, bytes_requested);
    /* Update out pointer to skip filled data? No, removing silence fill logic
     * duplication */

    if (bytes_read < bytes_requested) {
      memset((uint8_t *)out + bytes_read, 0, bytes_requested - bytes_read);
    }
  } else {
    memset(out, 0, bytes_requested);
  }

  /* Apply Volume (Software Mixer) */
  /* We do this here because we removed ma_engine */
  float vol = atomic_load(&g_volume);
  if (vol != 1.0f) {
    size_t sample_count = frameCount * pDevice->playback.channels;
    /* Simple scalar multiply - SIMD would be better but this is fine for now */
    for (size_t i = 0; i < sample_count; i++) {
      out[i] *= vol;
    }
  }

  /* ===================================================================
     Apply EQ (runs entirely on audio thread - safe to touch biquad state)
     Check per-band reinit flags set by the main thread, then process.
     =================================================================== */
  if (atomic_load(&g_eq_enabled) && atomic_load(&g_eq_initialized)) {
    /* Handle any pending reinit requests from the main thread */
    for (int i = 0; i < EQ_BAND_COUNT_MAX; i++) {
      if (atomic_load(&g_eq_reinit_pending[i])) {
        ma_biquad_reinit(&g_eq_biquad_configs[i], &g_eq_biquads[i]);
        atomic_store(&g_eq_reinit_pending[i], false);
      }
    }
    /* Process each band in series */
    for (int i = 0; i < EQ_BAND_COUNT_MAX; i++) {
      ma_biquad_process_pcm_frames(&g_eq_biquads[i], out, out, frameCount);
    }
  }

  /* Push to Visualizer Buffer (Non-blocking) */
  vis_buffer_push(out, frameCount * pDevice->playback.channels);
}

/* =========================================================================
   DECODER THREAD (Background)
   Handles File I/O, Decoding, and Writing to Ring Buffer
   ========================================================================= */
static void *decoder_thread_entry(void *pUserData) {
  (void)pUserData;
  float temp_buffer[4096]; /* Decode chunk buffer */

  while (atomic_load(&g_decoder_running)) {
    if (atomic_load(&g_stop_thread))
      break;

    /* Handle New File Request */
    if (atomic_load(&g_new_file_requested)) {
      atomic_store(&g_new_file_requested, false);

      /* 1. Stop Playback Safely */
      atomic_store(&g_sound_loaded, false);

      /* CRITICAL: Wait for audio callback to finish any current read cycle.
         Since we can't lock the callback, we sleep longer than a few callback
         periods. At 48kHz/1024frames, one period is ~21ms. Sleep 50ms to be
         safe. */
      ma_sleep(50);

      if (ma_decoder_uninit(&g_decoder) != MA_SUCCESS) {
        /* Was not initialized or error */
      }

      /* 2. Init New Decoder */
      ma_decoder_config config = ma_decoder_config_init(
          g_device.playback.format, g_device.playback.channels,
          g_device.sampleRate);

      if (ma_decoder_init_file(g_current_filepath, &config, &g_decoder) ==
          MA_SUCCESS) {

        /* 3. Safe to Clear Buffer now that callback is skipping reads */
        rb_clear(&g_rb);

        /* 4. Pre-fill Buffer Cleanly */
        /* Loop until buffer is mostly full or we run out of data */
        size_t available;
        size_t bytes_per_frame = sizeof(float) * g_device.playback.channels;
        ma_uint64 frames_read = 0;

        /* Pre-fill loop: Try to fill at least 320KB (approx 1s) or full */
        while ((available = rb_available_write(&g_rb)) > sizeof(temp_buffer)) {
          size_t frames_to_read = sizeof(temp_buffer) / bytes_per_frame;
          if (frames_to_read > available / bytes_per_frame)
            frames_to_read = available / bytes_per_frame;

          ma_result res = ma_decoder_read_pcm_frames(
              &g_decoder, temp_buffer, frames_to_read, &frames_read);
          if (frames_read > 0) {
            rb_write(&g_rb, temp_buffer, frames_read * bytes_per_frame);
          }
          if (res != MA_SUCCESS || frames_read < frames_to_read) {
            break; /* EOF or Error */
          }
        }

        /* 5. Start Playback */
        atomic_store(&g_sound_loaded, true);
      } else {
        /* Error loading file */
        char msg[512];
        snprintf(msg, sizeof(msg), "Failed to load audio file: %s",
                 g_current_filepath);
        log_message("ERROR", msg);
      }
    }

    /* Handle Seek */
    if (atomic_load(&g_seek_requested)) {
      float target = atomic_load(&g_seek_target);
      atomic_store(&g_seek_requested, false);

      if (atomic_load(&g_sound_loaded)) {
        ma_uint64 frame = (ma_uint64)(target * g_device.sampleRate);
        ma_decoder_seek_to_pcm_frame(&g_decoder, frame);

        atomic_store(&g_sound_loaded, false); /* Pause consumer */
        ma_sleep(50); /* Wait for consumer to stop reading */

        rb_clear(&g_rb);

        /* Pre-fill again for seek */
        size_t available;
        size_t bytes_per_frame = sizeof(float) * g_device.playback.channels;
        ma_uint64 frames_read = 0;
        while ((available = rb_available_write(&g_rb)) > sizeof(temp_buffer)) {
          size_t frames_to_read = sizeof(temp_buffer) / bytes_per_frame;
          ma_result res = ma_decoder_read_pcm_frames(
              &g_decoder, temp_buffer, frames_to_read, &frames_read);
          if (frames_read > 0) {
            rb_write(&g_rb, temp_buffer, frames_read * bytes_per_frame);
          }
          if (res != MA_SUCCESS || frames_read < frames_to_read)
            break;
        }

        atomic_store(&g_sound_loaded, true); /* Resume */
      }
    }

    /* Decode & Write */
    if (atomic_load(&g_sound_loaded) && !atomic_load(&g_cancel_decode)) {
      size_t available = rb_available_write(&g_rb);
      size_t bytes_per_frame = sizeof(float) * g_device.playback.channels;

      /* Write if we have ANY reasonably safe space (e.g. 1 frame is too small,
       * but 128 is fine) */
      /* Reduced threshold to keep buffer fuller */
      if (available >= bytes_per_frame * 128) {
        size_t cap_frames = sizeof(temp_buffer) / bytes_per_frame;
        size_t avail_frames = available / bytes_per_frame;

        size_t frames_to_read =
            (cap_frames < avail_frames) ? cap_frames : avail_frames;

        ma_uint64 frames_read = 0;
        ma_result res = ma_decoder_read_pcm_frames(
            &g_decoder, temp_buffer, frames_to_read, &frames_read);

        if (frames_read > 0) {
          rb_write(&g_rb, temp_buffer, frames_read * bytes_per_frame);
        }

        if (res == MA_AT_END) {
          atomic_store(&g_decoder_at_end, true);
          /* Check if ring buffer has drained (playback finished) */
          if (rb_available_read(&g_rb) == 0) {
            atomic_store(&g_track_finished, true);
          }
          ma_sleep(10);
        }
      } else {
        /* Buffer is very full. Sleep briefly. */
        ma_sleep(1); /* 1ms sleep for tighter control */
      }
    } else {
      /* Nothing to play, sleep */
      ma_sleep(10); /* 10ms is fine when idle */
    }
  }
  return NULL;
}

/* =========================================================================
   PUBLIC API
   ========================================================================= */

Result audio_init(void) {
  if (atomic_load(&g_is_initialized))
    return RESULT_SUCCESS;

  /* Init Ring Buffer */
  if (!rb_init(&g_rb, RING_BUFFER_SIZE)) {
    log_message("ERROR", "Failed to create audio ring buffer.");
    return RESULT_ERROR_GENERIC;
  }

  /* Init Visualizer Buffer */
  vis_buffer_init();

  /* Init Device */
  ma_device_config config = ma_device_config_init(ma_device_type_playback);
  config.playback.format = ma_format_f32;
  config.playback.channels = 2;
  config.sampleRate = 0; /* Use native */
  config.periodSizeInFrames =
      1024; /* Conservative buffer size to reduce underruns */
  config.dataCallback = data_callback;
  config.pUserData = NULL;

  if (ma_device_init(NULL, &config, &g_device) != MA_SUCCESS) {
    log_message("ERROR", "Failed to init audio device.");
    rb_free(&g_rb);
    return RESULT_ERROR_GENERIC;
  }

  /* Start Device (It will play silence from empty RB) */
  if (ma_device_start(&g_device) != MA_SUCCESS) {
    log_message("ERROR", "Failed to start audio device.");
    ma_device_uninit(&g_device);
    rb_free(&g_rb);
    return RESULT_ERROR_GENERIC;
  }

  /* Start Decoder Thread */
  atomic_store(&g_decoder_running, true);
  atomic_store(&g_stop_thread, false);

  /* ma_thread_create signature: (ma_thread* pThread, ma_thread_priority
   * priority, size_t stackSize, ma_thread_entry_proc entryProc, void* pData,
   * const ma_allocation_callbacks* pAllocationCallbacks) */
  /* Using Realtime priority to ensure decoder keeps up with audio callback */
  if (ma_thread_create(&g_decoder_thread, ma_thread_priority_realtime, 0,
                       decoder_thread_entry, NULL, NULL) != MA_SUCCESS) {
    log_message("ERROR", "Failed to create decoder thread.");
    ma_device_uninit(&g_device);
    rb_free(&g_rb);
    return RESULT_ERROR_GENERIC;
  }

  atomic_store(&g_is_initialized, true);

  /* Init EQ biquads (must happen after device is started so sample rate is known) */
  eq_init_flat(g_device.sampleRate, g_device.playback.channels);

  log_message("INFO", "Audio backend initialized (Lock-Free Ring Buffer).");
  return RESULT_SUCCESS;
}

void audio_shutdown(void) {
  if (atomic_load(&g_is_initialized)) {
    /* Stop Thread */
    atomic_store(&g_stop_thread, true);
    ma_thread_wait(&g_decoder_thread);
    atomic_store(&g_decoder_running, false);

    ma_device_uninit(&g_device);

    if (atomic_load(&g_sound_loaded)) {
      ma_decoder_uninit(&g_decoder);
    }

    rb_free(&g_rb);

    if (g_temp_wav_path) {
      cleanup_temp_wav(g_temp_wav_path);
      free(g_temp_wav_path);
      g_temp_wav_path = NULL;
    }

    atomic_store(&g_is_initialized, false);
  }
}

Result audio_play_file(const char *filepath) {
  if (!atomic_load(&g_is_initialized))
    return RESULT_ERROR_GENERIC;

  /* Clean up temp WAV if exists from previous track */
  if (g_temp_wav_path) {
    cleanup_temp_wav(g_temp_wav_path);
    free(g_temp_wav_path);
    g_temp_wav_path = NULL;
  }

  const char *play_path = filepath;

  /* M4A Handling (Synchronous conversion, but only happens on M4A)
     TODO: Move this to thread? For now left here as it's separate from
     decoding. */
  if (is_m4a_file(filepath)) {
    g_temp_wav_path = decode_m4a_to_wav(filepath);
    if (g_temp_wav_path) {
      play_path = g_temp_wav_path;
    }
  }

  strncpy(g_current_filepath, play_path, sizeof(g_current_filepath) - 1);

  /* Signal thread to reload */
  atomic_store(&g_cancel_decode, false); /* Ensure we aren't paused */
  atomic_store(&g_is_paused, false); /* Ensure audio callback isn't silenced */
  atomic_store(&g_track_finished, false);
  atomic_store(&g_decoder_at_end, false);
  atomic_store(&g_new_file_requested, true);

  return RESULT_SUCCESS;
}

void audio_stop(void) {
  atomic_store(&g_cancel_decode, true);
  rb_clear(&g_rb);
  if (atomic_load(&g_sound_loaded)) {
    /* Seek to 0 for next play */
    atomic_store(&g_seek_target, 0.0f);
    atomic_store(&g_seek_requested, true);
  }
}

void audio_pause(void) {
  atomic_store(&g_cancel_decode, true); /* Stop decoding new data */
  atomic_store(&g_is_paused, true);     /* Instant silence */
  /* We don't stop the device, we just stop filling buffer.
     RB will drain and then play silence.
     Ideally we might want to pause device but that can pop.
     Silence is better. */
}

void audio_resume(void) {
  atomic_store(&g_cancel_decode, false);
  atomic_store(&g_is_paused, false);
}

bool audio_is_playing(void) {
  /* Rough check: if decoder is active or RB has data */
  return (!atomic_load(&g_cancel_decode) && atomic_load(&g_sound_loaded));
}

void audio_set_volume(float volume) { atomic_store(&g_volume, volume); }

float audio_get_duration(void) {
  if (!atomic_load(&g_sound_loaded))
    return 0.0f;

  ma_uint64 length;
  if (ma_decoder_get_length_in_pcm_frames(&g_decoder, &length) == MA_SUCCESS) {
    return (float)length / (float)g_decoder.outputSampleRate;
  }
  return 0.0f;
}

float audio_get_position(void) {
  if (!atomic_load(&g_sound_loaded))
    return 0.0f;

  /* Approximate based on decoder cursor.
     NOTE: This is "read position", not "hear position".
     "Hear position" = Read Position - (RB Available / Rate). */

  ma_uint64 cursor;
  if (ma_decoder_get_cursor_in_pcm_frames(&g_decoder, &cursor) == MA_SUCCESS) {
    float decoder_time = (float)cursor / (float)g_decoder.outputSampleRate;

    /* Adjust for latency in Ring Buffer */
    size_t buffered_bytes = rb_available_read(&g_rb);
    size_t bytes_per_sec =
        g_decoder.outputSampleRate * g_decoder.outputChannels * sizeof(float);
    float latency = (float)buffered_bytes / (float)bytes_per_sec;

    return (decoder_time > latency) ? (decoder_time - latency) : 0.0f;
  }
  return 0.0f;
}

void audio_seek(float seconds) {
  atomic_store(&g_seek_target, seconds);
  atomic_store(&g_seek_requested, true);
  atomic_store(&g_track_finished, false);
  atomic_store(&g_decoder_at_end, false);
}

bool audio_is_finished(void) { return atomic_load(&g_track_finished); }

/* =========================================================================
   PUBLIC EQ API
   ========================================================================= */

void audio_set_eq_band(int band_index, float gain_db, int band_count) {
  if (band_index < 0 || band_index >= EQ_BAND_COUNT_MAX)
    return;
  if (!atomic_load(&g_eq_initialized))
    return;

  /* Clamp gain to ±12 dB */
  if (gain_db >  12.0f) gain_db =  12.0f;
  if (gain_db < -12.0f) gain_db = -12.0f;

  /* Pick the right frequency table. Fallback to 10-band if index out of range for 5-band. */
  /* Safety: Ensure device is active and has valid metrics */
  if (g_device.sampleRate == 0) return;

  const double *freqs = g_eq_freqs_10band;
  if (band_count == 5 && band_index < 5) {
    freqs = g_eq_freqs_5band;
  }
  double center_freq = freqs[band_index];

  /* Build new biquad config on main thread */
  g_eq_biquad_configs[band_index] = eq_make_peaking_config(
      (double)gain_db, center_freq, 0.707,
      g_device.sampleRate, g_device.playback.channels);

  /* Signal audio thread to reinit this band */
  atomic_store(&g_eq_reinit_pending[band_index], true);
}

void audio_set_eq_enabled(bool enabled) {
  atomic_store(&g_eq_enabled, enabled);
}

void audio_eq_reset(void) {
  if (!atomic_load(&g_eq_initialized))
    return;
  for (int i = 0; i < EQ_BAND_COUNT_MAX; i++) {
    g_eq_biquad_configs[i] = eq_make_peaking_config(
        0.0, g_eq_freqs_10band[i], 0.707,
        g_device.sampleRate, g_device.playback.channels);
    atomic_store(&g_eq_reinit_pending[i], true);
  }
}
