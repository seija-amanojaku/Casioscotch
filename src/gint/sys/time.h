#ifndef FAKEASS_SYS_TIME_H
#define FAKEASS_SYS_TIME_H

#include <time.h>

// very safe timeval do not steal
struct timeval { time_t tv_sec; uint64_t tv_usec; };

int gettimeofday(struct timeval *tv_time, void *scheisse2);

#endif // FAKEASS_SYS_TIME_H
