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

## 0.2.0 progress pages

- The original large dashboard and the compact quota/three-session page were both captured from
  the physical device using synthetic data. The public images contain no real conversation data.
- The user verified short UP/DOWN page switching, held UP/DOWN quota-window switching, and
  wake-only first press. Short OK acknowledgement and held OK reconnection retain their behavior.
- 22 Python tests cover the new three-session bound, current status after a new turn, removal of
  the previous turn's completion summary, escaped UTF-8 payload limits, and existing transport tests.
- Both firmware pages build with strict warnings. The 16 px OFL CJK bitmap uses large font indices;
  the app fits the factory partition. No bootloader, partition table or pairing storage was replaced.
- Progress changes do not change the selected page and do not create new alerts for quiet starts.
  There are no conversation actions, detail menus, automatic scrolling or fabricated percentages.
- The device receives bounded titles and summaries over encrypted BLE in this version. The phone
  retains its separate notification inbox. Unsupported device glyphs are omitted instead of boxes.
- Four real-device transitions (computer → phone → computer → phone) again delivered matching
  device event receipts with the new progress payload. The initial computer delivery waited behind
  an existing queue; this is not a latency benchmark. The final route was restored to phone forwarding.

## 0.2.1 development firmware: sound and hardware checks

- Firmware builds with warning-as-error application compilation and fits the existing factory partition. Python regression suite: 22 tests passed. Native controls and alert-policy tests passed (baseline, replay, batches, cooldown, mute, read suppression and quiet lifecycle updates).
- Short chimes played on the physical speaker; the user confirmed hearing them. Three playback cycles completed; subsequent cycles returned to the same free-heap value. The worker creates only a DAC/TX path and releases audio resources after playback.
- Restored notification firmware received new synthetic event ACKs over computer BLE and phone BLE; final route is phone. Wireless ADB was unavailable, so phone delivery was checked by actual device receipt, not by assuming an Android UI state.
- Double-OK mute is covered by native event-sequence tests, including a double press that starts asleep. The user subsequently verified double OK on the 0.3.0 development firmware: the speaker icon changed and unread notifications were retained. This development firmware is not a published release.
- Wi-Fi, microphone, battery, backlight and NVS results and their limits are recorded in [the hardware check report](HARDWARE_CHECKS.zh_CN.md). Wi-Fi credentials are excluded from both source and notification firmware; temporary credential-bearing probe artifacts were removed after restoration.

## 0.3.0 interaction ordering

- 33 Python tests passed, including six interaction-ordering regressions. Phone inbox and
  Passport progress are ordered by visible conversation activity, independently of lifecycle status.
- Tests cover new messages, background tool/status updates, duplicate hooks, older records,
  deterministic ties, restarts, incremental messages and bounded backward history indexing.
- The LAN-only relay was restarted with the change. Existing history indexing completed;
  the live API and test phone contain descending interaction timestamps. A fresh physical
  Passport receipt was observed through phone BLE after deployment.
- The setup command was tested with an isolated state directory: private host selection, generated token file permissions, displayed pairing details and relay startup.

## 0.3.0 quiet settings, onboarding and Wi-Fi

- Native control, protocol and alert-policy tests pass. Only input/approval requests qualify for sound or waking; chimes are at least 120 seconds apart. Boot baselines, replay, coalesced batches, mute, read receipts and ordinary lifecycle events do not replay sounds.
- The user confirmed physical double OK changes the mute icon without clearing unread notifications. Existing two-page and wake-only controls retain their earlier physical acceptance.
- The Android onboarding flow was completed on the test phone, including an authenticated computer check, actual phone BLE receipt, button guide and completion. Device preferences were applied through the UI and acknowledged by Passport. Updated APK build and installation passed.
- Wi-Fi provisioning from Android produced actual device HTTP receipts. Device restart restored Wi-Fi and preferences. Wi-Fi settings writes and Wi-Fi/computer BLE/phone BLE transitions each received physical acknowledgements, including returning to a previously saved network.
- An intentionally unreachable private relay port induced real Wi-Fi synchronization failure. Passport automatically returned to paired phone BLE. The correct network and relay were restored, verified, then the route was returned to phone forwarding.
- Battery endurance, RF range and off-LAN WireGuard remain excluded from this acceptance. No microphone capture is enabled in the notification firmware.

- The final 0.3.0 firmware was flashed to the app partition only and verified by checksum.
  Both full display pages were captured as 16 streamed display tiles using synthetic data;
  the large arcs, percentage and CJK labels were visually inspected. The diagnostic capture
  uses the existing display buffer, avoiding incomplete large-object snapshots under low memory.
- The actual wheel passed all 33 tests in an isolated installation. Source, unpacked Python
  archives, APK contents and firmware were checked against real private credentials, personal
  device/network identifiers and private paths; no matches were found.
