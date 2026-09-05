# OFE-Firmware signieren

## Grafische Windows-App

`Firmware Signer starten.bat` öffnet die lokale Signier-App im Browser. Eine
Firmware-BIN kann in das Ablagefeld gezogen oder über die Dateiauswahl geöffnet werden.
Die App erkennt Ziel und Version, erzeugt eine `.signed.bin` und prüft SHA-256
und Ed25519 direkt nach dem Signieren erneut.

Die App bindet ausschließlich an `127.0.0.1`. Firmware und privater Schlüssel
werden nur im Arbeitsspeicher des lokalen Signierprozesses verarbeitet und nicht
ins Netzwerk übertragen.

## Kommandozeile

```powershell
python tools\ofe_firmware_sign.py sign `
  --key "C:\Users\User\Documents\Open Fume Extractor Signing Key\ofe_ed25519_private.pem" `
  --input "Pfad\zur\Firmware.ino.bin"
```

Das Werkzeug liest Ziel und Version aus `OFE_FW_SIG:v1`, hängt den signierten
Authentifizierungs-Trailer an und erzeugt standardmäßig eine `.signed.bin`.

## Sicherheit

Der private Ed25519-Schlüssel darf nicht ins Repository, in Firmware-Dateien
oder in öffentliche Backups gelangen. Für die Verifikation ist im Master nur
der öffentliche Schlüssel eingebettet.
