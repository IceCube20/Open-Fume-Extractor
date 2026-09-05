#pragma once

// Web security headers, CSRF, captive portal guard and HTTP auth.
static const char* WEB_COLLECT_HEADERS[] = {"Host", "Origin", "Referer", "X-CSRF-Token"};
static char web_csrf_token[17] = {0};

static void web_security_headers() {
  web.sendHeader("Cache-Control", "no-store");
  web.sendHeader("X-Frame-Options", "DENY");
  web.sendHeader("X-Content-Type-Options", "nosniff");
  web.sendHeader("Referrer-Policy", "same-origin");
}

static bool web_same_origin_value(const String& value, const String& host) {
  if (!value.length()) return true;
  if (!host.length()) return true;
  String http = String("http://") + host;
  String https = String("https://") + host;
  return value == http || value == https || value.startsWith(http + "/") || value.startsWith(https + "/");
}

static bool web_post_origin_ok() {
  if (web.method() == HTTP_GET) return true;
  const String host = web.header("Host");
  const String origin = web.header("Origin");
  const String referer = web.header("Referer");
  if (origin.length()) return web_same_origin_value(origin, host);
  if (referer.length()) return web_same_origin_value(referer, host);
  return true;
}

static void web_csrf_init() {
  if (web_csrf_token[0]) return;
  static const char hex[] = "0123456789abcdef";
  uint32_t a = esp_random();
  uint32_t b = esp_random();
  for (uint8_t i = 0; i < 8; ++i) web_csrf_token[i] = hex[(a >> (28 - i * 4)) & 0x0F];
  for (uint8_t i = 0; i < 8; ++i) web_csrf_token[8 + i] = hex[(b >> (28 - i * 4)) & 0x0F];
  web_csrf_token[16] = 0;
}

static bool web_csrf_ok() {
  if (web.method() == HTTP_GET) return true;
  web_csrf_init();
  String token = web.header("X-CSRF-Token");
  if (!token.length()) token = web.arg("csrf");
  return token.length() == 16 && token == web_csrf_token;
}

static bool web_block_captive_non_config() {
  if (!captive_active) return false;
  if (web.method() == HTTP_GET) web_redirect_config();
  else web.send(403, "text/plain; charset=utf-8", "setup mode: only network configuration is available");
  return true;
}

static bool web_password_change_blocked() {
  if (!web_password_change_required) return false;
  const String path = web.uri();
  if (path == "/config" || path == "/config/save" || path == "/config/password") return false;
  if (web.method() == HTTP_GET) {
    web.sendHeader("Location", "/config", true);
    web.send(302, "text/plain; charset=utf-8", "");
    return true;
  }
  web.send(428, "text/plain; charset=utf-8", "Set a new web password on /config before continuing");
  return true;
}

static bool web_require_auth() {
  web_security_headers();
  if (web_block_captive_non_config()) return false;
  if (!web_post_origin_ok()) {
    web.send(403, "text/plain; charset=utf-8", "forbidden origin");
    return false;
  }
  if (!web_csrf_ok()) {
    web.send(403, "text/plain; charset=utf-8", "bad csrf token");
    return false;
  }
#if WEB_AUTH_ENABLE
  if (!web.authenticate(web_auth_user, web_auth_password)) {
    web.requestAuthentication(BASIC_AUTH, "Open Fume Extractor");
    return false;
  }
#else
  // Authentication is disabled by the build configuration.
#endif
  if (web_password_change_blocked()) return false;
  return true;
}
static bool web_require_config_auth() {
  web_security_headers();
  if (!web_post_origin_ok()) {
    web.send(403, "text/plain; charset=utf-8", "forbidden origin");
    return false;
  }
  if (!web_csrf_ok()) {
    web.send(403, "text/plain; charset=utf-8", "bad csrf token");
    return false;
  }
  if (web_password_change_blocked()) return false;
  if (captive_active) return true;
  return web_require_auth();
}
