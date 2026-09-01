#pragma once

// CPU load and developer heap diagnostics helpers.
static uint32_t loop_window_ms = 0;
static uint32_t loop_max_us = 0;
static uint8_t cpu_load_pct = 0;
static uint16_t loop_max_ms = 0;
static configRUN_TIME_COUNTER_TYPE cpu_prev_total = 0;
static configRUN_TIME_COUNTER_TYPE cpu_prev_idle[configNUMBER_OF_CORES] = {};
static bool cpu_runtime_valid = false;

struct HeapDiagSnapshot {
  bool active = false;
  uint32_t session_ms = 0;
  uint32_t samples = 0;
  uint32_t free_now = 0;
  uint32_t internal_now = 0;
  uint32_t largest_now = 0;
  uint32_t psram_now = 0;

  // Coherent snapshot captured when total free heap reached its minimum.
  uint32_t low_free = 0;
  uint32_t low_internal = 0;
  uint32_t low_largest = 0;
  uint32_t low_psram = 0;
  uint32_t low_at_ms = 0;
  int8_t low_core = -1;
  char low_label[72] = {0};
  char low_task[20] = {0};
  char low_context[112] = {0};

  // Separate coherent snapshot captured when the largest contiguous block
  // reached its minimum. This avoids mixing values from different moments.
  uint32_t block_free = 0;
  uint32_t block_internal = 0;
  uint32_t block_largest = 0;
  uint32_t block_psram = 0;
  uint32_t block_at_ms = 0;
  int8_t block_core = -1;
  char block_label[72] = {0};
  char block_task[20] = {0};
  char block_context[112] = {0};
};

enum HeapDiagContextSlot : uint8_t {
  HEAP_DIAG_CTX_LOOP = 0,
  HEAP_DIAG_CTX_WEB = 1,
  HEAP_DIAG_CTX_MQTT = 2,
};

static bool developer_mode_enabled = false;
static portMUX_TYPE heap_diag_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t heap_diag_started_ms = 0;
static uint32_t heap_diag_samples = 0;

static uint32_t heap_diag_low_free = UINT32_MAX;
static uint32_t heap_diag_low_internal = 0;
static uint32_t heap_diag_low_largest = 0;
static uint32_t heap_diag_low_psram = 0;
static uint32_t heap_diag_low_at_ms = 0;
static int8_t heap_diag_low_core = -1;
static char heap_diag_low_label[72] = "-";
static char heap_diag_low_task[20] = "-";
static char heap_diag_low_context[112] = "-";

static uint32_t heap_diag_block_largest = UINT32_MAX;
static uint32_t heap_diag_block_free = 0;

// Developer heap diagnostics still capture every exact low-water mark for the
// web/status snapshot. Serial is warning-oriented instead: it prints only when
// the heap crosses a meaningful warning/critical threshold. Full details remain
// available on the status page and through the interactive `heap` CLI command.
static constexpr uint32_t HEAP_DIAG_SERIAL_FREE_WARN_BYTES = 64UL * 1024UL;
static constexpr uint32_t HEAP_DIAG_SERIAL_FREE_CRIT_BYTES = 48UL * 1024UL;
static constexpr uint32_t HEAP_DIAG_SERIAL_BLOCK_WARN_BYTES = 32UL * 1024UL;
static constexpr uint32_t HEAP_DIAG_SERIAL_BLOCK_CRIT_BYTES = 24UL * 1024UL;
static uint8_t heap_diag_serial_free_level = 0;
static uint8_t heap_diag_serial_block_level = 0;
static bool heap_diag_serial_debug = false;
static uint32_t heap_diag_block_internal = 0;
static uint32_t heap_diag_block_psram = 0;
static uint32_t heap_diag_block_at_ms = 0;
static int8_t heap_diag_block_core = -1;
static char heap_diag_block_label[72] = "-";
static char heap_diag_block_task[20] = "-";
static char heap_diag_block_context[112] = "-";

static char heap_diag_context_loop[36] = "";
static char heap_diag_context_web[36] = "";
static char heap_diag_context_mqtt[36] = "";
static TaskHandle_t heap_diag_task_handle = nullptr;
static HeapDiagSnapshot heap_diag_current_snapshot;

static bool heap_diag_active() {
  return developer_mode_enabled;
}

static void heap_diag_sample(const char* label);

static void heap_diag_set_serial_debug(bool enabled) {
  heap_diag_serial_debug = enabled;
}

static void heap_diag_copy(char* dst, size_t dst_len, const char* src) {
  if (!dst || !dst_len) return;
  if (!src) src = "";
  strncpy(dst, src, dst_len - 1);
  dst[dst_len - 1] = 0;
}

static void heap_diag_set_context(uint8_t slot, const char* value) {
  if (!heap_diag_active()) return;
  portENTER_CRITICAL(&heap_diag_mux);
  char* dst = nullptr;
  size_t dst_len = 0;
  if (slot == HEAP_DIAG_CTX_LOOP) {
    dst = heap_diag_context_loop;
    dst_len = sizeof(heap_diag_context_loop);
  } else if (slot == HEAP_DIAG_CTX_WEB) {
    dst = heap_diag_context_web;
    dst_len = sizeof(heap_diag_context_web);
  } else if (slot == HEAP_DIAG_CTX_MQTT) {
    dst = heap_diag_context_mqtt;
    dst_len = sizeof(heap_diag_context_mqtt);
  }
  if (dst && dst_len) heap_diag_copy(dst, dst_len, value);
  portEXIT_CRITICAL(&heap_diag_mux);
}

static void heap_diag_clear_context(uint8_t slot) {
  heap_diag_set_context(slot, "");
}

static void heap_diag_context_string(char* out, size_t out_len) {
  if (!out || !out_len) return;
  char loop_ctx[sizeof(heap_diag_context_loop)];
  char web_ctx[sizeof(heap_diag_context_web)];
  char mqtt_ctx[sizeof(heap_diag_context_mqtt)];
  portENTER_CRITICAL(&heap_diag_mux);
  heap_diag_copy(loop_ctx, sizeof(loop_ctx), heap_diag_context_loop);
  heap_diag_copy(web_ctx, sizeof(web_ctx), heap_diag_context_web);
  heap_diag_copy(mqtt_ctx, sizeof(mqtt_ctx), heap_diag_context_mqtt);
  portEXIT_CRITICAL(&heap_diag_mux);

  out[0] = 0;
  if (loop_ctx[0]) snprintf(out + strlen(out), out_len - strlen(out), "loop:%s", loop_ctx);
  if (web_ctx[0] && strlen(out) < out_len - 1) snprintf(out + strlen(out), out_len - strlen(out), "%sweb:%s", out[0] ? " | " : "", web_ctx);
  if (mqtt_ctx[0] && strlen(out) < out_len - 1) snprintf(out + strlen(out), out_len - strlen(out), "%smqtt:%s", out[0] ? " | " : "", mqtt_ctx);
  if (!out[0]) heap_diag_copy(out, out_len, "idle");
}

static void heap_diag_probe_task(void* parameter) {
  (void)parameter;
  for (;;) {
    if (heap_diag_active()) heap_diag_sample("probe");
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

static void heap_diag_start_task() {
  if (heap_diag_task_handle) return;
  xTaskCreatePinnedToCore(heap_diag_probe_task, "heap-diag", 3072, nullptr, 1, &heap_diag_task_handle, 1);
}

static void heap_diag_enable() {
  if (!developer_mode_enabled) return;
  heap_diag_start_task();
  const uint32_t now = millis();
  portENTER_CRITICAL(&heap_diag_mux);
  heap_diag_started_ms = now;
  heap_diag_samples = 0;

  heap_diag_low_free = UINT32_MAX;
  heap_diag_low_internal = 0;
  heap_diag_low_largest = 0;
  heap_diag_low_psram = 0;
  heap_diag_low_at_ms = 0;
  heap_diag_low_core = -1;
  heap_diag_copy(heap_diag_low_label, sizeof(heap_diag_low_label), "developer_mode");
  heap_diag_copy(heap_diag_low_task, sizeof(heap_diag_low_task), "-");
  heap_diag_copy(heap_diag_low_context, sizeof(heap_diag_low_context), "-");

  heap_diag_block_largest = UINT32_MAX;
  heap_diag_block_free = 0;
  heap_diag_serial_free_level = 0;
  heap_diag_serial_block_level = 0;
  heap_diag_block_internal = 0;
  heap_diag_block_psram = 0;
  heap_diag_block_at_ms = 0;
  heap_diag_block_core = -1;
  heap_diag_copy(heap_diag_block_label, sizeof(heap_diag_block_label), "developer_mode");
  heap_diag_copy(heap_diag_block_task, sizeof(heap_diag_block_task), "-");
  heap_diag_copy(heap_diag_block_context, sizeof(heap_diag_block_context), "-");

  heap_diag_context_loop[0] = 0;
  heap_diag_context_web[0] = 0;
  heap_diag_context_mqtt[0] = 0;
  portEXIT_CRITICAL(&heap_diag_mux);
  heap_diag_sample("developer_mode");
}

static void heap_diag_sample(const char* label) {
  if (!heap_diag_active()) return;
  const uint32_t free_now = ESP.getFreeHeap();
  const uint32_t internal_now = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const uint32_t largest_now = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  const uint32_t psram_now = ESP.getFreePsram();
  const uint32_t at_ms = millis();
  const int8_t core = (int8_t)xPortGetCoreID();
  const char* task_name_ptr = pcTaskGetName(nullptr);
  char task_name[20];
  heap_diag_copy(task_name, sizeof(task_name), task_name_ptr ? task_name_ptr : "unknown");
  char context[112];
  heap_diag_context_string(context, sizeof(context));

  bool new_low = false;
  bool new_block_low = false;
  bool print_low = false;
  bool print_block_low = false;
  portENTER_CRITICAL(&heap_diag_mux);
  ++heap_diag_samples;
  if (free_now < heap_diag_low_free) {
    new_low = true;
    heap_diag_low_free = free_now;
    heap_diag_low_internal = internal_now;
    heap_diag_low_largest = largest_now;
    heap_diag_low_psram = psram_now;
    heap_diag_low_at_ms = at_ms;
    heap_diag_low_core = core;
    heap_diag_copy(heap_diag_low_label, sizeof(heap_diag_low_label), label && *label ? label : "sample");
    heap_diag_copy(heap_diag_low_task, sizeof(heap_diag_low_task), task_name);
    heap_diag_copy(heap_diag_low_context, sizeof(heap_diag_low_context), context);
  }
  if (largest_now < heap_diag_block_largest) {
    new_block_low = true;
    heap_diag_block_largest = largest_now;
    heap_diag_block_free = free_now;
    heap_diag_block_internal = internal_now;
    heap_diag_block_psram = psram_now;
    heap_diag_block_at_ms = at_ms;
    heap_diag_block_core = core;
    heap_diag_copy(heap_diag_block_label, sizeof(heap_diag_block_label), label && *label ? label : "sample");
    heap_diag_copy(heap_diag_block_task, sizeof(heap_diag_block_task), task_name);
    heap_diag_copy(heap_diag_block_context, sizeof(heap_diag_block_context), context);
  }
  if (new_low) {
    const uint8_t level = free_now <= HEAP_DIAG_SERIAL_FREE_CRIT_BYTES ? 2 :
                          (free_now <= HEAP_DIAG_SERIAL_FREE_WARN_BYTES ? 1 : 0);
    if (level > heap_diag_serial_free_level) {
      print_low = true;
      heap_diag_serial_free_level = level;
    }
  }
  if (new_block_low) {
    const uint8_t level = largest_now <= HEAP_DIAG_SERIAL_BLOCK_CRIT_BYTES ? 2 :
                          (largest_now <= HEAP_DIAG_SERIAL_BLOCK_WARN_BYTES ? 1 : 0);
    if (level > heap_diag_serial_block_level) {
      print_block_low = true;
      heap_diag_serial_block_level = level;
    }
  }
  portEXIT_CRITICAL(&heap_diag_mux);

  if (heap_diag_serial_debug && new_low) {
    Serial.printf("[DEBUG HEAP] free-low=%lu largest=%lu label=%s task=%s core=%d ctx=%s\n",
                  (unsigned long)free_now, (unsigned long)largest_now,
                  label && *label ? label : "sample", task_name, (int)core, context);
  } else if (print_low) {
    Serial.printf("[HEAP WARN] free-low=%lu largest=%lu label=%s task=%s core=%d ctx=%s\n",
                  (unsigned long)free_now, (unsigned long)largest_now,
                  label && *label ? label : "sample", task_name, (int)core, context);
  }
  if (heap_diag_serial_debug && new_block_low && !new_low) {
    Serial.printf("[DEBUG HEAP] block-low=%lu free=%lu label=%s task=%s core=%d ctx=%s\n",
                  (unsigned long)largest_now, (unsigned long)free_now,
                  label && *label ? label : "sample", task_name, (int)core, context);
  } else if (print_block_low && !print_low && !(heap_diag_serial_debug && new_low)) {
    Serial.printf("[HEAP WARN] block-low=%lu free=%lu label=%s task=%s core=%d ctx=%s\n",
                  (unsigned long)largest_now, (unsigned long)free_now,
                  label && *label ? label : "sample", task_name, (int)core, context);
  }
}

static void heap_diag_refresh_snapshot() {
  HeapDiagSnapshot& out = heap_diag_current_snapshot;
  out.active = heap_diag_active();
  out.free_now = ESP.getFreeHeap();
  out.internal_now = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  out.largest_now = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  out.psram_now = ESP.getFreePsram();
  portENTER_CRITICAL(&heap_diag_mux);
  out.session_ms = heap_diag_started_ms ? (uint32_t)(millis() - heap_diag_started_ms) : 0;
  out.samples = heap_diag_samples;

  out.low_free = heap_diag_low_free == UINT32_MAX ? out.free_now : heap_diag_low_free;
  out.low_internal = heap_diag_low_free == UINT32_MAX ? out.internal_now : heap_diag_low_internal;
  out.low_largest = heap_diag_low_free == UINT32_MAX ? out.largest_now : heap_diag_low_largest;
  out.low_psram = heap_diag_low_free == UINT32_MAX ? out.psram_now : heap_diag_low_psram;
  out.low_at_ms = heap_diag_low_at_ms;
  out.low_core = heap_diag_low_core;
  heap_diag_copy(out.low_label, sizeof(out.low_label), heap_diag_low_label);
  heap_diag_copy(out.low_task, sizeof(out.low_task), heap_diag_low_task);
  heap_diag_copy(out.low_context, sizeof(out.low_context), heap_diag_low_context);

  out.block_free = heap_diag_block_largest == UINT32_MAX ? out.free_now : heap_diag_block_free;
  out.block_internal = heap_diag_block_largest == UINT32_MAX ? out.internal_now : heap_diag_block_internal;
  out.block_largest = heap_diag_block_largest == UINT32_MAX ? out.largest_now : heap_diag_block_largest;
  out.block_psram = heap_diag_block_largest == UINT32_MAX ? out.psram_now : heap_diag_block_psram;
  out.block_at_ms = heap_diag_block_at_ms;
  out.block_core = heap_diag_block_core;
  heap_diag_copy(out.block_label, sizeof(out.block_label), heap_diag_block_label);
  heap_diag_copy(out.block_task, sizeof(out.block_task), heap_diag_block_task);
  heap_diag_copy(out.block_context, sizeof(out.block_context), heap_diag_block_context);
  portEXIT_CRITICAL(&heap_diag_mux);
}

static TaskHandle_t cpu_idle_handle_for_core(BaseType_t core) {
#if defined(ESP_IDF_VERSION_MAJOR) && \
    ((ESP_IDF_VERSION_MAJOR > 5) || (ESP_IDF_VERSION_MAJOR == 5 && ESP_IDF_VERSION_MINOR >= 2))
  return xTaskGetIdleTaskHandleForCore(core);
#else
  return xTaskGetIdleTaskHandleForCPU(core);
#endif
}

static configRUN_TIME_COUNTER_TYPE cpu_runtime_counter_now() {
#if defined(portGET_RUN_TIME_COUNTER_VALUE)
  return (configRUN_TIME_COUNTER_TYPE)portGET_RUN_TIME_COUNTER_VALUE();
#else
  return (configRUN_TIME_COUNTER_TYPE)esp_timer_get_time();
#endif
}

static void sample_cpu_load() {
#if defined(configGENERATE_RUN_TIME_STATS) && (configGENERATE_RUN_TIME_STATS == 1)
  configRUN_TIME_COUNTER_TYPE idle_now[configNUMBER_OF_CORES] = {};
  uint64_t idle_delta_sum = 0;

  for (BaseType_t core = 0; core < configNUMBER_OF_CORES; ++core) {
    TaskHandle_t idle = cpu_idle_handle_for_core(core);
    if (!idle) return;

    TaskStatus_t info = {};
    vTaskGetInfo(idle, &info, pdFALSE, eInvalid);
    idle_now[core] = info.ulRunTimeCounter;

    if (cpu_runtime_valid) {
      idle_delta_sum +=
        (configRUN_TIME_COUNTER_TYPE)(idle_now[core] - cpu_prev_idle[core]);
    }
  }

  const configRUN_TIME_COUNTER_TYPE total_now = cpu_runtime_counter_now();
  if (cpu_runtime_valid) {
    const configRUN_TIME_COUNTER_TYPE elapsed =
      (configRUN_TIME_COUNTER_TYPE)(total_now - cpu_prev_total);
    const uint64_t capacity =
      (uint64_t)elapsed * (uint64_t)configNUMBER_OF_CORES;

    if (capacity) {
      if (idle_delta_sum > capacity) idle_delta_sum = capacity;
      cpu_load_pct = (uint8_t)(
        ((capacity - idle_delta_sum) * 100ULL + capacity / 2ULL) / capacity);
      if (cpu_load_pct > 100) cpu_load_pct = 100;
    }
  }

  cpu_prev_total = total_now;
  for (BaseType_t core = 0; core < configNUMBER_OF_CORES; ++core) {
    cpu_prev_idle[core] = idle_now[core];
  }
  cpu_runtime_valid = true;
#else
  cpu_load_pct = 0;
#endif
}
