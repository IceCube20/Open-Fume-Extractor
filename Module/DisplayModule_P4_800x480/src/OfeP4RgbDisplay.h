// Generated from the stable OFE drawing implementation and verified ELECROW timings.
#pragma once
class OfeP4RgbDisplay : public Arduino_GFX {
public:
  OfeP4RgbDisplay(int16_t w, int16_t h, uint8_t rotation)
      : Arduino_GFX(w, h) {
    setRotation(rotation);
  }

  bool begin(int32_t speed = GFX_NOT_DEFINED) override {
    (void)speed;
    if (panel_) return framebuffer_ != nullptr;

    // Stable ESP32-P4 RGB topology:
    //   one full RGB565 framebuffer in PSRAM
    //   + two small DMA bounce buffers in internal SRAM
    // LVGL itself keeps its tile/draw buffer in internal SRAM as well.
    // A second PSRAM framebuffer is intentionally NOT allocated here because
    // LVGL is using PARTIAL rendering, so it would not provide true page flipping.
    esp_lcd_rgb_panel_config_t panel_config = {
      .clk_src = LCD_CLK_SRC_DEFAULT,
      .timings = {
        .pclk_hz = DISPLAY_RGB_PCLK_HZ,
        .h_res = DISPLAY_RGB_WIDTH,
        .v_res = DISPLAY_RGB_HEIGHT,
        .hsync_pulse_width = 4,
        .hsync_back_porch = 8,
        .hsync_front_porch = 8,
        .vsync_pulse_width = 4,
        .vsync_back_porch = 16,
        .vsync_front_porch = 16,
        .flags = {
          // Match ELECROW's bundled BusRGB defaults.
          .hsync_idle_low = 0,
          .vsync_idle_low = 0,
          .de_idle_high = 0,
          .pclk_active_neg = 1,
          .pclk_idle_high = 1,
        },
      },
      .data_width = 16,
      .bits_per_pixel = 16,
      .num_fbs = 1,
      .bounce_buffer_size_px = DISPLAY_RGB_WIDTH * DISPLAY_RGB_BOUNCE_BUFFER_LINES,
      .sram_trans_align = 8,
      .psram_trans_align = 64,
      .hsync_gpio_num = ofe_p4::hsync,
      .vsync_gpio_num = ofe_p4::vsync,
      .de_gpio_num = ofe_p4::de,
      .pclk_gpio_num = ofe_p4::pclk,
      .disp_gpio_num = GPIO_NUM_NC,
      .data_gpio_nums = {0},
      .flags = {
        .disp_active_low = true,
        .refresh_on_demand = false,
        .fb_in_psram = true,
        .double_fb = false,
        .no_fb = false,
        // Keep false: LVGL/CPU writes into the PSRAM framebuffer while the
        // bounce-buffer ISR reads it. Invalidating those cache lines can lose
        // a concurrent write on the other core.
        .bb_invalidate_cache = false,
      },
    };

    // Match Arduino_ESP32RGBPanel's RGB565 bit order for this board.
    const auto& data_pins = ofe_p4::rgb_pins;
    for (uint8_t i = 0; i < 16; ++i) panel_config.data_gpio_nums[i] = data_pins[i];

    esp_err_t err = esp_lcd_new_rgb_panel(&panel_config, &panel_);
    if (err != ESP_OK) {
      Serial.printf("esp_lcd_new_rgb_panel failed: 0x%X (bounce=%u lines)\n",
                    (unsigned)err, (unsigned)DISPLAY_RGB_BOUNCE_BUFFER_LINES);
      panel_ = nullptr;
      return false;
    }

    err = esp_lcd_panel_reset(panel_);
    if (err != ESP_OK) {
      Serial.printf("esp_lcd_panel_reset failed: 0x%X\n", (unsigned)err);
      return false;
    }
    err = esp_lcd_panel_init(panel_);
    if (err != ESP_OK) {
      Serial.printf("esp_lcd_panel_init failed: 0x%X\n", (unsigned)err);
      return false;
    }

    void* fb0 = nullptr;
    err = esp_lcd_rgb_panel_get_frame_buffer(panel_, 1, &fb0);
    if (err != ESP_OK || !fb0) {
      Serial.printf("RGB framebuffer lookup failed: 0x%X fb=%p\n", (unsigned)err, fb0);
      return false;
    }

    framebuffer_ = static_cast<uint16_t*>(fb0);
    framebuffer_bytes_ = (size_t)DISPLAY_RGB_WIDTH * (size_t)DISPLAY_RGB_HEIGHT * sizeof(uint16_t);
    Serial.printf("RGB panel: single PSRAM framebuffer=%p (%u bytes), bounce=%u lines (%u bytes internal total)\n",
                  framebuffer_, (unsigned)framebuffer_bytes_,
                  (unsigned)DISPLAY_RGB_BOUNCE_BUFFER_LINES,
                  (unsigned)(2U * DISPLAY_RGB_WIDTH * DISPLAY_RGB_BOUNCE_BUFFER_LINES * sizeof(uint16_t)));
    return true;
  }

  void writePixelPreclipped(int16_t x, int16_t y, uint16_t color) override {
    if (!framebuffer_) return;
    mapPoint(x, y);
    if ((uint16_t)x >= DISPLAY_RGB_WIDTH || (uint16_t)y >= DISPLAY_RGB_HEIGHT) return;
    uint16_t* dst = framebuffer_ + ((int32_t)y * DISPLAY_RGB_WIDTH) + x;
    *dst = color;
    writeBack(dst, sizeof(uint16_t));
  }

  void writeFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) override {
    writeFillRectPreclipped(x, y, w, 1, color);
  }

  void writeFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) override {
    writeFillRectPreclipped(x, y, 1, h, color);
  }

  void writeFillRectPreclipped(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override {
    if (!framebuffer_ || w <= 0 || h <= 0) return;

    if (_rotation == 2) {
      x = DISPLAY_RGB_WIDTH - x - w;
      y = DISPLAY_RGB_HEIGHT - y - h;
    }

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x >= DISPLAY_RGB_WIDTH || y >= DISPLAY_RGB_HEIGHT || w <= 0 || h <= 0) return;
    if (x + w > DISPLAY_RGB_WIDTH) w = DISPLAY_RGB_WIDTH - x;
    if (y + h > DISPLAY_RGB_HEIGHT) h = DISPLAY_RGB_HEIGHT - y;

    uint16_t* first = framebuffer_ + ((int32_t)y * DISPLAY_RGB_WIDTH) + x;
    uint16_t* row = first;
    for (int16_t j = 0; j < h; ++j) {
      for (int16_t i = 0; i < w; ++i) row[i] = color;
      row += DISPLAY_RGB_WIDTH;
    }

    // One cache writeback per rectangle instead of one esp_lcd draw call per row.
    const size_t span_pixels = (size_t)(h - 1) * DISPLAY_RGB_WIDTH + (size_t)w;
    writeBack(first, span_pixels * sizeof(uint16_t));
  }

  void draw16bitRGBBitmap(int16_t x, int16_t y, const uint16_t bitmap[], int16_t w, int16_t h) override {
    draw16bitRGBBitmapCopy(x, y, bitmap, w, h);
  }

  void draw16bitRGBBitmap(int16_t x, int16_t y, uint16_t* bitmap, int16_t w, int16_t h) override {
    draw16bitRGBBitmapCopy(x, y, bitmap, w, h);
  }

  void flush(bool force_flush = false) override {
    if (framebuffer_ && force_flush) writeBack(framebuffer_, framebuffer_bytes_);
  }

  void requestDmaResync() {
    if (!panel_) return;
    // esp_lcd schedules the actual DMA restart for the next VSYNC, so the
    // raster is repaired without an asynchronous mid-frame restart.
    const esp_err_t err = esp_lcd_rgb_panel_restart(panel_);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
      Serial.printf("RGB DMA resync request failed: 0x%X\n", (unsigned)err);
    }
  }

  uint16_t* getFramebuffer() const { return framebuffer_; }

private:
  static void writeBack(const void* addr, size_t bytes) {
    (void)addr;
    (void)bytes;
    // IMPORTANT: no explicit PSRAM framebuffer cache writeback in bounce-buffer mode.
    //
    // esp_lcd's bounce-buffer ISR refills the internal DMA buffers with a CPU memcpy()
    // from the PSRAM framebuffer. The official RGB driver intentionally skips the
    // framebuffer esp_cache_msync() path when a bounce buffer is active.
    //
    // Forcing Cache_WriteBack_Addr() here after every LVGL rectangle can stall the
    // external-memory/cache path long enough for the bounce refill ISR to miss its
    // deadline. On ESP32-P4 that produces exactly the visible whole-frame drift /
    // vertical shift which the driver then repairs at VSYNC.
  }

  void mapPoint(int16_t& x, int16_t& y) const {
    if (_rotation == 2) {
      x = DISPLAY_RGB_WIDTH - 1 - x;
      y = DISPLAY_RGB_HEIGHT - 1 - y;
    }
  }

  void draw16bitRGBBitmapCopy(int16_t x, int16_t y, const uint16_t* bitmap, int16_t w, int16_t h) {
    if (!framebuffer_ || !bitmap || w <= 0 || h <= 0) return;

    const int16_t src_stride = w;
    if ((x + w - 1) < 0 || (y + h - 1) < 0 || x > _max_x || y > _max_y) return;

    int16_t src_x = 0;
    int16_t src_y = 0;
    if (x < 0) { src_x = -x; w += x; x = 0; }
    if (y < 0) { src_y = -y; h += y; y = 0; }
    if ((x + w - 1) > _max_x) w = _max_x - x + 1;
    if ((y + h - 1) > _max_y) h = _max_y - y + 1;
    if (w <= 0 || h <= 0) return;

    const uint16_t* src = bitmap + ((int32_t)src_y * src_stride) + src_x;

    if (_rotation == 2) {
      const int16_t dst_x = DISPLAY_RGB_WIDTH - x - w;
      const int16_t dst_y = DISPLAY_RGB_HEIGHT - y - h;
      uint16_t* first = framebuffer_ + ((int32_t)dst_y * DISPLAY_RGB_WIDTH) + dst_x;

      // LVGL's tile stays in internal SRAM. Rotate/copy it directly into the
      // single PSRAM framebuffer, then commit the whole affected span once.
      for (int16_t j = 0; j < h; ++j) {
        uint16_t* dst = framebuffer_ + ((int32_t)(dst_y + h - 1 - j) * DISPLAY_RGB_WIDTH) + dst_x;
        for (int16_t i = 0; i < w; ++i) dst[w - 1 - i] = src[i];
        src += src_stride;
      }

      const size_t span_pixels = (size_t)(h - 1) * DISPLAY_RGB_WIDTH + (size_t)w;
      writeBack(first, span_pixels * sizeof(uint16_t));
      return;
    }

    uint16_t* first = framebuffer_ + ((int32_t)y * DISPLAY_RGB_WIDTH) + x;
    uint16_t* dst = first;
    for (int16_t j = 0; j < h; ++j) {
      memcpy(dst, src, (size_t)w * sizeof(uint16_t));
      src += src_stride;
      dst += DISPLAY_RGB_WIDTH;
    }

    const size_t span_pixels = (size_t)(h - 1) * DISPLAY_RGB_WIDTH + (size_t)w;
    writeBack(first, span_pixels * sizeof(uint16_t));
  }

  esp_lcd_panel_handle_t panel_ = nullptr;
  uint16_t* framebuffer_ = nullptr;
  size_t framebuffer_bytes_ = 0;
};