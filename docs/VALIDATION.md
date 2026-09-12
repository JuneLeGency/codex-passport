# Validation

This file records evidence, not a claim of equivalence to Codex Mobile.

## Automated

- Python collector: lifecycle filtering, independent sessions, durable outbox, deduplication,
  bounded/escaped UTF-8 frames, expiry, HTTP authentication and acknowledgements.
- BLE transport: MTU fragmentation, write-with-response, matching sequence **and** event ID,
  split notifications, read receipts, route leases and stale callback rejection.
- Firmware: protocol parsing and pace boundaries; strict compiler warnings; physical-button
  event logic, wake-only first press, deliberate OK acknowledgement and long-press reconnect.
- Android: native usage pace boundaries; debug build on API 35; earlier compatibility smoke
  test on an API 23 emulator.

## Hardware and network

Verified on physical AI Passport, a macOS collector and an Android 15 phone:

- First computer pairing with a six-digit code, followed by successful encrypted GATT delivery.
- The macOS user service reconnects with the stored bond and receives device ACKs.
- Four ownership transitions: computer → phone → computer → phone. Each delivered a clearly
  labeled synthetic completion/approval event from the hook ingestion path to a device receipt.
- A real BLE interruption was induced by temporarily suspending the device application in ROM,
  without writing flash. The relay returned ownership to the phone, kept the unacknowledged
  event queued, and the paired phone reconnected and delivered it after device restart.
- Firmware build and app-only flash checksum passed; Android build and device installation passed.
- A source-only export passes the 20 Python tests independently of private runtime files.
- Staged-source privacy audit excludes the real relay token, personal identifiers, private paths,
  device addresses, databases, flash backups and generated binaries. The public screenshot is a
  synthetic quota fixture and contains no conversation data.

Validation performed on 2026-09-12. These are local hardware results, not a latency or range guarantee.
A unit test cannot certify an ADC switch, RF range, a cellular VPN route or battery endurance.
The user has verified physical UP/DOWN window switching and short OK acknowledgement;
the matching physical OK read receipt was also observed over USB.
The user has verified wake-only first OK press after the 60-second screen idle: the screen woke
and the unread indicator remained unchanged; no read receipt was generated.
The user has verified physical long OK reconnection: synchronization resumed without asking
for another pairing code. Wireless ADB was unavailable for that gesture, so the gesture result
is a user-observed hardware check. After wireless ADB was restored, a fresh device ACK was
confirmed through the phone and the pending notification queue had drained to zero.
Earlier automated interruption/replay results are separate.
Off-LAN WireGuard testing was explicitly excluded from this release acceptance by the user;
it has not been physically verified. LAN delivery does not certify a particular VPN setup.
There is no measured battery-current or endurance claim.

## Reproduce

```sh
uv sync --extra ble
uv run python -m unittest discover -s tests -v
cc -Wall -Wextra -Werror tests/test_controls.c firmware/main/controls.c -o /tmp/passport-controls
/tmp/passport-controls
javac -d /tmp/passport-pace android/app/src/main/java/dev/passport/codex/UsagePace.java tests/UsagePaceTest.java
java -cp /tmp/passport-pace UsagePaceTest
```

Run HTTP tests where local sockets are permitted. Hardware verification requires the matching
Passport firmware, Android configuration and explicit BLE pairing. Never publish raw device
flash dumps, relay databases, access tokens, private transcripts or personal screenshots.
