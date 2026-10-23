#pragma once
// Called on the existing bus/cache paths; all LVGL work remains deferred.
static bool append_display_value_request(Frame& response, uint8_t addr, uint8_t priority = 0) {
  if (!display_values_supported) return false;
  auto* cache = universal_cache_find(addr, false);
  if (!cache || cache->values_language != display_language ||
      (cache->type != MODULE_UNIVERSAL_RS232 && cache->type != MODULE_MODBUS_RTU) ||
      cache->universal_entity_total > ofe_display_values::MAX_ENTITIES ||
      cache->universal_entity_count != cache->universal_entity_total) return false;
  const uint32_t now = millis();
  if (!cache->values_refresh_ms || uint32_t(now - cache->values_refresh_ms) >= 10000U) {
    const unsigned n = cache->universal_entity_count;
    cache->values_refresh_mask = n == 32 ? UINT32_MAX : (uint32_t(1) << n) - 1;
    cache->values_refresh_ms = now;
  }
  ofe_display_values::Request request;
  request.valid = true; request.addr = addr; request.crc = cache->universal_descriptor_crc;
  request.count = cache->universal_entity_count;
  request.cursor = request.count ? cache->request_cursor % request.count : 0;
  request.priority = priority;
  for (uint8_t i = 0; i < request.count; ++i) {
    const auto& entity = cache->universal_entities[i];
    if (!entity.valid || entity.id < 20) return false;
    if (cache->values_refresh_mask & (uint32_t(1) << i)) continue;
    ofe_display_values::Value value;
    value.id = entity.id; value.type = entity.type; value.number = entity.value;
    memcpy(value.text, entity.text, sizeof(value.text));
    request.known[i] = ofe_display_values::hash(value);
  }
  const size_t bytes = ofe_display_values::encodeRequest(response.payload + response.len,
    MAX_PAYLOAD - response.len, request);
  response.len = uint8_t(response.len + bytes);
  return bytes != 0;
}
static void handle_display_value_page(const Frame& req) {
  ofe_display_values::Page page;
  if (!ofe_display_values::decodePage(req.payload, req.len, page)) {
    send_status_response(req, STATUS_BAD_LEN); return;
  }
  auto* cache = universal_cache_find(page.addr, false);
  if (!cache || cache->type != page.type || cache->universal_descriptor_crc != page.crc) {
    send_status_response(req, STATUS_BAD_VALUE); return;
  }
  // Validate every ID/type before acknowledging or changing any cached value.
  size_t pos = ofe_display_values::HEADER;
  for (uint8_t row = 0; row < page.count; ++row) {
    ofe_display_values::Value value;
    pos = ofe_display_values::read(page, pos, value);
    bool found = false;
    for (uint8_t i = 0; i < cache->universal_entity_count && i < DISPLAY_UNIVERSAL_ENTITY_CACHE_MAX; ++i)
      if (cache->universal_entities[i].valid && cache->universal_entities[i].id == value.id &&
          cache->universal_entities[i].type == value.type) { found = true; break; }
    if (!found) { send_status_response(req, STATUS_BAD_VALUE); return; }
  }
  send_status_response(req, STATUS_OK);
  bool changed = false, selected_changed = false;
  const bool selected = selected_module.valid && selected_module.addr == page.addr && selected_module.type == page.type;
  pos = ofe_display_values::HEADER;
  for (uint8_t row = 0; row < page.count; ++row) {
    ofe_display_values::Value value;
    pos = ofe_display_values::read(page, pos, value);
    for (uint8_t i = 0; i < cache->universal_entity_count && i < DISPLAY_UNIVERSAL_ENTITY_CACHE_MAX; ++i) {
      auto& entity = cache->universal_entities[i];
      if (!entity.valid || entity.id != value.id) continue;
      changed |= entity.value != value.number || strcmp(entity.text, value.text) != 0;
      entity.value = value.number; memcpy(entity.text, value.text, sizeof(entity.text));
      cache->values_refresh_mask &= ~(uint32_t(1) << i);
      universal_pending_observe(page.addr, entity);
      break;
    }
    if (selected) {
      auto* entity = selected_universal_entity_by_id(value.id);
      if (entity && entity->type == value.type) {
        selected_changed |= entity->value != value.number || strcmp(entity->text, value.text) != 0;
        entity->value = value.number; memcpy(entity->text, value.text, sizeof(entity->text));
        universal_pending_observe(page.addr, *entity);
      }
    }
  }
  cache->last_ms = millis(); cache->request_cursor = page.cursor; cache->flags = page.flags;
  if (selected) selected_universal_request_cursor = page.cursor;
  if (changed || selected_changed) {
    if (ui_needs_defer()) ui_defer_flags(UI_DEFER_APP_VALUES | (selected ? UI_DEFER_MODULE_DETAIL : 0));
    else {
      if (selected) lv_update_module_detail();
      lv_update_app_values();
    }
  }
}
