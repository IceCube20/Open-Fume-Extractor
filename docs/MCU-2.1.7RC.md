# MCU 2.1.7: OTA completion URL correction

MCU-P4 is now labelled `2.1.7RC` instead of `2.1.7RC-P4-Preview`.
The model remains `MCU-P4`; the image chip check, signing target `MASTER`,
partition layout and supported PRE-V3 board configuration are unchanged.
S3 version labels and display firmware are unaffected. The P4 manifest and
release tooling use the RC suffix too; older Preview versions remain valid
installed-version inputs during an OTA update.

The visible Master-to-MCU rename accidentally also changed two technical paths
inside C++ strings containing JavaScript: `/update/MCU/end` and
`/update/MCU/abort`. The registered server API remains `/update/master/...`.
Both MCU variants therefore could upload an image but received HTTP 404 at
completion; the error cleanup URL was wrong too.

The active JavaScript now uses the registered paths. The rename maintenance
tool preserves relative URLs even inside a whole embedded JS statement.
The upload transport test now checks a complete 70,000-byte transfer using the
actual `postBlob` helper and the real registered route list, rejecting unknown
paths instead of returning mock success for any URL. The test is part of normal
Python regression discovery and covers completion, error cleanup, restart
verification, both languages and developer mode.

## Recovering An Already Running Older UI

After a failed attempt, `tools/ota-mcu-url-recovery.js` can be run once in the
browser console of the affected MCU's `/update` page. It cancels the pending
upload and temporarily maps only the two incorrect relative URLs to the correct
API. It does not commit or start a firmware update, remove authentication/CSRF
checks, or affect module update requests. On a rejected cancellation it restores
the previous fetch handler and reports the error.

Then select the correct, newly compiled MCU 2.1.7 application BIN and start the
normal update without reloading the page first. A page reload discards the
temporary patch. Repeat separately for each MCU. Do not run this while another
upload is in progress. No USB flash or partition migration is required for this
URL defect; chip and signature verification remain enforced by the server.

Source-only fix: firmware compilation and installation remain with the owner.
GitHub publication remains deferred until OTA is verified on the devices.
