# OFE C6 compatibility catalog

Starting with master 2.0.101RC-P4-Preview, the C6 update panel checks the OFE
catalog once when the Updates page opens. The check button repeats this on
demand. The browser fetches the public JSON directly over HTTPS, with no OFE
credentials or CSRF token. There is no background Internet polling on the P4.

## Publication

Source: `updates/hosted-c6.json` in the main OFE repository. The firmware reads
the raw GitHub URL, not the latest Espressif release and not the web-flasher
catalog. The local publication checkout contains the same JSON; it must be
committed and pushed to `main` before the public check works. Until then a 404
is an unavailable online check, not a firmware or connectivity failure.

The catalog workflow runs the actual browser selector against the checked-in
catalog and firmware pins. Do not add an approval just because Espressif has
published a newer version. Review the exact board, SDIO transport, host SDK,
chip, image size and SHA-256 first.

## Schema 1

Each release has a unique ID, `approved` or `withdrawn` status, board ID,
`sdio` transport, exact Arduino/IDF/Hosted host versions, and the official C6
image URL, version, size and full-file SHA-256. Images are not redistributed.
The initial approval is Waveshare P4 Module DEV KIT PRE-V3, Arduino 3.3.12,
IDF 5.5.5, Hosted/C6 2.12.13. The owner reported a successful on-device SDIO
update from 2.12.3 to 2.12.13. This does not by itself verify every WiFi/BLE
operation or recovery procedure.

At most 32 entries and 32 KiB are accepted. Unknown schemas, duplicate IDs or
ambiguous approvals for a host combination, malformed versions/URLs/hashes
and mismatched Hosted/C6 versions invalidate the response.

## Trust boundary

The catalog is advisory. It never changes the firmware's pinned image URL,
size, hash or SDK guard, never uploads an image and never starts installation.
An online matching approval must also match all local image pins. A newer
approval for another SDK is displayed as requiring an updated master SDK.
Unknown installed versions are not reported as a confirmed update.

An unavailable, malformed or nonmatching catalog leaves the existing local
approval available. A withdrawn entry is no longer an online approval; it is
not a remotely enforced revocation of firmware already embedded in a master.
Enforced revocation requires a new master release. This preserves offline
operation and prevents a catalog edit or compromised metadata from granting
permission to flash an arbitrary image.

## Releasing a new combination

1. Verify the official image and host SDK, then update the master pins and
   add its exact catalog combination in the same reviewed change.
2. Run the catalog, native updater and browser tests, then the P4 build.
3. Publish the master and catalog. Existing masters can report that a newer
   SDK is required but cannot install an image they have not approved locally.
4. After updating the master, the C6 update still requires stable Ethernet,
   recovery availability and a separate explicit installation confirmation.

Native C6 write/hash/lifecycle protections are unchanged from 2.0.100.
No board was flashed during catalog development.

## P4 status LED default

At the owner's request, the public P4 master also enables the external OFE
status LED on GPIO4 by default from 2.0.101. This is not the board's power LED.
Local builds can still override the setting in `MasterBoardLocalConfig.h`;
the owner's existing enabled setting is retained. Private/local configuration
files are not published. The S3 status LED configuration is unchanged.

## Verification

The final local P4 build is 2.0.101RC-P4-Preview, with the shared GPIO4 LED
default enabled and the existing enabled local override retained. It uses
3,315,184 of 3,342,336 application-slot bytes (27,152 bytes spare), with
68,180 bytes reported static RAM. Application SHA-256:
`583b922a6f48af41cd61e0f961c7d3ac7846cab4be798ec8cbd7769813f659f5`.
ELF/BIN checks confirm the version and preserved software BLE ECC/RNG fix.

All 320 Python regressions passed. Native tests also passed against the
publication checkout without a local board configuration, confirming the LED
default. Browser tests exercised German/English at 320/390/768/1440 pixels,
offline/404 responses, empty and future-SDK catalogs, unknown installed
versions, no automatic installation, and the existing staged C6 workflow.
S3 compiler checks passed with the MQTT worker enabled and disabled. Browser
tests used mocked servers; the new catalog has not yet been published or
tested on a physical master. No device was flashed or GitHub push performed.
