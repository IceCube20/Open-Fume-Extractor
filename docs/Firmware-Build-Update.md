# Firmware, Build und Updates

## Unterstützte Arduino-Umgebung

Die zuletzt gespeicherten Buildoptionen verwenden Arduino ESP32 Core 3.x und
folgende Zielkonfigurationen.

## Bibliotheken

Der zuletzt geprüfte lokale Stand verwendet:

| Bibliothek | Version | Verwendung |
|---|---:|---|
| GFX Library for Arduino | 1.6.7 | beide Displaycontroller |
| LVGL | 9.5.0 | Displayoberfläche |
| Adafruit NeoPixel | 1.15.5 | SK6812-GRBW-Status-LEDs |
| PubSubClient3 | 3.3.0 | MQTT im Master |
| NimBLE-Arduino | 2.5.1 | temporäre Master-/Display-Kopplung über Bluetooth LE |

WiFi, WebServer, Preferences, LittleFS, Update, USB Host und FreeRTOS stammen
aus dem installierten ESP32-Core. Bei einem reproduzierbaren Release die
Bibliotheksversionen zusammen mit der verwendeten Core-Version notieren.

### Master

- Board: **ESP32S3 Dev Module**
- CPU: 240 MHz
- Flash: 16 MB, QIO
- PSRAM: OPI
- Partition Scheme: **Custom**
- USB: Hardware CDC
- Partitionsdatei: `OpenFumeExtractorMaster/partitions.csv`

Das Custom-Layout enthält zwei OTA-Slots mit je `0x330000`, 256 KB NVS, etwa
9,25 MB LittleFS und 64 KB Coredump. In Arduino IDE muss **Custom** gewählt sein,
damit die lokale `partitions.csv` verwendet wird.

### ESP32-Standardmodule

JBC Bus, Fan/IO, Fan/IO Pro, Weller, Universal RS232 und Modbus RTU:

- Board: **ESP32 Dev Module**
- CPU: 240 MHz
- Flash: 4 MB, QIO, 80 MHz
- PSRAM: deaktiviert
- Partition Scheme: Default

### JBC USB und Display 320x480

- Board: **ESP32S3 Dev Module**
- CPU: 240 MHz
- Flash: 16 MB, QIO
- PSRAM: OPI
- Partition Scheme: Default 8 MB with SPIFFS
- beim kleinen Display: USB CDC on Boot aktiv

### Display 800x480

Das große RGB-Display verwendet die gesicherte High-Performance-Plattform mit
64-Byte-DCache-Line und PSRAM-XIP:

```text
C:\Users\User\Documents\Arduino\ofe-high-performance-sdk
```

In Arduino IDE erscheint nach der Installation:

```text
OFE RGB High Performance > OFE Display 800x480 (High Performance)
```

Die Laufzeit kann reproduzierbar vorbereitet und installiert werden:

```powershell
.\tools\build_display_800x480_high_perf.ps1
.\tools\install_display_800x480_arduino.ps1
```

Danach Arduino IDE neu starten. Die SDK ist groß und muss separat vom
Projektordner gesichert werden.

## Kompilieren

Jeder Sketch liegt in einem eigenen Ordner und wird einzeln geöffnet. Nach dem
Kompilieren die erzeugte `.bin` anhand der OFE-Signatur prüfen. Die Firmwareversion
wird nur an einer Makro-Stelle im jeweiligen Sketch beziehungsweise beim Master
in `src/MasterBuildConfig.h` gepflegt.

Die aktuelle Versionsmatrix wird mit folgendem Befehl neu erzeugt:

```powershell
.\tools\update_firmware_docs.ps1
```

## Firmware signieren

Die normale Updateprüfung erwartet eine zur Zielhardware passende OFE-Signatur.
Für kryptografisch freigegebene Community-Builds wird die fertige BIN zusätzlich
mit Ed25519 signiert. Siehe [FIRMWARE_SIGNING.md](../FIRMWARE_SIGNING.md).

Der private Schlüssel darf nicht veröffentlicht oder in Firmware-/Projektbackups
abgelegt werden. Der Master enthält nur den öffentlichen Verifikationsschlüssel.

## Update über den Master

1. **Updates** öffnen.
2. Bei Master-OTA die Master-BIN auswählen oder bei Modul-OTA zuerst das Zielmodul.
3. Prüfen, ob erkannte Version und Zieltyp stimmen.
4. Update starten und bis zur Fertigmeldung warten.

Der Master zeigt bei der Modulauswahl die aktuell installierte Firmware und nach
der Dateiprüfung die neue Firmwareversion. Display-Updates können über RS485 oder
bei gekoppelten Displays über WLAN erfolgen.

Wird ein Modulupdate abgebrochen, sendet der Master einen Abbruch an das Ziel,
damit es den Zustand **FW busy** verlässt. Bei einem Stromausfall während des
Schreibens das Modul lokal per USB wiederherstellen.

## Entwicklermodus

Im Entwicklermodus kann die Zieltypprüfung bewusst übersprungen werden. Das ist
nur für Hardwaretests gedacht, beispielsweise um einen ESP32 zwischen Weller-,
Universal- und Modbus-Firmware umzunutzen. Eine falsche Firmware kann Pins mit
ungeeigneter Richtung oder Funktion ansteuern.

## Beschädigte LVGL-Buildarchive

Bei Linkerfehlern wie `objs.a: member ... is not an object` zuerst den
Buildcache prüfen. Ein gültiges einzelnes `.o` kann in einem beschädigten
Archiv liegen. Das erfordert keine Änderung an Displaytreiber oder SDK.

Unter Windows prüft `tools/Repair-DisplayBuildArchives.ps1` mit
`-BuildPaths @('vollständiger Arduino-Sketch-Cachepfad')` die LVGL-Archive.
Der Cachepfad steht in der Fehlermeldung. Alle Builds vorher beenden.
Ohne `-Repair` wird nur geprüft. Mit `-Repair` werden defekte Archive
gesichert und aus gültigen Originalobjekten neu aufgebaut. Die Sicherungen
liegen unter `tools/tests/build-checks/archive-diagnosis/backups`.
Das Werkzeug akzeptiert nur die drei Display-Sketches dieses Projekts und
bricht bei fehlenden oder ungültigen Originalobjekten ab. In diesem Fall
ist stattdessen ein sauberer Neu-Build nötig.

## Sicherung der Toolchain

Für einen vollständig reproduzierbaren Stand sichern:

- Projektordner
- `C:\Users\User\Documents\Arduino\ofe-high-performance-sdk`
- Arduino-Sketchbook/Libraries
- verwendete ESP32-Core-Version
- privaten Ed25519-Schlüssel getrennt und verschlüsselt

Die Junction unter `Documents\Arduino\hardware\ofe\esp32` ist nur ein Verweis
auf die SDK und muss nicht als zweite vollständige Kopie archiviert werden.
