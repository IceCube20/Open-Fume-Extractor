# Inbetriebnahme

Diese Anleitung beschreibt den ersten Start eines vollstaendigen OFE-Systems mit MCU 2.1.52RC.
Die Bedienoberflaeche nennt den Controller **MCU-S3** oder **MCU-P4**.
Aktuelle Bedienhinweise stehen in [Bedienung und Diagnose](Bedienung-Diagnose.md).

## Voraussetzungen

- MCU-S3: ESP32-S3 mit 16 MB Flash und 8 MB OPI-PSRAM, bis zu 16 Module
- oder MCU-P4: unterstuetztes Waveshare PRE-V3-Board mit 16 MB Flash / 32 MB PSRAM
- oder MCU-P4-PoE: Waveshare ESP32-P4-WIFI6-POE-ETH V3.1+, mit eigenem Buildprofil;
  das getestete V3.2-Board besitzt 32 MB Flash / 32 MB PSRAM; beide P4-Profile erlauben bis zu 32 Module
- mindestens ein OFE-Modul
- RS485-Verkabelung mit gemeinsamer Masse
- stabile 5-V-Versorgung mit ausreichender Stromreserve
- Arduino IDE 2.x mit Espressif ESP32 Core 3.x

Die genauen Boardoptionen stehen in
[Firmware, Build und Updates](Firmware-Build-Update.md). Vor dem Verdrahten die
[Pinbelegung](Hardware-Pinbelegung.md) prüfen.

## 1. RS485-Bus aufbauen

Der OFE-Bus arbeitet mit **250000 Baud, 8N1**. A mit A, B mit B und GND mit GND
verbinden. Der Bus soll als Linie aufgebaut werden; lange Stichleitungen vermeiden.
An die beiden physischen Busenden gehört jeweils ein **120-Ohm-Abschlusswiderstand**.

Die Adressfamilien sind fest gegliedert:

| Bereich | Modulfamilie |
|---|---|
| `0x10` bis `0x1F` | JBC FAE Bus und JBC USB |
| `0x20` bis `0x2F` | Fan/IO und Fan/IO Pro |
| `0x30` bis `0x3F` | Weller Zero Smog |
| `0x40` bis `0x4F` | Displays |
| `0x50` bis `0x5F` | Universal RS232 |
| `0x60` bis `0x6F` | Modbus RTU |

Mehrere Module derselben Familie sind möglich. Bei identischen Werksadressen die
Module zunächst einzeln anschließen oder anschließend die automatische
Adressvergabe verwenden.

## 2. Firmware erstmals flashen

Den Master und jedes Modul zunächst lokal per USB flashen. Die späteren Updates
können über die Master-Webseite erfolgen. Für den Master muss das projektspezifische
Partitionslayout verwendet werden; für das große RGB-Display ist die separate
High-Performance-Plattform vorgesehen.

Nach dem Start zeigt die serielle CLI des Masters mit `help` alle Befehle. Besonders
nützlich sind:

```text
status
modules
module 0x50
routes
bus
network
mqtt
scan
```

## 3. Master ins Netzwerk bringen

Ohne gültige WLAN-Konfiguration startet der Master seinen Einrichtungszugangspunkt.
SSID und AP-Passwort werden dabei auf der seriellen Konsole
ausgegeben. Beim ersten Start uebernimmt die Community-Firmware fuer den Benutzer
`admin` das kompilierte Einrichtungskennwort `extractor123` und zeigt es seriell
als `[AUTH] initial web ...` an. Das AP-Kennwort ist im Community-Standard ebenfalls
`extractor123`. Eigene Builds koennen andere Bootstrap-Werte enthalten. Bereits im
NVS gespeicherte Zugangsdaten bleiben erhalten. Das Web-Kennwort bei der Einrichtung
unbedingt aendern. `webauth reset` stellt den kompilierten Web-Einrichtungszugang
wieder her und zeigt ihn seriell an; es erzeugt kein neues Zufallskennwort.

Danach unter **Netzwerk/System** folgende Bereiche konfigurieren:

1. WLAN-Zugang und optional feste IP
2. Web-Login
3. MQTT und optional TLS-CA-Zertifikat
4. Status-LEDs

Beim P4 werden LAN und WLAN getrennt konfiguriert. Ethernet wird bei verfuegbarer
Verbindung bevorzugt; beim Umschalten zeigt das Header-Symbol den aktiven Transport.
Feste IPs fuer LAN und WLAN eindeutig vergeben. Auch eine parallel laufende alte MCU
braucht eine andere IP, einen eigenen Hostnamen und einen eigenen MQTT-Basispfad.

Netzwerkänderungen können einen Neustart erfordern. LED- und MQTT-Einstellungen
werden getrennt gespeichert und angewendet.

## 4. Module einlernen

1. **Bus Diagnose** öffnen.
2. **Module scannen** ausführen.
3. Falls Adressen Lücken oder Konflikte enthalten, **Adressvergabe** ausführen.
4. Prüfen, ob jedes Modul mit Typ, Seriennummer, Firmware und Busqualität erscheint.

Der Master speichert den Ausbau. Ein gelerntes, aber ausgeschaltetes Modul bleibt
als offline sichtbar und löst eine entsprechende Alarmmeldung aus. Ein erneuter
Scan kann nicht mehr vorhandene Module aus dem Ausbau entfernen.

## 5. Absaugung konfigurieren

Eine frisch eingerichtete MCU startet mit **Kein Eingang**. **Alle JBC-Module**
nur bewusst waehlen, wenn diese Sammelfunktion gewuenscht ist.

Auf der Statusseite:

1. **Haupteingang Absaugung** wählen, beispielsweise alle JBC-Module oder einen
   digitalen Fan/IO-Eingang.
2. **Hauptausgang Absaugung** wählen. Bei **Auto** nimmt der Master einen verfügbaren
   geeigneten Ausgang; eine manuelle Auswahl bleibt an das konkrete Modul gebunden.
3. Absaugstufe, Benutzerleistung, Work-/Stand-Nachlauf und optionale
   Nachlaufleistung einstellen.
4. Funktion bei ein- und ausgeschaltetem Eingang testen.

Ohne gültigen Haupteingang oder Hauptausgang meldet das System **Nicht bereit**.

## 6. Backup anlegen

Unter **Netzwerk/System > Backup / Restore** ein JSON-Backup herunterladen. Es
enthält sensible Daten wie WLAN-, Web- und MQTT-Zugangsdaten sowie Zertifikate.
Das Backup wird nur bei einem ausdrücklichen Restore eingespielt und nicht bei
jedem Neustart.

Auf dem P4 laesst sich das Backup auch in einem ausgewaehlten Ordner auf einem
USB-Stick speichern. Die Dateiauswahl allein fuehrt keinen Restore aus; den Import
gesondert bestaetigen. Siehe [USB-Browser](USB.md).

Zusätzlich separat sichern:

- den vollständigen Projektordner
- den privaten Firmware-Signaturschlüssel
- die High-Performance-SDK unter
  `C:\Users\User\Documents\Arduino\ofe-high-performance-sdk`

## 7. Abnahmetest

- alle Module online und nach Adresse sortiert
- Busqualität ohne Live-Misses
- Hauptsignalweg schaltet den Ausgang korrekt
- Nachlauf und Nachlaufleistung funktionieren
- Display zeigt dieselben Alarme und Modulwerte wie der Master
- MQTT-Entities sind in Home Assistant verfügbar
- Einstellungen bleiben nach einem Master-Neustart erhalten
- Backup exportieren, testweise wiederherstellen und erneut prüfen
- beim P4 LAN/WLAN-Wechsel und getrennte IP-Konfiguration pruefen
- optionale lokale Verbindungswarnung und Fehler eines benoetigten Hauptausgangs getrennt testen
- virtuelle LED-Ausgaenge nur als Anzeige verwenden, nicht als Sicherheitsfunktion
