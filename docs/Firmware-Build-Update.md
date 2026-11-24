# Firmware, Build und Updates

## Unterstützte Arduino-Umgebung

Die zuletzt gespeicherten Buildoptionen verwenden Arduino ESP32 Core 3.x und
folgende Zielkonfigurationen. Stand dieser Anleitung: MCU 2.1.52RC.
Die [Versionsmatrix](Firmwarestaende.md) und der [Downloadkatalog](FIRMWARE.md)
ordnen jede Datei ihrer Zielhardware zu; gleiche ESP32-Familie bedeutet nicht
identische Pinbelegung oder austauschbare Firmware.

## Bibliotheken

Der zuletzt geprüfte lokale Stand verwendet:

| Bibliothek | Version | Verwendung |
|---|---:|---|
| GFX Library for Arduino | 1.6.9 | Displaycontroller |
| LVGL | 9.6.0 | Displayoberflaeche |
| Adafruit NeoPixel | 1.15.5 | SK6812-GRBW-Status-LEDs |
| PubSubClient3 | 3.3.0 | MQTT im Master |
| NimBLE-Arduino | 2.5.1 | temporäre Master-/Display-Kopplung über Bluetooth LE |

WiFi, WebServer, Preferences, LittleFS, Update, USB Host und FreeRTOS stammen
aus dem installierten ESP32-Core. Bei einem reproduzierbaren Release die
Bibliotheksversionen zusammen mit der verwendeten Core-Version notieren.

### MCU-S3

- Board: **ESP32S3 Dev Module**
- CPU: 240 MHz
- Flash: 16 MB, QIO
- PSRAM: OPI
- Partition Scheme: **Custom**
- USB: Hardware CDC
- Sketch: `OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/OpenFumeExtractorMCU_S3.ino`
- Partitionsdatei: `OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/partitions.csv`

Das Custom-Layout enthält zwei OTA-Slots mit je `0x330000`, 256 KB NVS, etwa
9,25 MB LittleFS und 64 KB Coredump. In Arduino IDE muss **Custom** gewählt sein,
damit die lokale `partitions.csv` verwendet wird.

### MCU-P4: zwei getrennte Builds

Die P4-MCUs verwenden Arduino-ESP32 **3.3.12**, ESP-IDF **5.5.5** und
ESP-Hosted im MCU **2.12.13**. Die SDIO-Pins und Chiprevisionsgrenzen werden
boardbezogen vorbereitet, nicht zwischen den Boards vertauscht.

| Ziel | Sketchordner | Chiprevision | Kapazitaet |
|---|---|---|---|
| Waveshare Module DEV KIT PRE-V3 | `OpenFumeExtractorMaster/OpenFumeExtractorMCU_P4` | PRE-V3, Headerbereich 1..199 | 16 MB Flash / 32 MB PSRAM, 32 Module |
| Waveshare WIFI6-POE-ETH V3.1+ | `OpenFumeExtractorMaster/OpenFumeExtractorMCU_P4_POE` | 3.1 bis 3.99 | getestetes Board: 32 MB Flash / 32 MB PSRAM, 32 Module |

Die erlaubten Revisionswerte im Image-Header sind 1..199 bzw. 301..399;
das PRE-V3-Profil ist fuer die unterstuetzte aeltere P4-Hardware vorgesehen.
Die P4-Partitionsdateien im jeweiligen Sketch vor dem Erstflash verwenden.
Ein App-OTA ersetzt weder Bootloader noch Partitionslayout.
Boarddetails: [PoE V3](MCU-P4-PoE-V3.md).

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

Oeffentliche MCU-Builds werden aus isolierten Community-Snapshots erzeugt:

```powershell
.\tools\build_community_release.ps1 -Jobs 4
.\tools\prepare_github_release.ps1 -ValidateOnly
```

Das baut die drei MCU-Ziele, prueft die Exporte und flasht keine Hardware.
Die anschliessende Freigabepaketierung mit `prepare_github_release.ps1` benoetigt
den privaten Signaturschluessel des Herausgebers; dieser ist nicht im Repository.
Lokale Developer-Builds nicht als Community-Dateien veroeffentlichen.

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

1. **Updates** oeffnen.
2. Bei MCU-OTA die passende signierte App-BIN auswaehlen oder bei Modul-OTA zuerst das Zielmodul.
3. Prüfen, ob erkannte Version und Zieltyp stimmen.
4. Update starten und bis zur Fertigmeldung warten.

Fuer OTA die App-Datei verwenden, nicht die fuer den Erstflash zusammengefuegte
`merged.bin`. MCU-Chip und Revisionsbereich muessen passen; auch der
Entwicklermodus kann diese Hardwarepruefung nicht abschalten.

Der Master zeigt bei der Modulauswahl die aktuell installierte Firmware und nach
der Dateiprüfung die neue Firmwareversion. Display-Updates können über RS485 oder
bei gekoppelten Displays über WLAN erfolgen.

Wird ein Modulupdate abgebrochen, sendet der Master einen Abbruch an das Ziel,
damit es den Zustand **FW busy** verlässt. Bei einem Stromausfall während des
Schreibens das Modul lokal per USB wiederherstellen.

## USB und C6 getrennt aktualisieren

Beim P4 waehlt **Von USB installieren** die Datei aus einem Stickordner.
Das startet noch kein Update: Version und Ziel kontrollieren und den jeweiligen
Installationsknopf gesondert bestaetigen. S3 besitzt diesen USB-Host-Pfad nicht.
Siehe [USB-Browser](USB.md).

Der C6 ist ein eigener Funkprozessor, nicht die MCU-P4-App. Unter
**ESP32-C6 / WLAN / Bluetooth** zuerst Freigaben pruefen, dann die Datei laden und
pruefen, zuletzt installieren. C6-Updates nur ueber LAN starten; WLAN und Bluetooth
werden unterbrochen. Passend zum MCU-Treiber ist 2.12.13 die Basisfreigabe.
Die Freigabe von C6 2.13.1 gilt fuer das exakt getestete PoE-V3.2-/32-MB-Profil,
nicht pauschal fuer alle P4-Boards. Katalog, Hash und Boardauswahl sind verbindlich.

Private Developer-Builds bieten fuer beide P4-Profile eine Auswahl numerischer
Espressif-Versionen zum Testen und fuer ausdruecklich bestaetigte Rueckgaenge.
Ein gefundenes Upstream-Image ist noch keine OFE-Freigabe. Vor Tests den
boardgenauen UART-Wiederherstellungsweg pruefen. Eine unbekannte installierte
C6-Version berechtigt nicht automatisch zum Flashen.
Details: [C6-Katalog und Wiederherstellung](Hosted-C6-Catalog.md).

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
