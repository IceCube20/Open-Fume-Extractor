# Korrekturen für Routing / boolesche Logik (08.11.2026)

Änderungen gegenüber der bereitgestellten ZIP:

1. **JBC FAE Stand:** Verwendet in der Master-Auswertung die gleiche Bedeutung wie in der Statusanzeige: FAE-Modul online, Station verbunden und kein Work-Signal. Der JBC USB Host verwendet weiterhin seine echte Stand-Maske. Auch der aggregierte JBC-Zustand ist entsprechend angepasst.
2. **Hauptausgang gewählt / aktiv:** Die `/state`-Bitmasken enthalten jetzt die Systemzustände 1–10. Zustand 9 wird pro Modul gesetzt, wenn es *explizit gewählt* ist; Zustand 10 **nur dann**, wenn es als Hauptausgang *zugeordnet* **und die Absaugung eingeschaltet** ist. Der automatische Modus setzt Zustand 8 im System. „Gewählt“ bleibt bei ausgeschalteter Absaugung aktiv, wenn die Moduladresse manuell gewählt ist. „Aktiv“ wird beim Ausschalten der Absaugung wieder inaktiv. Im Automatikmodus kennzeichnet Bit 8 die automatische Auswahl; Bit 10 signalisiert den tatsächlich zugeordneten Ausgang bei laufender Absaugung. Diese Semantik gilt in zusätzlichen Routing-Regeln, boolescher Laufzeit und Statusmaske. Mit „eingeschaltet“ ist der von der Absaugungssteuerung angeforderte Betriebszustand (`extractor_.outputEnabled()`) gemeint, nicht zwingend eine bestätigte Lüfter-Drehzahl.
3. **Warnung und Fehler gleichzeitig:** Die Alarmberechnung und die Web-Alarmliste behandeln Warnungsbit `0x0002` und kritische Fehlerbits unabhängig. Beide Eingänge können gleichzeitig aktiv sein, auch bei einem gemeinsamen Fehlerwort (z. B. `0x0102`). Das gilt auch für gemischte JBC-Fehler. Ein vorhandenes Filterwarnungsbit bleibt als Filterwarnungs-Signal erhalten, selbst wenn detaillierte Filterdaten durch einen Sensorfehler ungültig sind.
4. **Neue Routing-Regeln:** Als Ausgang wird standardmäßig der logische **Absaugung Hauptausgang** (`type=1, addr=0, bit=0`) gesetzt; der bevorzugte physische Ausgang wird nicht mehr fälschlich als digitaler Kanal angelegt. Ein online bekanntes Modul ohne Digitalausgangsfähigkeit wird als digitales Routenziel abgelehnt. Früher gespeicherte ungültige Ziele werden in der Liste als **„Ungültiger Ausgang“** statt als „Kein Ausgang“ angezeigt.

## Hinweis zu bereits gespeicherten Regeln

Die Korrektur verändert gespeicherte Routing-Regeln **nicht automatisch**. Falls eine alte Regel noch „Ungültiger Ausgang (0x20 …)“ zeigt, bitte ihren Ausgang manuell auf „Absaugung Hauptausgang“ ändern oder die Regel löschen und neu anlegen. Eine pauschale automatische Migration wäre riskant, da andere digitale Ziele legitim sein können.

## Tests

Aus dem Projektordner:

```sh
g++ -std=c++11 -Isrc tests/automation_input_signals_test.cpp -o /tmp/ofe_signal_test && /tmp/ofe_signal_test
node tests/status_web_test.js
```

Die Tests prüfen FAE-/USB-Stand-Semantik, kombinierte Warnungs-/Fehlerbits, die JavaScript-Syntax, die Anzeige der kombinierten Fehler und das korrekte Ziel einer neuen Regel (auch bei physischem Ausgang `0x20`).

**Nicht durchgeführt:** vollständiger Arduino-/ESP32-Firmware-Build und Hardwaretests mit angeschlossenem FAN IO/JBC FAE/JBC USB Host (Build-Toolchain und reale Hardware hier nicht vorhanden).

## Nachkorrektur: Hauptausgang aktiv vs. gewählt (08.11.2026)

- Die vorherige Version setzte „Hauptausgang aktiv“ bereits bei der bloßen Zuordnung einer Moduladresse. Jetzt muss zusätzlich `extractor_.outputEnabled()` wahr sein.
- Das gilt konsistent in `MasterScheduler::systemInputMask`, `systemInputActive`, `WebLogic::logic_signal_active` sowie in der Live-Anzeige der booleschen Logik.
- Der Eintrag „Hauptausgang gewählt“ bleibt bei manuell gewähltem Ausgang auch dann aktiv, wenn die Absaugung AUS ist; Automatikbetrieb wird weiterhin über den separaten „Automatisch“-Eintrag signalisiert.
- Zusätzliche Testfälle prüfen die Zustandsmatrix (gewählt/zugeordnet/eingeschaltet) und die Live-Anzeige.
