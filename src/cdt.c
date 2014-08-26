#include "pebble.h"
#include "cdt.h"

static cdt_t cdt;

// Set/get lap
void cdt_set_lap(uint8_t i, SWTime time) {
  cdt.lap[i] = time;
}

SWTime cdt_get_lap(uint8_t i) {
  return cdt.lap[i];
}

cdt_t *cdt_get(void) {
  return &cdt;
}

void cdt_init(void) {
//  if (persist_exists(KEY_CDT)) {
//    persist_read_data(KEY_CDT, &cdt, sizeof(cdt));
//    APP_LOG(APP_LOG_LEVEL_DEBUG, "KEY_CDT exists");
//  } else {
    cdt_reset_all();
//    APP_LOG(APP_LOG_LEVEL_DEBUG, "KEY_CDT does not exist");
//  }
}

void cdt_deinit(void) {
//  if (persist_exists(KEY_CDT)) persist_delete(KEY_CDT);
//  persist_write_data(KEY_CDT, &cdt, sizeof(cdt));
}

// Call during stopwatch reset or save
void cdt_reset(void) {
  cdt.index = 0;
  cdt.next_split = cdt.lap[cdt.index];
  cdt.overflow = false;
}

// "Zero" all settings
void cdt_reset_all(void) {
  for (int i=0; i < CDT_MAX_LENGTH; i++) {
    cdt.lap[i] = (SWTime){0, 0, 0, 0};
  }
  cdt.next_split = cdt.display = (SWTime){0, 0, 0, 0};
  cdt.enable = cdt.repeat = false;
  cdt.length = cdt.index = 0;
}

void cdt_update(SWTime sw_elapsed) {
  // Preclude if disabled
  if (cdt.enable == false) { return; }
  
  // Invoke vibration alert
  if (SWTime_compare(cdt.next_split, sw_elapsed) == -1 && cdt.overflow == false) {
    // Call buzzer
    vibes_double_pulse();
  }

  // Increment cdt_index until overflow condition detected (only in single mode)
  // or cdt.next_split is greater than sw_elapsed
  while (!cdt.overflow && (SWTime_compare(cdt.next_split, sw_elapsed)==-1)) {
    cdt.index++;
    
    // Exit condition for single mode
    if (!cdt.repeat && (cdt.index >= cdt.length)) {
      cdt.overflow = true;
      break;
    }
    
    cdt.next_split = SWTime_add(cdt.next_split, cdt.lap[cdt.index%cdt.length]);
  }
  
  // Calculate displayed timer
  cdt.display = cdt.overflow ?
                SWTime_subtract(sw_elapsed, cdt.next_split) :
                SWTime_subtract(cdt.next_split, sw_elapsed);
  
  // Prevent displaying over 9:59:59.99
  if (cdt.display.hour >= 10) {
    cdt.display = (SWTime){.hour=9, .minute=59, .second=59, .centisecond=99};
  }
}
