# Hardware und Pinbelegung

Die Tabellen bilden den aktuellen Firmwarestand ab. Sie ersetzen keinen
Schaltplan und keine Schutzbeschaltung. Alle GPIO-Angaben beziehen sich auf
3,3-V-Logik. Stand: Release 2.1.52RC. GPIO-Nummern sind keine physischen
Stecker-Pinnummern. Gespeicherte Fan/IO-Hardwareprofile koennen die genannten
Standardwerte ersetzen. Die [englische Pinout-Uebersicht](PINOUT.md) beschreibt
dieselben Anschlussgruppen.

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

## MCU-S3

| Funktion | GPIO / Anschluss | Hinweis |
|---|---:|---|
| OFE RS485 RX | 17 | UART-Eingang vom RS485-Wandler |
| OFE RS485 TX | 16 | UART-Ausgang zum RS485-Wandler |
| SK6812 Status-LEDs | 4 | zwei GRBW-LEDs, 800 kHz |
| Native USB-C | Boardanschluss | native USB/OTG-Schnittstelle |
| UART USB-C | Boardanschluss | USB-UART-Wandler des DevKits |

Die beiden frontseitigen USB-C-Öffnungen des Mastergehäuses sollen als
**Native USB** und **UART USB** beschriftet werden.

## MCU-P4: Waveshare PRE-V3 und PoE V3.1+

Beide MCU-Profile verwenden dieselben OFE-Anschlusspins, aber getrennte Firmware.
Das PoE-Profil umfasst das getestete V3.2-Board. Die beiden Waveshare-MCUs und
das ELECROW-P4-Display sind nicht austauschbar.

| Funktion | P4 GPIO / Anschluss | Hinweis |
|---|---:|---|
| OFE RS485 RX | 21 | RO des Wandlers -> MCU RX |
| OFE RS485 TX | 20 | MCU TX -> DI des Wandlers |
| zwei externe SK6812-GRBW-Status-LEDs | 4 | nicht die Power-LED des Boards |
| C6 SDIO CLK / CMD | 18 / 19 | interne, reservierte Verbindung |
| C6 SDIO D0 / D1 / D2 / D3 | 14 / 15 / 16 / 17 | intern, reserviert |
| C6 Reset | 54 | intern, reserviert |
| Ethernet-PHY | IP101, Adresse 1 | interne RMII-Verbindung |
| Ethernet MDC / MDIO / Reset | 31 / 52 / 51 | intern, reserviert |
| USB-Stick | nativer USB-Host-Anschluss | nicht der USB-UART-Programmieranschluss |

Einen 3,3-V-kompatiblen UART-RS485-Wandler verwenden. A/B niemals direkt mit den
GPIOs verbinden. Die gelieferte Firmware verwendet automatische Senderichtung,
keinen zusaetzlichen DE/RE-Pin. Pro OFE-Bussegment nur eine MCU betreiben.

Die Boardkonfiguration sperrt GPIO14..19, 28..31, 34..38 und 49..54; beim PoE
zusaetzlich GPIO6, 7..13 und 39..45. Nicht gesperrt bedeutet nicht automatisch
frei oder am Stecker erreichbar. Interne Funk-, Ethernet- und andere
Boardverbindungen duerfen nicht umverdrahtet werden.
Details: [PRE-V3](Master-P4-Waveshare.md), [PoE V3](MCU-P4-PoE-V3.md).

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

Die Tabelle zeigt die Werkseinstellungen. Gespeicherte GPIO-Profile koennen
davon abweichen; Hardwareeditor und Modul pruefen reservierte Pins und Konflikte.

| Funktion | GPIO | Logik |
|---|---:|---|
| Hauptausgang Enable | 18 | aktiv High |
| Lüfter PWM | 19 | PWM |
| Lüfter Tacho | 21 | zwei Impulse/Umdrehung |
| IN1 | 32 | digitaler Eingang |
| IN2 | 33 | digitaler Eingang |
| OUT1 | 22 | digitaler Ausgang |
| OUT2 | 23 | digitaler Ausgang |

Weitere konfigurierbare Funktionen:

| Funktion | GPIO-Zuordnung | Hinweis |
|---|---|---|
| adressierbarer WS2812-/SK6812-Strang | freier ausgangsfaehiger GPIO aus der Pinliste | standardmaessig aus; ein Strang, bis zu 128 Pixel / 32 virtuelle Kanaele |
| Fan/IO Pro Analog-Drucksensor | konfigurierbarer Eingang | ab Werk kein Sensorpin zugeordnet |
| Fan/IO Pro SDP3x-Drucksensor | konfigurierbare SDA/SCL-Pins | ab Werk keine Sensorpins zugeordnet |

Der virtuelle Strang ist getrennt von den beiden OFE-/EVT-Status-LEDs auf GPIO4.
GPIO4, OFE RX/TX, reservierte und bereits verwendete Pins nicht fuer dessen
Datensignal verwenden. GPIO34..39 sind nur Eingaenge und besitzen keine internen
Pull-up-/Pull-down-Widerstaende; bei Verwendung passende externe Widerstaende
vorsehen. Fuer LEDs sind sie ungeeignet. Stromversorgung und Pegelanpassung des
Strangs passend auslegen; ein GPIO ist keine LED-Stromversorgung.
Anleitung: [LED-Ausgaenge](FANIO_LED_OUTPUTS.md).

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

## Display ST7796 320x480

Ziel: ST7796-SPI-Panel mit FT63x6-Touch.

| Funktion | GPIO |
|---|---:|
| OFE RS485 RX / TX | 7 / 15 |
| optionale externe Status-LED | 46 (ab Werk deaktiviert) |
| Hintergrundbeleuchtung PWM | 9 |
| Display SCK / MOSI | 10 / 11 |
| Display CS / DC / RESET | 12 / 13 / 14 |
| Touch SDA / SCL | 18 / 3 |
| Touch INT / RESET | 17 / 8 |

GPIO16 bleibt wegen des SD-CS-Anschlusses auf diesem Board unbenutzt.

## Display P4 800x480

Ziel: ELECROW CrowPanel Advanced 5 Zoll V1.0, RGB/ST7262 und GT911,
PRE-V3-P4-Buildprofil. Die folgenden Panel-/Touch-/Funkpins sind interne
Boardverbindungen, keine freien Anschlussplaetze.

| Funktion | GPIO / Anschluss |
|---|---:|
| OFE RS485 UART RX / TX | 48 / 47 |
| gemeinsames I2C SDA / SCL | 45 / 46 |
| Touch RESET / INT | 36 / 42 |
| DE / VSYNC / HSYNC / PCLK | 2 / 41 / 40 / 3 |
| RGB-Daten, Treiberreihenfolge D0..D15 | 8, 7, 6, 5, 4, 14, 13, 12, 11, 10, 9, 19, 18, 17, 16, 15 |
| Hintergrundbeleuchtung | STC8 an I2C `0x2F`, kein direkter PWM-Pin |
| C6 SDIO CLK / CMD | 53 / 54 |
| C6 SDIO D0 / D1 / D2 / D3 | 52 / 51 / 50 / 49 |
| C6 Reset | 20 |

Beim P4-Display ist ab Werk kein externer Status-LED-Pin zugeordnet
(`OFE_STATUS_LED_PIN = -1`). Die virtuelle OFE-/EVT-Anzeige entspricht keinem
freien physischen GPIO.

Der UART-Stecker hat keinen RS485-Wandler. Einen passenden 3,3-V-Wandler
zwischenschalten; RS485 A/B nicht direkt an GPIO48/47 legen. Die Funkpins
unterscheiden sich ausdruecklich von den Waveshare-MCUs.

## Mechanik

Der aktuelle Kassettenentwurf verwendet für Master und Module ein gemeinsames
Außenmaß von **30 x 100 x 100 mm**. Die Frontblende ist größer als die Kassette,
damit sie am Hauptrahmen verschraubt werden kann. USB und RS485-Schraubklemme
bleiben bei den Modulen hinten zugänglich; beim Master liegen beide USB-C-Anschlüsse
vorne. Diese Gehaeuseanordnung betrifft die MCU-S3; Anschlusspositionen der
P4-Traegerboards unterscheiden sich.

