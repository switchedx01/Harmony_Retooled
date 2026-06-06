#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint8_t *buffer;
  size_t capacity;
  atomic_size_t head; /* Write index */
  atomic_size_t tail; /* Read index */
} RingBuffer;

/* Initialize ring buffer. Capacity must be power of 2 for optimization,
   but we'll handle generic sizes for safety if needed, though power of 2 is
   preferred. Returns true on success. */
bool rb_init(RingBuffer *rb, size_t capacity);

/* Free resources */
void rb_free(RingBuffer *rb);

/* Return number of bytes available to read */
size_t rb_available_read(const RingBuffer *rb);

/* Return number of bytes available to write */
size_t rb_available_write(const RingBuffer *rb);

/* Write data to buffer. Returns number of bytes written.
   Thread-safe for Single Producer. */
size_t rb_write(RingBuffer *rb, const void *data, size_t count);

/* Read data from buffer. Returns number of bytes read.
   Thread-safe for Single Consumer. */
size_t rb_read(RingBuffer *rb, void *data, size_t count);

/* Clear the buffer (reset head/tail) */
void rb_clear(RingBuffer *rb);

#endif /* RING_BUFFER_H */
