#include "pebble.h"
#include "ui_main_menu.h"
#include "stopwatch.h"
#include "cdt.h"
#include "ui_review.h"
#include "ui_timer_config.h"
#include "ui_preset_assistant.h"
  
static Window *window;
static MenuLayer *menu_layer_main_menu;

// Helper function declaration
static void window_load(Window *);
static void window_appear(Window *);
static void window_unload(Window *);
static int16_t get_header_height_callback(MenuLayer *, uint16_t, void *);
static void draw_header_callback(GContext *, const Layer *, uint16_t, void *);
static uint16_t get_num_sections_callback(MenuLayer *, void *);
static void draw_row_callback(GContext *, const Layer *, MenuIndex *, void *);
static uint16_t num_rows_callback(MenuLayer *, uint16_t, void *);
static void select_click_callback(MenuLayer *, MenuIndex *, void *);

// Initialize recall window hander
void ui_main_menu_init(void) {
  window = window_create();
  window_set_window_handlers(window, (WindowHandlers){
    .load = window_load,
    .appear = window_appear,
    .unload = window_unload,
  });
}

void ui_main_menu_deinit(void) {
  window_destroy(window);
}

void ui_main_menu_spawn(void) {
  window_stack_push(window, false);
}

//----- Begin main menu window load/unload
void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect frame = layer_get_frame(window_layer);
  
  menu_layer_main_menu = menu_layer_create(frame);
  menu_layer_set_click_config_onto_window(menu_layer_main_menu, window);
 
  MenuLayerCallbacks callbacks = {
    .get_header_height= (MenuLayerGetHeaderHeightCallback) get_header_height_callback,
    .draw_header      = (MenuLayerDrawHeaderCallback) draw_header_callback,
    .get_num_sections = (MenuLayerGetNumberOfSectionsCallback) get_num_sections_callback,
    .draw_row         = (MenuLayerDrawRowCallback) draw_row_callback,
    .get_num_rows     = (MenuLayerGetNumberOfRowsInSectionsCallback) num_rows_callback,
    .select_click     = (MenuLayerSelectCallback) select_click_callback
  };
  menu_layer_set_callbacks(menu_layer_main_menu, NULL, callbacks);
  
  layer_add_child(window_layer, menu_layer_get_layer(menu_layer_main_menu));
}

void window_appear(Window *window) {
  // Session deletion from review UI could the selected row when returning to this window
  MenuIndex menu_index = menu_layer_get_selected_index(menu_layer_main_menu);
  uint16_t section = menu_index.section;
  uint16_t row = menu_index.row;
  uint16_t num_rows = num_rows_callback(menu_layer_main_menu, section, NULL);
  
  window_unload(window);
  window_load(window);
  
  if (row >= num_rows) {
    // Exception handling for review UI deleting a session
    //window_unload(window);
    //window_load(window);
    menu_layer_set_selected_index(menu_layer_main_menu, MenuIndex(section,0), MenuRowAlignCenter, false);
  } else {
    menu_layer_set_selected_index(menu_layer_main_menu, MenuIndex(section,row), MenuRowAlignCenter, false);
  }
  
  layer_mark_dirty(menu_layer_get_layer(menu_layer_main_menu));
}

void window_unload(Window *window) {
  menu_layer_destroy(menu_layer_main_menu);
}

int16_t get_header_height_callback(MenuLayer *menu_layer, uint16_t section_index, void *callback_context) {
  return 17;
}

void draw_header_callback(GContext *ctx, const Layer *cell_layer, uint16_t section_index, void *callback_context) {
  switch(section_index) {
    case 0:
      menu_cell_basic_header_draw(ctx, cell_layer, "Appearance");
      break;
    case 1:
      menu_cell_basic_header_draw(ctx, cell_layer, "Session Review");
      break;
    case 2:
      menu_cell_basic_header_draw(ctx, cell_layer, "Pacerband");
      break;
  }
}

uint16_t get_num_sections_callback(MenuLayer *menu_layer, void *callback_context) {
  return 3;
}
  
void draw_row_callback(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *callback_context) {
  char title[17];
  char body[20];
  int index;
  cdt_t *cdt = cdt_get();
  SWTime cdt_lap;
  
  switch(cell_index->section) {
    case 0:
      menu_cell_basic_draw(ctx, cell_layer, "Display Color", invert_color ? "Inverted" : "Regular", NULL);
      break;
    case 1:
      switch (cell_index->row) {
        case 0:
          snprintf(body, sizeof(body), "%d/%d Free",
                   NUM_LAP_MEMORY-session[session_index].end_index, NUM_LAP_MEMORY);
          menu_cell_basic_draw(ctx, cell_layer, "Memory Status", body, NULL);
          break;
        default:
          snprintf(title, sizeof(title), "Session %d", cell_index->row);
          strftime(body, sizeof(body), "%m/%d/%Y %I:%M %p", localtime(&save_time[cell_index->row-1]));
          menu_cell_basic_draw(ctx, cell_layer, title, body, NULL);
          break;
      }
      break;
    case 2:
      switch (cell_index->row) {
        case 0:
          menu_cell_basic_draw(ctx, cell_layer, "Pacerband",
                               !(cdt->enable) ? "Disabled" :
                               !(cdt->repeat) ? "Enabled, Single" :
                                                "Enabled, Repeat", NULL);
          break;
        case 1:
          snprintf(body, sizeof(body),
                   "%d/%d Free",
                   CDT_MAX_LENGTH-(cdt->length),
                   CDT_MAX_LENGTH);
          menu_cell_basic_draw(ctx, cell_layer, "Reset All?", body, NULL);
          break;
        case 2:
          menu_cell_basic_draw(ctx, cell_layer, "Preset Assistant", "Overwrite All Segment", NULL);
          break;
        default:        
          index=cell_index->row-3;
          cdt_lap = cdt_get_lap(index);
          snprintf(title, sizeof(title),
                   "Timer Segment %d",
                   index+1);
          snprintf(body, sizeof(body),
                   "%d:%02d:%02d",
                   cdt_lap.hour,
                   cdt_lap.minute,
                   cdt_lap.second);
          menu_cell_basic_draw(ctx, cell_layer, title, body, NULL);
          break;
      }
      break;
  }
}
 
uint16_t num_rows_callback(MenuLayer *menu_layer, uint16_t section_index, void *callback_context) {
  cdt_t *cdt = cdt_get();

  switch (section_index) {
    case 0:
      return 1;
      break;
    case 1:
      // 1 for memory status
      return session_index + 1;
      break;
    case 2:
      // 1 for config, 1 for reset-all, and cdt_length + 1
      return (cdt->length) + (((cdt->length)==CDT_MAX_LENGTH) ? 3 : 4) ;
      break;
  }
  return 0;
}
 
void select_click_callback(MenuLayer *menu_layer, MenuIndex *cell_index, void *callback_context) {
  bool next_cdt_enable, next_cdt_repeat;
  cdt_t *cdt = cdt_get();

  switch(cell_index->section) {
    case 0:
      invert_color = (invert_color==false) ? true : false;
      layer_mark_dirty(menu_layer_get_layer(menu_layer_main_menu));
      break;
    case 1:
      switch (cell_index->row) {
        case 0:
          break;
        default:
          ui_review_spawn(cell_index->row-1);
          break;
      }
      break;
    case 2:
      switch (cell_index->row) {
        case 0:
          // Toggle "disabled" -> "enabled, single" -> "enabled, repeat"
          next_cdt_enable = (cdt->enable && cdt->repeat) ? false : true;
          next_cdt_repeat = (cdt->enable && !cdt->repeat) ? true : false;
          (cdt->enable) = next_cdt_enable;
          (cdt->repeat) = next_cdt_repeat;
          layer_mark_dirty(menu_layer_get_layer(menu_layer_main_menu));
          break;
        case 1:
          // Re-initialize all countdown timers
          cdt_reset_all();
          vibes_short_pulse();
          layer_mark_dirty(menu_layer_get_layer(menu_layer_main_menu));
          break;
        case 2:
          ui_preset_assistant_spawn();
          break;
        default:
          // Edit pacerband setting
          ui_timer_config_spawn(cell_index->row-3);
          break;
      }
      break;
  }
}