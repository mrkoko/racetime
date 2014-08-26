#include "swtime.h"

SWTime SWTime_add(SWTime augend, SWTime addend) {
  signed int centisecond = (signed int)augend.centisecond + (signed int)addend.centisecond;
  signed int second = (signed int)augend.second + (signed int)addend.second;
  signed int minute = (signed int)augend.minute + (signed int)addend.minute;
  signed int hour   = (signed int)augend.hour + (signed int)addend.hour;

  // Carry-over calculation
  while (centisecond >= 100) {
    centisecond -= 100;
    second++;
  }
  while (second >= 60) {
    second -= 60;
    minute++;
  }
  while (minute >= 60) {
    minute -= 60;
    hour++;
  }
  SWTime sum = {.centisecond=(signed char)centisecond,
                .second=(signed char)second,
                .minute=(signed char)minute,
                .hour=(signed char)hour};
  return sum;
}

SWTime SWTime_subtract(SWTime minuend, SWTime subtrahend) {
  signed int centisecond = (signed int)minuend.centisecond - (signed int)subtrahend.centisecond;
  signed int second = (signed int)minuend.second - (signed int)subtrahend.second;
  signed int minute = (signed int)minuend.minute - (signed int)subtrahend.minute;
  signed int hour   = (signed int)minuend.hour - (signed int)subtrahend.hour;

  // Borrow-from calculation
  while (centisecond < 0) {
    centisecond += 100;
    second--;
  }
  while (second < 0) {
    second += 60;
    minute--;
  }
  while (minute < 0) {
    minute += 60;
    hour--;
  }
  SWTime difference = {.centisecond=(signed char)centisecond,
                       .second=(signed char)second,
                       .minute=(signed char)minute,
                       .hour=(signed char)hour};
  return difference;
}

// Return -1 if a is less than b, 0 if equal, 1 if a is greater than b
signed char SWTime_compare(SWTime a, SWTime b) {
  return (a.hour < b.hour) ? -1 :
         (a.hour > b.hour) ? 1 :
         (a.minute < b.minute) ? -1 :
         (a.minute > b.minute) ? 1 :
         (a.second < b.second) ? -1 :
         (a.second > b.second) ? 1 :
         (a.centisecond < b.centisecond) ? -1 :
         (a.centisecond > b.centisecond) ? 1 :
         0;
}