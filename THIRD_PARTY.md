# Third-party dependencies

Application code in `src/`, `firmware/main/`, `android/` and `scripts/` is independently authored.
Dependencies are fetched separately and retain their own licenses.

| Dependency | Purpose | License |
| --- | --- | --- |
| [FoloToy AI Passport](https://github.com/folotoy/ai-passport/tree/f75873f1aab24ac4c0ba9394c131669f66cce650) | Hardware BSP | MIT, © 2026 FoloToy |
| [ESP-IDF](https://github.com/espressif/esp-idf/tree/v5.5.3) | ESP32 runtime and drivers | Apache-2.0, with component-specific notices |
| [LVGL](https://github.com/lvgl/lvgl) | Device graphics and bundled fonts/icons | MIT; retain bundled asset notices |
| [Material Components Android](https://github.com/material-components/material-components-android/tree/1.14.0) | Native M3 Expressive components | Apache-2.0 |
| [AndroidX](https://developer.android.com/jetpack/androidx) | Android UI compatibility | Apache-2.0 |
| [Noto Sans CJK](https://github.com/notofonts/noto-cjk) | 16 px device text | [SIL OFL 1.1](firmware/fonts/OFL.txt) |
| [Bleak](https://github.com/hbldh/bleak) | Computer BLE client | MIT |
| [pySerial](https://github.com/pyserial/pyserial) | Optional USB diagnostics | BSD-3-Clause |

Transitive dependencies and exact ESP component hashes are recorded in the dependency locks.
Full bundled-runtime notices are included in [THIRD_PARTY_NOTICES.txt](docs/THIRD_PARTY_NOTICES.txt).
Distributors of built firmware/APKs must retain applicable third-party notices.
The studied [Codex Usage AI Passport](https://github.com/zt20/codex-usage-ai-passport) application
is a reference only: its implementation is not included or copied.
This is an independent community project, not an official OpenAI or FoloToy product.

## FoloToy BSP license

MIT License

Copyright (c) 2026 FoloToy

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
