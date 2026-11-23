#pragma once

static void web_append_diagnostic_exports(String& html) {
  html += F("<div class='diag-export-actions'><div class='diag-export-group' role='group' aria-label='CSV'><span class='diag-export-label'>CSV</span><button type='button' class='secondary' onclick='diagDownload(&quot;csv&quot;)'><svg viewBox='0 0 24 24' aria-hidden='true'><path d='M5,20H19V18H5M19,9H15V3H9V9H5L12,16L19,9Z'/></svg>");
  html += web_text("Herunterladen", "Download");
  html += F("</button></div><div class='diag-export-group' role='group' aria-label='JSON'><span class='diag-export-label'>JSON</span><button type='button' class='secondary' onclick='diagDownload(&quot;json&quot;)'><svg viewBox='0 0 24 24' aria-hidden='true'><path d='M5,20H19V18H5M19,9H15V3H9V9H5L12,16L19,9Z'/></svg>");
  html += web_text("Herunterladen", "Download");
  html += F("</button></div><div class='diag-export-group'><span class='diag-export-label'>");
  html += web_text("Diagnose-Sicherung (JSON)", "Diagnostic snapshot (JSON)");
  html += F("</span><a class='btn secondary' data-diag-snapshot href='/diagnostics/state' download='open-fume-extractor-diagnostics.json'><svg viewBox='0 0 24 24' aria-hidden='true'><path d='M5,20H19V18H5M19,9H15V3H9V9H5L12,16L19,9Z'/></svg>");
  html += web_text("Herunterladen", "Download");
  html += F("</a></div></div>");
}
