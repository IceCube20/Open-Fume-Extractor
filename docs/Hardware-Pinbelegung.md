# Hardware und Pinbelegung

Die Tabellen bilden den aktuellen Firmwarestand ab. Sie ersetzen keinen
Schaltplan und keine Schutzbeschaltung. Alle GPIO-Angaben beziehen sich auf
3,3-V-Logik.

## Gemeinsamer OFE-Bus

| Signal | Beschreibung |
|---|---|
| `A` / `A+` | RS485 Datenleitung A |
| `B` / `B-` | RS485 Datenleitung B |
| `GND` | gemeinsame Signalmasse |
| `VCC` | modulabhängige Versorgung; nicht mit einem GPIO verbinden |

Der verwendete TTL-RS485-Wandler übernimmt die Senderichtung automatisch. Deshalb
gibt es in der Firmware keinen separaten DE/RE-Pin. Die Schraubklemme des Wandlers
soll bei den Kassetten hinten zugänglich bleiben.

## Master ESP32-S3

| Funktion | GPIO / Anschluss | Hinweis |
|---|---:|---|
| OFE RS485 RX | 17 | UART-Eingang vom RS485-Wandler |
| OFE RS485 TX | 16 | UART-Ausgang zum RS485-Wandler |
| SK6812 Status-LEDs | 4 | zwei GRBW-LEDs, 800 kHz |
| Native USB-C | Boardanschluss | native USB/OTG-Schnittstelle |
| UART USB-C | Boardanschluss | USB-UART-Wandler des DevKits |

Die beiden frontseitigen USB-C-Öffnungen des Mastergehäuses sollen als
**Native USB** und **UART USB** beschriftet werden.

## Standardmodule auf ESP32 DevKit V1

Diese Grundbelegung gilt für JBC Bus, Weller, Fan/IO, Fan/IO Pro, Universal RS232
und Modbus RTU.

| Funktion | GPIO |
|---|---:|
| OFE RS485 RX | 26 |
| OFE RS485 TX | 25 |
| zwei SK6812 Status-LEDs | 4 |

### JBC FAE Bus

| Funktion | GPIO | Parameter |
|---|---:|---|
| JBC RX | 17 | lokale JBC-UART |
| JBC TX | 16 | lokale JBC-UART |

### Weller Zero Smog

| Funktion | GPIO | Parameter |
|---|---:|---|
| Weller RX | 17 | 1200 Baud |
| Weller TX | 16 | 1200 Baud |

Für die physische Schnittstelle wird das MAX3232-/D-Sub-9-Modul verwendet.

### Universal RS232

| Funktion | GPIO | Standard |
|---|---:|---|
| lokaler RX | 17 | 9600 Baud, 8N1 |
| lokaler TX | 16 | 9600 Baud, 8N1 |

Baudrate, Frame, Protokoll, Prüfsumme und Zeilenende werden durch das geladene
Profil überschrieben. Für echte RS232-Pegel ist ein MAX3232 erforderlich; TTL
darf nicht direkt an eine RS232-Buchse gelegt werden.

### Modbus RTU

| Funktion | GPIO | Standard |
|---|---:|---|
| lokaler Modbus RX | 17 | 9600 Baud, 8N1 |
| lokaler Modbus TX | 16 | 9600 Baud, 8N1 |

Der lokale Modbus-Gerätebus ist elektrisch vom OFE-RS485-Bus zu unterscheiden.
Beide Busse benötigen jeweils ihren eigenen Transceiver.

### Fan/IO und Fan/IO Pro

| Funktion | GPIO | Logik |
|---|---:|---|
| Hauptausgang Enable | 18 | aktiv High |
| Lüfter PWM | 19 | PWM |
| Lüfter Tacho | 21 | zwei Impulse/Umdrehung |
| IN1 | 32 | digitaler Eingang |
| IN2 | 33 | digitaler Eingang |
| OUT1 | 22 | digitaler Ausgang |
| OUT2 | 23 | digitaler Ausgang |

Die endgültige Lüfter-Leistungselektronik ist noch nicht als feste Hardwareversion
freigegeben. Pegelanpassung, Freilauf-/Überspannungsschutz, Sicherung und zulässige
Lasten müssen im finalen Schaltplan festgelegt werden.

## JBC USB auf ESP32-S3

| Funktion | GPIO / Anschluss | Hinweis |
|---|---:|---|
| OFE RS485 RX | 17 | OFE-Bus |
| OFE RS485 TX | 16 | OFE-Bus |
| SK6812 Status-LEDs | 4 | zwei GRBW-LEDs |
| USB D- | 19 | native USB-Host-Schnittstelle |
| USB D+ | 20 | native USB-Host-Schnittstelle |
| USB VBUS | geschützte 5 V | strombegrenzt ausführen |

Das Modul arbeitet als USB-Host für den CP210x-Adapter der JBC-Station. Die
USB-UART-Seite hinter dem CP210x läuft mit 500000 Baud, 8E1.

## Display 320x480

Zielgerät: JC3248W535C_I_Y mit AXS15231B und QSPI.

| Funktion | GPIO |
|---|---:|
| OFE RS485 RX / TX | 18 / 17 |
| Status-LED | 46 |
| Hintergrundbeleuchtung | 1 |
| Touch SDA / SCL | 4 / 8 |
| Touch RST / INT | 12 / 11 |
| Display CS / SCK | 45 / 47 |
| Display MOSI / MISO | 21 / 48 |
| Display Quad WP / HD | 40 / 39 |

## Display 800x480

Zielgerät: Guition JC8048W550 mit parallelem RGB565-Panel und GT911.

| Funktion | GPIO |
|---|---:|
| OFE RS485 RX / TX | 18 / 17 |
| Hintergrundbeleuchtung | 2 |
| Touch SDA / SCL | 19 / 20 |
| Touch RST | 38 |
| DE / VSYNC / HSYNC / PCLK | 40 / 41 / 39 / 42 |
| Rot R0..R4 | 45, 48, 47, 21, 14 |
| Grün G0..G5 | 5, 6, 7, 15, 16, 4 |
| Blau B0..B4 | 8, 3, 46, 9, 1 |

Dieses Board besitzt keine separate OFE-Status-LED in der aktuellen Firmware.
Das RGB-Panel ist timingkritisch; Build und SDK nicht ohne erneuten Displaytest
ändern.

## Mechanik

Der aktuelle Kassettenentwurf verwendet für Master und Module ein gemeinsames
Außenmaß von **30 x 100 x 100 mm**. Die Frontblende ist größer als die Kassette,
damit sie am Hauptrahmen verschraubt werden kann. USB und RS485-Schraubklemme
bleiben bei den Modulen hinten zugänglich; beim Master liegen beide USB-C-Anschlüsse
vorne.

