#include <assert.h>
#include <kern/ring_buffer.h>
#include <stdint.h>
#include <stdio.h>

/* These cases pin down the queue semantics used by interrupt-driven drivers. */
int main(void) {
    uint8_t storage[3], value = 0;
    struct byte_ring ring;
    byte_ring_init(&ring, storage, sizeof(storage));

    assert(!byte_ring_pop(&ring, &value));
    assert(byte_ring_push(&ring, 'a'));
    assert(byte_ring_push(&ring, 'b'));
    assert(byte_ring_push(&ring, 'c'));
    assert(!byte_ring_push(&ring, 'd')); /* Full queues drop new input. */
    assert(byte_ring_pop(&ring, &value) && value == 'a');
    assert(byte_ring_push(&ring, 'd')); /* The freed slot is reused after wraparound. */
    assert(byte_ring_pop(&ring, &value) && value == 'b');
    assert(byte_ring_pop(&ring, &value) && value == 'c');
    assert(byte_ring_pop(&ring, &value) && value == 'd');
    assert(!byte_ring_pop(&ring, &value));
    assert(!byte_ring_pop(&ring, 0));

    byte_ring_init(&ring, storage, 0);
    assert(!byte_ring_push(&ring, 1));
    puts("ring buffer tests passed");
    return 0;
}
