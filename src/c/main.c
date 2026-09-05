#include <pebble.h>

#define KEY_SYNC_HOUR 10
#define KEY_SYNC_MINUTE 11
#define KEY_LAST_SYNC_DATE 12
#define KEY_QUEUED_DATE 13
#define KEY_QUEUED_STEPS 14
#define KEY_QUEUED_PENDING 15

static Window *s_window;
static TextLayer *s_time_layer;
static TextLayer *s_steps_layer;
static TextLayer *s_status_layer;
static TextLayer *s_info_layer;
static char s_time_buf[16];
static char s_steps_buf[32];
static char s_status_buf[64];
static char s_date_buf[12];

static void format_date(time_t t, char *buf, size_t len) {
  struct tm *tm = localtime(&t);
  strftime(buf, len, "%Y-%m-%d", tm);
}

static int get_steps_today(void) {
#if defined(PBL_HEALTH)
  HealthServiceAccessibilityMask mask = health_service_metric_accessible(HealthMetricStepCount, time_start_of_today(), time(NULL));
  if (mask & HealthServiceAccessibilityMaskAvailable) {
    return (int)health_service_sum_today(HealthMetricStepCount);
  }
#endif
  return 0;
}

static bool has_health(void) {
#if defined(PBL_HEALTH)
  return true;
#else
  return false;
#endif
}

static void update_steps_display(void) {
  if (!has_health()) {
    snprintf(s_steps_buf, sizeof(s_steps_buf), "No Health");
    text_layer_set_text(s_steps_layer, s_steps_buf);
    return;
  }
  int steps = get_steps_today();
  snprintf(s_steps_buf, sizeof(s_steps_buf), "%d steps", steps);
  text_layer_set_text(s_steps_layer, s_steps_buf);
}

static void set_status(const char *msg) {
  snprintf(s_status_buf, sizeof(s_status_buf), "%s", msg);
  text_layer_set_text(s_status_layer, s_status_buf);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "status %s", msg);
}

static void send_steps(int steps, const char *date_str);

static void send_queued(void) {
  if (!persist_exists(KEY_QUEUED_PENDING) || !persist_read_bool(KEY_QUEUED_PENDING)) return;
  if (!persist_exists(KEY_QUEUED_DATE) || !persist_exists(KEY_QUEUED_STEPS)) return;
  char qdate[12];
  persist_read_string(KEY_QUEUED_DATE, qdate, sizeof(qdate));
  int qsteps = persist_read_int(KEY_QUEUED_STEPS);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "retry queued %d %s", qsteps, qdate);
  send_steps(qsteps, qdate);
}

static void queue_steps(int steps, const char *date_str) {
  persist_write_string(KEY_QUEUED_DATE, date_str);
  persist_write_int(KEY_QUEUED_STEPS, steps);
  persist_write_bool(KEY_QUEUED_PENDING, true);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "queued %d %s", steps, date_str);
  send_steps(steps, date_str);
}

static void send_steps(int steps, const char *date_str) {
  DictionaryIterator *out;
  AppMessageResult res = app_message_outbox_begin(&out);
  if (res != APP_MSG_OK) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "outbox begin fail %d", res);
    set_status("No outbox");
    return;
  }
  dict_write_int(out, MESSAGE_KEY_STEPS, &steps, sizeof(steps), true);
  dict_write_cstring(out, MESSAGE_KEY_STEPS_DATE, date_str);
  int32_t cmd = 0;
  dict_write_int(out, MESSAGE_KEY_CMD, &cmd, sizeof(cmd), true);
  res = app_message_outbox_send();
  if (res != APP_MSG_OK) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "send fail %d", res);
    set_status("Send fail");
  } else {
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "Sync %d", steps);
    set_status(tmp);
  }
}

static bool is_already_synced_today(const char *date_str) {
  if (!persist_exists(KEY_LAST_SYNC_DATE)) return false;
  char last[12];
  persist_read_string(KEY_LAST_SYNC_DATE, last, sizeof(last));
  return strcmp(last, date_str) == 0;
}

static void try_daily_sync(bool force) {
  if (!has_health()) {
    set_status("No Health");
    return;
  }
  time_t now = time(NULL);
  format_date(now, s_date_buf, sizeof(s_date_buf));
  if (!force && is_already_synced_today(s_date_buf)) return;
  if (persist_exists(KEY_QUEUED_PENDING) && persist_read_bool(KEY_QUEUED_PENDING)) {
    send_queued();
    return;
  }
  if (!force && is_already_synced_today(s_date_buf)) return;
  int steps = get_steps_today();
  queue_steps(steps, s_date_buf);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  strftime(s_time_buf, sizeof(s_time_buf), "%H:%M", tick_time);
  text_layer_set_text(s_time_layer, s_time_buf);
  update_steps_display();
  int sync_h = 23;
  int sync_m = 0;
  if (persist_exists(KEY_SYNC_HOUR)) sync_h = persist_read_int(KEY_SYNC_HOUR);
  if (persist_exists(KEY_SYNC_MINUTE)) sync_m = persist_read_int(KEY_SYNC_MINUTE);
  if (tick_time->tm_hour == sync_h && tick_time->tm_min == sync_m) {
    time_t now = time(NULL);
    format_date(now, s_date_buf, sizeof(s_date_buf));
    if (!is_already_synced_today(s_date_buf)) {
      APP_LOG(APP_LOG_LEVEL_DEBUG, "scheduled sync %d:%d", sync_h, sync_m);
      try_daily_sync(false);
    }
  } else {
    if (persist_exists(KEY_QUEUED_PENDING) && persist_read_bool(KEY_QUEUED_PENDING)) {
      if (tick_time->tm_min % 5 == 0) {
        APP_LOG(APP_LOG_LEVEL_DEBUG, "retry pending");
        send_queued();
      }
    }
  }
}

#if defined(PBL_HEALTH)
static void health_handler(HealthEventType event, void *ctx) {
  if (event == HealthEventMovementUpdate || event == HealthEventSignificantUpdate) {
    update_steps_display();
  }
}
#endif

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  Tuple *t;
  t = dict_find(iter, MESSAGE_KEY_SYNC_HOUR);
  if (t) {
    persist_write_int(KEY_SYNC_HOUR, (int)t->value->int32);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "sync hour %d", (int)t->value->int32);
  }
  t = dict_find(iter, MESSAGE_KEY_SYNC_MINUTE);
  if (t) {
    persist_write_int(KEY_SYNC_MINUTE, (int)t->value->int32);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "sync min %d", (int)t->value->int32);
  }
  t = dict_find(iter, MESSAGE_KEY_EMAIL);
  if (t) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "email set");
  }
  t = dict_find(iter, MESSAGE_KEY_STATUS);
  if (t) {
    const char *msg = t->value->cstring;
    set_status(msg);
    if (strncmp(msg, "OK", 2) == 0) {
      vibes_short_pulse();
      if (persist_exists(KEY_QUEUED_DATE)) {
        char qdate[12];
        persist_read_string(KEY_QUEUED_DATE, qdate, sizeof(qdate));
        persist_write_string(KEY_LAST_SYNC_DATE, qdate);
      } else {
        time_t now = time(NULL);
        format_date(now, s_date_buf, sizeof(s_date_buf));
        persist_write_string(KEY_LAST_SYNC_DATE, s_date_buf);
      }
      persist_write_bool(KEY_QUEUED_PENDING, false);
    } else if (strncmp(msg, "ERR", 3) == 0) {
      vibes_long_pulse();
      set_status(msg);
    }
  }
}

static void inbox_dropped_handler(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "inbox dropped %d", reason);
  set_status("Inbox drop");
}

static void outbox_failed_handler(DictionaryIterator *iter, AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "outbox failed %d", reason);
  set_status("No phone");
}

static void outbox_sent_handler(DictionaryIterator *iter, void *context) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "outbox sent");
}

static void select_click_handler(ClickRecognizerRef ref, void *ctx) {
  set_status("Sync now");
  try_daily_sync(true);
}

static void click_config_provider(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);
  int w = bounds.size.w;
  s_time_layer = text_layer_create(GRect(0, 10, w, 30));
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, GColorWhite);
  text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_time_layer));

  s_steps_layer = text_layer_create(GRect(0, 42, w, 26));
  text_layer_set_background_color(s_steps_layer, GColorClear);
  text_layer_set_text_color(s_steps_layer, GColorWhite);
  text_layer_set_font(s_steps_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_text_alignment(s_steps_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_steps_layer));

  s_status_layer = text_layer_create(GRect(5, 70, w - 10, 50));
  text_layer_set_background_color(s_status_layer, GColorClear);
  text_layer_set_text_color(s_status_layer, GColorLightGray);
  text_layer_set_font(s_status_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_status_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_status_layer));

  s_info_layer = text_layer_create(GRect(0, bounds.size.h - 22, w, 18));
  text_layer_set_background_color(s_info_layer, GColorClear);
  text_layer_set_text_color(s_info_layer, GColorLightGray);
  text_layer_set_font(s_info_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(s_info_layer, GTextAlignmentCenter);
  text_layer_set_text(s_info_layer, "SELECT to sync");
  layer_add_child(root, text_layer_get_layer(s_info_layer));

  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  tick_handler(t, MINUTE_UNIT);
  if (!has_health()) set_status("No Health");
  else if (persist_exists(KEY_QUEUED_PENDING) && persist_read_bool(KEY_QUEUED_PENDING)) {
    set_status("Queued retry");
    send_queued();
  } else if (persist_exists(KEY_LAST_SYNC_DATE)) {
    char last[12];
    persist_read_string(KEY_LAST_SYNC_DATE, last, sizeof(last));
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "Last %s", last);
    set_status(tmp);
  } else {
    set_status("Ready");
  }
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
#if defined(PBL_HEALTH)
  health_service_events_subscribe(health_handler, NULL);
#endif
}

static void window_unload(Window *window) {
  tick_timer_service_unsubscribe();
#if defined(PBL_HEALTH)
  health_service_events_unsubscribe();
#endif
  text_layer_destroy(s_time_layer);
  text_layer_destroy(s_steps_layer);
  text_layer_destroy(s_status_layer);
  text_layer_destroy(s_info_layer);
}

static void init(void) {
  app_message_register_inbox_received(inbox_received_handler);
  app_message_register_inbox_dropped(inbox_dropped_handler);
  app_message_register_outbox_failed(outbox_failed_handler);
  app_message_register_outbox_sent(outbox_sent_handler);
  app_message_open(512, 512);
  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_click_config_provider(s_window, click_config_provider);
  window_set_window_handlers(s_window, (WindowHandlers){ .load = window_load, .unload = window_unload });
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
