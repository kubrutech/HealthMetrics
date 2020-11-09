/*****************************************************************************
Health Metrics: A watchface for the Pebble watch

Copyright (C) 2016 Kubru Technology, LLC

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*****************************************************************************/

#include <pebble.h>
#define INDWIDTH 18
#define CELLHEIGHT 12

static bool blackface = 1; // 0 means a white face

// The main window
static Window 
  *s_main_window;

// All the text layers
static TextLayer  
  *s_time_layer,
  *s_date_layer,
  *s_ampm_layer,
  *s_steplabel_layer,
  *s_stepcount_layer,
  *s_kcallabel_layer,
  *s_kcal_layer,
  *s_distlabel_layer,  
  *s_distance_layer,
  *s_timelabel_layer,
  *s_activetime_layer;

// A layer for the battery indicator
static Layer
  *s_battery_layer;

// The bitmap layers (icons)
static BitmapLayer  
  *s_bticon_layer;

// The bitmaps themselves
static GBitmap  
  *s_bticon_bitmap;
  
// This is called when bluetooth status changes
static void bluetooth_callback(bool connected) {
  layer_set_hidden(bitmap_layer_get_layer(s_bticon_layer), connected);
  return;
}

// Draw the battery icon, including its charge level
static void draw_battery(Layer *layer, GContext *context) {
  // Battery outline is green while charging
  if (battery_state_service_peek().is_charging) {
    graphics_context_set_stroke_color(context, GColorGreen);
  } else {
    graphics_context_set_stroke_color(context, blackface ? GColorWhite : GColorBlack);
  } 
  graphics_draw_rect(context, GRect(7, 0, 5, 2));
  graphics_draw_rect(context, GRect(5, 2, 9, 16));

  int level = ((CELLHEIGHT / 100.0) * battery_state_service_peek().charge_percent);
  if (level <= 2) {
    graphics_context_set_fill_color(context, GColorRed);
  } else {
    graphics_context_set_fill_color(context, blackface ? GColorWhite : GColorBlack);
  }
//  if (level >= 2) {
    graphics_fill_rect(context, GRect(7, 4+CELLHEIGHT-level, 5, level), 0, GCornerNone);
//  } 
}

// Update all viewable data once each minute
static void update_watchface() {
  // Get a tm structure and fill it with the current local time
  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);

  // Buffers for all the text fields
  static char 
    s_time_buffer[8],
    s_date_buffer[12],
    s_ampm_buffer[4],
    s_stepcount_buffer[8],
    s_activetime_buffer[8],
    s_distance_buffer[8],
    s_kcal_buffer[8];
  
  // Computed integer values
  static int32_t 
    stepcount, 
    active_time, 
    active_hours, 
    active_mins;
  
  // Fill the text buffers formatted to user selections where appropriate
  strftime(s_time_buffer, sizeof(s_time_buffer), clock_is_24h_style() ?
                                          "%k:%M" : "%l:%M", tick_time);
  strftime(s_date_buffer, sizeof(s_date_buffer), "%a %d %b", tick_time);
  strftime(s_ampm_buffer, sizeof(s_ampm_buffer), clock_is_24h_style() ?
                                          "  " : "%p", tick_time);
  stepcount = (int)health_service_sum_today(HealthMetricStepCount);
  if (stepcount < 10000) {
    snprintf(s_stepcount_buffer, sizeof(s_stepcount_buffer), 
             "%i", (int)stepcount);
  } else {
    snprintf(s_stepcount_buffer, sizeof(s_stepcount_buffer), 
             "%i.%ik", (int)(stepcount/1000), (int)((stepcount - ((stepcount/1000)*1000))/100));
  } 
  
  active_time = health_service_sum_today(HealthMetricActiveSeconds);
  active_hours = active_time / 3600;
  active_mins = active_time / 60 - active_hours * 60;
  snprintf(s_activetime_buffer, sizeof(s_activetime_buffer), 
           "%ih %im", (int)(active_hours), (int)(active_mins));
  
  // Get the preferred measurement system
  MeasurementSystem system = 
    health_service_get_measurement_system_for_display(HealthMetricWalkedDistanceMeters);
  // Format accordingly
  switch(system) {
    case MeasurementSystemMetric: {
      int meters = health_service_sum_today(HealthMetricWalkedDistanceMeters);
      snprintf(s_distance_buffer, sizeof(s_distance_buffer), "%d.%dkm", 
               (int)(meters/1000), (int)((meters - ((meters/1000)*1000))/100));    
      break;
    }
    case MeasurementSystemImperial: {
      int feet = (int)((float)health_service_sum_today(HealthMetricWalkedDistanceMeters) * 3.281F); 
      snprintf(s_distance_buffer, sizeof(s_distance_buffer), "%d.%dmi", 
               (int)(feet/5280), (int)((feet - ((feet/5280)*5280))/528));
      break;
    }
    case MeasurementSystemUnknown:
    default:
      APP_LOG(APP_LOG_LEVEL_INFO, "MeasurementSystem unknown or does not apply");
      snprintf(s_distance_buffer, sizeof(s_distance_buffer), "Unknown");
  }

  snprintf(s_kcal_buffer, sizeof(s_kcal_buffer), 
           "%i", (int)health_service_sum_today(HealthMetricActiveKCalories) +
                 (int)health_service_sum_today(HealthMetricRestingKCalories));

  // Set all the text buffers
  text_layer_set_text(s_time_layer, s_time_buffer);
  text_layer_set_text(s_date_layer, s_date_buffer);
  text_layer_set_text(s_ampm_layer, s_ampm_buffer);
  text_layer_set_text(s_stepcount_layer, s_stepcount_buffer);
  text_layer_set_text(s_kcal_layer, s_kcal_buffer);
  text_layer_set_text(s_distance_layer, s_distance_buffer);
  text_layer_set_text(s_activetime_layer, s_activetime_buffer);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_watchface();
}

static void main_window_load(Window *window) {
  window_set_background_color(window, blackface ? GColorBlack : GColorWhite);
  
  // Get information about the Window
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  // Create the layers with specific bounds
  s_time_layer = text_layer_create(
      GRect(0, 0, bounds.size.w-INDWIDTH, 44));
  s_date_layer = text_layer_create(
      GRect(0, 40, bounds.size.w-INDWIDTH, 32));
  s_ampm_layer = text_layer_create(
      GRect(bounds.size.w-(INDWIDTH+4), PBL_IF_ROUND_ELSE(0, 6), INDWIDTH+4, 18));
  s_battery_layer = layer_create(GRect(bounds.size.w-INDWIDTH, 26, INDWIDTH, 18));
  layer_set_update_proc(s_battery_layer, draw_battery);
  s_bticon_layer = bitmap_layer_create(
      GRect(bounds.size.w-INDWIDTH, PBL_IF_ROUND_ELSE(0, 52), INDWIDTH, 18));
  s_steplabel_layer = text_layer_create(GRect(2,76,56,16));
  s_stepcount_layer = text_layer_create(
      GRect(0, 85, 60, 30));
  s_kcallabel_layer = text_layer_create(GRect(2,116,56,16));
  s_kcal_layer = text_layer_create(
      GRect(0, 125, 60, 30));
  s_distlabel_layer = text_layer_create(GRect(62,76,80,16));
  s_distance_layer = text_layer_create(
      GRect(60, 85, 84, 30));
  s_timelabel_layer = text_layer_create(GRect(62,116,80,16));
  s_activetime_layer = text_layer_create(
      GRect(60, 125, 84, 30));
  
  // Set the foreground and background colors for the layers
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_background_color(s_date_layer, GColorClear);
  text_layer_set_background_color(s_ampm_layer, GColorClear);
  bitmap_layer_set_background_color(s_bticon_layer, GColorClear);
  text_layer_set_background_color(s_steplabel_layer, blackface ? GColorWhite : GColorBlack);
  text_layer_set_background_color(s_stepcount_layer, GColorClear);
  text_layer_set_background_color(s_kcallabel_layer, blackface ? GColorWhite : GColorBlack);
  text_layer_set_background_color(s_kcal_layer, GColorClear);
  text_layer_set_background_color(s_distlabel_layer, blackface ? GColorWhite : GColorBlack);
  text_layer_set_background_color(s_distance_layer, GColorClear);
  text_layer_set_background_color(s_timelabel_layer, blackface ? GColorWhite : GColorBlack);
  text_layer_set_background_color(s_activetime_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, blackface ? GColorWhite : GColorBlack);
  text_layer_set_text_color(s_date_layer, blackface ? GColorWhite : GColorBlack);
  text_layer_set_text_color(s_ampm_layer, blackface ? GColorWhite : GColorBlack);
  text_layer_set_text_color(s_steplabel_layer, blackface ? GColorBlack : GColorWhite);
  text_layer_set_text_color(s_stepcount_layer, blackface ? GColorWhite : GColorBlack);
  text_layer_set_text_color(s_kcallabel_layer, blackface ? GColorBlack : GColorWhite);
  text_layer_set_text_color(s_kcal_layer, blackface ? GColorWhite : GColorBlack);
  text_layer_set_text_color(s_distlabel_layer, blackface ? GColorBlack : GColorWhite);
  text_layer_set_text_color(s_distance_layer, blackface ? GColorWhite : GColorBlack);
  text_layer_set_text_color(s_timelabel_layer, blackface ? GColorBlack : GColorWhite);
  text_layer_set_text_color(s_activetime_layer, blackface ? GColorWhite : GColorBlack);
  
  // Set starting text for each text layer
  text_layer_set_text(s_time_layer, "00:00");
  text_layer_set_text(s_date_layer, "          ");
  text_layer_set_text(s_ampm_layer, "   ");
  text_layer_set_text(s_steplabel_layer,"STEPS");
  text_layer_set_text(s_stepcount_layer, "     ");
  text_layer_set_text(s_kcallabel_layer,"KCAL");
  text_layer_set_text(s_kcal_layer, "     ");
  text_layer_set_text(s_distlabel_layer,"DISTANCE");
  text_layer_set_text(s_distance_layer, "     ");
  text_layer_set_text(s_timelabel_layer,"ACT TIME");
  text_layer_set_text(s_activetime_layer, "     ");

  // Create the bluetooth icon bitmap layer
  s_bticon_bitmap = gbitmap_create_with_resource(blackface ? RESOURCE_ID_BTICON_W : RESOURCE_ID_BTICON_B);
  bitmap_layer_set_bitmap(s_bticon_layer, s_bticon_bitmap);
  
  // Set the fonts for each text layer
  text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD));
  text_layer_set_font(s_date_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_font(s_ampm_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_font(s_steplabel_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
  text_layer_set_font(s_stepcount_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_font(s_kcallabel_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
  text_layer_set_font(s_kcal_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_font(s_distlabel_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
  text_layer_set_font(s_distance_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_font(s_timelabel_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
  text_layer_set_font(s_activetime_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));

  // Set the alignments for all the layers
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
  text_layer_set_text_alignment(s_date_layer, GTextAlignmentCenter);
  text_layer_set_text_alignment(s_ampm_layer, GTextAlignmentCenter);
  text_layer_set_text_alignment(s_steplabel_layer, GTextAlignmentCenter);
  text_layer_set_text_alignment(s_stepcount_layer, GTextAlignmentCenter);
  text_layer_set_text_alignment(s_kcallabel_layer, GTextAlignmentCenter);
  text_layer_set_text_alignment(s_kcal_layer, GTextAlignmentCenter);
  text_layer_set_text_alignment(s_distlabel_layer, GTextAlignmentCenter);
  text_layer_set_text_alignment(s_distance_layer, GTextAlignmentCenter);
  text_layer_set_text_alignment(s_timelabel_layer, GTextAlignmentCenter);
  text_layer_set_text_alignment(s_activetime_layer, GTextAlignmentCenter);
  bitmap_layer_set_alignment(s_bticon_layer, GAlignCenter);

  // Add all the layers to the window
  // Bitmap layers are added first, so that the text layers will be drawn on top of the bitmaps
  layer_add_child(window_layer, bitmap_layer_get_layer(s_bticon_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_date_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_ampm_layer));
  layer_add_child(window_layer, s_battery_layer);
  layer_add_child(window_layer, text_layer_get_layer(s_time_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_steplabel_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_stepcount_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_kcallabel_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_kcal_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_distlabel_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_distance_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_timelabel_layer));
  layer_add_child(window_layer, text_layer_get_layer(s_activetime_layer));
}

static void main_window_unload(Window *window) {
  // Destroy allocated resources
  text_layer_destroy(s_time_layer);
  text_layer_destroy(s_date_layer);
  text_layer_destroy(s_ampm_layer);
  layer_destroy(s_battery_layer);
  text_layer_destroy(s_steplabel_layer);
  text_layer_destroy(s_stepcount_layer);
  text_layer_destroy(s_kcallabel_layer);
  text_layer_destroy(s_kcal_layer);
  text_layer_destroy(s_distlabel_layer);
  text_layer_destroy(s_distance_layer);
  text_layer_destroy(s_timelabel_layer);
  text_layer_destroy(s_activetime_layer);
  gbitmap_destroy(s_bticon_bitmap);
  bitmap_layer_destroy(s_bticon_layer);
}

static void init() {
  // White and pink watches won't have a black face
  WatchInfoColor watchcolor = watch_info_get_color();
  if (watchcolor==WATCH_INFO_COLOR_WHITE || 
      watchcolor==WATCH_INFO_COLOR_PINK ||
      watchcolor==WATCH_INFO_COLOR_TIME_WHITE) {
    blackface = 0;
  } else {
    blackface = 1;
  }
  
  // Create main Window element and assign to pointer
  s_main_window = window_create();

  // Set handlers to manage the elements inside the Window
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload
  });

  // Show the Window on the watch, with animated=true
  window_stack_push(s_main_window, true);
  
  // Register with TickTimerService
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  // Register for Bluetooth connection updates
  connection_service_subscribe((ConnectionHandlers) {
    .pebble_app_connection_handler = bluetooth_callback
  });
  
  // Make sure the time is displayed from the start
  update_watchface();
  // Show the correct state of the BT connection from the start
  bluetooth_callback(connection_service_peek_pebble_app_connection());
}

static void deinit() {
  // Destroy Window
  window_destroy(s_main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}