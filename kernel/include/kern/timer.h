#ifndef UNITAS_KERN_TIMER_H
#define UNITAS_KERN_TIMER_H

#include <stdint.h>
/* Program the Programmable Interval Timer (PIT). Zero selects 100 Hz. */
void pit_init(uint32_t frequency_hz);
uint64_t timer_ticks(void);
/* Requires interrupts to be enabled. The delay uses PIT ticks. */
void timer_sleep(uint64_t ticks);

#endif
