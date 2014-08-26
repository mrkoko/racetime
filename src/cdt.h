#ifndef CDT_H
#define CDT_H
#include "swtime.h"

#define CDT_MAX_LENGTH 50
#define KEY_CDT           240

typedef struct CDT {
  SWTime lap[CDT_MAX_LENGTH];
  SWTime next_split;
  SWTime display;
  bool enable;
  bool repeat;
  bool overflow;
  uint8_t length;
  uint8_t index;
} __attribute__((__packed__)) cdt_t;

extern void cdt_set_lap(uint8_t, SWTime);
extern SWTime cdt_get_lap(uint8_t);
extern cdt_t *cdt_get(void);
extern void cdt_set_length(uint8_t);
extern uint8_t cdt_get_length(void);

extern void cdt_init(void);
extern void cdt_deinit(void);
extern void cdt_update(SWTime);
extern void cdt_reset();
extern void cdt_reset_all();

#endif