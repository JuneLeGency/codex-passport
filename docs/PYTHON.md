# Python 包：安装、uvx 和更新

Python 包名是 **`codex-passport-sync`**，安装后执行的命令叫 **`codex-passport`**。
Python 3.10+；电脑 BLE 直连需要 `[ble]` 可选依赖。Python 中继 0.1.1 与 APK/固件 0.1.0 配套使用，设备端不需要为本次文档与打包更新重新安装。

[PyPI 项目页](https://pypi.org/project/codex-passport-sync/) · [GitHub Releases](https://github.com/JuneLeGency/codex-passport/releases)

第一次用整套设备，先按 [从零入门指南](GETTING_STARTED.zh_CN.md) 操作。下面是**替代安装方式**；不要与源码安装同时启动两个中继。

## 方式一：uvx 临时运行

安装 [uv](https://docs.astral.sh/uv/getting-started/installation/) 后：

```sh
uvx --from 'codex-passport-sync==0.1.1' codex-passport --help
```

命令会自动下载 Python 包并显示帮助。临时启动中继也可以：

```sh
export COMPUTER_LAN_IP="替换为电脑的局域网 IPv4 地址"
uvx --from 'codex-passport-sync[ble]==0.1.1' codex-passport serve --host "$COMPUTER_LAN_IP" --port 18765
```

按 `Ctrl+C` 停止。默认状态目录是 `~/.local/state/codex-passport`，首次启动 `serve` 后在该目录生成 `relay-token`。
本例没有安装 Hooks；日志兼容采集可运行，但要稳定接收 Hooks 事件，请使用下一种持久安装方式。

**不要用临时 uvx 环境安装长期 Hooks 或登录服务**。Hooks 会记录安装时 Python 的绝对路径，临时缓存清理后可能失效。
这一区别也见 [uv 工具环境说明](https://docs.astral.sh/uv/guides/tools/)。

## 方式二：uv tool 持久安装

macOS / Linux 终端：

```sh
uv tool install 'codex-passport-sync[ble]==0.1.1'
uv tool update-shell
```

重新打开终端，然后运行：

```sh
codex-passport --help
codex-passport install-hooks
export COMPUTER_LAN_IP="替换为电脑的局域网 IPv4 地址"
codex-passport serve --host "$COMPUTER_LAN_IP" --port 18765
```

看到 `Relay listening at` 后保持该终端运行。手机填写完整的 `http://电脑IP:18765` 和 `~/.local/state/codex-passport/relay-token` 中的密钥。
已打开的 Codex 会话需要重新打开。可按 [入门指南第 6 步](GETTING_STARTED.zh_CN.md#6-手机填写地址并首次配对) 完成手机与设备配对。

可选电脑直连：停止上面的前台进程，再运行：

```sh
codex-passport serve --host "$COMPUTER_LAN_IP" --port 18765 --ble "替换为自己的 Passport-广播名"
```

首次在操作系统配对弹窗输入 Passport 当前的六位码。BLE 只能有一个发送方，先让手机释放连接。
`uv tool` 安装的是中继 CLI；macOS 登录服务脚本随 **GitHub 源码**提供。
需要本项目自启动安装器时，推荐采用完整入门指南的源码安装方式；不要假设 wheel 内存在 `scripts/install_macos_service.py`。

## 方式三：下载 wheel，直接安装

无法使用 PyPI 时，下载 [Python 发布文件](https://github.com/JuneLeGency/codex-passport/releases/tag/v0.1.1) 中的 wheel 和 `SHA256SUMS`，在文件所在目录运行：

```sh
uvx --from ./codex_passport_sync-0.1.1-py3-none-any.whl codex-passport --help
```

持久安装并启用电脑 BLE：

```sh
uv tool install 'codex-passport-sync[ble] @ ./codex_passport_sync-0.1.1-py3-none-any.whl'
uv tool update-shell
```

wheel 本身在本地，依赖仍可能需要联网下载。安装后使用上一节的 `codex-passport` 命令。
版本与包内容以发布页和 SHA256 为准，不要使用来源不明的同名文件。

## 已有 Python 环境，也可以使用 pip

```sh
python3 -m venv .venv
.venv/bin/python -m pip install 'codex-passport-sync[ble]==0.1.1'
.venv/bin/codex-passport --help
```

后续以 `.venv/bin/codex-passport` 执行命令，保留这个虚拟环境。这里的路径语法用于 macOS/Linux；Windows 主机尚未完成验收。

## 目录与命令速查

| 安装方法 | 状态与密钥位置 |
| --- | --- |
| 从零指南中的源码方式 | 项目下 `.runtime/live`，每条命令均显式传入 `--state-dir` |
| 本页 uvx / uv tool / pip 默认方式 | `~/.local/state/codex-passport` |
| 自定义 | 全部命令使用同一个 `--state-dir /绝对路径`，或设置 `CODEX_PASSPORT_STATE` |

`--state-dir` 和 `--codex-home` 属于主命令参数，放在 `serve` / `install-hooks` 等子命令**前面**。
Hooks、采集器和诊断命令必须指向同一状态目录，否则会出现一边有通知、另一边看不到的情况。
自定义 `CODEX_HOME` 也应在安装 Hooks 与启动中继时保持一致。

| 命令 | 做什么 |
| --- | --- |
| `codex-passport install-hooks` | 备份并合并当前用户的 Codex Hooks；不会启动中继 |
| `codex-passport serve --host IP --port 18765` | 持续采集并提供手机中继 API |
| `codex-passport serve --host IP --ble NAME` | 同时提供电脑 BLE 直连与手机回退 |
| `codex-passport status` | 查看已保存快照，不会主动刷新账户数据 |
| `codex-passport events` | 查看最近最多 100 条事件，含送达/已读字段 |
| `codex-passport run --dry-run` | 采集并尝试刷新用量后输出快照；不连接 USB |
| `codex-passport ble --device NAME --once` | 只传输既有状态并等待设备回执；先暂停其他 BLE 发送方 |

上述诊断输出可能包含私人摘要。不要公开上传状态目录、密钥或原始输出。

## 更新与卸载

先停止正在运行的中继。uv tool 安装的用户更新到后续版本时：

```sh
uv tool upgrade codex-passport-sync
codex-passport install-hooks
```

如果原安装锁定了某个版本且升级仍保留旧版，用 `uv tool install --force 'codex-passport-sync[ble]==目标版本'` 显式选择版本。
重新打开 Codex 会话，再用原来的 IP、状态目录和 BLE 配置启动中继。
不要删除状态目录来升级；其中保存密钥和待送达队列。

卸载前先停止中继、移除自己 `hooks.json` 中 command 含 `-m codex_passport ` 的条目，并重新打开 Codex 会话；保留其他 Hooks。
之后执行 `uv tool uninstall codex-passport-sync`。不先移除 Hooks，Codex 会留下指向不存在解释器的通知命令。
