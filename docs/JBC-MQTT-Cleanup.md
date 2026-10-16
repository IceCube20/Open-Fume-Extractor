# JBC USB MQTT Entity Reconciliation

Master 2.0.52 RC repairs retained Home Assistant discovery entries left by a
previous station class, removed ports, unsupported counters or old control types.
Only a master update is required; the JBC USB module and HA integration do not
need a firmware/software change for this fix.

The persisted manifest still performs normal differential cleanup. A separate
bounded repair pass also covers known JBC entities absent from that manifest,
including installations migrated from older firmware. Full topic names are used
for deletes, including long partial-counter names.

Since Master 2.0.53, removal requires two matching discovery inventories while
the module is online: either a recognized connected station or an explicit
report that no station is connected. In the latter case only module diagnostics
and connection/Work/Stand entities remain, even if old station ports are cached.
Offline modules and incomplete connected station reports preserve the last
inventory. A second discovery pass is requested
even when the general discovery signature has not changed. The regular check
interval is 15 seconds; cleanup then runs incrementally rather than blocking
normal state publishing with hundreds of retained deletes.

Each service tick checks at most 32 candidates and publishes at most two repair
deletes. Failed deletes retry the exact same topic. Other modules' topics and
retained module state/status are not touched by this repair. An unchanged,
completed station inventory does not trigger another sweep on every poll.

`tools/tests/mqtt_jbc_cleanup_native/run.ps1` compiles the actual manifest codec,
topic generation and repair functions. It covers all six station families,
orphan entries without a previous manifest, long names, wrong control platforms,
disconnects, interrupted repair, retries and identity/namespace changes.
