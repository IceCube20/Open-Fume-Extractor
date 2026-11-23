# RS485 auf eigenem FreeRTOS-Task / Gegen-Core

## Änderungen

Die sieben bisher im Arduino-`loop()` gepollten Module wurden um einen eigenen
`ofeRs485`-Task ergänzt:

- FanIoModule
- FanIoProModule
- JbcBusModule
- JbcUsbModule
- ModbusRtuModule
- UniversalRs232Module
- WellerZeroSmogModule

Die vier Display-Module wurden **nicht** verändert; sie besitzen bereits eine
vergleichbare Task-Aufteilung.

In jedem umgestellten Projekt befindet sich eine Kopie von
`src/OfeRs485Task.h`. Der Task übernimmt `poll_rs485()` inklusive bestehender
Frame-Handler sowie `poll_pending_discover_response()`,
`poll_join_announce()` und `fw_update_check_timeout()`. Dadurch besitzt stets
nur *ein* Ausführungskontext den physischen RS485-Bus. Es wurden keine
RS485-Kommandos, Paketformate oder Baudraten angepasst.

Die lokale Geräte-/USB-/Sensorarbeit bleibt im Arduino-`loop()`. Zugriffe auf
zwischen den beiden Aufgaben gemeinsam verwendete Zustände sind durch eine
FreeRTOS-Recursive-Mutex geschützt. Der Schutz wird jeweils nur während der
betroffenen Abschnitte gehalten; zwischen den Abschnitten kann RS485 sofort
weiterarbeiten. Während *eines* geschützten, länger blockierenden lokalen
Abschnitts wartet aber auch der RS485-Handler auf die Mutex: **Die Latenz ist
nicht unter allen Lastbedingungen garantiert.** Eine weitere Verbesserung
würde asynchrone Gerätekommandos/Status-Snapshots und kürzere Lock-Intervalle
erfordern. Gerade beim JBC-USB-Host sowie bei langsamen lokalen I/O-Aufrufen
sollte die Verzögerung gemessen werden.

## Core, Priorität und Fallback

`src/OfeRs485Task.h` lässt diese Einstellungen zu (vor seinem Include bzw.
als Build-Defines):

```
#define OFE_RS485_DEDICATED_TASK 1 // 0 = ursprünglicher loop()-Betrieb
#define OFE_RS485_TASK_CORE -1     // -1 = Core gegenüber setup()/loop()
#define OFE_RS485_TASK_PRIORITY 2  // Standard (JbcUsbModule: 5)
#define OFE_RS485_TASK_STACK 8192  // Stack in BYTES (ESP-IDF)
```

Standardmäßig läuft `setup()/loop()` bei Dual-Core-ESP32 auf Core 1, der
RS485-Task damit auf Core 0. Sollte der Arduino-Loop anders gepinnt sein,
wählt `-1` automatisch den anderen Core. Ein Core ist dabei nicht exklusiv
für RS485 reserviert: ESP-IDF- und USB-Tasks werden weiterhin eingeplant.

Der neue Task gibt die CPU nach jedem Durchlauf mit `vTaskDelay(1)` frei.
Beim JbcUsbModule ist die Standardpriorität 5, also oberhalb der beiden
vorhandenen USB-Tasks (3/4); sie können während des Delays arbeiten.

Bei Single-Core-Builds, abgeschaltetem Feature, ungeeigneter Core-Vorgabe
oder fehlgeschlagenem Task-/Mutex-Start wird die ursprüngliche
`loop()`-Verarbeitung automatisch benutzt. Ein stiller Fallback ist keine
Bestätigung, dass der Task wirklich aktiv ist: beim Inbetriebnehmen bitte
über Debugging/Task-Info prüfen.

## Mindesttests an der Hardware

1. Module einzeln kompilieren und starten; RS485-Task und tatsächlich
   verwendeten Core im Debugger/FreeRTOS-Taskmonitor überprüfen.
2. Master-PING, GET_STATE/FAST_POLL und Discovery im Leerlauf **und bei
   gleichzeitig aktiver lokaler Kommunikation** messen (Zeit und Jitter).
3. Besonders JBC-USB: USB-Hotplug/CP210x-Init, kontinuierliche Daten,
   CONFIG-Kommandos; dabei auf Prioritätskonflikte und Mutex-Wartezeiten
   achten.
4. Modbus/RS232: lokale UART-Timeouts und Wiederholungen bei gleichzeitigen
   MCU-Anfragen; Fan/IO/Weller: Failsafe und sichere Ausgangsschaltungen.
5. Adresse/Label, Join-Announcements, Konfigurationsspeicherung sowie
   FW-Update (Start/Chunks/Ende/Abbruch/Neustart) prüfen.
6. Falls nötig `OFE_RS485_DEDICATED_TASK` auf 0 setzen, um das Verhalten
   mit dem alten seriellen Loop direkt zu vergleichen.

**Prüfstatus:** Code statisch auf vorhandene UART-send/poll-Aufrufe und
unveränderte vier Display-Sketches verglichen; das gemeinsame Task-Header
wurde per C++-Syntaxprüfung mit FreeRTOS-Stubs geprüft. Kein ESP32-Board- oder
Arduino-Core-Build und kein Funktionstest mit realer MCU/RS485-Hardware
möglich in dieser Umgebung. Vor Produktion bitte auf den verwendeten
ESP32-Boardpaketen kompilieren und auf Hardware validieren.
