#include "visualizer_buffer.h"
#include <string.h>

static VisBuffer g_vis_buffer;

void vis_buffer_init(void) {
  atomic_init(&g_vis_buffer.write_pos, 0);
  atomic_init(&g_vis_buffer.read_pos, 0);
  memset(g_vis_buffer.buffer, 0, sizeof(g_vis_buffer.buffer));
}

// Single-Producer (Audio Callback)
// Simple circular buffer write - just advances write_pos
void vis_buffer_push(const float *samples, size_t count) {
  const size_t capacity = VIS_BUFFER_SIZE;

  size_t write_idx =
      atomic_load_explicit(&g_vis_buffer.write_pos, memory_order_relaxed);

  // Write samples to buffer (may overwrite old data, which is fine for
  // visualizer)
  for (size_t i = 0; i < count; ++i) {
    g_vis_buffer.buffer[write_idx] = samples[i];
    write_idx = (write_idx + 1) % capacity;
  }

  atomic_store_explicit(&g_vis_buffer.write_pos, write_idx,
                        memory_order_release);
}

// Single-Consumer (UI Thread) - reads and consumes data
size_t vis_buffer_read(float *out_buffer, size_t count) {
  size_t write_idx =
      atomic_load_explicit(&g_vis_buffer.write_pos, memory_order_acquire);
  size_t read_idx =
      atomic_load_explicit(&g_vis_buffer.read_pos, memory_order_relaxed);

  const size_t capacity = VIS_BUFFER_SIZE;
  size_t available = (write_idx + capacity - read_idx) % capacity;

  if (count > available) {
    count = available;
  }

  if (count == 0)
    return 0;

  for (size_t i = 0; i < count; ++i) {
    out_buffer[i] = g_vis_buffer.buffer[read_idx];
    read_idx = (read_idx + 1) % capacity;
  }

  atomic_store_explicit(&g_vis_buffer.read_pos, read_idx, memory_order_release);
  return count;
}

size_t vis_buffer_available(void) {
  size_t write_idx =
      atomic_load_explicit(&g_vis_buffer.write_pos, memory_order_acquire);
  size_t read_idx =
      atomic_load_explicit(&g_vis_buffer.read_pos, memory_order_relaxed);
  const size_t capacity = VIS_BUFFER_SIZE;
  return (write_idx + capacity - read_idx) % capacity;
}

// Peek at the most recent samples WITHOUT consuming them
// This reads the last 'count' samples ending at write_pos
// Works as a sliding window - always sees the most recent audio
size_t vis_buffer_peek(float *out_buffer, size_t count) {
  const size_t capacity = VIS_BUFFER_SIZE;

  size_t write_idx =
      atomic_load_explicit(&g_vis_buffer.write_pos, memory_order_acquire);

  // Limit count to buffer capacity
  if (count > capacity - 1) {
    count = capacity - 1;
  }

  if (count == 0)
    return 0;

  // Read the most recent 'count' samples ending at write_idx
  // Start position is (write_idx - count) wrapped around
  size_t start_idx = (write_idx + capacity - count) % capacity;

  for (size_t i = 0; i < count; ++i) {
    out_buffer[i] = g_vis_buffer.buffer[(start_idx + i) % capacity];
  }

  return count;
}
