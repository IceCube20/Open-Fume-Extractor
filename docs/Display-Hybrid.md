# Displays über RS485 und WLAN

Beide Displayfirmwares können den Master über den OFE-RS485-Bus oder gekoppelt
über WLAN erreichen. Die Bedienoberfläche und Moduladresse bleiben in beiden
Betriebsarten gleich.

## Systemseite

Die Einstellungen sind in Anzeige sowie Ruhe und Beleuchtung gegliedert.
Das 800x480-Display zeigt beide Bereiche nebeneinander; auf den kleinen Displays
stehen sie untereinander in einer vertikal scrollbaren Liste. Sprache und
Farbschema teilen sich eine Zeile. Der ST7796-Farbwähler bleibt zusätzlich
verfügbar. Die Verbindungseinrichtung und der nur im Entwicklermodus sichtbare
Benchmark sind weiterhin über die Systemseite erreichbar.

## Automatische Beleuchtungsabschaltung

Ab Master 2.0.55 und den Display-Versionen 320x480 1.4.5RC, 800x480 1.4.12RC
und ST7796 320x480 1.0.15RC lässt sich die Hintergrundbeleuchtung unabhängig
vom Ruhemodus abschalten. Die Einstellung gilt pro Display und funktioniert
über RS485 und WLAN gleichermaßen.

- **Beleuchtung ausschalten:** deaktiviert, nur im Energiesparmodus oder immer.
- **Wartezeit:** Auswahlliste mit 1 bis 60 Minuten seit der letzten Touch-Bedienung.
- Bei ausgeschalteter Beleuchtung schaltet die erste Berührung nur das Licht ein.
  Erst nach Loslassen und erneuter Berührung wird der Ruhemodus beendet.
  Beide Berührungen lösen keinen Button aus.
- Updates und die Verbindungseinrichtung halten die Beleuchtung eingeschaltet.
- Statusmeldungen wecken nicht auf. Die normale Helligkeit bleibt gespeichert.
- Es ist kein Tiefschlaf: Bus, WLAN und Steuerung bleiben aktiv.

Die Optionen stehen auf der Systemseite des Displays, in seiner Master-Karte,
über MQTT und ab Version 0.1.0rc24 in der nativen Home-Assistant-Integration
bereit. Beide HA-Anbindungen bieten zusätzlich den Status der Beleuchtung an.
Standardmäßig ist die automatische Abschaltung deaktiviert.

## Verbindungslogik

- Ist RS485 verfügbar, arbeitet das Display direkt am OFE-Bus.
- Fällt die Busverbindung aus und gültige WLAN-Daten sind gespeichert, verbindet
  sich das Display automatisch drahtlos mit dem Master.
- Die Kopfzeile zeigt rechts Alarmglocke mit Anzahl, Eco, Bluetooth und den aktiven
  Transport mit dem seriellen oder WLAN-Symbol in festen Bereichen. Die Kopfzeile
  bleibt auch während Verbindungseinrichtung, Tastatureingabe und Codebestätigung
  sichtbar (ab Display 1.4.9RC / 1.4.16RC / 1.0.19RC).
- Der Master kennzeichnet ein drahtlos verbundenes Display als WLAN-Modul und
  zählt den Transportwechsel nicht als unnötiges Offline-Ereignis.

## WLAN-Zeitmessung im Entwicklermodus

Die Master-Statusseite und die Bus-Diagnose zeigen ab **2.0.72RC** neben der
Gesamtlast auch **Core 0 / Core 1** in Prozent. Die CLI-Statusabfrage enthält die
gleichen Einzelwerte. Grundlage sind die bereits erfassten FreeRTOS-Leerlaufzeiten
je Kern, nicht die Loop-Zeit. Das Messfenster bleibt unverändert; es entstehen
keine zusätzlichen Mess-Tasks. Vor dem ersten vollständigen Messfenster oder
ohne verfügbare Laufzeitstatistik steht ein Strich. Die Gesamtlast bleibt der
bisherige Mittelwert beider Kerne: 50 % insgesamt kann beispielsweise 100 % auf
einem Kern und 0 % auf dem anderen bedeuten.

Ab Master **2.0.71RC** und Display **1.4.25RC / 1.4.37RC / 1.0.35RC** gibt es
in der **Bus-Diagnose** den Schalter **WLAN-Zeitmessung**. Die Messung ist nach
jedem Master-Neustart aus, wird nicht gespeichert und endet auch beim Abschalten
des Entwicklermodus. Der Pfeil-Button daneben setzt nur diese Messwerte zurück,
nicht die Busfehlerzähler. Nach einem Neustart oder einer neuen WLAN-Sitzung des
Displays beginnt dessen Messreihe ebenfalls neu.

Die Messung verwendet bestehende Live-/Cache-Anfragen und Antworten. Nach einer
Versionsaushandlung in den vorhandenen Keepalive-Paketen tragen diese Nachrichten
acht zusätzliche Bytes (Messversion 1). Ab Master **2.0.72RC** und Display
**1.4.26RC / 1.4.38RC / 1.0.36RC** erweitert Messversion 2 dies auf zwölf Bytes.
Version-1-Displays behalten das acht-Byte-Format; noch ältere Displays bleiben
beim bisherigen Nutzdatenformat. Während der Aushandlung läuft der normale
Nutzverkehr weiter. RS485,
Broadcasts, Kopplung und Firmware-Updates werden nicht mitgemessen; es gibt keine
zusätzlichen Probe-Telegramme.

Die WLAN-Displaykarte zeigt jeweils den letzten Wert, Durchschnitt und Höchstwert:

- **RTT:** Zeit im Master von der Paketvorbereitung bis zum Lesen der Antwort
  aus dem UDP-Socket. Enthält Hin- und Rückweg einschließlich Warteschlangen.
- **Display-Bearbeitung:** Zeit im Display vom Lesen des UDP-Pakets bis zur
  Vorbereitung der Antwort. Enthält Authentifizierung und Befehlshandler,
  aber nicht das spätere Zeichnen der LVGL-Oberfläche.
- **Transport + RX-Warteschlangen:** RTT minus Display-Bearbeitung. Enthält
  Funkstrecke, AP-/Netzwerk- und Socket-Warteschlangen sowie Sende-Protokolloverhead.
  Dies ist keine reine Messung der Funklaufzeit.
- **Master bis Verarbeitung:** Zeit vom Lesen des Antwortpakets bis zur
  Übernahme durch den zuständigen synchronen oder asynchronen Antwortpfad.
  Enthält Empfangsprüfung und gegebenenfalls das Warten auf den Scheduler.
- **Master RX-Lücke im RTT:** Größter Abstand zwischen UDP-Empfangsabfragen
  während genau dieser Anfrage, einschließlich der ersten und letzten Teillücke.
  Auch Abfragen ohne empfangenes Paket zählen. Eine große Lücke kann auf
  verzögertes Abholen der Antwort im Master hinweisen.
- **Display RX-Abstand vor Empfang:** Abstand der letzten zwei UDP-Abfragen
  beim Empfang der Anfrage (ab Messversion 2). Auch hier zählen leere Abfragen.
  Der Abstand kann schon vor dem Senden der Anfrage beginnen und ist deshalb
  keine zusätzliche RTT-Komponente. Ohne vorherige Abfrage bleibt er unbekannt.

Unter **Langsamste Antwort** stehen RTT, Bearbeitung, Empfangsabstände und
Master-Verarbeitung zusammen mit Befehl und Sequenznummer derselben Antwort.
Die Zuordnung erfolgt zusätzlich über ein Anfrage-Token, nicht nur über die
wiederverwendbare Sequenznummer. Noch nicht gemessene Werte stehen als Strich,
nicht als scheinbar perfekte Null. Dieser gemeinsame Datensatz hilft, einen
einzelnen Ausreißer zwischen Master-Abholung, Display-Abholung und verbleibendem
Transport einzugrenzen; er beweist allein noch keinen Fehler des Access Points.

Alle Differenzen entstehen auf der jeweiligen lokalen Mikrosekunden-Uhr;
Überläufe werden berücksichtigt. Es werden keine synchronisierten Uhren und
keine einzeln bestimmten Hin-/Rückwegzeiten behauptet. Die Höchstwerte einzelner
Abschnitte können zu unterschiedlichen Antworten gehören und dürfen nicht als
gemeinsamer Maximalfall addiert werden. Nur der Block **Langsamste Antwort**
zeigt zusammengehörige Werte. Fehlende Antworten erscheinen weiterhin
in den normalen Timeout-Zählern, nicht als erfundene Zeitmessung.

Für einen Vergleich Master und betroffene Displays aktualisieren, die Messung
einschalten, dieselbe Detailseite bzw. dasselbe Blinkmuster verwenden und die
Messwerte nach einigen Minuten aus der Bus-Diagnose übernehmen.

## Einrichtung mit vorhandenem RS485-Bus

1. Display am OFE-Bus starten.
2. Am Master unter **Netzwerk Setup** die Displaykopplung öffnen.
3. Kopplungscode für die Seriennummer des Displays anzeigen.
4. Am Display **System > Verbindung: RS485 / WLAN** öffnen.
5. WLAN-Daten vom Master übernehmen und den Kopplungscode bestätigen.
6. RS485 testweise abziehen und prüfen, ob das WLAN-Symbol erscheint.

## Direkte Bluetooth-Kopplung ohne RS485

Ab Master **2.0.58RC** und Display **1.4.8RC / 1.4.15RC / 1.0.18RC** kann die
Einrichtung direkt zwischen Master und Display erfolgen, ohne Handy:

1. Der Master muss bereits mit dem gewünschten WLAN verbunden sein.
2. Am Display **System > Verbindung > Per Bluetooth koppeln** öffnen.
3. Im Master das Bluetooth-Symbol in der Kopfzeile öffnen, **Displays suchen**
   wählen und das Display anhand der Seriennummer auswählen.
4. Den sechsstelligen Zahlencode vergleichen und auf **beiden Geräten** bestätigen.
5. Auf die erfolgreiche Speicherung warten. Danach läuft die Verbindung wie bisher
   über WLAN beziehungsweise bei angeschlossenem Bus über RS485.

Bluetooth läuft nur während der Einrichtung, höchstens zwei Minuten. Nach Abschluss,
Abbruch oder Fehler wird es abgeschaltet. Das blaue Bluetooth-Symbol kennzeichnet
das aktive Kopplungsfenster, nicht den normalen Datentransport.

Am Display lässt sich unter **Master-Kopplung löschen** die Masteradresse mit dem
Kopplungsschlüssel entfernen. WLAN-Zugangsdaten bleiben erhalten. Die automatische
Übernahme vom RS485-Master wird dabei ausgeschaltet; **Vom Master** aktiviert sie
wieder. Ein gespeicherter Geräteeintrag im Master wird dadurch nicht gelöscht.

Zum Kompilieren ist zusätzlich **NimBLE-Arduino 2.5.1** erforderlich. Die vorhandenen
Display-SDK-, PSRAM- und DMA-Einstellungen bleiben unverändert. Bei zu wenig freiem
internem RAM wird die Bluetooth-Einrichtung mit einer Meldung abgelehnt.

Für die korrigierte Speicherbelegung mindestens **Master 2.0.58RC** und die
Display-Versionen **1.4.8RC / 1.4.15RC / 1.0.18RC** verwenden. Die erste
Bluetooth-Version wurde von SDK-Vorgaben auf internen Speicher und drei
Verbindungsplätze zurückgestellt. Die neue projektspezifische
`OfeBleBuildConfig.h` wird deshalb in allen C-/C++-Dateien nach den SDK-Vorgaben
geladen. Diese Datei zusammen mit `build_opt.h` im jeweiligen Sketchordner
belassen. Die RAM-Schutzgrenzen werden nicht abgesenkt.

English instructions: [Direct Bluetooth Display Setup](Display-Bluetooth-Pairing.md).

### RAM während der Kopplung

Ab Master **2.0.59RC** und Display **1.4.10RC / 1.4.17RC / 1.0.20RC** hält der
Master beim Suchen nur sechs passende OFE-Einträge vor. Nach dem Suchlauf wird
Bluetooth bis zur Geräteauswahl abgeschaltet und anschließend für die gesicherte
Verbindung neu gestartet. Der Controller-Duplikatcache ist auf 20 Einträge reduziert;
das ist keine Begrenzung auf 20 auffindbare Geräte.

Der auslagerbare NimBLE-Hostspeicher liegt bereits in PSRAM. Controller,
Betriebssystemressourcen und die flashfähigen Task-Stacks benötigen weiterhin
internen RAM. Daher sind unveränderte 120 KiB während einer aktiven Funkverbindung
nicht zusicherbar. Displays müssen während des Kopplungsfensters weiter werben.
Nach Abschluss, Abbruch oder Zeitablauf wird Bluetooth wieder freigegeben; die
Anzeige des minimalen Heaps bleibt als historischer Tiefstwert niedrig.

Ab Master **2.0.60RC** liegt zusätzlich die Diagnose-Tabelle für alle 96 Busadressen
im PSRAM statt dauerhaft im internen RAM. Sie belegt mit dem aktuellen
ESP32-Compiler 24.576 Bytes. Sämtliche Zähler und Timeout-Beobachtungen bleiben
erhalten. Ist PSRAM nicht verfügbar, wird intern reserviert. Der Boot-Log bestätigt
die Belegung mit `[MEM] Bus diagnostics: 24576 bytes in PSRAM`. UART-Puffer,
Task-Stacks und Bus-Timings bleiben unverändert; der reale Heap-Gewinn muss am
Gerät geprüft werden.

### Aufbau der Kopplungsseite

Die Display-Versionen **1.4.11RC / 1.4.18RC / 1.0.21RC** trennen den aktuellen
Verbindungsstatus von den Rückmeldungen der Einrichtung. Auf 800x480 stehen WLAN
und Master-Daten nebeneinander; auf den kleinen Displays untereinander in einer
scrollbaren Ansicht. Die 44 Pixel hohen Bedienelemente folgen Sprache und
Farbschema. Die Kopfzeile bleibt auch bei Tastatur und Bestätigungsdialog sichtbar.
Während einer Kopplung kann Bluetooth direkt mit demselben Button abgebrochen
werden. Noch nicht übernommene Eingaben werden nicht durch Live-Status ersetzt.

## Manuelle Einrichtung ohne RS485

Auf der Systemseite des Displays können SSID, WLAN-Passwort, Masteradresse und
Kopplungscode manuell eingegeben werden. Damit ist die Erstinbetriebnahme auch
ohne Busverbindung möglich.

## Firmwareupdates

- Ein lokal ausgewähltes Display kann seine Firmware drahtlos empfangen.
- Während ein anderes Modul aktualisiert wird, zeigt das gekoppelte Display
  Fortschritt, Geschwindigkeit und die fünfsekündige Fertigmeldung ebenfalls an.
- Nach Ablauf der Haltezeit kehrt es auf die zuvor geöffnete Seite zurück.

## Display 320x480

Das kleine QSPI-Display verwendet einen vollständigen Canvas und eine softwareseitige
Rotation. Es läuft mit dem normalen Arduino-ESP32-Core.

## Display 800x480

Das Guition JC8048W550 verwendet ein paralleles RGB565-Panel. Für stabilen Scanout
ist die OFE-High-Performance-SDK verbindlicher Bestandteil des Builds. Beibehalten:

- 16 MHz PCLK
- 64-Byte-DCache-Line
- PSRAM-XIP
- 20-Zeilen-Bounce-Buffer
- aktuelles VSYNC-/Cache-Synchronisationsverfahren

Nach Änderungen an SDK, PCLK, Bounce-Buffer, LVGL-Flush oder PSRAM-Konfiguration
immer mehrere Minuten auf allen Seiten scrollen und gleichzeitig RS485/WLAN sowie
Firmwareupdates testen.

## Fehlersuche

| Symptom | Prüfung |
|---|---|
| Display bleibt bei Booting | seriellen Log auf LVGL-/PSRAM-Allokationsfehler prüfen |
| Keine WLAN-Netze sichtbar | WLAN-Initialisierung und freien internen Heap prüfen |
| Display offline trotz WLAN | Kopplung, Master-IP und Systemzeit prüfen |
| Artefakte am 800x480-Panel | richtige High-Performance-Boardplattform und 16 MHz PCLK prüfen |
| Update wird nicht angezeigt | Displaykopplung und aktiven Transport im Master prüfen |

