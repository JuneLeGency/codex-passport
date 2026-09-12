# Codex Passport

Codex session notifications and quota visualization on FoloToy AI Passport, with an Android companion.

**[从零开始：中文一步步入门](https://github.com/JuneLeGency/codex-passport/blob/main/docs/GETTING_STARTED.zh_CN.md)** · [中文介绍](https://github.com/JuneLeGency/codex-passport/blob/main/README.zh_CN.md) · [Python / uvx guide](https://github.com/JuneLeGency/codex-passport/blob/main/docs/PYTHON.md) · [Downloads](https://github.com/JuneLeGency/codex-passport/releases)

<img src="https://raw.githubusercontent.com/JuneLeGency/codex-passport/main/docs/images/dashboard-example.png" alt="Passport graphical quota dashboard" width="240">

Synthetic quota fixture rendered on the physical device; no account or conversation data.

## What it does

- Computer → encrypted BLE → Passport, or computer → private LAN/WireGuard → Android → encrypted BLE → Passport.
- Meaningful completion, failure, interruption, approval and input notifications across local Codex sessions. Starts and housekeeping update silently; internal guardian/memory workers are excluded.
- Durable delivery queue, device acknowledgements, offline replay, and one active BLE sender with phone fallback.
- Remaining quota compared with remaining cycle time. Orange means faster consumption, green balanced, blue slower.
- Native 240×320 Passport dashboard and physical-button controls. Material 3 Expressive Android overview, notification and connection pages.

Phone notifications show bounded titles and summaries for the four most recent sessions. Passport receives quota, counts and receipt IDs, with no conversation text.
**This version does not resume conversations, send replies or approve actions.** Those stay in Codex.
Hooks need existing sessions to restart after installation; incremental logs are a compatibility fallback, not a stable API or Codex Mobile push integration.
Other computers need their own collectors.

## Get started

The [beginner guide](https://github.com/JuneLeGency/codex-passport/blob/main/docs/GETTING_STARTED.zh_CN.md) covers tool installation, backup/app-only flashing, APK installation without ADB, relay configuration, pairing, a real notification test, startup, switching, troubleshooting and removal.
It uses the verified macOS + Android + Passport setup. Linux host deployment needs platform adaptation; Windows host deployment has not been validated.

Download the ready-made [Android APK and Passport firmware v0.1.0](https://github.com/JuneLeGency/codex-passport/releases/tag/v0.1.0).
Python 0.1.1 is a documentation/packaging update compatible with those device files; no device reinstall is needed for this update.
The APK is debug-signed. Back up the device before flashing and write **only the app at 0x10000 on the documented hardware layout**, preserving its other partitions.

## Python installation

Requires Python 3.10+ and a working, logged-in Codex CLI. Install [uv](https://docs.astral.sh/uv/getting-started/installation/), then try:

```sh
uvx --from 'codex-passport-sync==0.1.1' codex-passport --help
```

If your configured mirror has not synchronized this new package, add `--index https://pypi.org/simple` to the uvx or uv tool install command to prioritize official PyPI.

For persistent hooks, install a persistent tool environment instead:

```sh
uv tool install 'codex-passport-sync[ble]==0.1.1'
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

[PyPI package](https://pypi.org/project/codex-passport-sync/) · [Wheel downloads](https://github.com/JuneLeGency/codex-passport/releases/tag/v0.1.1) · [All installation options and state paths](https://github.com/JuneLeGency/codex-passport/blob/main/docs/PYTHON.md)

## Daily controls and scope

| Control | Result |
| --- | --- |
| UP / DOWN | Switch quota windows |
| Short OK while awake | Mark received notifications read; never approve an action |
| First press while asleep | Wake only |
| Hold OK while awake | Reconnect BLE, preserving bonds |

The backlight sleeps after 60 seconds without alerts/buttons. Daily BLE frames contain no conversation text.
An authenticated private relay and paired BLE protect access; bounded phone summaries can still contain private context.
Keep the relay within your private network/VPN. Do not expose its HTTP port publicly. The computer must stay awake and run the collector.

Both BLE routes, route switching, replay and physical controls were validated. Off-LAN WireGuard and battery endurance were not measured.
See the [validation record](https://github.com/JuneLeGency/codex-passport/blob/main/docs/VALIDATION.md) for the exact evidence and limitations.

## Development and attribution

[Build instructions](https://github.com/JuneLeGency/codex-passport/blob/main/docs/DEVELOPMENT.md) · [MIT license](https://github.com/JuneLeGency/codex-passport/blob/main/LICENSE) · [Third-party notices](https://github.com/JuneLeGency/codex-passport/blob/main/THIRD_PARTY.md)

Application code is independently authored. Official FoloToy BSP, Material Components and other dependencies retain their own licenses.
The [reference application](https://github.com/zt20/codex-usage-ai-passport) was studied, not copied.
This is a community project, not an official OpenAI or FoloToy product.
