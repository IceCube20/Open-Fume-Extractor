// Included after the screen helpers; all calls are made by the GUI owner.
static bool update_exit_blocked() {
  return fw_update_active.load() || fw_update_started_ms.load() || fw_update_reboot_ms.load() ||
    (status.update_active && !update_is_local_display_target(status.update_target));
}

static void lv_update_abort_back_event(lv_event_t*) {
  if (!display_update_exit.canReturn(update_exit_blocked())) return;
  display_update_exit.cancel();
  if (ui_status_back) lv_obj_add_flag(ui_status_back, LV_OBJ_FLAG_HIDDEN);
  lv_show_home_event(nullptr);
}

static void update_exit_tick() {
  if (!lvgl_ready) return;
  const bool updating = update_exit_blocked();
  const bool result_screen = lv_screen_active() == ui_status_screen ||
                             lv_screen_active() == ui_update_screen;
  const auto action = display_update_exit.poll(millis(), updating, result_screen);
  if (action == OfeDisplayUpdateExit::Action::ShowAborted) {
    screensaver_wake();
    show_status_message(update_message_text("Update aborted"));
  } else if (action == OfeDisplayUpdateExit::Action::Home) {
    lv_show_home_event(nullptr);
  }
  if (ui_status_back) {
    if (display_update_exit.canReturn(updating)) lv_obj_clear_flag(ui_status_back, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(ui_status_back, LV_OBJ_FLAG_HIDDEN);
  }
}
