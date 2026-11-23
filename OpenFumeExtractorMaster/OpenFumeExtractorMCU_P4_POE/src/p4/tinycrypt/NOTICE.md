# P4 BLE ECC Backport

These files are unchanged ESP-IDF sources from commit
`9fd7cb7e606a06111e1b14be7f4e00d77d9cf3dd`:
https://github.com/espressif/esp-idf/commit/9fd7cb7e606a06111e1b14be7f4e00d77d9cf3dd

`UPSTREAM.json` records the original paths and SHA-256 hashes. C source files
have only been renamed to `.inc.h` so Arduino copies and compiles them once, through the
four P4-only wrapper translation units. Contents and copyright notices are
unchanged. Do not copy these overrides into the S3 sketches.

The upstream Apache-2.0 license is included as `LICENSE`. The ECC sources also
retain Kenneth MacKay's and Intel's BSD redistribution conditions and notices.
Include this directory's licenses/notices when distributing P4 firmware binaries.

The patch fixes ECC scalar width and byte order. It does not disable Secure
Connections, encryption, numeric comparison, peer validation or MITM protection.
Normal Arduino 3.3.12 linking uses these sketch objects ahead of `libbt.a`.
Verify the final link map: the SDK's old `ecc.c.obj`, `ecc_dh.c.obj`,
`ecc_dsa.c.obj` and `esp_tinycrypt_port.c.obj` must not be pulled in.

## OFE 1.0.3 Software-Only Policy

The upstream files listed in UPSTREAM.json are unchanged. The adjacent OFE
wrapper translation units select their software branches with the scoped
OfeP4BleEccSoftware.h header. This avoids depending on the P4 ECC peripheral for
BLE while retaining Secure Connections, scalar regularization and RNG blinding.
This policy does not change installed SDK files or other crypto users.

