# Profile Preflight Native Tests

Run `./run.ps1` from PowerShell. MSVC compiles the master's actual allocation-free
preflight header and the function-name decoder extracted from the current Modbus
module source. Build artifacts go to the system temporary directory.

Coverage: corrected Weller v6 and its original invalid RW fan, both supplied
Modbus examples, every module function alias (also uppercase), explicit/inferred
RO/RW/WO access and function defaults, register and slave boundaries, malformed
numbers, sparse slots through 32, CRLF, aliases, duplicate keys and bounded input.
The web source tests verify transport selection and rejection before NVS writes
or bus commands. Modbus register parsing preserves base-0 notation but rejects
trailing garbage instead of silently accepting a numeric prefix.

This is a preflight, not a replacement for module validation. It does not test
hardware transport, UART timing, NVS capacity or the entire entity transform
parser. No firmware is flashed by these tests.
