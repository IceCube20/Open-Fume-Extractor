# Universal-/Modbus-Builder

Mit dem Builder beschreibt der Master Geräte, deren Protokoll nicht fest in der
OFE-Firmware implementiert ist. Das Profil wird an das Universal-RS232- oder
Modbus-RTU-Modul übertragen und dort persistent gespeichert. Weboberfläche,
Displays, Logik-Designer und MQTT verwenden anschließend denselben Descriptor.

## Grenzen

- maximal **32 Profil-Entities** je Universal-/Modbus-Modul
- maximal **8192 Byte Profiltext**
- Entity-IDs beginnen im Profilbereich bei 20
- System-/Debug-Entities bleiben unabhängig vom Profil erhalten

## Profil laden

1. Universal- oder Modbus-Modul auf der Master-Statusseite öffnen.
2. Profil-Builder beziehungsweise Expertenmodus öffnen.
3. Ein Profil aus `profiles/` laden oder neu anlegen.
4. Profil prüfen und an das Modul übertragen.
5. Descriptor neu laden und kontrollieren, ob `descriptor_truncated=0` und
   `profile_active=yes` gemeldet werden.

Beim Universalmodul werden die Werte nach dem Übertragen automatisch neu gelesen.
Beim Modbusmodul können Register- und Pollingwerte zusätzlich mit **Aus Modul
laden** synchronisiert werden.

## Gemeinsame Kopfdaten

```text
profile=Anzeigename
station=Gerätename
baud=9600
frame=8N1
protocol=...
checksum=...
```

Universalprofile können zusätzlich `line_end` verwenden. Modbusprofile enthalten
stattdessen Slave-Adresse und Standard-Pollzeit.

## Entity-Grundaufbau

```text
entity.1.type=number
entity.1.name=Leistung
entity.1.key=power
entity.1.access=rw
entity.1.role=main_output_power
entity.1.unit=%
```

Wichtige Felder:

| Feld | Bedeutung |
|---|---|
| `type` | `binary_sensor`, `sensor`, `number`, `switch`, `select` oder `text` |
| `name` | sichtbarer Name in Web, Display und MQTT |
| `key` | stabiler technischer Schlüssel |
| `access` | `ro`, `wo` oder `rw` |
| `role` | optionale OFE-Rolle für Routing und Hauptausgang |
| `unit` | Einheit wie `%`, `rpm`, `°C` |
| `poll_ms` | Abfrageintervall in Millisekunden |
| `min/max/step` | Wertebereich für Number-Entities |
| `scale` | Skalierung zwischen Roh- und Anzeigewert |
| `map` | Rohwert-zu-Text-Abbildung |

## OFE-Rollen

| Rolle | Verwendung |
|---|---|
| `main_input` | Signal kann Haupteingang oder Logik-Eingang sein |
| `main_output_enable` | Ein/Aus des Absaugungsausgangs |
| `main_output_power` | Leistungswert des Absaugungsausgangs |
| `output_enable` | zusätzlicher schaltbarer Ausgang |

Ein vollständiger Hauptausgang besitzt normalerweise eine Enable- und eine
Power-Entity. Profile mit nur einer schreibbaren Power-Entity können ebenfalls
verwendet werden, sofern die Rollen passend gesetzt sind.

## Universal RS232

Universalprofile beschreiben Poll-, Match- und Set-Telegramme. Beispiel:

```text
entity.1.type=number
entity.1.name=Leistung
entity.1.key=power
entity.1.access=rw
entity.1.role=main_output_power
entity.1.poll=S
entity.1.match=S###
entity.1.min=30
entity.1.max=100
entity.1.step=1
entity.1.set=d{value:03}
```

`{value:03}` formatiert den Wert dreistellig. Binäre Telegramme können über die
Hex-Felder des Builders beschrieben werden. Für Geräte, die ihren Schaltbefehl
regelmäßig benötigen, stehen `repeat_on_ms` und `repeat_off_ms` zur Verfügung.

Zeitwerte können mit `time_base` und `time_display` nachbehandelt werden:

```text
entity.5.scale=10
entity.5.time_base=m
entity.5.time_display=dhm
```

Damit wird der Rohwert zuerst skaliert und anschließend beispielsweise als
`1d 16h 50m` ausgegeben. Select-Entities benötigen gleich viele `options` und
`values`:

```text
entity.7.options=8 h|16 h|1 Tag|2 Tage
entity.7.values=48|96|144|288
```

## Modbus RTU

Jede Entity legt Register und Funktionscode fest:

```text
entity.3.type=number
entity.3.name=Lüfterleistung
entity.3.key=fan_power
entity.3.access=rw
entity.3.role=main_output_power
entity.3.reg=0x0001
entity.3.func=write_holding
entity.3.read_func=read_holding
entity.3.min=10
entity.3.max=100
entity.3.step=1
entity.3.poll_ms=500
```

Unterstützte typische Kombinationen:

- Discrete Input: `read_discrete`
- Coil lesen/schreiben: `read_coil`, `write_coil`
- Input Register: `read_input`
- Holding Register: `read_holding`, `write_holding`

Die Registeradresse ist die Protokolladresse, nicht die in manchen Handbüchern
ab 1 dargestellte 30001-/40001-Nummer. Im Zweifel ein bekanntes Register mit dem
lokalen Trace verifizieren.

## Builder-Nachbehandlung

Skalierung, Bitmasken, Text-Mapping und Zeitformatierung sind Teil des Descriptors.
Master, Display und MQTT sollen deshalb immer den nachbehandelten Anzeigewert
verwenden, nicht den rohen Register-/Telegrammwert.

Nach einer Profiländerung werden nicht mehr vorhandene Entities aus Cache und
Home-Assistant-Discovery entfernt. Falls ein alter Wert sichtbar bleibt:

1. Descriptor neu laden.
2. Prüfen, ob das Modul das Profil bestätigt hat.
3. MQTT-Verbindung und Discovery-Aufräumen kontrollieren.
4. Erst danach Modul oder Display neu starten.

## Diagnosewarnungen

Universal 1.0.77beta und Modbus 1.0.64beta melden zusaetzlich zur lokalen
Verbindungswarnung (0x0001) entprellte Diagnosewarnungen:

- 0x0800: wiederholte Antwort-Timeouts.
- 0x1000: gehaeufte Pruefsummen-/CRC-Fehler.
- 0x2000: wiederholte Exception-Antworten, nur Modbus.

Drei Vorfaelle innerhalb von zehn Sekunden aktivieren die jeweilige Warnung.
Drei gesunde Antworten/Pruefsummen loeschen sie; nach 30 Sekunden ohne neuen
Vorfall verfaellt sie ebenfalls. Einzelne Fehler sind weiter in den Zaehlern
und im Trace sichtbar. Modbus-Exceptions gelten als erreichbares Geraet, aber
nicht als erfolgreicher Registerwert; der letzte Empfangsstatus zeigt den Code.

Universal verfolgt nur gesendete Profilabfragen mit Antwortmuster. Passive
Empfangsprofile und reine Sendeprofile erhalten keinen kuenstlichen Timeout.
Der Universal-Timeout betraegt mindestens eine Sekunde und beruecksichtigt
langsame Baudraten. Eine Profil-/UART-Aenderung setzt die Diagnose zurueck.

Die neuen Bits sind Warnungen, keine Filter-/Drehzahlfehler. Ab MCU 2.1.41
werden sie typabhaengig ausgewertet; die aktualisierten Display-Firmwares
zeigen die passenden Klartexte. Alte MCU-/Displaystaende koennen unbekannte
Bits noch als Fehler/Hexwert anzeigen. Die Pflichtueberwachung eines gewaehlten
Hauptausgangs bleibt aktiv, auch bei abgeschalteten Verbindungswarnungen.

## Testablauf Modbus

Ab MCU 2.1.42 unterdrueckt ausgeschaltete Verbindungsueberwachung auch die
optionale Timeoutwarnung. Bei bereits getrennter lokaler Verbindung wird nur
die Verbindungswarnung statt eines zusaetzlichen Timeouts angezeigt. Universal
1.0.78beta und Modbus 1.0.65beta loeschen die Timeoutserie, solange kein gueltiger
lokaler Empfang vorliegt bzw. dieser aelter als 15 Sekunden ist. CRC- und
Exception-Warnungen bleiben echte Protokolldiagnosen. Ein nicht erreichbarer
gewaehlter Hauptausgang bleibt unabhaengig vom Schalter ein Fehler.

1. Einen Modbus-Slave oder PC-Simulator auf die konfigurierte Slave-Adresse setzen.
2. Discrete Inputs, Coils, Input- und Holding-Register mit bekannten Werten füllen.
3. Im Master den lokalen Gerätebus-Trace starten.
4. Lesezugriffe und CRC-Antworten prüfen.
5. Number/Switch im Web ändern und Registerwert im Simulator kontrollieren.
6. Dasselbe Entity in Display, MQTT und Logik-Designer prüfen.
7. Bus unterbrechen: Gerätebusfehler und LED-/Alarmzustand müssen erscheinen.

## Vorlagen

Im Ordner `profiles/` liegen unter anderem:

- `Universal_Profile_All_Entity_Types_v1.ofeprofile`
- `Universal_Transform_Bitmask_Time_v1.ofeprofile`
- `Weller_Zero_Smog_Shield_Pro_v6.ofeprofile`
- `Modbus_RTU_Generic_IO_v1.ofeprofile`
- `Modbus_Generic_Transform_TextMap_v1.ofeprofile`

Zum Start eine passende Vorlage kopieren und in kleinen Schritten erweitern. Nach
jeder neuen Entity Descriptor, Anzeige und Schreibpfad testen.

