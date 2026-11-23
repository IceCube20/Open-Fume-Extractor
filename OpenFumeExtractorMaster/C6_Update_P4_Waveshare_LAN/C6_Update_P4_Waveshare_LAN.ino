/*
  ESP32-C6 Firmware-Update ueber ESP32-P4 und Ethernet/SDIO
  Board: Waveshare ESP32-P4-WIFI6-POE-ETH (P4 rev 3.2)
  Arduino-ESP32: 3.3.12 (passende ESP-Hosted-C6-Version: 2.12.13)

  Achtung: Dieser Sketch wird auf den P4 geflasht, nicht auf den C6.
  Er ersetzt die laufende P4-Anwendung temporaer. Diese danach neu flashen.
  Der C6 wird nur bei Eingabe von UPDATE im Serial Monitor aktualisiert.

  Benoetigt keine WLAN-Zugangsdaten: Die Firmware wird ueber Ethernet
  heruntergeladen und per SDIO an den C6 uebertragen.
  Verwendet die offizielle Arduino ESP_HostedOTA-Bibliothek.
*/

#include <Arduino.h>
#include <WiFi.h>
#include <ETH.h>
#include <Network.h>
#include <esp32-hal-hosted.h>
#include <ESP_HostedOTA.h>
#include <esp_arduino_version.h>

#if !defined(CONFIG_IDF_TARGET_ESP32P4)
#error "Dieser Sketch ist fuer den ESP32-P4 bestimmt."
#endif
#if ESP_ARDUINO_VERSION != ESP_ARDUINO_VERSION_VAL(3, 3, 12)
#error "Bitte Arduino-ESP32 Version 3.3.12 waehlen (ESP-Hosted 2.12.13)."
#endif

static bool lanReady() {
  return ETH.linkUp() && ETH.hasIP();
}

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println("\n=== Waveshare P4 -> C6 ESP-Hosted OTA (LAN/SDIO) ===");

  // P4<->C6 SDIO: CLK, CMD, D0, D1, D2, D3, C6_RESET.
  if (!WiFi.setPins(18, 19, 14, 15, 16, 17, 54)) {
    Serial.println("FEHLER: SDIO-Pins konnten nicht gesetzt werden.");
    return;
  }

  // Initialisiert den Hosted-Link; WLAN muss NICHT mit einem AP verbunden sein.
  WiFi.STA.begin();
  if (!hostedIsInitialized()) {
    Serial.println("FEHLER: ESP-Hosted-Initialisierung fehlgeschlagen.");
    Serial.println("SDIO/Reset/Serial-Log pruefen.");
    return;
  }

  // Die Ethernet-Werte stammen aus der hochgeladenen Projektkonfiguration.
  if (!ETH.begin(ETH_PHY_IP101, 1, 31, 52, 51, EMAC_CLK_EXT_IN)) {
    Serial.println("FEHLER: Ethernet konnte nicht initialisiert werden.");
    return;
  }

  Serial.print("Warte auf LAN/DHCP");
  const uint32_t started = millis();
  while (!lanReady() && millis() - started < 30000UL) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (!lanReady()) {
    Serial.println("FEHLER: Keine Ethernet-Verbindung/IP-Adresse.");
    return;
  }
  Network.setDefaultInterface(ETH);
  Serial.print("Ethernet IP: ");
  Serial.println(ETH.localIP());

  uint32_t major = 0, minor = 0, patch = 0;
  hostedGetHostVersion(&major, &minor, &patch);
  Serial.printf("P4 ESP-Hosted: %lu.%lu.%lu\n",
                (unsigned long)major, (unsigned long)minor, (unsigned long)patch);

  // Der Core versucht die C6-Version zu lesen. Wenn das nicht klappt,
  // behandelt er die Version 0 als veraltet und bietet das Update an.
  const bool available = hostedHasUpdate();
  hostedGetSlaveVersion(&major, &minor, &patch);
  Serial.printf("C6-Version (0.0.0 = unbekannt): %lu.%lu.%lu\n",
                (unsigned long)major, (unsigned long)minor, (unsigned long)patch);
  Serial.print("Offizielles C6-Image: ");
  Serial.println(hostedGetUpdateURL());

  if (available) {
    Serial.println("Update moeglich, sofern die aktuelle C6-Firmware OTA unterstuetzt.");
    Serial.println("Zum Starten UPDATE im seriellen Monitor senden (Neue Zeile).");
  } else {
    Serial.println("Der Core erkennt kein neueres C6-Image. Kein Update noetig.");
  }
}

void loop() {
  if (!Serial.available()) {
    delay(50);
    return;
  }

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  cmd.toUpperCase();
  if (cmd != "UPDATE") {
    Serial.println("Zum Aktualisieren UPDATE eingeben.");
    return;
  }

  if (!hostedIsInitialized() || !lanReady()) {
    Serial.println("Abbruch: ESP-Hosted oder Ethernet nicht bereit.");
    return;
  }
  Network.setDefaultInterface(ETH);
  if (!hostedHasUpdate()) {
    Serial.println("Abbruch: Der Core meldet kein Update (oder die Versionspruefung ist fehlgeschlagen).");
    return;
  }

  Serial.println("Starte offizielles C6-OTA ueber Ethernet und SDIO ...");
  Serial.println("WICHTIG: Waehrend des Flashens Stromversorgung nicht trennen!");
  if (updateEspHostedSlave()) {
    Serial.println("C6-OTA erfolgreich, P4 wird neu gestartet.");
    delay(1000);
    ESP.restart();
  } else {
    Serial.println("C6-OTA fehlgeschlagen/abgebrochen. Serielle Fehlermeldungen pruefen.");
    Serial.println("Bei zu alter C6-Firmware ist ggf. direktes UART-Flashen noetig.");
  }
}
