#include "pebble.h"
#include "ui_timer_config.h"
#include "swtime.h"
#include "cdt.h"
  
// Instant recall window and layers
static Window *window;
static TextLayer *text_layer_header;
static TextLayer *text_layer_digit[3];
static TextLayer *text_layer_delimiter[2];

static Layer *graphics_layer;

// Helper function declaration
static void window_load(Window *);
static void window_disappear(Window *);
static void window_unload(Window *);
static void click_config_provider(void *);
static void single_click_handler(ClickRecognizerRef, void *);
static void repeating_click_handler(ClickRecognizerRef, void *);
static void update_displayed_digits(unsigned char);
static void update_displayed_focus(void);

// Main string container
static char text_header[26];
static char text_digit[3][3];      // Fixed-length substring
static unsigned char timer_index;  // Timer config index
static unsigned char focus_index;  // One of enum_fields
static SWTime new_lap_time;

enum enum_fields {FOCUS_INDEX_HOUR,
                  FOCUS_INDEX_MINUTE,
                  FOCUS_INDEX_SECOND,
                  FOCUS_INDEX_SIZE};

// Initialize recall window hander
void ui_timer_config_init(void) {
  window = window_create();
  window_set_background_color(window, GColorWhite);
  window_set_window_handlers(window, (WindowHandlers) {
    .load = window_load,
    .disappear = window_disappear,
    .unload = window_unload,
  });
  window_set_click_config_provider(window, click_config_provider);
}

void ui_timer_config_deinit(void) {
  window_destroy(window);
}

void ui_timer_config_spawn(uint16_t number) {
  timer_index = number;
  new_lap_time = cdt_get_lap(timer_index);
  focus_index = FOCUS_INDEX_HOUR;
  window_stack_push(window, false);
}

// Sandbox
static void graphics_layer_update_callback(Layer *me, GContext *ctx) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  switch(focus_index) {
    case FOCUS_INDEX_HOUR:
      graphics_fill_rect(ctx, GRect(1,70,24,34), 5, GCornersAll);
      break;
    case FOCUS_INDEX_MINUTE:
      graphics_fill_rect(ctx, GRect(34,70,50,34), 5, GCornersAll);
      break;
    case FOCUS_INDEX_SECOND:
      graphics_fill_rect(ctx, GRect(93,70,50,34), 5, GCornersAll);
      break;
  }
}

//----- Begin recall window load/unload
void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  
  graphics_layer = layer_create(layer_get_frame(window_layer));
  layer_set_update_proc(graphics_layer, graphics_layer_update_callback);
  layer_add_child(window_layer, graphics_layer);
  
  // Set up header (one time only)
  text_layer_header = text_layer_create(GRect(0,35,144,30));
  text_layer_set_font(text_layer_header, fonts_get_system_font(FONT_KEY_GOTHIC_24));
  text_layer_set_text_alignment(text_layer_header, GTextAlignmentCenter);
  snprintf(text_header, sizeof(text_header), "Edit Segment %d", timer_index+1);
  text_layer_set_text(text_layer_header, text_header);
  layer_add_child(window_layer, text_layer_get_layer(text_layer_header));
  
  // Digit width=24, delimiter width=7
  // Character height 35  
  text_layer_digit[0]     = text_layer_create(GRect(0,65,26,40));
  text_layer_delimiter[0] = text_layer_create(GRect(26,65,7,40));
  text_layer_digit[1]     = text_layer_create(GRect(33,65,52,40));
  text_layer_delimiter[1] = text_layer_create(GRect(85,65,7,40));
  text_layer_digit[2]     = text_layer_create(GRect(92,65,52,40));

  text_layer_set_font(text_layer_digit[0],     fonts_get_system_font(FONT_KEY_BITHAM_34_MEDIUM_NUMBERS));
  text_layer_set_font(text_layer_delimiter[0], fonts_get_system_font(FONT_KEY_BITHAM_34_MEDIUM_NUMBERS));
  text_layer_set_font(text_layer_digit[1],     fonts_get_system_font(FONT_KEY_BITHAM_34_MEDIUM_NUMBERS));
  text_layer_set_font(text_layer_delimiter[1], fonts_get_system_font(FONT_KEY_BITHAM_34_MEDIUM_NUMBERS));
  text_layer_set_font(text_layer_digit[2],     fonts_get_system_font(FONT_KEY_BITHAM_34_MEDIUM_NUMBERS));

  text_layer_set_text_alignment(text_layer_digit[0],     GTextAlignmentCenter);
  text_layer_set_text_alignment(text_layer_delimiter[0], GTextAlignmentCenter);
  text_layer_set_text_alignment(text_layer_digit[1],     GTextAlignmentCenter);
  text_layer_set_text_alignment(text_layer_delimiter[1], GTextAlignmentCenter);
  text_layer_set_text_alignment(text_layer_digit[2],     GTextAlignmentCenter);
  
  text_layer_set_text(text_layer_delimiter[0], ":");
  text_layer_set_text(text_layer_delimiter[1], ":");
  
  update_displayed_digits(FOCUS_INDEX_SIZE);
  update_displayed_focus();
  
  layer_add_child(window_layer, text_layer_get_layer(text_layer_digit[0]));
  layer_add_child(window_layer, text_layer_get_layer(text_layer_delimiter[0]));
  layer_add_child(window_layer, text_layer_get_layer(text_layer_digit[1]));
  layer_add_child(window_layer, text_layer_get_layer(text_layer_delimiter[1]));
  layer_add_child(window_layer, text_layer_get_layer(text_layer_digit[2]));
}

void window_disappear(Window *window) {
  cdt_t *cdt = cdt_get();
  
  cdt_set_lap(timer_index, new_lap_time);
  if (!(new_lap_time.hour==0 && new_lap_time.minute==0 && new_lap_time.second==0) && (timer_index == cdt->length)) {
    cdt->length++;
    cdt->enable = (!cdt->enable) ? true : (cdt->enable);
  }
}

void window_unload(Window *window) {
  layer_destroy(graphics_layer);
  text_layer_destroy(text_layer_header);
  text_layer_destroy(text_layer_digit[0]);
  text_layer_destroy(text_layer_digit[1]);
  text_layer_destroy(text_layer_digit[2]);
  text_layer_destroy(text_layer_delimiter[0]);
  text_layer_destroy(text_layer_delimiter[1]);
  cdt_reset();
}
//----- End recall window load/unload

// Subscribe to click handlers
static void click_config_provider(void *context) {
  // Single and multiple clicks
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 40, repeating_click_handler);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 40, repeating_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, single_click_handler);
}

static void single_click_handler(ClickRecognizerRef recognizer, void *context) {
  int button_id = click_recognizer_get_button_id(recognizer);
  if (button_id == BUTTON_ID_SELECT) {
    focus_index = (focus_index+1)%FOCUS_INDEX_SIZE;
    update_displayed_focus();
  }
}

static void repeating_click_handler(ClickRecognizerRef recognizer, void *context) {
  int button_id = click_recognizer_get_button_id(recognizer);
 
  switch (button_id) {
    case BUTTON_ID_UP:    
      switch (focus_index) {
        case FOCUS_INDEX_HOUR:
          new_lap_time.hour = (new_lap_time.hour+1)%10;
          break;
        case FOCUS_INDEX_MINUTE:
          new_lap_time.minute = (new_lap_time.minute+1)%60;
          break;
        case FOCUS_INDEX_SECOND:
          new_lap_time.second = (new_lap_time.second+1)%60;
          break;
      }
      break;
    case BUTTON_ID_DOWN:
      switch (focus_index) {
        case FOCUS_INDEX_HOUR:
          new_lap_time.hour = (new_lap_time.hour==0) ? 9 : (new_lap_time.hour-1);
          break;
        case FOCUS_INDEX_MINUTE:
          new_lap_time.minute = (new_lap_time.minute==0) ? 59 : (new_lap_time.minute-1);
          break;
        case FOCUS_INDEX_SECOND:
          new_lap_time.second = (new_lap_time.second==0) ? 59 : (new_lap_time.second-1);
          break;
      }
      break;
  }
  update_displayed_digits(focus_index);
}

void update_displayed_digits(unsigned char index) {
  if ((index==FOCUS_INDEX_HOUR) || (index==FOCUS_INDEX_SIZE)) {
    snprintf(text_digit[0], sizeof(text_digit[0]), "%1d", new_lap_time.hour);
    text_layer_set_text(text_layer_digit[0], text_digit[0]);
  }
  if ((index==FOCUS_INDEX_MINUTE) || (index==FOCUS_INDEX_SIZE)) {
    snprintf(text_digit[1], sizeof(text_digit[1]), "%02d", new_lap_time.minute);
    text_layer_set_text(text_layer_digit[1], text_digit[1]);
  }
  if ((index==FOCUS_INDEX_SECOND) || (index==FOCUS_INDEX_SIZE)) {
    snprintf(text_digit[2], sizeof(text_digit[2]), "%02d", new_lap_time.second);
    text_layer_set_text(text_layer_digit[2], text_digit[2]);
  }
}

void update_displayed_focus(void) {
  text_layer_set_text_color(text_layer_digit[0], (focus_index==FOCUS_INDEX_HOUR)   ? GColorWhite : GColorBlack);
  text_layer_set_text_color(text_layer_digit[1], (focus_index==FOCUS_INDEX_MINUTE) ? GColorWhite : GColorBlack);
  text_layer_set_text_color(text_layer_digit[2], (focus_index==FOCUS_INDEX_SECOND) ? GColorWhite : GColorBlack);
  text_layer_set_background_color(text_layer_digit[0], GColorClear);
  text_layer_set_background_color(text_layer_digit[1], GColorClear);
  text_layer_set_background_color(text_layer_digit[2], GColorClear);
  layer_mark_dirty(graphics_layer);
}