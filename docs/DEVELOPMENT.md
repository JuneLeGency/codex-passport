# Build and development

End users can use the [step-by-step guide](GETTING_STARTED.zh_CN.md) and download the APK/firmware.
This page is for rebuilding them. Version 0.2.0 adds the device progress page and bounded thread summaries over BLE; update all three components together.

## Python

```sh
uv sync --extra ble
uv run python -m unittest discover -s tests -v
uv build --no-sources
uvx --from ./dist/codex_passport_sync-0.2.0-py3-none-any.whl codex-passport --help
```

Python 3.10+. Tests require local socket access. `src/`, CLI commands and protocol are independently authored.
Runtime state belongs outside packages; do not include `.runtime`, credentials, private logs or device backups.
The sdist includes the Python source, tests, guides and license files. Full Android/firmware source is in the GitHub repository archive.

## Android

Install JDK 17, Android SDK (platform 35), Gradle 8.14.4. AGP is pinned at 8.10.1.
Configure `ANDROID_HOME` to your SDK installation or create an untracked `android/local.properties` with `sdk.dir`.
This repository does not currently include a Gradle wrapper; use the installed `gradle` executable.

```sh
gradle -p android --no-daemon assembleDebug
```

Output: `android/app/build/outputs/apk/debug/app-debug.apk`. Package: `dev.passport.codex`.
The published 0.2.0 APK uses a debug signature. A locally built APK may have a different signing key and cannot necessarily replace an installed APK without uninstalling it; that would remove its saved relay settings.
Published APK installation does not require ADB. Developers can install to an explicitly selected device with `adb -s DEVICE install -r PATH_TO_APK`.

## Firmware

Install and activate ESP-IDF 5.5.3 using Espressif's instructions for your OS.
Then fetch the official BSP and generate the OFL bitmap font (Node.js/npm required by the pinned font converter):

```sh
uv run python scripts/fetch_bsp.py
uv run python scripts/build_font.py
idf.py -C firmware build
```

The fetched checkout is ignored under `upstream/ai-passport`. CMake references its official BSP; the application under `firmware/main/` is original.
Output: `firmware/build/codex-passport.bin`.
Only flash the application at `0x10000` on the documented official layout, preserving bootloader, partitions, NVS, cardid and Recovery.
See the backup and flashing steps in the beginner guide. Do not substitute a full erase or a combined image.

## Additional local checks

```sh
cc -Wall -Wextra -Werror tests/test_controls.c firmware/main/controls.c -o /tmp/passport-controls
/tmp/passport-controls
javac -d /tmp/passport-pace android/app/src/main/java/dev/passport/codex/UsagePace.java tests/UsagePaceTest.java
java -cp /tmp/passport-pace UsagePaceTest
```

`tests/test_protocol.c` additionally needs cJSON headers/library; IDF builds use its bundled cJSON component.
GitHub Actions runs Python and native control tests. Manual physical-button acceptance and BLE delivery evidence are recorded separately in [VALIDATION.md](VALIDATION.md).
USB screenshot tooling is in `scripts/capture_device.py`; it requires an explicit `--port`. Use `--fixture synthetic.json --page 0` or `--page 1` to render and capture a synthetic snapshot on one USB connection. The diagnostic page selection is temporary and does not mark notifications read. Use synthetic data before sharing screenshots.

## Release procedure

1. Update Python version metadata and the lockfile; keep device versions unchanged if their binaries are unchanged.
2. Build distributions from the release source with `uv build --no-sources` and validate package metadata with `uvx --from twine twine check --strict DIST_FILES`.
3. Test wheel installation in an isolated environment and install from PyPI again after publishing.
4. Audit source, history and unpacked artifacts for private paths, tokens, device identifiers and runtime files.
5. Publish explicit distribution filenames using `uv publish` with `UV_PUBLISH_TOKEN` supplied privately, or a configured PyPI trusted publisher. Never put a token in command arguments, source, logs or documentation.
6. Tag the reviewed commit, attach the exact same distributions and SHA256 checksums to GitHub, and verify remote file digests and CI.

Do not rebuild or replace files under an existing public version. Retain [third-party notices](../THIRD_PARTY.md) with distributed firmware/APKs.
Publishing workflow follows [uv's package guide](https://docs.astral.sh/uv/guides/package/).
