# OFE C6 compatibility catalog

Starting with master 2.0.101RC-P4-Preview, the C6 update panel checks the OFE
catalog once when the Updates page opens. The check button repeats this on
demand. The browser fetches the public JSON directly over HTTPS, with no OFE
credentials or CSRF token. There is no background Internet polling on the P4.

## Publication

Source since MCU 2.1.42: `updates/hosted-c6-v2.json` in the main OFE repository. The firmware reads
the raw GitHub URL, not the latest Espressif release and not the web-flasher
catalog. The legacy `updates/hosted-c6.json` retains only equal host/C6 2.12.13
combinations: older MCU selectors reject mixed host/C6 versions even for
unrelated boards. The new URL preserves their online checks. The upstream
watcher and current validation tests use the new catalog.
The local publication checkout contains the same JSON; it must be
committed and pushed to `main` before the public check works. Until then a 404
is an unavailable online check, not a firmware or connectivity failure.

The catalog workflow runs the actual browser selector against the checked-in
catalog and firmware pins. Do not add an approval just because Espressif has
published a newer version. Review the exact board, SDIO transport, host SDK,
chip, image size and SHA-256 first.

## Automatic Upstream Watch

The separate `Watch Espressif C6 images` GitHub Actions workflow is scheduled
daily at **06:23 UTC**, can be started manually in Actions, and also runs when
its script, tests or the approval catalog change on `main`.

It reads Espressif's official `arduino-esp32` hosted-image directory at an
immutable `gh-pages` commit. Newer numeric ESP32-C6 image versions are downloaded
with bounded size/time limits. The ESP image header, chip ID, actual size and
Git blob are checked; SHA-256 is computed for the review notice. Filenames
alone do not establish SDK compatibility or OFE approval.

New version/hash combinations create a review issue assigned to **IceCube20**,
with image provenance and the current approved board/SDK combinations. Existing
bot-created issues are checked in both open and closed states to avoid daily
duplicates. A changed binary for the same candidate version requires a new review.
API/download failures fail the job rather than reporting that firmware is current.

The watcher cannot write repository contents and never edits this catalog,
firmware pins or devices. The MCU UI continues to show approved combinations;
new candidates appear as GitHub issues, not immediately as installable updates.
Review and hardware testing remain manual. No MCU firmware rebuild is needed
to enable the watcher itself.

Local read-only check:

```sh
node tools/maintenance/check_c6_upstream.cjs
node tools/tests/test_c6_upstream.cjs
```

`--publish` is restricted to GitHub Actions and requires its scoped token.
GitHub scheduled runs are best-effort and can be delayed or disabled after
repository inactivity; see [GitHub's schedule notes](https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows#schedule).

## Developer Version Selection (MCU 2.1.51)

Developer builds now offer version selection on both supported P4 profiles:
the Module DEV KIT PRE-V3 (16/32 MiB flash) and PoE V3.x (including V3.2).
Community builds and a disabled runtime developer mode cannot authorize it.
Normal board-specific approvals and their downgrade block are unchanged.

On Updates, use **Load Espressif versions**, then select the desired numeric
version. The browser reads Espressif's `arduino-esp32` `gh-pages` ref over
HTTPS, pins that commit, and lists its `hosted/esp32c6-v*.bin` files. It ignores
listing-provided download URLs and constructs an immutable official raw URL.
GitHub API failures/rate limits are shown as errors, not as an up-to-date result.
Only Hosted 2.x versions >= 2.6.0 and images <= 2 MiB are offered. This is an
experimental OTA envelope, NOT a claim that every offered version is compatible.
The browser requires Internet access; no API token or MCU credentials leave it.

Loading only stages an image in PSRAM. The authenticated developer explicitly
supplies its selected version, exact size, upstream commit and Git blob identity.
The MCU binds that descriptor to the staging token, checks the actual C6 header,
`network_adapter` application descriptor/version, and Git blob hash, and computes
SHA-256. The displayed commit and SHA-256 are useful for the later review issue.
This developer-supplied descriptor is NOT a signed approval: its trust boundary
is the authenticated developer and the browser's HTTPS connection to Espressif's
repository. Regular updates still use the firmware's compiled SHA-256 pins.

A separate recovery checkbox, target-specific confirmation and Install action
are required to write. Downgrades are explicitly labelled in the confirmation.
Both the currently running and target versions must be known Hosted 2.x versions
>= 2.6.0; unknown/older versions use the separate existing legacy/UART path.
LAN, idle-radio/update exclusion and developer authorization are checked again
on installation. Reloading a staged update restores its selected version and
provenance, but resets risk consent. No automatic downgrade/installation occurs.

After a test, select the specific previous version in the same list to restore
it, including 2.12.13. MCU 2.1.52 shows numeric Espressif versions only, without
separate TEST/restore entries; the regular approved version remains available
as a pinned Espressif image even when the online listing is unavailable. SDIO restoration requires a working
C6 transport; keep a direct UART recovery path for incompatible images. The
P4-side SDK/driver is NOT changed by selecting a C6 image. Test WiFi, Bluetooth,
reconnect and recovery on the exact board before manually approving the exact
version/hash/SDK combination in `updates/hosted-c6-v2.json`. Neither this UI nor
successful version verification publishes a GitHub approval.

## Original Developer LAN Trial (MCU 2.1.40)

MCU 2.1.40 adds an explicitly experimental browser path for the owner's
Waveshare P4 PoE revision 3.2 / 32 MiB board. It is unavailable in community
builds, on other boards/revisions and outside authenticated developer mode.
After enabling developer mode, reload Updates to show the C6 version selector.

- `2.12.13 - approved release` retains the normal updater and downgrade block.
- `2.13.1 - TEST` permits only a known C6 2.12.13 -> 2.13.1 transition.
- `2.12.13 - restore baseline` explicitly permits the reverse transition.

The P4-side Hosted driver stays 2.12.13. This is a C6-only compatibility trial,
not a complete 2.13.1 SDK upgrade or an approval. Stable catalog entries do not
change. Each image has a compiled size, C6 header and SHA-256 pin. Arbitrary
images, unknown C6 versions, other downgrades and legacy/trial mixing are refused.
The target is frozen to the authenticated staging token; server-side checks run
again when installation is requested. Reloading an owned staged update restores
its target but requires fresh risk consent.

The browser downloads/stages/verifies first; a separate checkbox, confirmation
dialog and Install action are required to write. The risk text does not claim
that the owner's direct UART recovery has already been tested. Restoring via
SDIO depends on a responsive C6. A bad activated image may still require direct
C6 UART access at H7. No automatic return to the baseline is promised.

Use an isolated board, back up settings, disconnect controlled equipment and
retain a normal MCU image. Install the updated MCU through its normal MCU OTA
section first. Then use the C6 section on the same Updates page over Ethernet;
no serial console or standalone trial sketch is needed for this path.

## Approved PoE V3.2 Update

MCU 2.1.42 selects C6 2.13.1 as the regular update only on the PoE board with
silicon revision 3.2 and 32 MiB flash. This does not require developer mode.
The host stays Arduino 3.3.12 / IDF 5.5.5 / ESP-Hosted 2.12.13. Host and C6
versions are deliberately independent; the exact combination is approved.
Other revisions, PRE-V3 boards and displays retain their existing C6 pins.

The revision-specific catalog ID is waveshare-p4-wifi6-poe-eth-v3.2. The
generic PoE v3 ID retains its 2.12.13 approval. A catalog edit alone cannot
change the flashed image: the MCU checks board/revision/flash, size and hash.
Staging and installation both check the local approval. Normal downgrades
remain blocked; the explicit developer restore continues to stage 2.12.13.

The owner reported successful WLAN, Bluetooth and baseline restoration on
this exact board on 2026-11-23. This is not a test report for other P4 boards.

## Schema 1

Owner hardware report (2026-11-23): C6 2.13.1 was recognized after installation
on PoE silicon revision 3.2 / 32 MiB. WiFi STA obtained an address and the web
interface was reachable with Arduino 3.3.12 / IDF 5.5.5 / host 2.12.13. This
confirms startup/WiFi for that board only. A subsequent owner report also
confirmed Bluetooth and installation of the old C6 firmware, permitting the
revision-specific approval above. There is no approval for untested boards.

Each release has a unique ID, `approved` or `withdrawn` status, board ID,
`sdio` transport, exact Arduino/IDF/Hosted host versions, and the official C6
image URL, version, size and full-file SHA-256. Images are not redistributed.
The initial approval is Waveshare P4 Module DEV KIT PRE-V3, Arduino 3.3.12,
IDF 5.5.5, Hosted/C6 2.12.13. The owner reported a successful on-device SDIO
update from 2.12.3 to 2.12.13. This does not by itself verify every WiFi/BLE
operation or recovery procedure.

Release 2.1.38RC adds a separate `waveshare-p4-wifi6-poe-eth-v3` approval for
the same exact Arduino 3.3.12 / IDF 5.5.5 / Hosted/C6 2.12.13 combination and
image pins. The owner reported successful standalone recovery and 2.12.13
operation on the PoE V3.2 board. PRE-V3 and PoE approvals remain independent;
one board's entry is never used as the other board's fallback.

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

## Historical Verification (2.0.101 Development)

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
