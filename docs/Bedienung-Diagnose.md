# Bedienung und Diagnose

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
rot; ein nur getrennter Stations-/Weller-Link bleibt gelb.

## Status-LEDs

Die Module verwenden zwei SK6812-GRBW-LEDs: **OFE** für Transport und **EVT** für
Modul-/Anlagenzustand. Der Master synchronisiert Phase und globale Helligkeit.

| Effekt | Bedeutung |
|---|---|
| Grün/weiß atmend | OFE-Bus online |
| kurzes grünes Blitzen | Busaktivität |
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
**Netzwerk Setup > Status LEDs** eingestellt. Bei USB-Versorgung kann 100 % je
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

## MQTT und Home Assistant

Der Master veröffentlicht sein eigenes Gerät sowie je ein Gerät pro Modul. Namen,
Aliase, Alarmtexte und Sprache folgen der Masterkonfiguration. Universal- und
Modbus-Entities werden aus dem aktiven Descriptor erzeugt.

Wird MQTT deaktiviert, publiziert der Master den Offline-/Availability-Zustand,
damit alte Werte in Home Assistant nicht fälschlich als aktuell erscheinen. Nach
Profil-, Stationsklassen- oder Ausbauänderungen räumt der Discovery-Abgleich nicht
mehr gültige Entities auf.

## Entwicklermodus

Der Entwicklermodus blendet Heap-, Loop-, Miss- und OTA-Chunkdetails ein und kann
Firmwaretypprüfungen für Tests übergehen. Er ist kein Benutzerbetriebsmodus. Das
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

