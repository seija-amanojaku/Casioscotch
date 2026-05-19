#include <sys/time.h>

#include <stddef.h>

int gettimeofday(struct timeval *tv_time, void *scheisse2) /// IT'S T-V TIMMME
{
    // bery safe code
    tv_time->tv_sec = time(NULL);
    tv_time->tv_usec = 0; // microseconds may be viable lol
    return 0; // TODO
}
