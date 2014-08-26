#include "pebble.h"
#include "ui_preset_assistant.h"
//#include "stopwatch.h"
#include "swtime.h"
#include "cdt.h"

// Instant recall window and layers
static Window *window;
static TextLayer *text_layer_header;
static TextLayer *text_layer_digit[6];
static TextLayer *text_layer_delimiter[2];
static TextLayer *text_layer_footer;

static Layer *graphics_layer;

// Helper function declaration
static void window_load(Window *);
static void window_appear(Window *);
static void window_disappear(Window *);
static void window_unload(Window *);
static void click_config_provider(void *);
static void single_click_handler(ClickRecognizerRef, void *);
static void repeating_click_handler(ClickRecognizerRef, void *);
static void update_displayed_digits(unsigned char);
static void update_displayed_focus(void);

// Main string container
static char text_digit[4][5];  // Fixed-length substring
static uint8_t focus_index;    // One of enum_fields
static uint16_t cdt_new_length_tenth;
static SWTime target_time;     // Target time

enum enum_fields {FOCUS_INDEX_HOUR,
                  FOCUS_INDEX_MINUTE,
                  FOCUS_INDEX_SECOND,
                  FOCUS_INDEX_NUM_SEGMENT,
                  FOCUS_INDEX_SIZE};

// Initialize recall window hander
void ui_preset_assistant_init(void) {
  window = window_create();
  window_set_background_color(window, GColorWhite);
  window_set_window_handlers(window, (WindowHandlers) {
    .load = window_load,
    .appear = window_appear,
    .disappear = window_disappear,
    .unload = window_unload,
  });
  window_set_click_config_provider(window, click_config_provider);
}

void ui_preset_assistant_deinit(void) {
  window_destroy(window);
}

void ui_preset_assistant_spawn() {
  focus_index = FOCUS_INDEX_HOUR;
  window_stack_push(window, false);
}

// Sandbox
static void graphics_layer_update_callback(Layer *me, GContext *ctx) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  switch(focus_index) {
    case FOCUS_INDEX_HOUR:
      graphics_fill_rect(ctx, GRect(1,55,24,34), 5, GCornersAll);
      break;
    case FOCUS_INDEX_MINUTE:
      graphics_fill_rect(ctx, GRect(34,55,50,34), 5, GCornersAll);
      break;
    case FOCUS_INDEX_SECOND:
      graphics_fill_rect(ctx, GRect(93,55,50,34), 5, GCornersAll);
      break;
    case FOCUS_INDEX_NUM_SEGMENT:
      graphics_fill_rect(ctx, GRect(10,94,46,25), 5, GCornersAll);
      break;
  }
}

//----- Begin recall window load/unload
void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  target_time = (SWTime){.hour=0, .minute=0, .second=0, .centisecond=0};
  
  graphics_layer = layer_create(layer_get_frame(window_layer));
  layer_set_update_proc(graphics_layer, graphics_layer_update_callback);
  layer_add_child(window_layer, graphics_layer);
  
  // Set up header (one time only)
  text_layer_header = text_layer_create(GRect(0,20,144,30));
  text_layer_set_font(text_layer_header, fonts_get_system_font(FONT_KEY_GOTHIC_24));
  text_layer_set_text_alignment(text_layer_header, GTextAlignmentCenter);
  text_layer_set_text(text_layer_header, "Total / Segments");
  layer_add_child(window_layer, text_layer_get_layer(text_layer_header));
  
  // Digit width=24, delimiter width=7
  // Character height 35  
  text_layer_digit[0]     = text_layer_create(GRect(0,50,26,40));
  text_layer_delimiter[0] = text_layer_create(GRect(26,50,7,40));
  text_layer_digit[1]     = text_layer_create(GRect(33,50,52,40));
  text_layer_delimiter[1] = text_layer_create(GRect(85,50,7,40));
  text_layer_digit[2]     = text_layer_create(GRect(92,50,52,40));
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
  
  // Segment layers  
  text_layer_digit[3] = text_layer_create(GRect(8,90,50,30));
  text_layer_set_font(text_layer_digit[3], fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_alignment(text_layer_digit[3], GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(text_layer_digit[3]));
    
  text_layer_footer = text_layer_create(GRect(60,90,90,30));
  text_layer_set_font(text_layer_footer, fonts_get_system_font(FONT_KEY_GOTHIC_24));
  text_layer_set_text_alignment(text_layer_footer, GTextAlignmentLeft);
  text_layer_set_text(text_layer_footer, "Segments");
  layer_add_child(window_layer, text_layer_get_layer(text_layer_footer));
  
  layer_add_child(window_layer, text_layer_get_layer(text_layer_digit[0]));
  layer_add_child(window_layer, text_layer_get_layer(text_layer_delimiter[0]));
  layer_add_child(window_layer, text_layer_get_layer(text_layer_digit[1]));
  layer_add_child(window_layer, text_layer_get_layer(text_layer_delimiter[1]));
  layer_add_child(window_layer, text_layer_get_layer(text_layer_digit[2]));

  layer_add_child(window_layer, text_layer_get_layer(text_layer_digit[3]));
  layer_add_child(window_layer, text_layer_get_layer(text_layer_footer));
}

void window_appear(Window *window) {
  cdt_new_length_tenth = 0;
  cdt_reset_all();
  update_displayed_digits(FOCUS_INDEX_SIZE);
  update_displayed_focus();
}

void window_disappear(Window *window) {
  // Begin assigning timer segments
  cdt_t *cdt = cdt_get();
  if (cdt_new_length_tenth == 0) {
    cdt_reset_all();
  } else if (target_time.hour==0 && target_time.minute==0 && target_time.second==0) {
    cdt_reset_all();
  } else {
    long target_end_centisecond = (long)target_time.hour*360000 +
                                  (long)target_time.minute*6000 +
                                  (long)target_time.second*100;
    long target_lap_centisecond = (target_end_centisecond * 10 / cdt_new_length_tenth);
    
    SWTime cdt_split_acc = (SWTime){0, 0, 0, 0};  // CDT split accumulator
    for (uint8_t i=0; i*10 < cdt_new_length_tenth; i++) {
      long target_split_centisecond = (cdt_new_length_tenth - i*10 < 10) ? 
                                      target_end_centisecond : (i+1) * target_lap_centisecond;
      SWTime cdt_target_split = (SWTime) {.hour = (uint8_t)(target_split_centisecond/360000),
                                          .minute = (uint8_t)((target_split_centisecond%360000)/6000),
                                          .second = (uint8_t)((target_split_centisecond%6000)/100),
                                          .centisecond = 0};
      cdt_set_lap(i, SWTime_subtract(cdt_target_split, cdt_split_acc));
      cdt_split_acc = SWTime_add(cdt_split_acc, (cdt->lap)[i]);
    }
    
    // Set new countdown timer length
    cdt->length = cdt_new_length_tenth / 10 + ((cdt_new_length_tenth % 10 == 0) ? 0 : 1);
    
    // Enable CDT if not already
    cdt->enable = (!cdt->enable) ? true : (cdt->enable);
  }
  // End assigning timer segments
}
  
void window_unload(Window *window) {
  layer_destroy(graphics_layer);
  text_layer_destroy(text_layer_header);
  text_layer_destroy(text_layer_digit[0]);
  text_layer_destroy(text_layer_digit[1]);
  text_layer_destroy(text_layer_digit[2]);
  text_layer_destroy(text_layer_digit[3]);
  text_layer_destroy(text_layer_delimiter[0]);
  text_layer_destroy(text_layer_delimiter[1]);
  text_layer_destroy(text_layer_footer);
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
          target_time.hour   = (target_time.hour+1)%10;
          break;
        case FOCUS_INDEX_MINUTE:
          target_time.minute = (target_time.minute+1)%60;
          break;
        case FOCUS_INDEX_SECOND:
          target_time.second = (target_time.second+1)%60;
          break;
        case FOCUS_INDEX_NUM_SEGMENT:
          cdt_new_length_tenth = (cdt_new_length_tenth==CDT_MAX_LENGTH*10) ? 0 : (cdt_new_length_tenth+1);
      }
      update_displayed_digits(focus_index);
      break;
    case BUTTON_ID_DOWN:
      switch (focus_index) {
        case FOCUS_INDEX_HOUR:
          target_time.hour   = (target_time.hour==0)   ?  9 : (target_time.hour-1);
          break;
        case FOCUS_INDEX_MINUTE:
          target_time.minute = (target_time.minute==0) ? 59 : (target_time.minute-1);
          break;
        case FOCUS_INDEX_SECOND:
          target_time.second = (target_time.second==0) ? 59 : (target_time.second-1);
          break;
        case FOCUS_INDEX_NUM_SEGMENT:
          cdt_new_length_tenth = (cdt_new_length_tenth==0) ? CDT_MAX_LENGTH*10 : (cdt_new_length_tenth-1);
          break;
      }
      update_displayed_digits(focus_index);
      break;
  }
}

void update_displayed_digits(unsigned char index) {
  if ((index==FOCUS_INDEX_HOUR) || (index==FOCUS_INDEX_SIZE)) {
    snprintf(text_digit[0], sizeof(text_digit[0]), "%1d", target_time.hour);
    text_layer_set_text(text_layer_digit[0], text_digit[0]);
  }
  if ((index==FOCUS_INDEX_MINUTE) || (index==FOCUS_INDEX_SIZE)) {
    snprintf(text_digit[1], sizeof(text_digit[1]), "%02d", target_time.minute);
    text_layer_set_text(text_layer_digit[1], text_digit[1]);
  }
  if ((index==FOCUS_INDEX_SECOND) || (index==FOCUS_INDEX_SIZE)) {
    snprintf(text_digit[2], sizeof(text_digit[2]), "%02d", target_time.second);
    text_layer_set_text(text_layer_digit[2], text_digit[2]);
  }
  if ((index==FOCUS_INDEX_NUM_SEGMENT) || (index==FOCUS_INDEX_SIZE)) {
    snprintf(text_digit[3], sizeof(text_digit[3]), "%d.%1d", cdt_new_length_tenth/10, cdt_new_length_tenth%10);
    text_layer_set_text(text_layer_digit[3], text_digit[3]);
  }
}

void update_displayed_focus(void) {
  text_layer_set_text_color(text_layer_digit[0], (focus_index==FOCUS_INDEX_HOUR)   ? GColorWhite : GColorBlack);
  text_layer_set_text_color(text_layer_digit[1], (focus_index==FOCUS_INDEX_MINUTE) ? GColorWhite : GColorBlack);
  text_layer_set_text_color(text_layer_digit[2], (focus_index==FOCUS_INDEX_SECOND) ? GColorWhite : GColorBlack);
  text_layer_set_text_color(text_layer_digit[3], (focus_index==FOCUS_INDEX_NUM_SEGMENT) ? GColorWhite : GColorBlack);
  text_layer_set_background_color(text_layer_digit[0], GColorClear);
  text_layer_set_background_color(text_layer_digit[1], GColorClear);
  text_layer_set_background_color(text_layer_digit[2], GColorClear);
  text_layer_set_background_color(text_layer_digit[3], GColorClear);
  layer_mark_dirty(graphics_layer);
}