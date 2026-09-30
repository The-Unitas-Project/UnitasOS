#ifndef UNITAS_KERN_RTC_H
#define UNITAS_KERN_RTC_H

#include <stdint.h>

/* Read stable CMOS time. Return Unix seconds or -1 on invalid data or timeout. */
int rtc_read_unix_seconds(int64_t *seconds);

#endif
