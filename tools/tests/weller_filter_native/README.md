# Weller Filter Freshness Regression

Run `./run.ps1`. This compiles the actual filter-publishing, fault-update and
telemetry functions extracted from `WellerZeroSmogModule.ino`, with a simulated
clock and bus. It checks a powered-off Weller when the master reconnects,
genuine warning/full states, UART traffic without fresh filter replies, invalid
status codes, telemetry layout, counter preservation, reconnection and millis
wraparound. It does not emulate the physical UART or the Weller controller.

Only a recognized, current `L` reply establishes filter status. UART silence
invalidates it after the existing 5-second link deadline; `L` replies also have
their own 12-second expiry even if other responses continue. Unknown is code 0,
already understood by master, MQTT and display firmware. Nothing resets the
physical filter counter or replacement interval.
