#ifndef VISUALIZER_BUFFER_H
#define VISUALIZER_BUFFER_H

#include "common.h"
#include <stdatomic.h>

/**
 * Visualizer Ring Buffer
 * ----------------------
 * A Single-Producer (Audio Thread), Single-Consumer (UI Thread) lock-free ring
 * buffer. Stores raw PCM float samples.
 *
 * P10 Compliance:
 * - No locks (prevents audio thread blocking)
 * - Fixed size (determined at compile time or initialization)
 * - Atomic read/write indices
 */

// Size in float samples. 4096 is enough for ~92ms at 44.1kHz mono,
// or ~46ms stereo. We probably want enough for a full frame of visualization.
// Let's go with 8192 to be safe (approx 185ms @ 44.1kHz).
#define VIS_BUFFER_SIZE 8192

typedef struct {
  float buffer[VIS_BUFFER_SIZE];
  atomic_size_t write_pos; // Modified by Audio Thread
  atomic_size_t read_pos;  // Modified by UI Thread
} VisBuffer;

// Initialize the buffer (clears it)
void vis_buffer_init(void);

// Push samples into the buffer (Called by Audio Thread)
// If buffer is full, oldest data will be overwritten (circular) or dropped?
// For visualizers, dropping old data or overwriting is fine, but we should
// prioritize new data. A standard ring buffer behavior is "if full, return
// error" or "overwrite". We'll implement "overwrite" if full, OR just standard
// "drop if full" to keep it simple lock-free. Actually, specifically for
// visualizers, we want the *latest* data. So if the UI is slow, we should
// probably just advance the write head and overwrite? A standard FIFO drops if
// full. Let's stick to standard behavior: If full, we can drop the *new*
// samples (bad for visualizer sync) or overwrite (complex for lock-free).
// Simpler approach: Just write. If read_pos is lapped, so be it. The consumer
// might read torn data. Ideally: Check available space. If not enough, skip
// write.
void vis_buffer_push(const float *samples, size_t count);

// Read samples from the buffer (Called by UI Thread)
// Returns number of samples read. This CONSUMES the data.
size_t vis_buffer_read(float *out_buffer, size_t count);

// Peek at the most recent samples WITHOUT consuming them (Called by UI Thread)
// This reads the last 'count' samples from the buffer without advancing
// read_pos. Returns number of samples peeked. Ideal for visualizers that need
// consistent data.
size_t vis_buffer_peek(float *out_buffer, size_t count);

// Get number of available samples
size_t vis_buffer_available(void);

#endif // VISUALIZER_BUFFER_H
