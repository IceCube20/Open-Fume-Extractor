#pragma once

#include <Arduino.h>
#include <freertos/semphr.h>
#include "bus/Rs485PeripheralBus.h"
#include "ModuleRegistry.h"
#include "ExtractorLogic.h"

#ifndef JBC_MODULE_ADDR
#define JBC_MODULE_ADDR 0x10
#endif

#ifndef OUTPUT_MODULE_ADDR
#define OUTPUT_MODULE_ADDR 0x20
#endif

#ifndef FAST_POLL_MIN_INTERVAL_MS
#define FAST_POLL_MIN_INTERVAL_MS 5
#endif

#ifndef JBC_FAST_POLL_PER_MODULE_MS
#define JBC_FAST_POLL_PER_MODULE_MS 100
#endif

#ifndef FAST_POLL_TIMEOUT_MS
#define FAST_POLL_TIMEOUT_MS 20
#endif

#ifndef JBC_BUS_FAST_POLL_TIMEOUT_MS
#define JBC_BUS_FAST_POLL_TIMEOUT_MS 35
#endif

#ifndef JBC_USB_FAST_POLL_TIMEOUT_MS
#define JBC_USB_FAST_POLL_TIMEOUT_MS 35
#endif

#ifndef HOTPLUG_DISCOVERY_INTERVAL_MS
#define HOTPLUG_DISCOVERY_INTERVAL_MS 120000UL
#endif

#ifndef HOTPLUG_DISCOVERY_WINDOW_MS
#define HOTPLUG_DISCOVERY_WINDOW_MS 450UL
#endif

#ifndef OFFLINE_REPROBE_INTERVAL_MS
#define OFFLINE_REPROBE_INTERVAL_MS 1500UL
#endif

#ifndef JBC_DEFAULT_PROBE_INTERVAL_MS
#define JBC_DEFAULT_PROBE_INTERVAL_MS 500UL
#endif

#ifndef JBC_DEFAULT_STATE_PROBE_INTERVAL_MS
#define JBC_DEFAULT_STATE_PROBE_INTERVAL_MS 1500UL
#endif

class MasterScheduler {
public:
  static const uint8_t MAX_INPUT_RULES = 16;
  static const uint16_t TRACE_EVENT_CAPACITY = 4096;
  static const uint8_t TRACE_DATA_PREVIEW = 48;

  enum TraceDirection : uint8_t {
    TRACE_TX = 0,
    TRACE_RX = 1,
    TRACE_TIMEOUT = 2,
    TRACE_INFO = 3,
    TRACE_LOCAL_TX = 4,
    TRACE_LOCAL_RX = 5,
  };

  struct TraceEvent {
    uint32_t ms = 0;
    uint32_t seq = 0;       // monotonically increasing trace-event number
    uint8_t frame_seq = 0xFF; // OFE protocol SEQ for request/response pairing
    uint8_t addr = 0;
    uint8_t direction = TRACE_INFO;
    uint8_t cmd = 0;
    uint8_t status = 0xFF;
    uint8_t len = 0;
    uint8_t data_len = 0;
    uint8_t data[TRACE_DATA_PREVIEW] = {0};
    uint16_t latency_ms = 0;
    char text[96] = {0};
  };

  struct BusModuleDiag {
    uint32_t requests = 0;
    uint32_t responses = 0;
    uint32_t timeouts = 0;
    uint32_t bad_seq = 0;
    uint32_t bad_cmd = 0;
    uint32_t tx_frames = 0;
    uint32_t rx_frames = 0;
    uint64_t tx_wire_bytes = 0;
    uint64_t rx_wire_bytes = 0;
    uint64_t latency_sum_ms = 0;
    uint16_t latency_max_ms = 0;
    uint16_t latency_last_ms = 0;
    uint32_t last_activity_ms = 0;
  };

  struct TraceStats {
    bool active = false;
    uint8_t target_addr = 0;
    uint32_t started_ms = 0;
    uint32_t requests = 0;
    uint32_t responses = 0;
    uint32_t timeouts = 0;
    uint32_t dropped_events = 0;
    uint32_t bad_seq = 0;
    uint32_t bad_cmd = 0;
    uint32_t avg_latency_ms = 0;
    uint16_t max_latency_ms = 0;
    uint16_t stored_events = 0;
  };

  enum InputSourceType : uint8_t {
    INPUT_SRC_NONE = 0,
    INPUT_SRC_JBC_WORK = 1,
    INPUT_SRC_IO_INPUT = 2,
    INPUT_SRC_UNIVERSAL_ENTITY = 3,
  };

  enum InputTargetType : uint8_t {
    INPUT_TGT_NONE = 0,
    INPUT_TGT_EXTRACTOR = 1,
    INPUT_TGT_IO_OUTPUT = 2,
    INPUT_TGT_UNIVERSAL_ENTITY = 3,
    INPUT_TGT_EXTRACTOR_ACTION = 4,
  };

  enum ExtractorAction : uint8_t {
    EXTRACTOR_ACTION_NONE = 0,
    EXTRACTOR_ACTION_LEVEL_NEXT = 1,
    EXTRACTOR_ACTION_LEVEL_PREVIOUS = 2,
    EXTRACTOR_ACTION_LEVEL_HIGH = 3,
    EXTRACTOR_ACTION_LEVEL_MEDIUM = 4,
    EXTRACTOR_ACTION_LEVEL_LOW = 5,
    EXTRACTOR_ACTION_LEVEL_CUSTOM = 6,
    EXTRACTOR_ACTION_POWER_PLUS_1 = 7,
    EXTRACTOR_ACTION_POWER_MINUS_1 = 8,
    EXTRACTOR_ACTION_POWER_PLUS_10 = 9,
    EXTRACTOR_ACTION_POWER_MINUS_10 = 10,
    EXTRACTOR_ACTION_LAST = EXTRACTOR_ACTION_POWER_MINUS_10,
  };

  struct InputActionRule {
    bool enabled = false;
    uint8_t source_type = INPUT_SRC_NONE;
    uint8_t source_addr = 0;
    uint8_t source_bit = 0;
    uint8_t target_type = INPUT_TGT_NONE;
    uint8_t target_addr = 0;
    uint8_t target_bit = 0;
    bool last_active = false;
    bool edge_armed = false;
  };

  MasterScheduler(jbc_rs485::Link& link, ModuleRegistry& registry, ExtractorLogic& extractor)
    : link_(link), registry_(registry), extractor_(extractor) {}

  void begin();
  void setLedConfig(bool enabled, uint8_t brightness_pct);
  uint32_t requestBadCmdCount() const { return request_bad_cmd_total_; }
  uint32_t requestBadSeqCount() const { return request_bad_seq_total_; }
  uint32_t requestCount() const { return request_total_; }
  uint64_t requestTxPayloadBytes() const { return request_tx_payload_bytes_; }
  uint64_t responseRxPayloadBytes() const { return response_rx_payload_bytes_; }
  uint32_t compactIoPollCount() const { return compact_io_poll_total_; }
  uint32_t fullIoPollCount() const { return full_io_poll_total_; }
  uint64_t ofeTxWireBytes() const { return ofe_tx_wire_bytes_; }
  uint64_t ofeRxWireBytes() const { return ofe_rx_wire_bytes_; }
  uint32_t ofeTxFrameCount() const { return ofe_tx_frames_; }
  uint32_t ofeRxFrameCount() const { return ofe_rx_frames_; }
  const jbc_rs485::BusStats& ofeParserStats() const { return link_.stats(); }
  bool busModuleDiag(uint8_t addr, BusModuleDiag& out) const;
  void tick();
  void setServiceCallback(void (*callback)()) { service_callback_ = callback; }
  void setSerialDebugLog(bool enabled) { serial_debug_log_ = enabled; }
  bool serialDebugLog() const { return serial_debug_log_; }
  void scanKnownModules();
  void requestScanKnownModules(bool with_auto_address = false, bool prune_missing = false);
  bool scanJobActive() const { return scan_job_active_; }
  bool consumeScanJobFinished() { const bool done = scan_job_finished_; scan_job_finished_ = false; return done; }
  void probeModule(uint8_t addr);
  uint8_t jbcAddr() const { return active_jbc_addr_; }
  uint8_t outputAddr() const { return active_output_addr_; }
  uint8_t autoOutputCandidateAddr() const;
  uint8_t preferredOutputAddr() const { return preferred_output_addr_; }
  bool jbcInputEnabled() const { return main_input_source_type_ == INPUT_SRC_JBC_WORK; }
  uint8_t mainInputSourceType() const { return main_input_source_type_; }
  uint8_t mainInputSourceAddr() const { return main_input_source_addr_; }
  uint8_t mainInputSourceBit() const { return main_input_source_bit_; }
  bool mainInputSourceAvailable() const;
  void setLogicExternalInput(bool active);
  bool logicExternalInput() const { return logic_external_input_; }
  const InputActionRule& inputRule(uint8_t index) const { return input_rules_[index]; }
  void setPreferredOutputAddr(uint8_t addr);
  void setJbcInputEnabled(bool enabled);
  bool setMainInputSource(uint8_t source_type, uint8_t source_addr, uint8_t source_bit, bool persist = true);
  bool setInputRule(uint8_t index, const InputActionRule& rule);
  bool setIoInputRoute(uint8_t addr, uint8_t bit, bool enabled);
  bool moduleFwBegin(uint8_t addr, uint32_t size);
  bool moduleFwChunk(uint8_t addr, uint32_t offset, const uint8_t* data, uint8_t len);
  bool moduleFwEnd(uint8_t addr);
  bool moduleFwAbort(uint8_t addr);
  bool moduleReboot(uint8_t addr);
  const char* lastModuleFwError() const { return last_fw_error_; }
  uint8_t lastModuleFwChunkAttempts() const { return last_fw_chunk_attempts_; }
  uint32_t moduleFwChunkRetryCount() const { return fw_chunk_retry_count_; }
  bool firmwareUpdateActive() const { return module_fw_active_ || display_update_active_; }
  bool moduleFirmwareUpdateActive() const { return module_fw_active_; }
  uint8_t moduleFirmwareUpdateTarget() const { return module_fw_target_; }
  void notifyDisplayUpdate(bool active, uint8_t target_addr, uint8_t progress, uint32_t speed_bps = 0);
  void setMasterTelemetry(uint8_t cpu_load_pct, uint16_t loop_max_ms) {
    master_cpu_load_pct_ = cpu_load_pct;
    master_loop_max_ms_ = loop_max_ms;
  }
  void setMasterInfo(uint8_t major, uint8_t minor, uint8_t patch, const char* name, const char* suffix = "") {
    master_fw_major_ = major;
    master_fw_minor_ = minor;
    master_fw_patch_ = patch;
    if (!name) name = "Master";
    if (!suffix) suffix = "";
    strncpy(master_name_, name, sizeof(master_name_) - 1);
    master_name_[sizeof(master_name_) - 1] = 0;
    strncpy(master_fw_suffix_, suffix, sizeof(master_fw_suffix_) - 1);
    master_fw_suffix_[sizeof(master_fw_suffix_) - 1] = 0;
  }
  void setAfterrunPowerProfile(bool enabled, uint16_t power, bool persist = true);
  bool afterrunPowerProfileEnabled() const { return extractor_.afterrunPowerProfileEnabled(); }
  uint16_t afterrunPower() const { return extractor_.afterrunPower(); }
  void setControlSettings(uint8_t suction, uint16_t select_flow, uint16_t delay_work, uint16_t delay_stand, bool stand_intakes, bool continuous, bool persist = true);
  void queueExtractorAction(uint8_t action);
  void persistControlSettingsNow();
  const JbcModuleState& controlSettings() const { return desired_jbc_settings_; }
  uint16_t systemJbcError() const;
  uint16_t systemJbcFilterLife() const;
  uint16_t systemJbcFilterSaturation() const;
  uint16_t activeOutputMinSelectFlow() const { return minSelectFlowForActiveOutput(); }
  bool setJbcSettings(uint8_t addr, uint8_t suction, uint16_t select_flow, uint16_t delay_work, uint16_t delay_stand, bool stand_intakes, bool continuous);
  bool setIoOutput(uint8_t addr, uint16_t mask, uint16_t value);
  bool setIoAlias(uint8_t addr, uint8_t channel, const char* alias);
  bool setModulePower(uint8_t addr, uint16_t power);
  bool setModuleOutput(uint8_t addr, bool enabled, uint16_t power);
  bool setWellerSpeed(uint8_t addr, uint8_t percent);
  bool resetWellerFilter(uint8_t addr);
  bool setWellerFilterRuntime(uint8_t addr, uint16_t minutes);
  bool calibrateFanIoProFilter(uint8_t addr, uint8_t action, uint16_t warn_raw = 0, uint16_t full_raw = 0);
  bool setDisplaySettings(uint8_t addr, uint8_t brightness, uint8_t language, uint8_t theme, uint8_t screensaver_min = 0xFF);
  bool setUniversalProfile(uint8_t addr, const char* profile, const char* station, uint32_t baud, const char* frame, const char* protocol, const char* checksum = "NONE", const char* line_end = "CR", const char* profile_text = nullptr);
  bool uploadUniversalProfileText(uint8_t addr, const char* profile_text);
  bool readUniversalProfileText(uint8_t addr, char* out, size_t out_len, uint32_t* out_crc = nullptr, bool* out_truncated = nullptr);
  bool setUniversalEntity(uint8_t addr, uint8_t entity_id, const uint8_t* data, uint8_t len);
  bool setJbcUsbStationName(uint8_t addr, const char* name);
  bool setJbcUsbConfig(uint8_t addr, const uint8_t* data, uint8_t len);
  bool refreshUniversalDescriptor(uint8_t addr, bool force = false);
  bool readUniversalEntities(uint8_t addr);
  bool setModuleLabel(uint8_t addr, const char* label);
  bool setModuleAddress(uint8_t old_addr, uint8_t new_addr);
  uint8_t autoAddressModules(bool preserve_remembered = true);
  void traceStart(uint8_t target_addr, bool local_trace = false);
  void traceStop();
  void traceTouch();
  void traceClear();
  bool traceUsesPsram() const { return trace_events_psram_; }
  TraceStats traceStats() const;
  uint16_t traceEventCount() const;
  uint32_t traceOldestSeq() const;
  uint32_t traceNewestSeq() const;
  bool traceEventAt(uint16_t index, TraceEvent& out) const;

private:
  bool request(uint8_t dst, uint8_t cmd, const uint8_t* payload, uint8_t len, jbc_rs485::Frame& resp, uint32_t timeout_ms, bool physical = false);
  void serviceWhileWaiting();
  void serviceDelay(uint32_t delay_ms);
  void traceLog(uint8_t addr, TraceDirection direction, uint8_t cmd, uint8_t status, const uint8_t* data, uint8_t len, uint16_t latency_ms, const char* text = nullptr, uint8_t frame_seq = 0xFF);
  bool traceMatches(uint8_t addr) const;
  static int16_t busDiagIndex(uint8_t addr);
  static uint16_t ofeWireBytesApprox(const jbc_rs485::Frame& frame);
  void busDiagRecordTx(const jbc_rs485::Frame& frame);
  void busDiagRecordRx(const jbc_rs485::Frame& frame);
  bool traceStorageReady();
  void tracePollLocal();
  void traceSetLocalTarget(bool enabled, bool clear);
  bool traceLocalControl(uint8_t addr, bool enabled, bool clear);
  bool traceLocalRead(uint8_t addr);
  void noticeDiscoveryResponse(const jbc_rs485::Frame& resp);
  void drainUnsolicitedFrames();
  void broadcastLedSync(uint32_t now);
  bool readInfo(uint8_t addr);
  bool readCaps(uint8_t addr);
  bool fastPollJbc(uint8_t addr);
  bool readJbcState(uint8_t addr);
  bool readJbcUsbState(uint8_t addr);
  bool readOutputStatus(uint8_t addr);
  bool readIoStatus(uint8_t addr, bool include_aliases = false);
  bool readTelemetry(uint8_t addr);
  bool pollNextJbc();
  uint8_t onlineJbcModuleCount() const;
  bool readNextJbcState();
  bool pollNextWeller();
  bool pollNextUniversal();
  bool pollNextTelemetry();
  bool pollNextIoStatus();
  bool pollNextOutputStatus();
  void handleDisplayEvent(uint8_t type, int16_t value, uint8_t display_view_arg = 0);
  bool applyJbcSettingsToOnlineModules(const JbcModuleState& state);

  enum : uint8_t { DISPLAY_ALARM_MAX_ITEMS = 6 };
  struct DisplayAlarmItem {
    uint8_t addr = 0;
    uint8_t type = 0;
    uint8_t code = 0;
    uint16_t value = 0;
  };
  struct DisplayAlarmSnapshot {
    uint8_t alarm_count = 0;
    uint8_t item_count = 0;
    uint8_t critical_mask = 0;
    uint16_t jbc_error = 0;
    DisplayAlarmItem items[DISPLAY_ALARM_MAX_ITEMS];
  };
  void buildDisplayAlarmSnapshot(bool jbc_present, DisplayAlarmSnapshot& snapshot) const;

  void pushDisplayStatus();
  bool sendDisplayStatus(uint8_t addr);
  bool sendDisplayAlarms(uint8_t display_addr, const DisplayAlarmSnapshot& snapshot);
  bool sendDisplayModuleList(uint8_t display_addr, uint8_t start_index);
  bool sendDisplayModuleDetail(uint8_t display_addr, uint8_t target_addr);
  bool sendDisplayUniversalEntityPage(uint8_t display_addr, const ModuleRecord& rec, uint8_t start_entity);
  bool sendDisplayUpdate(uint8_t addr, bool active, uint8_t target_addr, uint8_t progress,
                         bool wait_for_ack = true);
  void pollHotplugDiscovery();
  void pollOfflineModules();
  void pollScanJob();
  void updateJbcAggregate();
  uint16_t minSelectFlowForActiveOutput() const;
  void selectFlowBoundsForActiveOutput(uint16_t& min_flow, uint16_t& max_flow, uint16_t& step_flow) const;
  bool moduleProvidesExtractorOutput(const ModuleRecord& rec) const;
  bool universalFindMainOutputEntities(const ModuleRecord& rec, uint8_t& enable_id, uint8_t& power_id) const;
  bool universalSetMainOutput(ModuleRecord& rec, bool enabled, uint16_t power);
  bool universalEntityBoolActive(const ModuleRecord& rec, uint8_t entity_id) const;
  void updateUniversalOutputStateFromEntities(ModuleRecord& rec);
  void syncSystemJbcError(bool force = false);
  void updateInputRouting();
  bool inputRuleSourceActive(const InputActionRule& rule) const;
  bool mainInputSourceActive() const;
  void applyInputRuleTarget(InputActionRule& rule, bool active);
  void processPendingExtractorActions();
  void adoptJbcSettings(const JbcModuleState& state);
  bool jbcSettingsDiffer(const JbcModuleState& a, const JbcModuleState& b) const;
  bool recordSettingsDiffer(const ModuleRecord& rec, const JbcModuleState& state) const;
  void rememberJbcSettings(ModuleRecord& rec, const JbcModuleState& state);
  void copyDesiredJbcSettings(JbcModuleState& state) const;
  void syncOtherJbcSettings(uint8_t source_addr);
  void pushOutputIfNeeded();
  void pollOneBackgroundJob(uint32_t now);
  bool sendOutputEnable(uint8_t addr, bool enabled);
  bool sendOutputPower(uint8_t addr, uint16_t power);
  void scanAddress(uint8_t addr);
  void selectRoles();
  void scheduleControlSettingsPersist();
  void flushControlSettingsPersist(bool force = false);

  jbc_rs485::Link& link_;
  ModuleRegistry& registry_;
  ExtractorLogic& extractor_;
  SemaphoreHandle_t bus_mutex_ = nullptr;

  uint8_t seq_ = 1;
  uint32_t last_led_sync_ms_ = 0;
  bool led_enabled_ = true;
  uint8_t led_brightness_pct_ = 20;
  bool serial_debug_log_ = false;
  uint32_t last_fast_poll_ms_ = 0;
  uint32_t last_output_status_ms_ = 0;
  uint32_t last_community_output_reassert_ms_ = 0;
  uint32_t last_weller_poll_ms_ = 0;
  uint32_t last_universal_poll_ms_ = 0;
  uint32_t last_module_telemetry_ms_ = 0;
  uint32_t last_jbc_state_ms_ = 0;
  uint32_t last_display_status_ms_ = 0;
  uint32_t last_hotplug_discovery_ms_ = 0;
  uint32_t hotplug_discovery_window_until_ms_ = 0;
  uint32_t last_offline_reprobe_ms_ = 0;
  uint32_t pending_hotplug_scan_ms_ = 0;
  uint32_t last_default_jbc_probe_ms_ = 0;
  uint32_t last_default_jbc_state_probe_ms_ = 0;
  uint16_t last_jbc_state_event_seq_ = 0;
  uint16_t last_system_jbc_error_ = 0xFFFF;
  uint16_t last_system_jbc_filter_life_ = 0xFFFF;
  uint16_t last_system_jbc_filter_sat_ = 0xFFFF;
  uint8_t last_system_jbc_output_enabled_ = 0xFF;
  uint32_t last_system_jbc_push_ms_ = 0;
  uint32_t control_persist_due_ms_ = 0;
  uint32_t control_persist_first_dirty_ms_ = 0;
  bool control_persist_dirty_ = false;
  uint16_t pending_extractor_actions_ = 0;
  uint8_t next_jbc_poll_index_ = 0;
  uint8_t next_jbc_state_index_ = 0;
  uint8_t pending_jbc_state_addr_ = 0;
  uint8_t next_weller_poll_index_ = 0;
  uint8_t next_universal_poll_index_ = 0;
  uint8_t universal_entity_repair_cursor_ = 20;
  uint8_t next_telemetry_index_ = 0;
  uint8_t next_io_poll_index_ = 0;
  uint8_t next_output_status_index_ = 0;
  uint8_t next_display_index_ = 0;
  uint8_t next_background_poll_slot_ = 0;
  uint8_t next_offline_probe_index_ = 0;
  uint8_t active_jbc_addr_ = JBC_MODULE_ADDR;
  uint8_t active_output_addr_ = OUTPUT_MODULE_ADDR;
  uint8_t preferred_output_addr_ = 0;
  uint8_t main_input_source_type_ = INPUT_SRC_JBC_WORK;
  uint8_t main_input_source_addr_ = 0;
  uint8_t main_input_source_bit_ = 0;
  bool applying_input_rules_ = false;
  bool logic_external_input_ = false;
  bool pending_hotplug_scan_ = false;
  bool pending_hotplug_full_scan_ = false;
  uint8_t pending_hotplug_addr_ = jbc_rs485::ADDR_INVALID;
  bool scan_job_active_ = false;
  bool scan_job_auto_address_ = false;
  bool scan_job_auto_done_ = true;
  bool scan_job_prune_missing_ = false;
  bool scan_job_finished_ = false;
  uint8_t scan_job_next_addr_ = 0x10;
  uint32_t last_scan_job_step_ms_ = 0;
  bool display_update_active_ = false;
  uint8_t display_update_target_ = 0;
  uint8_t display_update_progress_ = 0;
  uint32_t display_update_speed_bps_ = 0;
  uint8_t master_cpu_load_pct_ = 0;
  uint16_t master_loop_max_ms_ = 0;
  uint8_t master_fw_major_ = 0;
  uint8_t master_fw_minor_ = 0;
  uint8_t master_fw_patch_ = 0;
  char master_fw_suffix_[8] = {0};
  char master_name_[24] = "Master";
  bool module_fw_active_ = false;
  uint8_t module_fw_target_ = jbc_rs485::ADDR_INVALID;
  char last_fw_error_[40] = {0};
  uint8_t last_fw_chunk_attempts_ = 0;
  uint32_t fw_chunk_retry_count_ = 0;
  bool have_jbc_settings_ = false;
  JbcModuleState desired_jbc_settings_;
  InputActionRule input_rules_[MAX_INPUT_RULES];
  TraceStats trace_stats_;
  TraceEvent* trace_events_ = nullptr;
  bool trace_events_psram_ = false;
  uint16_t trace_head_ = 0;
  uint16_t trace_count_ = 0;
  uint32_t trace_seq_ = 0;
  uint32_t last_trace_client_ms_ = 0;
  uint32_t last_trace_local_poll_ms_ = 0;
  uint32_t last_service_callback_ms_ = 0;
  uint32_t request_bad_cmd_total_ = 0;
  uint32_t request_bad_seq_total_ = 0;
  uint32_t request_total_ = 0;
  uint64_t request_tx_payload_bytes_ = 0;
  uint64_t response_rx_payload_bytes_ = 0;
  uint32_t compact_io_poll_total_ = 0;
  uint32_t full_io_poll_total_ = 0;

  // Passive OFE-bus diagnostics. 0x10..0x6F map to 96 compact counters.
  // These counters never influence scheduling or timeout behavior.
  static const uint8_t BUS_DIAG_ADDR_COUNT = 0x60;
  BusModuleDiag bus_module_diag_[BUS_DIAG_ADDR_COUNT];
  uint64_t ofe_tx_wire_bytes_ = 0;
  uint64_t ofe_rx_wire_bytes_ = 0;
  uint32_t ofe_tx_frames_ = 0;
  uint32_t ofe_rx_frames_ = 0;

  void (*service_callback_)() = nullptr;
  uint8_t trace_local_addr_ = 0;
  bool trace_local_mode_ = false;
  bool trace_local_enabled_ = false;
  bool trace_local_desired_ = false;
  bool trace_local_clear_pending_ = false;
};







