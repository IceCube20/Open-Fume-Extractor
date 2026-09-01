#pragma once
#if WEB_ENABLE
static void web_handle_display_link() {
  if (!web_require_auth()) return;
  String serial=web.arg("serial"); serial.trim(); serial.toUpperCase();
  char code[33]={};
  bool requested=web.method()==HTTP_POST, valid=false;
  if (requested) {
    uint8_t bytes[8];
    if (ofe_wifi::unhex(serial.c_str(),bytes,sizeof(bytes)) && (bytes[0]>>4)==4) {
      uint64_t uid=0; for (uint8_t b:bytes) uid=(uid<<8)|b;
      valid=master_display_wifi.keyText(uid,code);
    }
  }
  String html;
  web_shell_begin(html,web_text("Display-Verbindung","Display connection"),web_text("Netzwerk","Network"),"config");
  html+=F("<section class='panel'><h2>");
  html+=web_text("Display koppeln","Pair display");
  html+=F("</h2><form method='post' action='/display-link'><label for='display_serial'>");
  html+=web_text("Display-Seriennummer","Display serial number");
  html+=F("</label><input id='display_serial' name='serial' required minlength='16' maxlength='16' pattern='[4][0-9A-Fa-f]{15}' value='");
  html+=html_escape(serial); html+=F("'><div class='actions' style='margin-top:14px'><button type='submit'>");
  html+=web_text("Kopplungscode anzeigen","Show pairing code"); html+=F("</button></div></form>");
  if (requested) {
    if (valid) {
      html+=F("<dl><dt>Master</dt><dd>"); html+=html_escape(WiFi.localIP().toString());
      html+=F("</dd><dt>"); html+=web_text("Kopplungscode","Pairing code");
      html+=F("</dt><dd><code>"); html+=code; html+=F("</code></dd></dl>");
    } else { html+=F("<p>"); html+=web_text("Ungültige Seriennummer oder Kopplungsspeicher nicht verfügbar.","Invalid serial number or pairing storage unavailable."); html+=F("</p>"); }
  }
  html+=F("</section><section class='panel'><h2>Displays</h2><table><thead><tr><th>");
  html+=web_text("Seriennummer","Serial number"); html+=F("</th><th>"); html+=web_text("Verbindung","Connection");
  html+=F("</th></tr></thead><tbody>");
  for (uint8_t i=0;i<registry.count();++i) {
    const auto& m=registry.at(i); if (m.type!=MODULE_DISPLAY) continue;
    char uid[17]; snprintf(uid,sizeof(uid),"%016llX",(unsigned long long)m.uid);
    html+=F("<tr><td>"); html+=uid; html+=F("</td><td>");
    html+=!m.online ? web_text("Offline","Offline") : master_display_wifi.active(m.addr) ? web_text("WLAN","WiFi") : "RS485";
    html+=F("</td></tr>");
  }
  html+=F("</tbody></table></section>");
  web_shell_end(html);
  web.send(200,"text/html; charset=utf-8",html);
}
#endif
