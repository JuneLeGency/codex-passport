# 从零开始：让 Codex 通知出现在 AI Passport 上

这份指南按 **macOS 电脑 + Android 手机 + FoloToy AI Passport** 的实测组合编写。
不要求会写代码，也不需要安装 Android Studio 或 ESP-IDF；直接使用已编译的 APK 和固件。
Linux 用户可参考相同的前台中继命令，但自启动和系统蓝牙配置需自行适配；Windows 主机尚未完成验收，本指南的终端命令不适用于 PowerShell。

完成后的效果：电脑运行 Codex，手机通过局域网读取通知与用量，再通过 BLE 发给 Passport。
Passport 首页显示原来的大用量环，第二页显示紧凑用量和最近三个会话的状态/短摘要；手机“通知”页显示最近四个会话的标题与短摘要。
本版不支持在手机继续对话、回复问题或批准操作，这些仍需回到 Codex 完成。

## 1. 准备好这些东西

| 需要什么 | 检查方法 |
| --- | --- |
| FoloToy AI Passport | 本项目对应 ESP32-C3、240×320 屏幕和官方 3 MB factory 应用分区布局 |
| 可传数据的 USB 线 | 充电线不一定能让电脑发现串口 |
| Android 6 或更新版本手机 | Android 15 真机验证；Android 6 模拟器做过兼容性冒烟检查 |
| 运行 Codex 的电脑 | 在终端执行 `codex --version` 能看到版本，并且 Codex 已登录、能正常完成任务 |
| 局域网 | 第一次让手机和电脑连接同一局域网，先不要排查 VPN |

首次配置把手机和 Passport 放在一起。日常使用不需要无线 ADB，也不需要 USB 连着电脑。
电脑需要保持开机且中继运行；电脑睡眠期间不会实时采集并转发新消息。

打开 macOS 的“终端”。以下代码块按顺序执行，不要复制代码块外的解释。
带“替换”的内容必须改成你自己的值；不要照抄示例设备名。

如果 `codex --version` 提示找不到命令，先完成 [Codex CLI 安装与登录](https://developers.openai.com/codex/cli/)，再继续。
如果平时只使用 Codex 桌面 App，也需要让中继能够调用已登录的 Codex CLI 来读取账户用量。

## 2. 安装 uv，下载项目

uv 用来准备 Python 和依赖。已经安装时跳过安装命令。

```sh
curl -LsSf https://astral.sh/uv/install.sh | sh
```

关闭并重新打开终端，再执行：

```sh
uv --version
git --version
```

应该分别显示版本号。如果 macOS 弹出开发者命令行工具安装提示，完成安装后再运行 `git --version`。
安装方式来自 [uv 官方说明](https://docs.astral.sh/uv/getting-started/installation/)。

```sh
git clone https://github.com/JuneLeGency/codex-passport.git "$HOME/codex-passport"
cd "$HOME/codex-passport"
uv sync --extra ble
uv run codex-passport --help
```

看到 `serve`、`install-hooks`、`status` 等子命令就成功了。uv 会按需要准备 Python；下载依赖时需联网。
如果目标目录已经存在，先确认它是不是你之前下载的本项目，不要覆盖别的目录。
后续命令默认都在这个目录执行。**不要移动或删除项目及其 `.venv` 目录**，安装后的 Hooks 和自启动服务会使用这里的 Python。

这里采用固定项目环境，便于新手安装自启动服务。只想安装 Python 包或试用 uvx，见 [Python 安装方式](PYTHON.md)；两种方法选择一种即可。

## 3. 给 Passport 安装固件（只做一次）

已经安装 0.3.1 固件时可跳到第 4 步。从 0.3.0 升级时，新亮屏策略需要更新固件，APK 更新预设和说明；已有 0.3.0 中继仍兼容。更早版本建议同时更新三个组件，保留设置和配对。

打开 [设备文件下载页 v0.3.1](https://github.com/JuneLeGency/codex-passport/releases/tag/v0.3.1)，在 Assets 下载：

- `codex-passport-0.3.1.bin`：Passport 应用固件。
- `SHA256SUMS`：文件校验值。
- `THIRD_PARTY_NOTICES.txt`：随二进制保留的第三方许可证。

将文件放进项目内自己新建的 `downloads` 文件夹。把 Passport 用数据线接上电脑，执行：

```sh
uv run python -m serial.tools.list_ports -v
```

拔插一次设备，比较列表，找出新增的串口。macOS 通常形如 `/dev/cu.usbmodem…`；使用完整的实际路径。

```sh
export PASSPORT_PORT="替换为刚找到的完整串口路径"
shasum -a 256 downloads/codex-passport-0.3.1.bin
```

输出开头的哈希应与 `SHA256SUMS` 中对应 `.bin` 一行完全一致。不一致时重新下载，不要烧录。

先备份整机 Flash，文件保存到项目之外：

```sh
mkdir -p "$HOME/passport-backups"
uv run --no-project --with 'esptool>=5,<6' python -m esptool --port "$PASSPORT_PORT" read-flash 0 ALL "$HOME/passport-backups/passport-before.bin"
```

首次备份若已有同名文件，先换一个文件名，保留原始备份。备份包含设备私有信息，不要上传到 GitHub 或发送给别人。
备份成功后，只写应用分区：

```sh
uv run --no-project --with 'esptool>=5,<6' python -m esptool --port "$PASSPORT_PORT" write-flash 0x10000 downloads/codex-passport-0.3.1.bin
```

预期：工具完成写入和校验，设备重启后出现圆环仪表。还没连接中继时用量暂不可用，这是正常的。
这里的地址只适用于上述硬件的官方分区布局；不要执行 `erase-flash`，不要覆盖 bootloader、分区表、NVS、cardid 或 Recovery。
其他硬件/分区布局先不要套用这条命令。命令依据 [Espressif esptool 文档](https://docs.espressif.com/projects/esptool/en/latest/esp32c3/esptool/basic-commands.html)。

## 4. 在手机安装 App

用**手机浏览器**打开同一个 [v0.3.1 下载页](https://github.com/JuneLeGency/codex-passport/releases/tag/v0.3.1)，下载 `codex-passport-0.3.1.apk` 并点击安装。
按 Android 的提示，给当前浏览器/文件管理器允许“安装未知应用”。这是本项目的调试签名 APK，尚未上架应用商店。
不需要开启开发者模式、USB 调试或无线 ADB。

打开 Codex Passport，会看到分步新手引导；可选「稍后继续」保留进度。先开启手机蓝牙，按下一步让电脑中继运行。

## 5. 安装通知 Hooks，启动电脑中继

在电脑的“系统设置 → 网络 → 当前已连接网络 → 详细信息 → TCP/IP”查看 **电脑的 IPv4 地址**。
不要填手机 IP、无线 ADB 端口、路由器管理地址或 WireGuard 服务器入口地址。

```sh
cd "$HOME/codex-passport"
export COMPUTER_LAN_IP="替换为电脑的局域网 IPv4 地址"
uv run codex-passport --state-dir .runtime/live install-hooks
uv run codex-passport --state-dir .runtime/live serve --host "$COMPUTER_LAN_IP" --port 18765
```

预期：先看到 `Hooks installed`，然后看到 `Relay listening at` 和刚填写的地址。
最后一条命令会持续运行、不返回提示符，**这是正常的，先保留此终端窗口**。
如果系统询问是否允许传入网络连接，允许该程序在你的私有网络接收连接。

安装器会备份并合并 Hooks，不替你批准 Codex 操作。已打开的 Codex 会话需要重新打开才能加载 Hooks。
采集范围是这台电脑、当前 Codex 用户目录中的会话；另外一台电脑需要自己的采集器。
如果你自定义了 `CODEX_HOME`，请先在此终端设置为同一个目录，再安装 Hooks 和启动服务。

另开一个终端窗口，复制专用中继密钥到电脑剪贴板：

```sh
cd "$HOME/codex-passport"
pbcopy < .runtime/live/relay-token
```

macOS 执行后不会打印内容。把剪贴板中的值安全地填到自己的手机，或用本机文本编辑器打开该文件并输入。
不要发到公开聊天、截图或 Issue。**这是本项目生成的中继密钥，不是 OpenAI API Key，也不是六位蓝牙配对码。**

## 6. 手机填写地址并首次配对

1. 在新手引导中进入“连接你的电脑”；已有用户也可从“连接 → 查看入门指南”进入。
2. “中继地址”填写 `http://电脑的实际局域网IP:18765`，包含 `http://`，不加 `/v1/snapshot`。
3. “配对密钥”填写上一节的 `relay-token` 文件内容。
4. 点击“验证并继续”，看到电脑验证成功后，再点击“允许权限并连接”。允许“附近设备”和通知权限；Android 11 及更早版本还需定位权限与定位开关来扫描 BLE。
5. 将手机与 Passport 放近。在手机系统配对弹窗中，输入 **Passport 此刻屏幕显示的六位数字**。弹窗过期就重试，使用新显示的码。

预期：概览显示“同步正常 · 电脑 → 手机 → BLE”，Passport 的蓝牙图标显示连接状态，用量数据随后出现。
用量大约每 60 秒刷新一次；账户接口没有返回额度时会显示不可用，不能用“必须出现满额”判断成功。
看到手机同步正常意味着已收到 Passport 回执，单纯“BLE 已连接”还只是连接建立。引导随后介绍实体按键，点击“开始使用”完成。已有配置也可在“连接”页直接保存并连接。

## 7. 发一条真实通知验收

重新打开一个 Codex 会话，发送：“请只回复：Passport 通知测试完成”。等待这一轮结束。

预期：手机“通知”页出现这一会话的短摘要，Passport 未读铃铛数量增加；普通完成安静地更新列表；只有待回复或待批准才会唤醒/按规则短响。
第一次运行可能先补送已有待送达事件，等待队列处理。任务开始等状态会静默更新，不应该每次都提醒。

在电脑第二个终端可以检查本机记录：

```sh
cd "$HOME/codex-passport"
uv run codex-passport --state-dir .runtime/live events
```

这是最近最多 100 条事件的诊断输出；`delivered: 1` 表示已收到设备送达回执，`read: 1` 表示已标为已读。
输出可能包含自己的会话摘要，不要直接贴到公开 Issue。

## 8. 看懂 Passport，学会按键

| 画面/按键 | 含义 |
| --- | --- |
| 外环、中心百分比 | 本周期剩余额度 |
| 内环 | 本周期剩余时间 |
| 橙色 | 消耗进度比时间进度快超过 5 个百分点 |
| 绿色 | 与匀速预算相差不超过 5 个百分点 |
| 蓝色 | 消耗较慢，余量相对充裕 |
| 底部三个图标 | 运行、待回复、未读数量 |
| 短按上 / 下 | 切换大 dashboard / 三会话进展页 |
| 长按上 / 下 | 切换额度周期，两页共享所选周期 |
| 亮屏时短按 OK | 将当前通知标为已读，不会批准任务 |
| 熄屏后第一次按键 | 只唤醒；不换页、不清未读 |
| 亮屏时长按 OK 约 2 秒再松开 | Wi-Fi 模式先返回 BLE；BLE 模式重连并保留绑定，正常不需重新输入验证码 |

**固件 0.3.1 的声音提醒：**只有新的未读待回复、待批准事件会发出约 0.26 秒短提示音。普通完成、失败或中断只更新列表，不反复亮屏。亮屏时双击 OK 切换静音，顶部显示喇叭/静音图标；不会标为已读，重启后保留设置。

开机后第一次同步只恢复画面，不补响历史提醒。正常重连不会重响已收到的事件，同批补送合并为一次，提示音至少间隔两分钟；静音期间的提醒不会在解除静音后补响。任务开始、额度刷新仍不发声。

进展页只展示当前状态：进行中、待回复、待批准、已完成等，不提供任务操作或详情菜单。内容按会话合并，最多三条；长文本省略。中继按最后一次实际交互倒序排列，发言、新回复或明确的输入/批准请求会让对应会话前移；后台工具和状态刷新不会影响排序。手机采用相同规则。新进展不会强制切换你正在看的页面。

屏幕不是触屏，不能点击圆环或图标。0.3.1 首次使用默认 35% 亮度、30 秒无按键后熄屏，已保存的设置保留。待回复、待批准通知自动亮屏约 15 秒，两次至少间隔 5 分钟；连续提醒不延长查看时间，冷却期内提醒不补亮。普通数据刷新不唤醒。详见[省电策略与验证状态](POWER.zh_CN.md)。
例如周期过去一半，额度已用 70%，就比匀速预算快 20 个百分点；颜色反映当前进度，不预测未来用量。
周期刚开始的前 1% 时间暂不判断快慢。

### 8.1 调整提醒与屏幕

1. 打开手机底部「设备」。顶部应显示 Passport 的电量和连接方式。
2. 按需关闭「需要我处理时短响」，或调整音量、亮度、自动熄屏时间。
3. 点击「应用到 Passport」，等待“已应用”。按钮不可点时，先回概览连接设备。
4. 「安静阅读」会选择静音、25% 亮度和 30 秒熄屏；点应用才发送。

设置保存在 Passport，重启保留。双击 OK 的静音状态也保留。电量不超过 10% 时背光最多 20%、亮屏最多 30 秒。
「找一下 Passport」会让它亮屏；未静音时也会短响。它不会清除未读。

## 9. macOS 登录后自动运行

先完成前台同步验收。在运行中继的终端按 `Ctrl+C` 停止它，然后执行：

```sh
cd "$HOME/codex-passport"
export COMPUTER_LAN_IP="替换为电脑的局域网 IPv4 地址"
.venv/bin/python scripts/install_macos_service.py --host "$COMPUTER_LAN_IP" --state-dir .runtime/live
launchctl print "gui/$(id -u)/dev.codex-passport.relay"
```

预期：显示 `Installed service`，随后服务信息包含运行状态。现在可以关闭终端；手机仍应保持同步。
这是当前用户**登录后**启动的服务，不是未登录前的系统服务。保持电脑开机、网络可用。
电脑 IP 改变后，需要用新地址重新运行安装命令，并修改手机地址；可在路由器为电脑设置固定租约。
不要同时启动多个使用相同目录、端口的中继。

## 10. 可选：电脑直接通过 BLE 连接 Passport

先在手机“连接”页停止手机同步，让出 BLE。若已经安装自启动服务，先停止它：

```sh
launchctl bootout "gui/$(id -u)/dev.codex-passport.relay"
```

没有安装服务时跳过这一条。找到自己设备的 BLE 广播名，一般是 `Passport-` 后接六位字符，可在系统附近蓝牙设备列表查看。

```sh
cd "$HOME/codex-passport"
export COMPUTER_LAN_IP="替换为电脑的局域网 IPv4 地址"
export PASSPORT_BLE_NAME="替换为自己设备的 Passport-广播名"
uv run codex-passport --state-dir .runtime/live serve --host "$COMPUTER_LAN_IP" --ble "$PASSPORT_BLE_NAME"
```

第一次在电脑前台完成蓝牙权限授权和配对：在电脑的系统弹窗输入 Passport 当前六位码。
之后在手机概览点击“连接 Passport”启动手机服务；进入“连接”页可选择“使用电脑蓝牙直连”或“使用手机转发”。
电脑选项不可点时，确认中继是带 `--ble` 启动，并等手机刷新配置。

一台 Passport 同时只允许一个 BLE 发送方。电脑直连失败会退回手机转发；退回后保持手机模式，避免电脑反复抢连接。
回到电脑旁时，可以在手机主动切回电脑。消息共用队列，切换不会自动清未读。

前台直连验收后按 `Ctrl+C`，用同样配置安装自启动：

```sh
.venv/bin/python scripts/install_macos_service.py --host "$COMPUTER_LAN_IP" --state-dir .runtime/live --ble "$PASSPORT_BLE_NAME"
```

### 10.1 可选：Passport 通过 Wi-Fi 直连

先完成手机 BLE 配对和正常同步，再尝试这条可选路径：

1. 让 Passport 与电脑处于同一局域网。网络需要支持 **2.4GHz、WPA2 或 WPA2/WPA3 兼容模式**。
2. 电脑中继地址必须是私有 IPv4 的 HTTP 地址，例如 `http://电脑的局域网IP:18765`；暂不支持域名、HTTPS、企业 Wi-Fi 或门户登录。
3. 手机进入「设备 → 设置 Wi-Fi」，填写网络名称和密码，点击「发送到 Passport」。
4. “网络配置已保存”表示设备收到了配置。继续等待“Wi-Fi 已连接”，这才表示实际直连同步成功。
5. 以后可点击「使用已保存的 Wi-Fi」重新启用；不用重复输入密码。

Wi-Fi 接管期间会释放 BLE，节省同时运行两种无线协议所需的内存。手机仍能通过中继查看通知和调整设备。
点击「返回手机转发」或在亮屏时长按 OK，可切回 BLE。网络或中继不可达时，设备会结束直连并恢复 BLE；手机服务仍需运行才能接续，通常几十秒内恢复。失败后保持手机路径，避免不断争抢连接。
网络信息只保存在设备的 NVS，手机配置和事件数据库不保存 Wi-Fi 密码。使用可信局域网或自己的 VPN；中继 HTTP 本身没有 TLS。
点击「忘记 Passport 的 Wi-Fi」并确认，可删除设备网络和直连凭据；原蓝牙配对保留。更换电脑 IP 或中继密钥后需重新配置 Wi-Fi。

## 11. 可选：带手机和 Passport 出门

保持电脑中继运行。在手机上开启你已经配置好的 WireGuard，使手机能够访问**电脑的中继 IP 和端口**。
App 中继地址仍填隧道内可路由的电脑地址；若内外网使用不同地址，需要相应修改。
在 App“连接”页选择“使用手机转发”，让手机与 Passport 保持 BLE 可达。

WireGuard 的握手成功不等于已经配置了到电脑局域网的路由。需同时满足路由/AllowedIPs、回程路由和防火墙允许访问中继。
不要把中继 HTTP 端口直接暴露到公网。本项目不代配 VPN；本次发布的外网 WireGuard 场景未做实地验收。

## 12. 常见问题：从上往下检查

| 现象 | 先检查什么 |
| --- | --- |
| `uv: command not found` | 安装后重新打开终端，重试 `uv --version` |
| `codex: command not found` / `Usage unavailable` | 同一终端能否执行已登录的 Codex CLI；使用自定义路径时给 `serve` 或安装器传 `--codex /实际路径/codex` |
| `Cannot assign requested address` | `--host` 必须是这台电脑目前实际拥有的 IP |
| `Address already in use` | 之前的前台或自启动中继是否仍在运行；保留一个即可 |
| 手机“电脑 未连接” | 地址含 `http://` 和 `:18765`，密钥完整，电脑中继运行，手机能到达局域网；不要填 ADB 端口 |
| 配对码错误/超时 | 使用 Passport 当前码；不是中继密钥，也不是无线调试配对码；放近后重试 |
| 找不到 Passport / 回执超时 | 蓝牙和权限已开；旧系统定位已开；其他手机/电脑是否占用 BLE；亮屏后长按 OK 重连 |
| 手机后台一段时间后中断 | 系统应用电池设置允许后台活动，重新打开 App 并点“连接 Passport”；强行停止 App 后需要重新启动同步 |
| 收不到刚打开会话的通知 | 安装 Hooks 后重新打开 Codex 会话；检查安装与服务是否使用同一状态目录和 `CODEX_HOME` |
| 只有用量，没有开始提醒 | 开始事件本来就静默；用真实完成或待回复事件验证 |
| 额度不可用 | 可能接口未返回、窗口已过期或超过 180 秒未更新；检查电脑日志，不代表额度已用光 |
| 熄屏后 OK 没有清未读 | 第一次只唤醒；屏幕亮起后再短按一次 |
| 想立即熄屏 | 目前没有主动熄屏按键；可在手机「设备 → 自动熄屏」设为最短 15 秒，应用后停止操作等待熄屏 |
| 完成任务没有声音 | 完成只更新列表；待回复/待批准才短响，至少间隔两分钟；也检查顶部静音图标 |
| Wi-Fi 显示保存成功但未联网 | 等待实际同步回执；检查 2.4GHz、密码、电脑中继地址及防火墙；失败会回 BLE |
| 外网 WireGuard 已连接仍失败 | 检查到电脑中继地址的隧道路由、防火墙和回程路由，不能只看 VPN 开关 |

macOS 服务日志在项目的 `.runtime/live/service-error.log`，可本机查看最近几行：

```sh
cd "$HOME/codex-passport"
tail -n 40 .runtime/live/service-error.log
```

提交问题前去掉密钥、会话摘要、个人路径、设备地址。不要上传 `.runtime`、Flash 备份或手机配置文件。

## 13. 更新、暂停与卸载

更新代码前，先停止前台中继或执行 `launchctl bootout "gui/$(id -u)/dev.codex-passport.relay"`。
然后在项目目录执行：

```sh
cd "$HOME/codex-passport"
git pull --ff-only
uv sync --extra ble
uv run codex-passport --state-dir .runtime/live install-hooks
```

再按第 9/10 步以自己的 IP 和 BLE 配置重新启动。重新打开 Codex 会话。
除非发布说明要求更新设备文件，否则无需重装 APK 或重烧固件；保留状态目录即可保留中继密钥和通知队列。

临时暂停：手机点“停止手机同步”。这只暂停手机链路；电脑直连或采集器需在电脑停止。
取消 macOS 登录启动：

```sh
launchctl bootout "gui/$(id -u)/dev.codex-passport.relay"
rm "$HOME/Library/LaunchAgents/dev.codex-passport.relay.plist"
```

彻底卸载前，先在自己的 Codex 用户目录 `hooks.json` 中移除 command 含 `-m codex_passport ` 的本项目 hook，保留其他 hook。
安装前备份文件名为 `hooks.json.passport-backup-*`；只有确认安装后没新增其他 Hooks 时才整份恢复备份。
重新打开 Codex 会话，再卸载手机 App、删除项目目录。删除 `.runtime/live` 会丢失密钥及事件历史，下一次需重新配置手机。

## 进一步阅读

- [Python 包、uvx 与安装方式](PYTHON.md)
- [构建与开发说明](DEVELOPMENT.md)
- [测试记录与尚未实测项目](VALIDATION.md)
- [中文项目说明](../README.zh_CN.md)
