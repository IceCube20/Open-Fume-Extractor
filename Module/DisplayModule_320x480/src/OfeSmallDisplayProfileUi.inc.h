// Included after UI/benchmark definitions; all LVGL work stays on the GUI owner.
static bool small_profile_command(const char* command) {
  if (!strcmp(command, "render")) {
    debug_printf("RENDER active=B%u pending=B%u; use render b16 / render b32 / render b48\n",
      (unsigned)small_display_profiles.active(), (unsigned)small_display_profiles.pending());
    return true;
  }
  const uint8_t lines = ofe_small_display::commandLines(command);
  if (!lines) {
    if (!strncmp(command, "render ", 7)) {
      debug_println("Usage: render | render b16 | render b32 | render b48");
      return true;
    }
    return false;
  }
  if (!lvgl_ready) {
    debug_println("RENDER unavailable: LVGL is not ready");
  } else if (!small_display_profiles.request(lines)) {
    debug_println("RENDER request already pending; wait for applied/failed before retrying");
  } else {
    debug_printf("RENDER queued B%u; waiting for GUI outside benchmark/OTA/pairing/screensaver\n",
      (unsigned)lines);
  }
  return true;
}

static void small_profile_tick() {
  if (!small_display_profiles.pending() || !lvgl_ready || !lvgl_disp || !gfx ||
      fw_update_active || status.update_active || display_benchmark.active ||
      display_ble.active() || screensaver_active) return;
  const uint8_t lines = small_display_profiles.take();
  const uint32_t width = gfx->width();
  const uint32_t bytes = ofe_small_display::bufferBytes(width, lines);
  if (!bytes) return;

  // Allocate before replacing anything: a failed experiment keeps the live UI.
  auto* candidate = ofe_small_display::allocatePsram(width, lines);
  if (!candidate) {
    debug_printf("RENDER B%u allocation failed; retaining B%u\n",
      (unsigned)lines, (unsigned)small_display_profiles.active());
    return;
  }
#ifdef OFE_SMALL_PROFILE_REPLACE
  if (!OFE_SMALL_PROFILE_REPLACE(candidate, bytes)) {
    heap_caps_free(candidate);
    debug_printf("RENDER B%u transport allocation failed; retaining B%u\n",
      (unsigned)lines, (unsigned)small_display_profiles.active());
    return;
  }
#else
  uint8_t* previous = lvgl_draw_buf;
  lv_display_set_buffers(lvgl_disp, candidate, nullptr, bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lvgl_draw_buf = candidate;
  heap_caps_free(previous);
#endif
  lvgl_buf_pixels = bytes / 2U;
  small_display_profiles.publish(lines);
  display_benchmark_reset_counters(millis());
  lv_obj_invalidate(lv_screen_active());
  debug_printf("RENDER applied B%u PSRAM, %u bytes; internal free=%u largest=%u\n",
    (unsigned)lines, (unsigned)bytes,
    (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
    (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
}
