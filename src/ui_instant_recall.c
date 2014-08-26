#include "pebble.h"
#include "ui_instant_recall.h"
#include "stopwatch.h"
  
// Instant recall window and layers
static Window *window;
static ScrollLayer *scroll_layer;
static TextLayer *text_layer_header;
static TextLayer *text_layer_left;
static TextLayer *text_layer_right;
static InverterLayer *inverter_layer;

// Helper function declaration
static void window_load(Window *);
static void window_unload(Window *);

// Main string container
static char* str_left;   // Dynamically allocated string for left side
static char* str_right;  // Dynamically allocated string for right side
static char substr[18];  // Fixed-length substring

// Initialize recall window hander
void ui_instant_recall_init(void) {
  window = window_create();
  window_set_window_handlers(window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_set_background_color(window, GColorWhite);
}

void ui_instant_recall_deinit(void) {
  window_destroy(window);
}

void ui_instant_recall_spawn(void) {
  window_stack_push(window, false);
}

//----- Begin recall window load/unload
void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect frame = layer_get_frame(window_layer);
  
  scroll_layer = scroll_layer_create(frame);
  scroll_layer_set_click_config_onto_window(scroll_layer, window);
  
  // Set up recall text layer (header)
  text_layer_header = text_layer_create(GRect(frame.origin.x+5, frame.origin.y, frame.size.w-10, 30));
  text_layer_set_font(text_layer_header, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_alignment(text_layer_header, GTextAlignmentCenter);
  static char header[20];
  snprintf(header, sizeof(header), "Session %d\n", session_index+1);
  text_layer_set_text(text_layer_header, header);
  scroll_layer_add_child(scroll_layer, text_layer_get_layer(text_layer_header));
  
  // Left side: lap index
  str_left = malloc(1 + 7 * (session[session_index].end_index - session[session_index].start_index));
  strcpy(str_left, "");
  for (int i=session[session_index].end_index-1; i>=session[session_index].start_index ; i--) {
    snprintf(substr,
             sizeof(substr),
             "Lap %d\n",
             i-session[session_index].start_index+1);
    strcat(str_left, substr);
  }
  GSize max_size_left = graphics_text_layout_get_content_size(
                          str_left,
                          fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), 
                          GRect(0,0,frame.size.w,3000),
                          GTextOverflowModeWordWrap,
                          GTextAlignmentLeft);
  
  // Right side: lap time report (only cover logged laps)
  str_right = malloc(1 + 9 * (session[session_index].end_index - session[session_index].start_index));
  strcpy(str_right, "");
  for (int i=session[session_index].end_index-1; i>=session[session_index].start_index ; i--) {
    SWTime lap_time = get_lap_time(session[session_index], i);
    snprintf(substr,
             sizeof(substr),
             "%d:%02d:%02d\n",
             lap_time.hour,
             lap_time.minute,     
             lap_time.second);
    strcat(str_right, substr);
  }
  GSize max_size_right = graphics_text_layout_get_content_size(
                          str_right,
                          fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD), 
                          GRect(0,0,frame.size.w,3000),
                          GTextOverflowModeWordWrap,
                          GTextAlignmentRight);
  
  // Set up recall text layer (left)
  text_layer_left = text_layer_create(GRect((frame.size.w-max_size_left.w-max_size_right.w-10)/2, 30, max_size_left.w, max_size_right.h+15));
  text_layer_set_font(text_layer_left, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_alignment(text_layer_left, GTextAlignmentLeft);  
  text_layer_set_text(text_layer_left, str_left);

  // Set up recall text layer (right)
  text_layer_right = text_layer_create(GRect((frame.size.w-max_size_left.w-max_size_right.w-10)/2+max_size_left.w+10, 30, max_size_right.w, max_size_left.h+15));
  text_layer_set_font(text_layer_right, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_alignment(text_layer_right, GTextAlignmentRight);
  text_layer_set_text(text_layer_right, str_right);
    
  GSize max_size = text_layer_get_content_size(text_layer_right);
  scroll_layer_set_content_size(scroll_layer, GSize(frame.size.w, max_size.h+15+30));  
  scroll_layer_add_child(scroll_layer, text_layer_get_layer(text_layer_left));
  scroll_layer_add_child(scroll_layer, text_layer_get_layer(text_layer_right));
  layer_add_child(window_layer, scroll_layer_get_layer(scroll_layer));

  // Add inverter layer
  inverter_layer = inverter_layer_create(frame);
  layer_add_child(window_layer, inverter_layer_get_layer(inverter_layer));
  layer_set_hidden(inverter_layer_get_layer(inverter_layer), (invert_color==true) ? 0 : 1);
}

void window_unload(Window *window) {
  free(str_left);
  free(str_right);
  text_layer_destroy(text_layer_header);
  text_layer_destroy(text_layer_left);
  text_layer_destroy(text_layer_right);
  scroll_layer_destroy(scroll_layer);
  inverter_layer_destroy(inverter_layer);
}
//----- End recall window load/unload