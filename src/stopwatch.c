#include "pebble.h"
#include "stopwatch.h"
#include "cdt.h"
#include "ui_review.h"
#include "ui_instant_recall.h"
#include "ui_main_menu.h"
#include "ui_timer_config.h"
#include "ui_preset_assistant.h"

// Enums
enum enum_sw_state {SW_STATE_IDLE,
                    SW_STATE_RUN,
                    SW_STATE_LAP_RECORD,
                    SW_STATE_STOP,
                    SW_STATE_RESET,
                    SW_STATE_RESET_CONFIRM,
                    SW_STATE_SAVE_CONFIRM};

enum warning_flag_e {WARNING_FLAG_IDLE,
                     WARNING_FLAG_MESSAGE,
                     WARNING_FLAG_LAP};

// Typedefs
typedef struct WatchfaceDigit {
  GRect frame;
  GFont font;
  char str[2];
} __attribute__((__packed__)) WatchfaceDigit_t;

// Time components
typedef struct Stopwatch {
  WatchTime_t time_elapsed;
  WatchTime_t time_current;
  WatchTime_t time_start;
  WatchTime_t time_offset;
  uint8_t sw_state;
} __attribute__((__packed__)) Stopwatch_t;

// Helper function declaration
static void window_load(Window *);
static void window_appear(Window *);
static void window_disappear(Window *);
static void window_unload(Window *);
static void move_to_state(uint8_t);
static void update_display_guide(uint8_t);
static void watchface_init(void);
static void persist_deinit(void);

static Window *window;

// Label text layer
static TextLayer *text_layer_label[NUM_ROWS];
static char text_header[3][11];

// Main watch face
static Layer *layer_watchface;
static WatchfaceDigit_t watchface_digit[NUM_ROWS][NUM_DIGITS];
static WatchfaceDigit_t watchface_delimiter[NUM_ROWS][NUM_DELIMITERS];

// Guide bitmap on the right side
static BitmapLayer *bitmap_layer[NUM_ROWS];
static GBitmap *gbitmap_start;
static GBitmap *gbitmap_menu;
static GBitmap *gbitmap_lap;
static GBitmap *gbitmap_reset;
static GBitmap *gbitmap_save;
static GBitmap *gbitmap_stop;
static GBitmap *gbitmap_view;

// Temporary warning text layer
static Layer *layer_warning;
static char *warning_font_key;
static char warning_str_title[7];
static char warning_str_long[28];
static char warning_str_short[16];
static uint8_t warning_flag;
static signed int ms_to_clear_warning;

// Color inversion layer
static InverterLayer *inverter_layer;

static AppTimer *timer;
static Stopwatch_t stopwatch;

// Displayed stopwatch time, and lap memory
static SWTime sw_elapsed = {0, 0, 0, 0};

// Lap memory-related components
SWTime    split_memory[NUM_LAP_MEMORY+1]; // +1 to be extra safe
Session_t session[NUM_LAP_MEMORY];
time_t    save_time[NUM_LAP_MEMORY];
uint8_t   session_index;

// 0 for white background, 1 for black background
bool invert_color;

SWTime get_lap_time(Session_t s, uint8_t abs_lap_index) {
  SWTime lap_time = (SWTime){0,0,0,0};
  if (s.start_index == abs_lap_index) {
    lap_time = split_memory[abs_lap_index];
  } else {
    lap_time = SWTime_subtract(split_memory[abs_lap_index], split_memory[abs_lap_index-1]);
  }
  return lap_time;
}

// Short-hand for setting warning text layer
static void set_warning_text(char *font_key, char *str) {
  warning_font_key = font_key;
  strcpy(warning_str_long, str);
  warning_flag = WARNING_FLAG_MESSAGE;
  layer_mark_dirty(layer_warning);
}

// Short-hand for setting warning text layer
static void set_warning_lap(char *font_key) {
  warning_font_key = font_key;
  warning_flag = WARNING_FLAG_LAP;
  layer_mark_dirty(layer_warning);
}

// Short-hand for clearning warning textLayer
static void clear_warning_text(void) {
  warning_flag = WARNING_FLAG_IDLE;
  layer_mark_dirty(layer_warning);
}

// Update elapsed stopwatch time
static void update_time(void) {
  switch (stopwatch.sw_state) {
    case SW_STATE_RUN:
    case SW_STATE_LAP_RECORD:
      // What is real time right now?
      time_ms(&stopwatch.time_current.s, &stopwatch.time_current.ms);
  
      // Recalculate elapsed time
      // Calculate millisecond, borrow from seconds if negative, carry over if 1000 or greater
      int s =  (int)stopwatch.time_offset.s  + (int)stopwatch.time_current.s -  (int)stopwatch.time_start.s;
      int ms = (int)stopwatch.time_offset.ms + (int)stopwatch.time_current.ms - (int)stopwatch.time_start.ms;
      
      // Borrow from s
      while (ms < 0) { ms += 1000; s--; }
    
      // Carry over to s
      while (ms >= 1000) { ms -= 1000; s++; }
    
      // Update quickly
      stopwatch.time_elapsed = (WatchTime_t){(time_t)s, (uint16_t)ms};
      break;
  }
  
  // Convert in terms of SWTime struct
  sw_elapsed.hour        = (signed int)stopwatch.time_elapsed.s / 3600;
  sw_elapsed.minute      = ((signed int)stopwatch.time_elapsed.s % 3600) / 60;
  sw_elapsed.second      = (signed int)stopwatch.time_elapsed.s % 60;
  sw_elapsed.centisecond = (signed int)stopwatch.time_elapsed.ms / 10;
  
  // split_memory at session[session_index].end_index is displayed as current split time
  split_memory[session[session_index].end_index] = sw_elapsed;
  
  // Update countdown timer
  cdt_update(sw_elapsed);
}

// Short-hand to record a lap
static void record_lap() {
  char substr[11];
  cdt_t *cdt = cdt_get();

  update_time();
  if (session[session_index].end_index<NUM_LAP_MEMORY-1) {
    session[session_index].end_index++;
    
    uint8_t prev_abs_lap_index = session[session_index].end_index - 1;
    uint8_t prev_rel_lap_index = prev_abs_lap_index - session[session_index].start_index;

    // Push temporary lap record message
    SWTime prev_split_time = split_memory[prev_abs_lap_index];
    SWTime prev_lap_time = get_lap_time(session[session_index], prev_abs_lap_index);
    
    // Header message
    snprintf(warning_str_title, sizeof(warning_str_title),
             "LAP %d\n", prev_rel_lap_index+1);
    
    // Calculate target split at previous timer index
    if (cdt->enable) {
      SWTime prev_cdt_target_split = (SWTime){0, 0, 0, 0};
      SWTime cdt_delta = (SWTime){0, 0, 0, 0};
      bool cdt_delta_minus = false;
      cdt_t *cdt = cdt_get();
      
      for (uint8_t i = 0; i<=prev_rel_lap_index; i++) {
        // Break condition for single mode: iterator i at cdt_length or higher
        if (!cdt->repeat && (i >= cdt->length)) { break; }
        
        // Below formula looks strange first, but it considers loop-over in repeat mode
        prev_cdt_target_split = SWTime_add(prev_cdt_target_split, cdt_get_lap(i%cdt->length));
      }
      cdt_delta_minus = (SWTime_compare(prev_split_time, prev_cdt_target_split) == -1);
      cdt_delta = cdt_delta_minus ? 
                  SWTime_subtract(prev_cdt_target_split, prev_split_time) : 
                  SWTime_subtract(prev_split_time, prev_cdt_target_split);
    
      // Offset from target timer split
      if (cdt_delta.hour > 0) {
        snprintf(substr, sizeof(substr), "%d:%02d:%02d\n",
                 cdt_delta.hour, cdt_delta.minute, cdt_delta.second);
      } else if (cdt_delta.minute > 0) {
        snprintf(substr, sizeof(substr), "%d:%02d\n", cdt_delta.minute, cdt_delta.second);
      } else {
        snprintf(substr, sizeof(substr), "%d\n", cdt_delta.second);
      }
      strcpy(warning_str_long, cdt_delta_minus ? "-" : "+");
      strcat(warning_str_long, substr);
      strcpy(warning_str_short, "Timer\nSplit\nLap");
    } else {
      strcpy(warning_str_long,  "");
      strcpy(warning_str_short, "Split\nLap");
    }
    
    // Display split time
    if (prev_split_time.hour > 0)
      snprintf(substr, sizeof(substr), "%d:%02d:%02d\n", prev_split_time.hour, prev_split_time.minute, prev_split_time.second);
    else
      snprintf(substr, sizeof(substr), "%d:%02d\n", prev_split_time.minute, prev_split_time.second);
    strcat(warning_str_long, substr);
    
    // Display lap time
    if (prev_lap_time.hour > 0)
      snprintf(substr, sizeof(substr), "%d:%02d:%02d\n", prev_lap_time.hour, prev_lap_time.minute, prev_lap_time.second);
    else
      snprintf(substr, sizeof(substr), "%d:%02d\n", prev_lap_time.minute, prev_lap_time.second);
    strcat(warning_str_long, substr);
    
    // Push temporary warning message
    set_warning_lap(FONT_KEY_GOTHIC_24_BOLD);
    ms_to_clear_warning = 5000;
  } else {
    // Push temporary warning message
    set_warning_text(FONT_KEY_GOTHIC_24_BOLD, "OUT\nOF\nMEMORY");
    ms_to_clear_warning = 5000;
    vibes_short_pulse();
  }
}

// Short-hand to stop the stopwatch
static void stop_sw() {
  update_time();
  stopwatch.time_offset = stopwatch.time_elapsed;
  move_to_state(SW_STATE_STOP);
}

static void layer_watchface_update_callback(Layer *layer, GContext *ctx) {
  graphics_context_set_text_color(ctx, GColorBlack);
  for (int i=0; i<NUM_ROWS; i++) {
    for (int j=0; j<NUM_DIGITS; j++)
      graphics_draw_text(ctx, watchface_digit[i][j].str, watchface_digit[i][j].font, watchface_digit[i][j].frame,
                         GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    for (int j=0; j<NUM_DELIMITERS; j++)
      graphics_draw_text(ctx, watchface_delimiter[i][j].str, watchface_delimiter[i][j].font, watchface_delimiter[i][j].frame,
                         GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
  }
}

static void layer_warning_update_callback(Layer *layer, GContext *ctx) {
  GRect frame = layer_get_frame(layer);
  GSize content_size[3];
  GRect resized_text[3], resized_box;
  
  switch (warning_flag) {
    case WARNING_FLAG_IDLE: 
      return;
    case WARNING_FLAG_MESSAGE:
      content_size[0] = graphics_text_layout_get_content_size(
                          warning_str_long, fonts_get_system_font(warning_font_key), frame,
                          GTextOverflowModeWordWrap, GTextAlignmentCenter);
      resized_text[0] = GRect((frame.size.w-content_size[0].w)/2, (frame.size.h-content_size[0].h)/2-5,
                              content_size[0].w, content_size[0].h);
      resized_box  = GRect(resized_text[0].origin.x-7, resized_text[0].origin.y,
                           resized_text[0].size.w+14, resized_text[0].size.h+10);

      // Draw bounding box
      graphics_context_set_fill_color(ctx, GColorWhite);
      graphics_fill_rect(ctx, resized_box, 0, GCornerNone);
      graphics_context_set_stroke_color(ctx, GColorBlack);
      graphics_draw_rect(ctx, resized_box);
  
      // Draw text
      graphics_context_set_text_color(ctx, GColorBlack);
      graphics_draw_text(ctx, warning_str_long, fonts_get_system_font(warning_font_key), resized_text[0],
                         GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
      break;
    case WARNING_FLAG_LAP:
      // Get width from title, short, large content
      content_size[0] = graphics_text_layout_get_content_size(
                          warning_str_title, fonts_get_system_font(warning_font_key), frame,
                          GTextOverflowModeWordWrap, GTextAlignmentCenter);
      content_size[1] = graphics_text_layout_get_content_size(
                          warning_str_short, fonts_get_system_font(warning_font_key), frame,
                          GTextOverflowModeWordWrap, GTextAlignmentRight);
      content_size[2] = graphics_text_layout_get_content_size(
                          warning_str_long, fonts_get_system_font(warning_font_key), frame,
                          GTextOverflowModeWordWrap, GTextAlignmentRight);
      
      resized_text[0] = GRect((frame.size.w - content_size[1].w - content_size[2].w - 10)/2,
                              (frame.size.h - content_size[0].h - content_size[1].h)/2 - 5,
                              content_size[1].w + content_size[2].w + 10,
                              content_size[0].h);
      resized_text[1] = GRect(resized_text[0].origin.x,
                              resized_text[0].origin.y + resized_text[0].size.h,
                              content_size[1].w,
                              content_size[1].h);
      resized_text[2] = GRect(resized_text[1].origin.x + resized_text[1].size.w + 10,
                              resized_text[1].origin.y,
                              content_size[2].w,
                              content_size[2].h);
      resized_box  = GRect(resized_text[0].origin.x-7,
                           resized_text[0].origin.y,
                           resized_text[1].size.w + resized_text[2].size.w + 24,
                           resized_text[0].size.h + resized_text[1].size.h + 10);
    
      // Draw bounding box
      graphics_context_set_fill_color(ctx, GColorWhite);
      graphics_fill_rect(ctx, resized_box, 0, GCornerNone);
      graphics_context_set_stroke_color(ctx, GColorBlack);
      graphics_draw_rect(ctx, resized_box);
  
      // Draw text
      graphics_context_set_text_color(ctx, GColorBlack);
      graphics_draw_text(ctx, warning_str_title, fonts_get_system_font(warning_font_key), resized_text[0],
                         GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
      graphics_draw_text(ctx, warning_str_short, fonts_get_system_font(warning_font_key), resized_text[1],
                         GTextOverflowModeWordWrap, GTextAlignmentRight, NULL);
      graphics_draw_text(ctx, warning_str_long, fonts_get_system_font(warning_font_key), resized_text[2],
                         GTextOverflowModeWordWrap, GTextAlignmentRight, NULL);
      break;
  }
}

void move_to_state(uint8_t state) {
  stopwatch.sw_state = state;
  update_display_guide(state);
}

void update_display_guide(uint8_t state) {
  // Display guide text
  switch (state) {
    case SW_STATE_IDLE:
      bitmap_layer_set_bitmap(bitmap_layer[0], gbitmap_start);
      bitmap_layer_set_bitmap(bitmap_layer[1], gbitmap_menu);
      break;
    case SW_STATE_STOP:
    case SW_STATE_SAVE_CONFIRM:
    case SW_STATE_RESET_CONFIRM:
      bitmap_layer_set_bitmap(bitmap_layer[0], gbitmap_start);
      bitmap_layer_set_bitmap(bitmap_layer[1], gbitmap_reset);
      bitmap_layer_set_bitmap(bitmap_layer[2], gbitmap_save);
      break;
    case SW_STATE_RUN:
    case SW_STATE_LAP_RECORD:
      bitmap_layer_set_bitmap(bitmap_layer[0], gbitmap_lap);
      bitmap_layer_set_bitmap(bitmap_layer[1], gbitmap_view);
      bitmap_layer_set_bitmap(bitmap_layer[2], gbitmap_stop);
      break;
  }
  layer_set_hidden(bitmap_layer_get_layer(bitmap_layer[0]), false);
  layer_set_hidden(bitmap_layer_get_layer(bitmap_layer[1]), false);
  layer_set_hidden(bitmap_layer_get_layer(bitmap_layer[2]), (state==SW_STATE_IDLE) ? true : false);
}

// Update displayed text
static void update_display(void) {
  uint8_t row;
  cdt_t *cdt = cdt_get();
  
  // 1st row: display cdt time
  row = 0;
  strcpy(watchface_delimiter[row][0].str, ":");
  strcpy(watchface_delimiter[row][1].str, ":");
  if (cdt->enable) {
    snprintf(watchface_digit[row][0].str, 2, (cdt->overflow==true) ? "+" : "-");
    snprintf(watchface_digit[row][1].str, 2, "%d", cdt->display.hour%10);
    snprintf(watchface_digit[row][2].str, 2, "%d", cdt->display.minute/10);
    snprintf(watchface_digit[row][3].str, 2, "%d", cdt->display.minute%10);
    snprintf(watchface_digit[row][4].str, 2, "%d", cdt->display.second/10);
    snprintf(watchface_digit[row][5].str, 2, "%d", cdt->display.second%10);
  } else {
    strcpy(watchface_digit[row][0].str, " ");
    strcpy(watchface_digit[row][1].str, "-");
    strcpy(watchface_digit[row][2].str, "-");
    strcpy(watchface_digit[row][3].str, "-");
    strcpy(watchface_digit[row][4].str, "-");
    strcpy(watchface_digit[row][5].str, "-");
  }
  
  // 2nd row, display total elapsed time
  row = 1;
  if (sw_elapsed.hour > 0) {
    snprintf(watchface_digit[row][0].str, 2, "%d", sw_elapsed.hour/10);
    if (sw_elapsed.hour<10) strcpy(watchface_digit[row][0].str, "");
    snprintf(watchface_digit[row][1].str, 2, "%d", sw_elapsed.hour%10);
    snprintf(watchface_digit[row][2].str, 2, "%d", sw_elapsed.minute/10);
    snprintf(watchface_digit[row][3].str, 2, "%d", sw_elapsed.minute%10);
    snprintf(watchface_digit[row][4].str, 2, "%d", sw_elapsed.second/10);
    snprintf(watchface_digit[row][5].str, 2, "%d", sw_elapsed.second%10);
    strcpy(watchface_delimiter[row][0].str, ":");
    strcpy(watchface_delimiter[row][1].str, ":");
  } else {
    snprintf(watchface_digit[row][0].str, 2, "%d", sw_elapsed.minute/10);
    if (sw_elapsed.minute<10) strcpy(watchface_digit[row][0].str, "");
    snprintf(watchface_digit[row][1].str, 2, "%d", sw_elapsed.minute%10);
    snprintf(watchface_digit[row][2].str, 2, "%d", sw_elapsed.second/10);
    snprintf(watchface_digit[row][3].str, 2, "%d", sw_elapsed.second%10);
    snprintf(watchface_digit[row][4].str, 2, "%d", sw_elapsed.centisecond/10);
    snprintf(watchface_digit[row][5].str, 2, "%d", sw_elapsed.centisecond%10);
    strcpy(watchface_delimiter[row][0].str, ":");
    strcpy(watchface_delimiter[row][1].str, ".");
  }
  
  // 3rd row: display lap time
  row = 2;
  SWTime lap_time = get_lap_time(session[session_index], session[session_index].end_index);
  if (lap_time.hour > 0) {
    snprintf(watchface_digit[row][0].str, 2, "%d", lap_time.hour/10);
    if (lap_time.hour<10) strcpy(watchface_digit[row][0].str, ""); 
    snprintf(watchface_digit[row][1].str, 2, "%d", lap_time.hour%10);
    snprintf(watchface_digit[row][2].str, 2, "%d", lap_time.minute/10);
    snprintf(watchface_digit[row][3].str, 2, "%d", lap_time.minute%10);
    snprintf(watchface_digit[row][4].str, 2, "%d", lap_time.second/10);
    snprintf(watchface_digit[row][5].str, 2, "%d", lap_time.second%10);
    strcpy(watchface_delimiter[row][0].str, ":");
    strcpy(watchface_delimiter[row][1].str, ":");
  } else {
    snprintf(watchface_digit[row][0].str, 2, "%d", lap_time.minute/10);
    if (lap_time.minute<10) strcpy(watchface_digit[row][0].str, ""); 
    snprintf(watchface_digit[row][1].str, 2, "%d", lap_time.minute%10);
    snprintf(watchface_digit[row][2].str, 2, "%d", lap_time.second/10);
    snprintf(watchface_digit[row][3].str, 2, "%d", lap_time.second%10);
    snprintf(watchface_digit[row][4].str, 2, "%d", lap_time.centisecond/10);
    snprintf(watchface_digit[row][5].str, 2, "%d", lap_time.centisecond%10);
    strcpy(watchface_delimiter[row][0].str, ":");
    strcpy(watchface_delimiter[row][1].str, ".");
  }
  
  // No need to mark the layer dirty - text_layer_set_text will take care of that
  //layer_mark_dirty(layer_watchface);
  snprintf(text_header[0], sizeof(text_header[0]), "TIMER %d", (cdt->repeat) ? ((cdt->index)%(cdt->length))+1 : (cdt->index)+1);
  text_layer_set_text(text_layer_label[0], text_header[0]);
  snprintf(text_header[1], sizeof(text_header[1]), "SESSION %d", session_index+1);
  text_layer_set_text(text_layer_label[1], text_header[1]);
  snprintf(text_header[2], sizeof(text_header[2]), "LAP %d",
           session[session_index].end_index-session[session_index].start_index+1);
  text_layer_set_text(text_layer_label[2], text_header[2]);
}

// Called every SW_STEP_MS ms
static void timer_callback(void *data) {
  // Update time and display
  update_time();
  update_display();
  
  if (ms_to_clear_warning > 0) {
    ms_to_clear_warning -= SW_STEP_MS_SHORT;
    if (ms_to_clear_warning < 0) {
      clear_warning_text();
      move_to_state((stopwatch.sw_state == SW_STATE_LAP_RECORD) ? SW_STATE_RUN : stopwatch.sw_state);
    }
  }
  
  // Restart timer
  timer = app_timer_register(SW_STEP_MS_SHORT, timer_callback, NULL);
}

//----- Begin window load/unload
static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect frame = layer_get_frame(window_layer);
  
  // Watchface layer
  layer_watchface = layer_create(frame);
  layer_set_update_proc(layer_watchface, layer_watchface_update_callback);
  layer_add_child(window_layer, layer_watchface);
  
  // Set up text_layers for NUM_ROWS rows, NUM_DIGITS digits
  for (int row=0; row < NUM_ROWS; row++) {
    text_layer_label[row] = text_layer_create(GRect(2, 2+ROW_HEIGHT*row, 60, 14));
    text_layer_set_text_alignment(text_layer_label[row], GTextAlignmentLeft);
    layer_add_child(window_layer, text_layer_get_layer(text_layer_label[row]));
  }

  // Add bitmap layers
  bitmap_layer[0] = bitmap_layer_create(GRect(133, 2, 9, 40));
  bitmap_layer_set_alignment(bitmap_layer[0], GAlignTop);
  layer_add_child(window_layer, bitmap_layer_get_layer(bitmap_layer[0]));
  
  bitmap_layer[1] = bitmap_layer_create(GRect(133, 55, 9, 40));
  bitmap_layer_set_alignment(bitmap_layer[1], GAlignCenter);
  layer_add_child(window_layer, bitmap_layer_get_layer(bitmap_layer[1]));
  
  bitmap_layer[2] = bitmap_layer_create(GRect(133, 109, 9, 40));
  bitmap_layer_set_alignment(bitmap_layer[2], GAlignBottom);
  layer_add_child(window_layer, bitmap_layer_get_layer(bitmap_layer[2]));
  
  // Warning layers
  layer_warning = layer_create(frame);
  layer_set_update_proc(layer_warning, layer_warning_update_callback);
  layer_add_child(window_layer, layer_warning);
  
  // Add inverter layer
  inverter_layer = inverter_layer_create(frame);
  layer_add_child(window_layer, inverter_layer_get_layer(inverter_layer));
}

static void window_appear(Window *window) {
  // Create bitmap
  gbitmap_start = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_START);
  gbitmap_stop  = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_STOP);
  gbitmap_save  = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_SAVE);
  gbitmap_menu  = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_MENU);
  gbitmap_lap   = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_LAP);
  gbitmap_reset = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_RESET);
  gbitmap_view  = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_VIEW);
  
  update_display_guide(stopwatch.sw_state);
  layer_set_hidden(inverter_layer_get_layer(inverter_layer), invert_color ? 0 : 1);
  update_time();
  update_display();
  
  timer = app_timer_register(SW_STEP_MS_SHORT, timer_callback, NULL);
}

static void window_disappear(Window *window) {
  // Destroy bitmap
  gbitmap_destroy(gbitmap_start);
  gbitmap_destroy(gbitmap_stop);
  gbitmap_destroy(gbitmap_save);
  gbitmap_destroy(gbitmap_menu);
  gbitmap_destroy(gbitmap_lap);
  gbitmap_destroy(gbitmap_reset);
  gbitmap_destroy(gbitmap_view);
  
  app_timer_cancel(timer);
}

static void window_unload(Window *window) {
  for (int row=0; row < NUM_ROWS; row++) {
    text_layer_destroy(text_layer_label[row]);
  }
  layer_destroy(layer_watchface);
  for (int i=0; i < NUM_GUIDES; i++)
     bitmap_layer_destroy(bitmap_layer[i]);
  layer_destroy(layer_warning);
  inverter_layer_destroy(inverter_layer);
}
//----- End window load/unload

//----- Begin single, long, raw click handlers and stopwatch state transitions
static void single_click_handler(ClickRecognizerRef recognizer, void *context) {
  int button_id = click_recognizer_get_button_id(recognizer);
  switch (stopwatch.sw_state) {
    case SW_STATE_IDLE:
      if (button_id == BUTTON_ID_UP) {
        time_ms(&stopwatch.time_start.s, &stopwatch.time_start.ms);
        move_to_state(SW_STATE_RUN);
      } else if (button_id == BUTTON_ID_SELECT) {
        ui_main_menu_spawn();
      }
      break;
    case SW_STATE_STOP:
      if (button_id == BUTTON_ID_UP) {
        time_ms(&stopwatch.time_start.s, &stopwatch.time_start.ms);
        move_to_state(SW_STATE_RUN);
      }
      break;
    case SW_STATE_RUN:
      if (button_id == BUTTON_ID_UP) {
        record_lap();
        move_to_state(SW_STATE_LAP_RECORD);
      } else if (button_id == BUTTON_ID_DOWN) {
        stop_sw();
      } else if (button_id == BUTTON_ID_SELECT) {
        ui_instant_recall_spawn();
      }
      break;
    case SW_STATE_LAP_RECORD:
      if (button_id == BUTTON_ID_UP) {
        record_lap();        
        move_to_state(SW_STATE_LAP_RECORD); 
      } else if (button_id == BUTTON_ID_DOWN) {
        clear_warning_text();
        stop_sw();
        move_to_state(SW_STATE_STOP);
      }
      break;
  }
}

static void raw_click_down_handler (ClickRecognizerRef recognizer, void *context) {  
  int button_id = click_recognizer_get_button_id(recognizer);
  switch (stopwatch.sw_state) {
    case SW_STATE_STOP:
      if (button_id == BUTTON_ID_DOWN) {
        if ((session[session_index].end_index<NUM_LAP_MEMORY) && (session_index<NUM_LAP_MEMORY-1))
          set_warning_text(FONT_KEY_BITHAM_30_BLACK, "HOLD\nTO\nSAVE");
        else
          set_warning_text(FONT_KEY_BITHAM_30_BLACK, "HOLD\nTO\nRESET");
        move_to_state(SW_STATE_SAVE_CONFIRM);
      } else if (button_id == BUTTON_ID_SELECT) {
        set_warning_text(FONT_KEY_BITHAM_30_BLACK, "HOLD\nTO\nRESET");
        move_to_state(SW_STATE_RESET_CONFIRM);
      }
      break;
  }
}

static void raw_click_up_handler (ClickRecognizerRef recognizer, void *context) {
  int button_id = click_recognizer_get_button_id(recognizer);
  switch (stopwatch.sw_state) {
    case SW_STATE_SAVE_CONFIRM:
      if (button_id != BUTTON_ID_DOWN) break;
      clear_warning_text();
      move_to_state(SW_STATE_STOP);
      break;
    case SW_STATE_RESET_CONFIRM:
      if (button_id != BUTTON_ID_SELECT) break;
      clear_warning_text();
      move_to_state(SW_STATE_STOP);
      break;
  }
}

static void long_click_down_handler(ClickRecognizerRef recognizer, void *context) {
  int button_id = click_recognizer_get_button_id(recognizer);
  switch (stopwatch.sw_state) {
    case SW_STATE_SAVE_CONFIRM:
      if (button_id != BUTTON_ID_DOWN) break;
      // Reset sw time
      stopwatch.time_elapsed = (WatchTime_t){0, 0};
      stopwatch.time_offset  = (WatchTime_t){0, 0};
    
      if ((session[session_index].end_index<NUM_LAP_MEMORY) && (session_index<NUM_LAP_MEMORY-1)) {
        // Save recorded time
        save_time[session_index] = time(NULL);
    
        // Initialize next session
        session[session_index+1].start_index = session[session_index].end_index+1;
        session[session_index+1].end_index   = session[session_index+1].start_index;
    
        session_index++; // Move on to next session_index
        cdt_reset();     // Reset countdown timer
      }
    
      // Issue a short vibe
      vibes_short_pulse();
      clear_warning_text();
      move_to_state(SW_STATE_IDLE);
      break;
    case SW_STATE_RESET_CONFIRM:
      if (button_id != BUTTON_ID_SELECT) break;
      // Reset sw time
      stopwatch.time_elapsed = stopwatch.time_offset = (WatchTime_t){0, 0};
      
      // Clobber lap and split memory
      for (int i=session[session_index].start_index; i<=session[session_index].end_index; i++)
        split_memory[i]=(SWTime){0, 0, 0, 0};
    
      // Reset countdown timer
      cdt_reset();
      
      // Revert end_index to start_index
      session[session_index].end_index = session[session_index].start_index;
    
      // Issue a short vibe
      vibes_short_pulse();
      clear_warning_text();
      move_to_state(SW_STATE_IDLE);
      break;
  }
}

// Subscribe to click handlers
static void click_config_provider(void *context) {
  // Single click
  window_single_click_subscribe(BUTTON_ID_UP,     single_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, single_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN,   single_click_handler);
  // Long click
  window_long_click_subscribe(BUTTON_ID_UP,     CLICK_HOLD_MS, long_click_down_handler, NULL);
  window_long_click_subscribe(BUTTON_ID_SELECT, CLICK_HOLD_MS, long_click_down_handler, NULL);
  window_long_click_subscribe(BUTTON_ID_DOWN,   CLICK_HOLD_MS, long_click_down_handler, NULL);
  // Raw click
  window_raw_click_subscribe(BUTTON_ID_UP,     raw_click_down_handler, raw_click_up_handler, NULL);
  window_raw_click_subscribe(BUTTON_ID_SELECT, raw_click_down_handler, raw_click_up_handler, NULL);
  window_raw_click_subscribe(BUTTON_ID_DOWN,   raw_click_down_handler, raw_click_up_handler, NULL);
}
//----- End single, long, raw click handlers and stopwatch state transitions

// Initialize
static void init(void) {  
  // Initialize countdown timer
  cdt_init();
  
  // Begin persist-initialization
  if (persist_exists(KEY_SPLIT_MEMORY)) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "KEY_SPLIT_MEMORY found");
  } else {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "KEY_SPLIT_MEMORY not found");
  }
  
  if (persist_exists(KEY_SESSION) &&
      persist_exists(KEY_SAVE_TIME) &&
      persist_exists(KEY_STOPWATCH) &&
      persist_exists(KEY_SESSION_INDEX)) {
    persist_read_data(KEY_SESSION, &session, sizeof(session));
    persist_read_data(KEY_SPLIT_MEMORY, &split_memory, sizeof(split_memory));
    persist_read_data(KEY_SAVE_TIME, &save_time, sizeof(save_time));
    persist_read_data(KEY_STOPWATCH, &stopwatch, sizeof(stopwatch));
    session_index = persist_read_int(KEY_SESSION_INDEX);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "KEY_SESSION, KEY_SPLIT_MEMORY, KEY_SAVE_TIME, KEY_SESSION_INDEX found");
  } else {
    for (int i=0; i < NUM_LAP_MEMORY; i++) {
      session[i]=(Session_t){0, 0};
      split_memory[i] = (SWTime){0, 0, 0, 0};
      save_time[i]    = 0;
    }
    stopwatch = (Stopwatch_t){
      .time_elapsed = {0, 0},
      .time_current = {0, 0},
      .time_start   = {0, 0},
      .time_offset  = {0, 0},
      .sw_state     = SW_STATE_IDLE,
    };
    session_index = 0;
    APP_LOG(APP_LOG_LEVEL_DEBUG, "KEY_SESSION, KEY_SPLIT_MEMORY, KEY_SAVE_TIME, KEY_SESSION_INDEX not found");
  }
  if (persist_exists(KEY_INVERT_COLOR)) {
    invert_color = persist_read_bool(KEY_INVERT_COLOR);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "KEY_INVERT_COLOR found\n");
  } else {
    invert_color = false;
    APP_LOG(APP_LOG_LEVEL_DEBUG, "KEY_INVERT_COLOR not found\n");
  }
  // End persist-initialization

  ms_to_clear_warning = 0;
  
  warning_font_key = FONT_KEY_GOTHIC_28;
  strcpy(warning_str_short, "");
  strcpy(warning_str_long, "");
  
  // TODO: remove later. Test-only initialization
  //time_offset = (WatchTime_t){.s=10760, .ms=0};

  // Data persistence
  watchface_init();
  
  // Create all window layers
  ui_instant_recall_init();
  ui_main_menu_init();
  ui_review_init();
  ui_timer_config_init();
  ui_preset_assistant_init();
  
  // Initialize window hander
  window = window_create();
  window_set_click_config_provider(window, click_config_provider);
  window_set_window_handlers(window, (WindowHandlers) {
    .load = window_load,
    .appear = window_appear,
    .disappear = window_disappear,
    .unload = window_unload
  });
  window_set_background_color(window, GColorWhite);
  window_stack_push(window, false);
}

// Deinitialize
static void deinit(void) {
  /*
  char str[9];
  time_t time_ms(&tloc, &ms);
  uint16_t strftime(str, sizeof(str), "%I:%M:%S", localtime(&tloc));
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Started deinit: %s.%d\n", str, ms);
  */
  
  cdt_deinit();
  persist_deinit();
  
  // Destroy all window layers
  ui_instant_recall_deinit();
  ui_main_menu_deinit();
  ui_review_deinit();
  ui_timer_config_deinit();
  ui_preset_assistant_deinit();
  
  window_destroy(window);
  
  /*
  time_ms(&tloc, &ms);
  strftime(str, sizeof(str), "%I:%M:%S", localtime(&tloc));
  APP_LOG(APP_LOG_LEVEL_DEBUG, "Finished deinit: %s.%d\n", str, ms);
  */
}

void watchface_init(void) {
  for (int row=0; row < NUM_ROWS; row++) {
    watchface_digit[row][0].frame     = GRect(0*WIDTH_BITHAM34MN,                    12+ROW_HEIGHT*row, WIDTH_BITHAM34MN, HEIGHT_BITHAM34MN);
    watchface_digit[row][1].frame     = GRect(1*WIDTH_BITHAM34MN,                    12+ROW_HEIGHT*row, WIDTH_BITHAM34MN, HEIGHT_BITHAM34MN);
    watchface_digit[row][2].frame     = GRect(2*WIDTH_BITHAM34MN+WIDTH_DELIMITER,    12+ROW_HEIGHT*row, WIDTH_BITHAM34MN, HEIGHT_BITHAM34MN);
    watchface_digit[row][3].frame     = GRect(3*WIDTH_BITHAM34MN+WIDTH_DELIMITER,    12+ROW_HEIGHT*row, WIDTH_BITHAM34MN, HEIGHT_BITHAM34MN);
    watchface_digit[row][4].frame     = GRect(4*WIDTH_BITHAM34MN+WIDTH_DELIMITER*2,  12+(HEIGHT_BITHAM34MN-HEIGHT_GOTHIC28B)+ROW_HEIGHT*row, WIDTH_GOTHIC28B, HEIGHT_GOTHIC28B);
    watchface_digit[row][5].frame     = GRect(4*WIDTH_BITHAM34MN+WIDTH_DELIMITER*2+WIDTH_GOTHIC28B, 12+(HEIGHT_BITHAM34MN-HEIGHT_GOTHIC28B)+ROW_HEIGHT*row, WIDTH_GOTHIC28B, HEIGHT_GOTHIC28B);
    watchface_delimiter[row][0].frame = GRect(2*WIDTH_BITHAM34MN,                    12+ROW_HEIGHT*row, WIDTH_DELIMITER,  HEIGHT_BITHAM34MN);
    watchface_delimiter[row][1].frame = GRect(4*WIDTH_BITHAM34MN+WIDTH_DELIMITER,    12+ROW_HEIGHT*row, WIDTH_DELIMITER,  HEIGHT_BITHAM34MN);

    // Set font
    watchface_digit[row][0].font = fonts_get_system_font((row == 0) ? FONT_KEY_BITHAM_30_BLACK : FONT_KEY_BITHAM_34_MEDIUM_NUMBERS);
    watchface_digit[row][1].font = fonts_get_system_font(FONT_KEY_BITHAM_34_MEDIUM_NUMBERS);
    watchface_digit[row][2].font = fonts_get_system_font(FONT_KEY_BITHAM_34_MEDIUM_NUMBERS);
    watchface_digit[row][3].font = fonts_get_system_font(FONT_KEY_BITHAM_34_MEDIUM_NUMBERS);
    watchface_digit[row][4].font = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
    watchface_digit[row][5].font = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
    watchface_delimiter[row][0].font = fonts_get_system_font(FONT_KEY_BITHAM_34_MEDIUM_NUMBERS);
    watchface_delimiter[row][1].font = fonts_get_system_font(FONT_KEY_BITHAM_34_MEDIUM_NUMBERS); 
  }
}

// Save persistent data
void persist_deinit(void) {
  
  if (persist_exists(KEY_SPLIT_MEMORY)) persist_delete(KEY_SPLIT_MEMORY);
  persist_write_data(KEY_SPLIT_MEMORY, &split_memory, sizeof(split_memory));
  
  /*
  if (persist_exists(KEY_SESSION)) persist_delete(KEY_SESSION);
  persist_write_data(KEY_SESSION, &session, sizeof(session));
  
  if (persist_exists(KEY_STOPWATCH)) persist_delete(KEY_STOPWATCH);
  persist_write_data(KEY_STOPWATCH, &stopwatch, sizeof(stopwatch));
  
  if (persist_exists(KEY_SAVE_TIME)) persist_delete(KEY_SAVE_TIME);
  persist_write_data(KEY_SAVE_TIME, &save_time, sizeof(save_time));
  */
  
  if (persist_exists(KEY_SESSION_INDEX)) persist_delete(KEY_SESSION_INDEX);
  persist_write_int(KEY_SESSION_INDEX, session_index);
  
  if (persist_exists(KEY_INVERT_COLOR)) persist_delete(KEY_INVERT_COLOR);
  persist_write_bool(KEY_INVERT_COLOR, invert_color);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}