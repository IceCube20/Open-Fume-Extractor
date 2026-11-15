#pragma once
#if defined(CONFIG_IDF_TARGET_ESP32P4)
#include "UsbStoragePolicy.h"
#include "WebUsbStorageUi.h"
static bool web_usb_chunk_ok = false;
static size_t web_usb_chunk_bytes = 0;
static bool web_usb_available() {
  return !master_update_active && !module_update_addr && !scheduler.firmwareUpdateActive() &&
         !master_update_restart_due_ms.load() && !hosted_c6().busy();
}
static void web_usb_reply(bool ok) {
  web.send(ok ? 200 : 409, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"USB busy, disconnected, or invalid selection\"}");
}
static void web_usb_command() {
  uint32_t generation = 0, index = 0;
  const String action = web.arg("action");
  const bool valid = action != "read" || (web_c6_number("generation", UINT32_MAX, generation) && web_c6_number("index", ofe_usb::MAX_FILES - 1, index));
  web_usb_reply(web_usb_available() && valid && ofe_usb::command(action, generation, index));
}
static void web_usb_begin() {
  uint32_t size = 0;
  const uint32_t token = web_usb_available() && web_c6_number("size", ofe_usb::MAX_FILE_BYTES, size)
    ? ofe_usb::receiveBegin(web.arg("name").c_str(), size) : 0;
  if (!token) { web_usb_reply(false); return; }
  web.send(200, "application/json", String("{\"ok\":true,\"token\":") + token + '}');
}
static void web_usb_upload() {
  HTTPUpload& upload = web.upload();
  uint32_t token = 0, offset = 0;
  if (upload.status == UPLOAD_FILE_START) {
    web_usb_chunk_bytes = 0;
    web_usb_chunk_ok = web_usb_available() && web_c6_number("token", UINT32_MAX, token) &&
      web_c6_number("offset", ofe_usb::MAX_FILE_BYTES, offset);
  } else if (upload.status == UPLOAD_FILE_WRITE && web_usb_chunk_ok) {
    web_usb_chunk_ok = web_usb_available() && web_c6_number("token", UINT32_MAX, token) &&
      web_c6_number("offset", ofe_usb::MAX_FILE_BYTES, offset) &&
      upload.currentSize <= 8192 - web_usb_chunk_bytes &&
      ofe_usb::receiveChunk(token, size_t(offset) + web_usb_chunk_bytes, upload.buf, upload.currentSize);
    web_usb_chunk_bytes += upload.currentSize;
  } else if (upload.status == UPLOAD_FILE_ABORTED) web_usb_chunk_ok = false;
}
static void web_usb_routes() {
  web.on("/usb/status", HTTP_GET, [](){ if (web_require_auth()) web.send(200, "application/json", ofe_usb::statusJson(developer_mode_enabled)); });
  web.on("/usb/ui.js", HTTP_GET, [](){ if (web_require_auth()) { web.sendHeader("Cache-Control", "no-store"); web.send(200, "application/javascript", OFE_USB_UI); } });
  web.on("/usb/command", HTTP_POST, [](){ if (web_require_auth()) web_usb_command(); });
  web.on("/usb/write/begin", HTTP_POST, [](){ if (web_require_auth()) web_usb_begin(); });
  web.on("/usb/write/chunk", HTTP_POST, [](){ if (web_require_auth()) { const bool ok = web_usb_chunk_ok && web_usb_chunk_bytes; web_usb_chunk_ok = false; web_usb_reply(ok); } },
    [](){ if (web_require_auth()) web_usb_upload(); });
  web.on("/usb/write/commit", HTTP_POST, [](){ if (web_require_auth()) { uint32_t t = 0; web_usb_reply(web_usb_available() && web_c6_number("token", UINT32_MAX, t) && ofe_usb::receiveCommit(t)); } });
  web.on("/usb/release", HTTP_POST, [](){ if (web_require_auth()) { uint32_t t = 0; web_usb_reply(web_c6_number("token", UINT32_MAX, t) && ofe_usb::release(t)); } });
  web.on("/usb/file", HTTP_GET, [](){ if (web_require_auth()) { uint32_t t = 0; if (!web_usb_available() || !web_c6_number("token", UINT32_MAX, t) || !ofe_usb::sendFile(web, t)) web_usb_reply(false); } });
}
static void web_append_usb_panel(String& html, const char* purpose) {
  html += F("<section class='panel' id='usb-storage' data-usb-purpose='");
  html += purpose;
  html += F("'></section><script defer src='/usb/ui.js'></script>");
}
#else
static void web_append_usb_panel(String&, const char*) {}
#endif
