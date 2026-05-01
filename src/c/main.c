#include <pebble.h>

#define STORAGE_KEY_COLOR 0

static Window *s_window;
static TextLayer *s_time_layer;
static char s_time_buf[8];

static GColor get_time_color(int index) {
  switch (index) {
    case 1:  return GColorGreen;
    case 2:  return GColorYellow;
    case 3:  return GColorCyan;
    case 4:  return GColorOrange;
    default: return GColorWhite;
  }
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  strftime(s_time_buf, sizeof(s_time_buf),
           clock_is_24h_style() ? "%H:%M" : "%I:%M", tick_time);
  text_layer_set_text(s_time_layer, s_time_buf);
}

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  Tuple *t = dict_find(iter, MESSAGE_KEY_COLOR_INDEX);
  if (t) {
    int index = (int)t->value->int32;
    persist_write_int(STORAGE_KEY_COLOR, index);
    text_layer_set_text_color(s_time_layer, get_time_color(index));
  }
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);

  s_time_layer = text_layer_create(
    GRect(0, bounds.size.h / 2 - 28, bounds.size.w, 56));
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer,
    get_time_color(persist_read_int(STORAGE_KEY_COLOR)));
  text_layer_set_font(s_time_layer,
    fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS));
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_time_layer));

  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  tick_handler(t, MINUTE_UNIT);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
}

static void window_unload(Window *window) {
  tick_timer_service_unsubscribe();
  text_layer_destroy(s_time_layer);
}

static void init(void) {
  app_message_register_inbox_received(inbox_received_handler);
  app_message_open(64, 64);

  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);
}

static void deinit(void) {
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
