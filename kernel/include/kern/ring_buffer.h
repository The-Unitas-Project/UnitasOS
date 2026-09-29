#ifndef UNITAS_KERN_RING_BUFFER_H
#define UNITAS_KERN_RING_BUFFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Callers must serialize push/pop when an IRQ can access the queue. */
struct byte_ring {
    uint8_t *data;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
};

static inline void byte_ring_init(struct byte_ring *ring, uint8_t *storage,
                                  size_t capacity) {
    ring->data = storage;
    ring->capacity = capacity;
    ring->head = ring->tail = ring->count = 0;
}
static inline bool byte_ring_push(struct byte_ring *ring, uint8_t value) {
    if (!ring || !ring->data || !ring->capacity || ring->count == ring->capacity)
        return false;
    ring->data[ring->head] = value;
    ring->head = (ring->head + 1) % ring->capacity;
    ring->count++;
    return true;
}
static inline bool byte_ring_pop(struct byte_ring *ring, uint8_t *value) {
    if (!ring || !value || !ring->data || ring->count == 0)
        return false;
    *value = ring->data[ring->tail];
    ring->tail = (ring->tail + 1) % ring->capacity;
    ring->count--;
    return true;
}

#endif
