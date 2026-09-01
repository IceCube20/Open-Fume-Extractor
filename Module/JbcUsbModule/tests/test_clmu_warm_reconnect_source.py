from pathlib import Path

src = (Path(__file__).resolve().parents[1] / "JbcUsbModule.ino").read_text()

# Warm OFE reboot must generate a modem-output edge even when the CP210x stayed
# powered. Keep passive low probes around one deliberate active wake probe.
assert "cp210x_reopen_uart_for_discovery(bool modem_outputs_active = false)" in src
assert "const bool wake_modem_outputs = (detect_reopen_count % 3U) == 1U;" in src
assert "usb_serial_reopen_for_discovery(wake_modem_outputs)" in src
assert "case USB_SERIAL_BACKEND_CP210X: return cp210x_reopen_uart_for_discovery(modem_outputs_active);" in src
assert "if (modem_outputs_active) mhs |=" in src
assert "CP210X_CONTROL_DTR | CP210X_CONTROL_RTS" in src
# The close half of every reopen must explicitly force both lines low first.
needle = "CP210X_CONTROL_WRITE_DTR | CP210X_CONTROL_WRITE_RTS),\n                        nullptr, 0);\n  delay(8);"
assert needle in src
assert "#define OFE_MODULE_FW_PATCH 75" in src
