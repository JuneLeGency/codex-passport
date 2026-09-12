# Codex Passport

[English](README.md)
<img src="docs/images/dashboard-example.png" alt="Passport graphical dashboard" width="240">

设备实机渲染的合成示例，不包含账号用量或会话内容。


为 FoloToy AI Passport 独立开发的 Codex 通知与用量应用。
支持**电脑 → 加密 BLE → Passport**，或**电脑中继 → 局域网/WireGuard → Android → 加密 BLE → Passport**。
USB 仅用于烧录和诊断。应用、协议、采集器和 Android 界面均独立编写；官方 BSP、
Material Components是保留许可证的第三方依赖，不复用参考项目的应用实现。

## 通知规则

- 任务开始、会话打开/关闭、压缩等状态静默更新，不亮屏、不计入未读。
- 任务完成、执行失败、中断、需要批准、等待回复进入通知队列。
- 过滤 Codex 内部 guardian 审核与记忆整理会话，保留用户可见会话和子代理事件。
- 按会话合并收件箱，显示标题与简短结果/问题。手机展示四个最近会话；Passport 用图标和数字显示汇总。
- SQLite 持久化、去重；只有 Passport 的明确回执才算送达。断线后继续补送。
- 已读操作不会替用户执行批准；批准、回复仍在 Codex 中完成。

标题最多 60 字节，摘要最多 120 字节，按 UTF-8 字符边界截断。Passport 图形模式不渲染会话正文，完整摘要在手机上查看。摘要去除代码块和常见
凭据模式；只通过自己的中继和已配对设备传递。不会发送完整对话或原始命令参数。
摘要仍可能包含会话中的私人内容，应只给自己的手机配置密钥。

全局 Hooks 负责生命周期和审批事件，增量日志作为兼容性补偿。安装 Hooks 后，已有
Codex 会话需重新打开才能加载。日志格式不是稳定 API。本项目没有接入 Codex Mobile
私有推送服务，不能保证与 Mobile 逐项等价；其他电脑的会话需在相应主机安装采集器。

## 运行中继

```sh
uv sync --extra ble
export COMPUTER_LAN_IP="填写电脑的局域网地址"
uv run codex-passport --state-dir .runtime/live install-hooks
uv run codex-passport --state-dir .runtime/live serve --host "$COMPUTER_LAN_IP" --port 18765
```

中继地址为 `http://<COMPUTER_LAN_IP>:18765`，与手机的无线 ADB 端口无关。
电脑与手机保存同一专用密钥：`.runtime/live/relay-token`。Android“连接”页可配置地址与密钥。
离开局域网时，开启你现有的 WireGuard，让手机能路由到该电脑地址。跨公网不直接开放 HTTP。
Android 允许私有网络 HTTP 或 HTTPS；这里使用已有 WireGuard 保护远程链路。

macOS 可安装仅监听指定局域网地址的用户服务：

```sh
.venv/bin/python scripts/install_macos_service.py --host "$COMPUTER_LAN_IP"
launchctl print gui/$(id -u)/dev.codex-passport.relay
# 停止
launchctl bootout gui/$(id -u)/dev.codex-passport.relay
```

该服务随用户登录启动；电脑地址改变时，用新地址重新运行安装脚本。Hooks 安装器会备份并合并
现有配置，不改变审批决定。安装 Hooks 使用持久 Python 环境，避免依赖临时 uvx 缓存。

## Android 与 Passport

先构建 Android，再安装 `android/app/build/outputs/apk/debug/app-debug.apk`。Android 界面使用官方 **Material 3 Expressive 1.14.0**，
含概览、通知与连接三页、日夜主题、波形用量条和明确的待回复状态。首次连接输入 Passport
显示的六位配对码；已有绑定自动重连。

Android 6+；Android 12+ 使用附近设备权限，旧系统扫描需定位权限及定位开关。
同步通过 connectedDevice 前台服务运行；系统电池限制可能影响后台存活。
手机不替用户配置 WireGuard。当前 APK 是调试签名的内部测试版本。

Passport 使用独立的 240×320 图形仪表：外环表示剩余额度，内环表示本周期剩余时间，
中心数字是剩余额度。外环橙色表示消耗领先时间超过 5 个百分点，绿色表示基本同步，
蓝色表示用得较慢。颜色表示相对匀速预算的位置，不是未来消耗预测；周期最初 1% 时间不判断快慢。
下方三个图标分别表示运行、待回复、未读数量；连接与电量放在顶部。
上/下键切换额度窗口，OK 标为已读，长按 OK 重连并保留配对。
熄屏后首次按键只唤醒，不改变页面或已读状态。60 秒无提醒或按键时熄屏，新提醒或按键唤醒。
BLE 使用低功耗扫描、较长连接间隔、从机延迟；未变化数据约每 12 秒发送一次心跳
（3 秒轮询、至少间隔 10 秒）。尚未用电流仪测量，不承诺续航天数。

额度来自官方 `account/rateLimits/read`，可用时读取 `account/usage/read`；窗口长度按接口
实际返回显示。过期或超过 180 秒未更新的额度显示不可用，不推测满额。

## 构建与验证

```sh
uv run python -m unittest discover -s tests -v
uv build
uvx --from ./dist/codex_passport_sync-0.1.0-py3-none-any.whl codex-passport --help
```

Python 3.10+。本地 wheel 可用 uvx，无须先发布 PyPI。尚未发布到 PyPI。
Android：Gradle 8.14.4、AGP 8.10.1，`gradle -p android assembleDebug`。
固件：ESP-IDF 5.5.3，`idf.py -C firmware build`；先运行 `python scripts/fetch_bsp.py` 获取固定版本 BSP，CMake 通过路径引用。

烧录必须**只写应用分区**：

```sh
uv run --no-project --with esptool python -m esptool --port PORT write-flash 0x10000 firmware/build/codex-passport.bin
```

应用分区 3 MB。构建器会提示 1 MB Recovery 分区放不下
该应用，这不影响 3 MB factory 应用分区；不将本应用写入 Recovery。
保留原 bootloader、分区表、cardid、NVS 绑定和 Recovery；不运行全盘擦除或合并烧录。
请将设备原始 Flash 备份保存在源码目录之外；备份含设备私有信息，不随包发布。

USB 诊断：`codex-passport --state-dir .runtime/live run --port PORT --once`。
屏幕采集：`scripts/capture_device.py`。完整通知历史：`codex-passport --state-dir .runtime/live events`。

## 参考与依赖

- [FoloToy AI Passport BSP](https://github.com/folotoy/ai-passport)，固定提交 `f75873f1aab24ac4c0ba9394c131669f66cce650`。
- [Codex Usage AI Passport](https://github.com/zt20/codex-usage-ai-passport)，仅研究，固定提交 `b2e6a3fd2d405fe4f397fc74084715ef1cb83128`。
- [Codex Hooks](https://learn.chatgpt.com/docs/hooks)、[App Server](https://learn.chatgpt.com/docs/app-server)。
- [Building with M3 Expressive](https://m3.material.io/blog/building-with-m3-expressive)、[官方 Android 组件](https://github.com/material-components/material-components-android/releases/tag/1.14.0)。

## 电脑直接连接蓝牙与切换

```sh
uv run --extra ble codex-passport --state-dir .runtime/live serve --host "$COMPUTER_LAN_IP" --ble "Passport-XXXXXX"
# 常驻服务也可增加同样的 --ble 参数
```

使用设备广播名或操作系统分配的蓝牙地址，首次连接由系统弹窗完成六位码配对。
macOS 需允许启动该服务的程序使用蓝牙，首次配对应在前台完成。手机“连接”页可选电脑直连或手机转发。
只允许一条 BLE 连接：切换时手机先释放连接，电脑连接失败或回执超时后自动退回手机。
退回后保持手机连接，不因电脑仍在线而反复抢占；回到电脑旁再选择直连。VPN 可达不代表 BLE 在附近。
两条链路共用事件 ID、持久队列和设备回执；切换不会清空未读或自动批准任务。
独立诊断命令 `codex-passport --state-dir .runtime/live ble --device "Passport-XXXXXX" --once`
只发送现有采集状态，需先暂停手机转发；日常使用集成的 `serve --ble`。

## 验证边界

参见 [验证记录](docs/VALIDATION.md)。实体按键手动验收、脱离 Wi-Fi 的 WireGuard 实测和电流测量
需要真实场景协助，不应把自动化状态机测试当作这些项目的完成证明。

BLE 日常帧仅传图形所需用量、计数和回执 ID，不传会话标题、摘要或项目名；这些内容仅在手机上显示。
