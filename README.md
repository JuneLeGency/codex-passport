# Codex Passport

Codex session notifications and quota visualization on FoloToy AI Passport, with an Android companion.

**[从零开始：中文一步步入门](https://github.com/JuneLeGency/codex-passport/blob/main/docs/GETTING_STARTED.zh_CN.md)** · [中文介绍](https://github.com/JuneLeGency/codex-passport/blob/main/README.zh_CN.md) · [Python / uvx guide](https://github.com/JuneLeGency/codex-passport/blob/main/docs/PYTHON.md) · [Downloads](https://github.com/JuneLeGency/codex-passport/releases)

<img src="https://raw.githubusercontent.com/JuneLeGency/codex-passport/main/docs/images/dashboard-example.png" alt="Passport graphical quota dashboard" width="240">

<img src="https://raw.githubusercontent.com/JuneLeGency/codex-passport/main/docs/images/progress-example.png" alt="Three read-only session progress cards on Passport" width="240">

Synthetic fixtures rendered on the physical device; no account or real conversation data.

## What it does

- Computer → encrypted BLE → Passport, or computer → private LAN/WireGuard → Android → encrypted BLE → Passport.
- Meaningful completion, failure, interruption, approval and input notifications across local Codex sessions. Starts and housekeeping update silently; internal guardian/memory workers are excluded.
- Optional 2.4 GHz Wi-Fi directly from Passport to a private IPv4 HTTP relay, provisioned from the paired phone. Failed synchronization falls back to phone BLE.
- Durable delivery queue, device acknowledgements, offline replay, and one active BLE sender with phone fallback.
- Remaining quota compared with remaining cycle time. Orange means faster consumption, green balanced, blue slower.
- Native 240×320 Passport dashboard and physical-button controls. Material 3 Expressive Android overview, notifications, device settings, connection pages and guided onboarding.

Phone notifications show bounded titles and summaries for up to four sessions. Passport has two pages: the original large quota dashboard and a compact quota view with the three most recent visible sessions. Progress cards show a title, current state and one short summary; there are no conversation controls.
The relay orders threads by their last actual interaction: user messages, visible Codex replies or explicit input/approval requests. Tool execution, usage refreshes, reconnects and read receipts do not reorder them. Phone and Passport share this rule; historical indexing is bounded and does not replay alerts.

**This version does not resume conversations, send replies or approve actions.** Those stay in Codex.
Hooks need existing sessions to restart after installation; incremental logs are a compatibility fallback, not a stable API or Codex Mobile push integration.
Other computers need their own collectors.

## Get started

The [beginner guide](https://github.com/JuneLeGency/codex-passport/blob/main/docs/GETTING_STARTED.zh_CN.md) covers tool installation, backup/app-only flashing, APK installation without ADB, relay configuration, pairing, a real notification test, startup, switching, troubleshooting and removal.
It uses the verified macOS + Android + Passport setup. Linux host deployment needs platform adaptation; Windows host deployment has not been validated.

Download the ready-made [Android APK and Passport firmware v0.3.0](https://github.com/JuneLeGency/codex-passport/releases/tag/v0.3.0).
Upgrade the Python relay, Android APK and Passport firmware together to 0.3.0 to enable device settings and Wi-Fi provisioning. Existing BLE bonds and relay settings are retained.
The APK is debug-signed. Back up the device before flashing and write **only the app at 0x10000 on the documented hardware layout**, preserving its other partitions.

## Python installation

Requires Python 3.10+ and a working, logged-in Codex CLI. Install [uv](https://docs.astral.sh/uv/getting-started/installation/), then try:

```sh
uvx --from 'codex-passport-sync==0.3.0' codex-passport --help
```

If your configured mirror has not synchronized this new package, add `--index https://pypi.org/simple` to the uvx or uv tool install command to prioritize official PyPI.

For persistent hooks, install a persistent tool environment instead:

```sh
uv tool install 'codex-passport-sync[ble]==0.3.0'
uv tool update-shell
```

Open a new terminal, then:

```sh
codex-passport install-hooks
export COMPUTER_LAN_IP="replace-with-this-computers-private-IP"
codex-passport serve --host "$COMPUTER_LAN_IP" --port 18765
```

Keep that terminal open. Configure Android's Connection page with `http://YOUR_COMPUTER_IP:18765` and the contents of `~/.local/state/codex-passport/relay-token`.
Allow Bluetooth permissions and enter the current six-digit Passport code in the phone's system pairing dialog.
Restart existing Codex sessions to load the hooks. The address is the relay API, not wireless ADB or a WireGuard endpoint.

Add `--ble "Passport-XXXXXX"` using your device's actual advertised name for computer BLE. First pairing requires the computer's OS dialog and Bluetooth permission.
The `[ble]` extra is required for this route. Select the sender on Android's Connection page; a failed computer connection falls back to the phone and stays there until selected again.
Do not install permanent hooks from a temporary uvx environment, whose cache can be removed.

[PyPI package](https://pypi.org/project/codex-passport-sync/) · [Wheel downloads](https://github.com/JuneLeGency/codex-passport/releases/tag/v0.3.0) · [All installation options and state paths](https://github.com/JuneLeGency/codex-passport/blob/main/docs/PYTHON.md)

## Daily controls and scope

| Control | Result |
| --- | --- |
| Short UP / DOWN | Switch dashboard / progress page |
| Hold UP / DOWN | Switch quota window on either page |
| Short OK while awake | Mark received notifications read; never approve an action |
| First press while asleep | Wake only |
| Double OK while awake | Toggle persistent mute, retaining unread notifications |
| Hold OK while awake | Leave Wi-Fi or reconnect BLE, preserving bonds |

Firmware 0.3.0 adds a short chime only for new unread input/approval requests and **double OK while awake** to toggle mute (saved across restarts; speaker icon at the top). The first wake gesture still only wakes. Initial synchronization is silent; retries, already received batches and unmuting do not replay sounds. Chimes are at least two minutes apart; ordinary completions, failures and interruptions update silently without waking the screen. Update the relay, APK and firmware together to use device settings and Wi-Fi provisioning.

The backlight sleeps after 60 seconds by default; brightness, timeout, volume and mute are adjustable on Android’s Device page. At 10% battery or below, brightness is capped at 20% and screen-on time at 30 seconds. Device receipts confirm settings. Encrypted BLE frames now include up to three bounded session titles and summaries. Unsupported emoji/rare glyphs are omitted on the device; spaces, Latin and basic CJK characters use a bundled OFL font.
An authenticated private relay and paired BLE protect access; bounded phone summaries can still contain private context.
Keep the relay within your private network/VPN. Do not expose its HTTP port publicly. The computer must stay awake and run the collector.

Both BLE routes, Wi-Fi synchronization, route switching, failed Wi-Fi fallback, replay and physical controls were validated. Updates do not change the selected page; progress updates stay silent unless an alert is due. Off-LAN WireGuard and battery endurance were not measured.
See the [validation record](https://github.com/JuneLeGency/codex-passport/blob/main/docs/VALIDATION.md) for the exact evidence and limitations.

## Development and attribution

[Build instructions](https://github.com/JuneLeGency/codex-passport/blob/main/docs/DEVELOPMENT.md) · [MIT license](https://github.com/JuneLeGency/codex-passport/blob/main/LICENSE) · [Third-party notices](https://github.com/JuneLeGency/codex-passport/blob/main/THIRD_PARTY.md)

Application code is independently authored. Official FoloToy BSP, Material Components and other dependencies retain their own licenses.
The [reference application](https://github.com/zt20/codex-usage-ai-passport) was studied, not copied.
This is a community project, not an official OpenAI or FoloToy product.
