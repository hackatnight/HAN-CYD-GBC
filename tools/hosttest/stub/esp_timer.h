#pragma once
#include <stdint.h>
#include <sys/time.h>
static inline int64_t esp_timer_get_time(void){struct timeval t;gettimeofday(&t,0);return (int64_t)t.tv_sec*1000000+t.tv_usec;}
