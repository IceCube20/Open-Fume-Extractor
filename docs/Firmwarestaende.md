# FirmwarestÃ¤nde

Diese Datei wird aus den Versionsmakros der Sketches erzeugt. Manuelle Ã„nderungen
an der Tabelle werden beim nÃ¤chsten Lauf von `tools/update_firmware_docs.ps1` Ã¼berschrieben.

Stand: **2026-11-15**

| Firmware | Version | Signaturziel | Werksadresse | Hardware |
|---|---:|---|---:|---|
| [MCU-S3](../OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src/MasterBuildConfig.h) | `2.1.9RC` | `MASTER` | `0x01` | ESP32-S3, 16 MB Flash, 8 MB OPI-PSRAM, 16 modules |
| [MCU-P4](../OpenFumeExtractorMaster/OpenFumeExtractorMCU_P4/src/MasterBuildConfig.h) | `2.1.9RC` | `MASTER` | `0x01` | Waveshare ESP32-P4 PRE-V3, 16 MB Flash, 32 MB PSRAM, 32 modules |
| [JBC FAE Bus](../Module/JbcBusModule/JbcBusModule.ino) | `1.1.70RC` | `JBC_BUS` | `0x10` | ESP32 DevKit V1 |
| [JBC USB](../Module/JbcUsbModule/JbcUsbModule.ino) | `1.1.84RC` | `JBC_USB` | `0x10` | ESP32-S3, USB Host |
| [Fan/IO](../Module/FanIoModule/FanIoModule.ino) | `1.1.76RC` | `FAN_IO` | `0x20` | ESP32 DevKit V1 |
| [Fan/IO Pro](../Module/FanIoProModule/FanIoProModule.ino) | `1.1.71RC` | `FAN_IO_PRO` | `0x20` | ESP32 DevKit V1 |
| [Weller Zero Smog](../Module/WellerZeroSmogModule/WellerZeroSmogModule.ino) | `1.1.84RC` | `WELLER_ZERO_SMOG` | `0x30` | ESP32 DevKit V1 |
| [Display 320x480](../Module/DisplayModule_320x480/DisplayModule_320x480.ino) | `1.4.37RC` | `DISPLAY_320X480` | `0x40` | ESP32-S3 / JC3248W535C_I_Y |
| [Display 800x480](../Module/DisplayModule_800x480/DisplayModule_800x480.ino) | `1.4.53RC` | `DISPLAY_800X480` | `0x40` | ESP32-S3 / Guition JC8048W550 |
| [Display P4 800x480](../Module/DisplayModule_P4_800x480/DisplayModule_P4_800x480.ino) | `1.0.16RC` | `DISPLAY_P4_800X480` | `0x40` | ELECROW P4 PRE-V3 |
| [Display ST7796 320x480](../Module/DisplayModule_ST7796_320x480/DisplayModule_ST7796_320x480.ino) | `1.0.47RC` | `DISPLAY_ST7796_320X480` | `0x40` | ESP32-S3 / ST7796 320x480 |
| [Universal RS232](../Module/UniversalRs232Module/UniversalRs232Module.ino) | `1.0.74beta` | `UNIVERSAL_RS232` | `0x50` | ESP32 DevKit V1 |
| [Modbus RTU](../Module/ModbusRtuModule/ModbusRtuModule.ino) | `1.0.61beta` | `MODBUS_RTU` | `0x60` | ESP32 DevKit V1 |

## Hinweise

- Die Adresse ist die Werkseinstellung. Weitere Module derselben Familie erhalten fortlaufende Adressen innerhalb ihres Familienbereichs.
- Alle Displayvarianten beginnen bei `0x40`; sie sind durch Capability und Firmware-Signatur eindeutig unterscheidbar.
- Hardwareversion `0x0100` entspricht Revision 1.0 der derzeitigen Moduldefinitionen.
- Vor einem Update muss das erkannte Signaturziel zum ausgewÃ¤hlten ZielgerÃ¤t passen. Nur der Entwicklermodus darf diese PrÃ¼fung bewusst Ã¼bergehen.
- Der MCU-Chiptyp S3/P4 wird zusÃ¤tzlich im tatsÃ¤chlichen ESP-Image-Header geprÃ¼ft. Diese PrÃ¼fung bleibt auch im Entwicklermodus aktiv.

## Aktualisieren

```powershell
.\tools\update_firmware_docs.ps1
```
