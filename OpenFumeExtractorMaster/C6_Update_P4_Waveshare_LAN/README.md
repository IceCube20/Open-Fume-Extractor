# ESP32-C6 Recovery Through the P4

This standalone Arduino sketch updates the ESP-Hosted C6 firmware through the
P4's Ethernet connection and SDIO link. It is intended for the **Waveshare
ESP32-P4-WIFI6-POE-ETH, P4 V3.1+ / V3.2**, with the wiring encoded in the sketch.
It is not a universal C6 UART programmer or a sketch for other P4 carriers.

## Before Starting

- Prefer the normal OFE MCU C6 updater when it can communicate with the C6.
- Back up the MCU configuration and use a reliable power supply and Ethernet.
- Install **Arduino-ESP32 3.3.12**. The sketch deliberately requires that version;
  its matching ESP-Hosted firmware is **2.12.13**.
- Select **ESP32P4 Dev Module**, the **V3/post-V3** chip variant, **32 MB flash**,
  and enabled PSRAM. Select the serial settings appropriate to your USB port.

## Procedure

1. Flash `C6_Update_P4_Waveshare_LAN.ino` onto the **P4, not the C6**. This
   temporarily replaces the OFE MCU application.
2. Connect Ethernet and open the serial monitor at **115200 baud**. Wait for a
   DHCP address and review the detected host/C6 versions and official image URL.
3. Only after checking those details, send **UPDATE** followed by a newline.
   Setup does not install C6 firmware automatically.
4. Do not disconnect power during the transfer. A successful update restarts P4.
5. Flash the matching **MCU-P4 PoE** firmware back onto the P4. The recovery
   sketch does not restore the MCU application automatically.

No WLAN credentials are needed. The download uses the official Arduino
ESP-Hosted updater over Ethernet; the C6 is programmed through SDIO.
The installed C6 firmware must still support OTA. A damaged or sufficiently old
C6 may require a separate direct UART recovery procedure. A successful compile
does not guarantee recovery from every installed C6 firmware version.
