#include "ring_buffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MIN(a, b) ((a) < (b) ? (a) : (b))

bool rb_init(RingBuffer *rb, size_t capacity) {
  if (!rb)
    return false;

  rb->buffer = malloc(capacity);
  if (!rb->buffer)
    return false;

  rb->capacity = capacity;
  atomic_init(&rb->head, 0);
  atomic_init(&rb->tail, 0);

  return true;
}

void rb_free(RingBuffer *rb) {
  if (rb && rb->buffer) {
    free(rb->buffer);
    rb->buffer = NULL;
  }
}

size_t rb_available_read(const RingBuffer *rb) {
  size_t head = atomic_load_explicit(&rb->head, memory_order_acquire);
  size_t tail = atomic_load_explicit(&rb->tail, memory_order_acquire);

  if (head >= tail) {
    return head - tail;
  } else {
    return rb->capacity - (tail - head);
  }
}

size_t rb_available_write(const RingBuffer *rb) {
  /* Capacity - 1 to distinguish full from empty (if head==tail, it's empty) */
  size_t read_avail = rb_available_read(rb);
  return (rb->capacity - 1) - read_avail;
}

size_t rb_write(RingBuffer *rb, const void *data, size_t count) {
  if (!rb || !data || count == 0)
    return 0;

  const uint8_t *in_data = (const uint8_t *)data;
  size_t available = rb_available_write(rb);
  if (count > available) {
    count = available;
  }

  if (count == 0)
    return 0;

  size_t head = atomic_load_explicit(&rb->head, memory_order_relaxed);
  size_t tail = atomic_load_explicit(
      &rb->tail, memory_order_acquire); /* Sync with reader */

  size_t to_end = rb->capacity - head;
  if (count <= to_end) {
    memcpy(rb->buffer + head, in_data, count);
    head += count;
    if (head == rb->capacity)
      head = 0;
  } else {
    /* Wrap around */
    memcpy(rb->buffer + head, in_data, to_end);
    memcpy(rb->buffer, in_data + to_end, count - to_end);
    head = count - to_end;
  }

  atomic_store_explicit(&rb->head, head, memory_order_release);
  return count;
}

size_t rb_read(RingBuffer *rb, void *data, size_t count) {
  if (!rb || !data || count == 0)
    return 0;

  uint8_t *out_data = (uint8_t *)data;
  size_t available = rb_available_read(rb);
  if (count > available) {
    count = available;
  }

  if (count == 0)
    return 0;

  size_t tail = atomic_load_explicit(&rb->tail, memory_order_relaxed);
  size_t head = atomic_load_explicit(
      &rb->head, memory_order_acquire); /* Sync with writer */

  size_t to_end = rb->capacity - tail;
  if (count <= to_end) {
    memcpy(out_data, rb->buffer + tail, count);
    tail += count;
    if (tail == rb->capacity)
      tail = 0;
  } else {
    /* Wrap around */
    memcpy(out_data, rb->buffer + tail, to_end);
    memcpy(out_data, rb->buffer, count - to_end);
    tail = count - to_end;
  }

  atomic_store_explicit(&rb->tail, tail, memory_order_release);
  return count;
}

void rb_clear(RingBuffer *rb) {
  if (!rb)
    return;
  atomic_store_explicit(&rb->head, 0, memory_order_relaxed);
  atomic_store_explicit(&rb->tail, 0, memory_order_relaxed);
}
