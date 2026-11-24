# libavoid Browser Router

Unmodified libavoid-js 0.4.5, commit `28a6fe20c2f1f809afdbeca8d789e65357614c4f`:
https://github.com/Aksem/libavoid-js/tree/28a6fe20c2f1f809afdbeca8d789e65357614c4f

License: LGPL-2.1-or-later. Full license: `LICENSE`, also served at
`/logic/router-license.txt` on an authenticated MCU. libavoid copyright:
Monash University; authors Michael Wybrow, Tim Dwyer and Vladyslav Hnatiuk.
JavaScript/WebAssembly bindings: libavoid-js contributors.

Corresponding libavoid sources and build instructions are available through the
pinned libavoid-js repository (`tools/generate.py`). That version builds the
Aksem/adaptagrams `v1.0.4` fork:
https://github.com/Aksem/adaptagrams/tree/v1.0.4

The JavaScript and WebAssembly are separate browser modules, not linked into MCU
execution. There are no CDN requests. `tools/logic/build_router_assets.py --fetch`
downloads and verifies the pinned Git blobs, then creates deterministic gzip
flash assets. The default command regenerates offline; `--check` verifies them.

To use a modified library, rebuild its distribution following the upstream
instructions, update the reviewed pins in the generator, regenerate the flash
assets and rebuild the MCU. The OFE adapter is in `WebLogicConnections.h`.
