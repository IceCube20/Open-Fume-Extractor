# Bedienung und Diagnose

Stand: MCU 2.1.52RC. Ergaenzende Schrittfolgen:
[Weboberflaeche](WEB-INTERFACE.md), [USB](USB.md),
[LED-Ausgaenge](FANIO_LED_OUTPUTS.md) und [Logikdesigner](System-Automation.md).

## Darstellung und Mobilgeraete

Auto, Hell und Dunkel werden pro Browser gespeichert. Auto folgt dem Betriebssystem.
Die Statusseite nutzt auf breiten Bildschirmen bis zu fuenf Modulkarten pro Reihe;
die Modulliste bleibt breit und auf dem Handy bedienbar. Die Spaltenwahl der
HA-Uebersichtskarte ist davon unabhaengig und erlaubt eine bis vier Spalten.
Editoren haben einen erreichbaren Kopfbereich zum Schliessen; die USB-Dateiauswahl
erscheint als Dialog, der eigenstaendige Dateimanager unter **USB**.

## Statusseite

Die Statusseite ist die zentrale Betriebsansicht. Sie zeigt Bereitschaft,
angeforderte Leistung, Alarme, Hauptsignalweg und alle eingelernten Module.

### Hauptsignalweg

Der **Haupteingang Absaugung** entscheidet, wann abgesaugt wird. Der
**Hauptausgang Absaugung** bestimmt, welches Ausgangsmodul Lüfter und Leistung
steuert. Ist ein manuell ausgewähltes Modul offline, bleibt seine frühere Auswahl
in Klammern sichtbar, der aktive Wert fällt aber auf **Kein Eingang** oder
**Kein Ausgang** zurück. **Auto** sucht selbst einen geeigneten Online-Ausgang.

### Absaugungseinstellungen

- **Absaugstufe:** Niedrig, Mittel, Hoch oder Benutzer
- **Benutzerleistung:** frei einstellbare Leistung
- **Nachlauf Work/Stand:** Laufzeit nach Ende des jeweiligen Eingangszustands
- **Continuous:** Laufzeitmodus ohne Eingang; bewusst aktivieren
- **Nachlaufleistung:** optionale, abweichende Leistung während des Nachlaufs

Änderungen werden verzögert zusammengefasst persistent geschrieben, damit nicht
jeder einzelne Slider-Schritt einen NVS-Schreibvorgang erzeugt.

### Eingangsrouting und Logik

Einfache Zusatzregeln verbinden einen Eingang direkt mit einem Ausgang oder einer
Absaugungsaktion. Für umfangreichere Verknüpfungen dient der grafische
**Logik Designer**. Dort stehen unter anderem UND, ODER, NICHT, RS-Latch, Toggle,
TON, TOFF, Puls und Taktgeber zur Verfügung. Bis zu 32 Definitionen können in
LittleFS gespeichert und unabhängig aktiviert werden.
Pro Definition sind 64 Bausteine, 128 Verbindungen und 24000 UTF-8-Bytes erlaubt.
Zur Laufzeit stehen insgesamt 32 unterschiedliche boolesche Ausgangsziele zur
Verfuegung. Die Oberflaeche meldet ueberschrittene Grenzen.
Mehrere Bausteine per Auswahlrahmen markieren und gemeinsam verschieben.
Verbindungen lassen sich mit Strg/Umschalt-Klick oder dem Auswahlwerkzeug auf
Touchgeraeten gemeinsam markieren. Segmentgriffe erlauben manuelle Leitungswege;
manuell gesperrte Wege vor erneutem automatischem Routing entsperren.

Bei Fan/IO und Fan/IO Pro werden im Designer die im Modul gespeicherten Ein-/
Ausgangsaliase verwendet. Simulationseingänge testen die Logik, ohne reale
Hardwareausgänge als Eingang zu missbrauchen.

## Alarmzentrale

Der Master erzeugt die Alarmtexte zentral und verteilt denselben Zustand an Web,
Displays und MQTT. Dadurch sollen Titel, Adresse, Text und Schweregrad überall
identisch sein.

Typische Warnungen:

- kein Haupteingang Absaugung gewählt
- kein Hauptausgang Absaugung gewählt
- JBC-Station oder Weller-Gerätebus getrennt
- Filter bald voll

Typische kritische Alarme:

- eingelerntes Modul offline
- Ausgangs-/Drehzahlfehler
- kritischer JBC- oder Gerätebusfehler
- Filter voll, sofern das Modul dies als Fehler meldet

Gelb kennzeichnet Warnungen, Rot kritische Zustände. Ein Offline-Modul ist daher
rot. Ein optionaler getrennter Stations-/Weller-Link ist eine Warnung, wenn seine
Verbindungsueberwachung aktiv ist. Wird der lokale Link fuer den gewaehlten
Hauptausgang benoetigt, bleibt sein Fehler auch bei abgeschalteter optionaler
Ueberwachung bestehen.

### Verbindungsueberwachung

Die Checkbox je unterstuetztem Modul in der Modulliste steuert optionale lokale
Verbindungswarnungen. Sie stoppt weder die Busabfragen noch versteckt sie ein
OFE-Offline-Modul, einen echten Hardwarefehler oder einen benoetigten Hauptausgang.
Bei Universal/Modbus ist ein fehlendes lokales Geraet von einem aktiven
Antwort-Timeout zu unterscheiden; der Text darf nicht als fehlende
Drehzahlrueckmeldung eines Luefters dargestellt werden.

Seit 2.1.52RC ignorieren die physische und die virtuelle MCU-EVT-Anzeige dieselben
stummgeschalteten optionalen Links. Aktive erforderliche Fehler bleiben sichtbar.
Auf Displays zeigen Karten und Modulliste Warnungen und Fehler mit verschiedenen
Schweregraden; laufende Updates erhalten einen Fortschrittsbalken.

## Status-LEDs

Die Module verwenden zwei SK6812-GRBW-LEDs: **OFE** für Transport und **EVT** für
Modul-/Anlagenzustand. Der Master synchronisiert Phase und globale Helligkeit.

| Effekt | Bedeutung |
|---|---|
| Grün/weiß atmend | OFE-Bus online |
| kurzes grünes Blitzen | Busaktivitaet, soweit im jeweiligen Modul aktiviert; an der MCU deaktiviert |
| Rot atmend | Bus oder lokales Gerät offline |
| Orange blinkend | nicht gekoppelt oder ungültige Adresse |
| Blau/weiß atmend | Firmwareupdate |
| Weiß atmend | lokaler Gerätebus online |
| Grün dauerhaft | JBC Work aktiv |
| Blau atmend | Absaugung aktiv |
| Violett blinkend | Nachlauf |
| Blau blinkend | Continuous |
| Gelb blinkend | Warnung |
| Rot doppelt blinkend | kritischer Alarm |

Die globale Helligkeit und das vollständige Abschalten werden unter
**Netzwerk/System > Status-LEDs** eingestellt. Bei USB-Versorgung kann 100 % je
nach DevKit, Schutzdiode, Kabel und Netzteil zu sichtbaren Spannungseinbrüchen
führen; 80 % ist ein sinnvoller Ausgangspunkt.

## Bus Diagnose

Die Diagnoseseite trennt OFE-Transport und lokalen Gerätebus. Wichtige Werte:

- **Buslast:** geschätzter Anteil der belegten RS485-Zeit
- **TX/RX und Frames/s:** aktueller Durchsatz
- **Latenz/max:** Antwortzeit des Moduls
- **T:** Timeouts seit Boot
- **S:** Sequenzfehler seit Boot
- **C:** CRC-/Transportfehler seit Boot
- **Miss Historie/live:** Gesamtwert und aktuell aufeinanderfolgende Misses

Historische Zähler sind nach einem absichtlichen Abziehen nicht automatisch ein
aktueller Fehler. Entscheidend sind Onlinezustand, Live-Misses und weiter steigende
Zähler. Der Trace sollte nur während einer gezielten Analyse laufen.

## Virtuelle LED-Ausgaenge

Fan/IO ab 1.1.78 und Fan/IO Pro ab 1.1.73 koennen einen adressierbaren LED-Strang
mit bis zu 128 Pixeln und 32 benannten Kanaelen betreiben. Im GPIO-Editor einen
freien Pin aus der Pinliste, LED-Typ und Stromversorgung passend zur Hardware
waehlen, dann Pixelbereiche/Farben/Effekte konfigurieren und speichern.
Die Kanaele erscheinen als Ausgaenge in Zusatzregeln und Logikdesigner.
Details und ein Beispiel: [Fan/IO LED Outputs](FANIO_LED_OUTPUTS.md).

## MQTT und Home Assistant

Der Master veröffentlicht sein eigenes Gerät sowie je ein Gerät pro Modul. Namen,
Aliase, Alarmtexte und Sprache folgen der Masterkonfiguration. Universal- und
Modbus-Entities werden aus dem aktiven Descriptor erzeugt.

Wird MQTT deaktiviert, publiziert der Master den Offline-/Availability-Zustand,
damit alte Werte in Home Assistant nicht fälschlich als aktuell erscheinen. Nach
Profil-, Stationsklassen- oder Ausbauänderungen räumt der Discovery-Abgleich nicht
mehr gültige Entities auf.

Die native Integration fragt die MCU ohne MQTT ab; ein Intervall ab 0,5 Sekunden
ist einstellbar. Das automatische Dashboard und die Uebersichtskarte im eigenen
Dashboard bieten eine bis vier Spalten. Die native Integration und MQTT koennen
unabhaengig voneinander verwendet werden.

Wenn LED-Farben in HA stimmen, aber nur am PC die Effekte stillstehen, zuerst die
Betriebssystem-/Browseroption fuer reduzierte Bewegung pruefen. Diese
Barrierefreiheitseinstellung wird von den Karten respektiert; ein stiller Effekt
ist deshalb nicht automatisch ein Kommunikationsfehler.

## USB und Exporte

Nur MCU-P4 unterstuetzt den USB-Host-Dateimanager. Sicherungen, Profile und
Diagnoseexporte koennen in einem ausgewaehlten Zielordner gespeichert werden.
CSV und JSON enthalten dekodierte Busdaten, sofern der Decoder den Befehl kennt;
Raw-Daten bleiben fuer die Analyse erhalten. **Von USB laden/installieren**
waehlt eine Datei, startet aber noch keinen Import oder Flashvorgang.
Firmware, Zielhardware und Bestaetigung werden separat geprueft. Auf MCU-S3
weiterhin den Browser-Download/-Upload verwenden.

## Entwicklermodus

Der Entwicklermodus blendet Heap-, Loop-, Miss- und OTA-Chunkdetails ein und kann
Modul-Firmwaretyppruefungen fuer Tests uebergehen. MCU-Chip- und
Siliziumrevisionspruefungen bleiben wirksam. Er ist kein Benutzerbetriebsmodus. Das
Passwort schützt die Oberfläche vor versehentlicher Bedienung, ersetzt aber keine
vollständige kryptografische Gerätezugangskontrolle.

## Fehleranalyse in sinnvoller Reihenfolge

1. Alarmzentrale und Bereitschaft prüfen.
2. Modulkarte auf OFE-Bus und lokalen Gerätebus prüfen.
3. Bus Diagnose auf Live-Misses, Timeouts und Latenz prüfen.
4. Bei Universal/Modbus Descriptor und Gerätebus-Trace kontrollieren.
5. Bei MQTT zuerst Masterverbindung und Availability prüfen, danach Discovery.
6. Erst nach der Zustandsaufnahme neu starten; sonst gehen seit Boot geführte
   Diagnosezähler verloren.

