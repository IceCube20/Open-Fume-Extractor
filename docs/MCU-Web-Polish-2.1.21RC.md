# MCU 2.1.21: Browse Hover and Diagnostic Exports

File-picker Browse buttons brighten on hover, with a short color transition.
Disabled fields do not highlight.

Trace start/stop/clear remain next to the target selector. Exports move into a
separate full-width strip, grouped by CSV, JSON and diagnostic snapshot. Each
group has Download and, on P4, To USB actions. The passive quality-test report
uses the same arrangement, separate from its Start/Stop controls. Mobile uses
stacked format groups; paired actions have consistent heights and spacing.

USB destination actions mirror the enabled/disabled state of their native
download button and remain locked while their dialog/transfer is active. The
existing file validation, destination picker and transfer paths are unchanged.

Verified: 364 Python regressions, native render/policy checks and ten browser
suites passed. The PRE-V3 P4 app is 3,555,968 bytes, marked
`2.1.21RC-Developer`. Build/export hashes and compiled/source UI headers match.
No device was flashed.
