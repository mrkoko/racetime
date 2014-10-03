#ifndef STOPWATCH_H
#define STOPWATCH_H
#include "swtime.h"

#define CLICK_HOLD_MS 1000
#define SW_STEP_MS_SHORT 130
#define MAX_STRLEN 5
#define NUM_ROWS 3
#define NUM_GUIDES 3
#define NUM_DIGITS 8
#define NUM_LAP_MEMORY 50
#define ROW_HEIGHT 49

// Persist data keys
#define KEY_SPLIT_MEMORY  140
#define KEY_SESSION       160
#define KEY_STOPWATCH     180
#define KEY_SESSION_INDEX 200
#define KEY_SAVE_TIME     220
#define KEY_INVERT_COLOR  260
  
// frame size for bitham_34_medium_numbers and gothic_28_bold
#define WIDTH_BITHAM34MN  23
#define HEIGHT_BITHAM34MN 35
#define WIDTH_GOTHIC28B   12
#define HEIGHT_GOTHIC28B  29  
#define WIDTH_DELIMITER   7

// Placeholder for time_ms values
typedef struct WatchTime {
  time_t s;
  uint16_t ms;
} __attribute__((__packed__)) WatchTime_t;

typedef struct Session {
  uint8_t start_index;
  uint8_t end_index;
} __attribute__((__packed__)) Session_t;

extern SWTime get_lap_time(Session_t, uint8_t);

// Lap memory-related components
extern SWTime    split_memory[NUM_LAP_MEMORY+1];
extern Session_t session[NUM_LAP_MEMORY];
extern time_t    save_time[NUM_LAP_MEMORY];
extern uint8_t   session_index;
extern bool      invert_color;

#endif