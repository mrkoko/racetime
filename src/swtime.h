#ifndef SWTIME_H
#define SWTIME_H

// 32-bit representation of "stopwatch" time
typedef struct SWTime {
  signed char centisecond; // 0-99
  signed char second;      // 0-59
  signed char minute;      // 0-59
  signed char hour;        // 0-99
} __attribute__((__packed__)) SWTime;

extern SWTime SWTime_add(SWTime, SWTime);
extern SWTime SWTime_subtract(SWTime, SWTime);
extern signed char SWTime_compare(SWTime, SWTime);

#endif