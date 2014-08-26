#include "pebble.h"
#include "ui_review.h"
#include "stopwatch.h"
  
// Instant review window and layers
static Window *window;
static ScrollLayer *scroll_layer_review;
static TextLayer *text_layer_review_header;
static TextLayer *text_layer_review_lap_num;
static TextLayer *text_layer_review_lap;
static TextLayer *text_layer_review_split;

// Warning TextLayer and the border TextLayer around it
static TextLayer *text_layer_review_warning_border;
static TextLayer *text_layer_review_warning;

static uint16_t review_index;

static char* lap_num_str;
static char* lap_str;
static char* split_str;

enum state_e {
  IDLE,
  RESET_ONE_CONFIRM,
  RESET_ALL_CONFIRM
};

static int state;

// Helper function declaration
static void raw_click_down_handler (ClickRecognizerRef, void *);
static void raw_click_up_handler (ClickRecognizerRef, void *);
static void long_click_down_handler(ClickRecognizerRef, void *);
static void click_config_provider(void *);
static void clear_warning_layer(void);
static void set_warning_text(const Window *, const char *, char *);
static void window_load_review(Window *);
static void window_unload_review(Window *);

// Short-hand for clearning warning textLayer
void clear_warning_layer(void) {
  layer_set_hidden(text_layer_get_layer(text_layer_review_warning_border), true);
  layer_set_hidden(text_layer_get_layer(text_layer_review_warning), true);
}

// Short-hand for setting warning textLayer
void set_warning_text(const Window *window, const char *font_key, char *str) {
  Layer *window_layer = window_get_root_layer(window);
  GRect frame = layer_get_frame(window_layer);	
  
  // Reset bounds to whole frame
  layer_set_frame(text_layer_get_layer(text_layer_review_warning), frame);
  layer_set_bounds(text_layer_get_layer(text_layer_review_warning), GRect(0, 0, frame.size.w, frame.size.h));
  text_layer_set_font(text_layer_review_warning, fonts_get_system_font(font_key));
  text_layer_set_text(text_layer_review_warning, str);
  text_layer_set_background_color(text_layer_review_warning_border, GColorBlack);
  text_layer_set_background_color(text_layer_review_warning, GColorWhite);

  // Resize the textLayer
  GSize text_frame = text_layer_get_content_size(text_layer_review_warning);
  int16_t x = (frame.size.w - text_frame.w) / 2 - 10;
  int16_t y = (frame.size.h - text_frame.h) / 2 - 6;
  int16_t w = text_frame.w + 20;
  int16_t h = text_frame.h + 12;
  layer_set_frame(text_layer_get_layer(text_layer_review_warning_border), GRect(x-1, y-1, w+2, h+2));
  layer_set_frame(text_layer_get_layer(text_layer_review_warning), GRect(x, y, w, h));
  layer_set_bounds(text_layer_get_layer(text_layer_review_warning), GRect(0, 0, w, h));

  layer_set_hidden(text_layer_get_layer(text_layer_review_warning_border), false);
  layer_set_hidden(text_layer_get_layer(text_layer_review_warning), false);
}

// Initialize review window UI
void ui_review_init(void) {
  state = IDLE;
  window = window_create();
  window_set_window_handlers(window, (WindowHandlers) {
    .load = window_load_review,
    .unload = window_unload_review,
  });
  window_set_background_color(window, GColorWhite);
}

void ui_review_deinit(void) {
  window_destroy(window);
}

// Initialize review window hander
void ui_review_spawn(uint16_t index) {
  review_index = index;
  window_stack_push(window, false);
}

//----- Begin click handlers
void raw_click_down_handler (ClickRecognizerRef recognizer, void *context) {  
  int button_id = click_recognizer_get_button_id(recognizer);
  switch (state) {
    case IDLE:
      if (button_id != BUTTON_ID_SELECT) { break; }
      set_warning_text(window, FONT_KEY_BITHAM_30_BLACK, "Reset?");
      state = RESET_ONE_CONFIRM;
      break;
    default:
      break;
  }
}

void raw_click_up_handler (ClickRecognizerRef recognizer, void *context) {
  int button_id = click_recognizer_get_button_id(recognizer);
  switch (state) {
    case RESET_ONE_CONFIRM:
    case RESET_ALL_CONFIRM:
      if (button_id != BUTTON_ID_SELECT) { break; }
      clear_warning_layer();
      state = IDLE;
      break;
    default:
      break;
  }
}

void long_click_down_handler(ClickRecognizerRef recognizer, void *context) {
  int button_id = click_recognizer_get_button_id(recognizer);
  switch (state) {
    case RESET_ONE_CONFIRM:
      if (button_id != BUTTON_ID_SELECT) { break; }
      
      // Shift down all data
      int shift_down = session[review_index].end_index - session[review_index].start_index + 1;
      for (uint16_t i=review_index+1; i<session_index; i++) {
        for (int src_index=session[i].start_index; src_index<=session[i].end_index; src_index++) {
          int dst_index = src_index - shift_down;
          split_memory[dst_index] = split_memory[src_index];
        }
        session[i-1].start_index = session[i].start_index - shift_down;
        session[i-1].end_index   = session[i].end_index   - shift_down;
        //session[i-1].recorded_time = session[i].recorded_time;
        save_time[i-1] = save_time[i];
      }
    
      session_index--;

      // Define the latest session
      session[session_index].start_index = (session_index==0) ? 0 : session[session_index-1].end_index+1;
      session[session_index].end_index = session[session_index].start_index;
    
      // Issue a short vibe
      vibes_short_pulse();
    
      clear_warning_layer();
      state = IDLE;
    
      // Pop the window
      window_stack_pop(false);
      break;
    default:
      break;
  }
}

void click_config_provider(void *context) {  
  // Long click
  window_long_click_subscribe(BUTTON_ID_SELECT, CLICK_HOLD_MS, long_click_down_handler, NULL);
  
  // Raw click
  window_raw_click_subscribe(BUTTON_ID_SELECT, raw_click_down_handler, raw_click_up_handler, NULL);
}
//----- End click handlers

//----- Begin review window load/unload
void window_load_review(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect frame = layer_get_frame(window_layer);

  // Configure up/down/select click provider
  scroll_layer_review = scroll_layer_create(frame);
  scroll_layer_set_click_config_onto_window(scroll_layer_review, window);
  scroll_layer_set_callbacks(scroll_layer_review, (ScrollLayerCallbacks) {
    .click_config_provider = click_config_provider
  });
 
  // Set up review text layer (header)
  text_layer_review_header = text_layer_create(GRect(0, 0, frame.size.w, 18));
  text_layer_set_font(text_layer_review_header, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_alignment(text_layer_review_header, GTextAlignmentCenter);
  static char header[20];
  snprintf(header, sizeof(header), "Session %d Review\n", review_index+1);
  text_layer_set_text(text_layer_review_header, header);
  scroll_layer_add_child(scroll_layer_review, text_layer_get_layer(text_layer_review_header));
  
  // Set up review text layer (body)
  text_layer_review_lap_num = text_layer_create(GRect(5, 18, 16, 2000));
  text_layer_set_font(text_layer_review_lap_num, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(text_layer_review_lap_num, GTextAlignmentRight);
  
  text_layer_review_lap = text_layer_create(GRect(28, 18, 58, 100));
  text_layer_set_font(text_layer_review_lap, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(text_layer_review_lap, GTextAlignmentLeft);
  
  text_layer_review_split = text_layer_create(GRect(86, 18, 58, 100));
  text_layer_set_font(text_layer_review_split, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(text_layer_review_split, GTextAlignmentLeft);
  
  // Lap memory report (only cover logged laps)
  int max_str_length;
  max_str_length = (session[review_index].end_index - session[review_index].start_index + 1 >= 10) ?
                   4 + 18 + 3 * (session[review_index].end_index - session[review_index].start_index + 1 - 10) :
                   4 + 2 * (session[review_index].end_index - session[review_index].start_index + 1);
  lap_num_str = malloc(max_str_length);
  max_str_length = 8 + 8 * (session[review_index].end_index - session[review_index].start_index + 1);
  lap_str = malloc(max_str_length);
  split_str = malloc(max_str_length);
  
  strcpy(lap_num_str, "\n");
  strcpy(lap_str, "Lap\n");
  strcpy(split_str, "Split\n");
  
  char substr[12];
  for (int i = session[review_index].start_index; i <= session[review_index].end_index; i++) {
    SWTime lap_time = get_lap_time(session[review_index], i);
    snprintf(substr, sizeof(substr), "%d\n", i-session[review_index].start_index+1);
    strcat(lap_num_str, substr);
    snprintf(substr,
             sizeof(substr),
             "%d:%02d:%02d\n",
             lap_time.hour,
             lap_time.minute,     
             lap_time.second);
    strcat(lap_str, substr);
    snprintf(substr,
             sizeof(substr),
             "%d:%02d:%02d\n",
             split_memory[i].hour,
             split_memory[i].minute,     
             split_memory[i].second);
    strcat(split_str, substr);
  }
  
  text_layer_set_text(text_layer_review_lap_num, lap_num_str);
  text_layer_set_text(text_layer_review_lap, lap_str);
  text_layer_set_text(text_layer_review_split, split_str);
  
  // Resize content
  GSize max_size = text_layer_get_content_size(text_layer_review_lap_num);
  text_layer_set_size(text_layer_review_lap_num, GSize(layer_get_frame(text_layer_get_layer(text_layer_review_lap_num)).size.w, max_size.h + 15));
  text_layer_set_size(text_layer_review_lap,     GSize(layer_get_frame(text_layer_get_layer(text_layer_review_lap)).size.w,     max_size.h + 15));
  text_layer_set_size(text_layer_review_split,   GSize(layer_get_frame(text_layer_get_layer(text_layer_review_split)).size.w,   max_size.h + 15));

  scroll_layer_set_content_size(scroll_layer_review, GSize(frame.size.w, max_size.h + 30));  
  scroll_layer_add_child(scroll_layer_review, text_layer_get_layer(text_layer_review_lap_num));
  scroll_layer_add_child(scroll_layer_review, text_layer_get_layer(text_layer_review_lap));
  scroll_layer_add_child(scroll_layer_review, text_layer_get_layer(text_layer_review_split));
  layer_add_child(window_layer, scroll_layer_get_layer(scroll_layer_review));
  
  // Warning layers
  text_layer_review_warning_border = text_layer_create(GRect(10, 10, frame.size.w-20, frame.size.h-20));
  layer_add_child(window_layer, text_layer_get_layer(text_layer_review_warning_border));

  text_layer_review_warning = text_layer_create(GRect(11, 11, frame.size.w-22, frame.size.h-22));
  text_layer_set_text_alignment(text_layer_review_warning, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(text_layer_review_warning));
  
  clear_warning_layer();
}

void window_unload_review(Window *window) {
  free(lap_num_str);
  free(lap_str);
  free(split_str);
  
  // Warning layers
  text_layer_destroy(text_layer_review_warning_border);
  text_layer_destroy(text_layer_review_warning);
  
  text_layer_destroy(text_layer_review_header);
  text_layer_destroy(text_layer_review_lap_num);
  text_layer_destroy(text_layer_review_lap);
  text_layer_destroy(text_layer_review_split);
  scroll_layer_destroy(scroll_layer_review);
}
//----- End review window load/unload