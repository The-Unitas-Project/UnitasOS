#ifndef UNITAS_KERN_TIMER_H
#define UNITAS_KERN_TIMER_H

#include <stdint.h>
void pit_init(uint32_t frequency_hz);
uint64_t timer_ticks(void);
void timer_sleep(uint64_t ticks);

#endif
