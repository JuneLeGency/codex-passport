"""CLI, lifecycle-hook installation and a reconnecting USB worker."""
from __future__ import annotations
import argparse
import asyncio
import json
import logging
import os
from pathlib import Path
import shlex
import sys
import time
from .store import Store, LABELS, clipped
from .sessions import Sessions
from .usage import UsageClient

LOG = logging.getLogger('codex-passport')
HOOK_EVENTS = ['SessionStart', 'SessionEnd', 'UserPromptSubmit', 'Stop', 'Interrupt',
               'SubagentStart', 'SubagentStop', 'PermissionRequest', 'PreCompact', 'PostCompact',
               'PreToolUse', 'PostToolUse']


def state_dir():
    return Path(os.environ.get('CODEX_PASSPORT_STATE', str(Path.home()/'.local/state/codex-passport'))).expanduser()


def hook_config(command):
    events = {}
    for event in HOOK_EVENTS:
        handler = {'type': 'command', 'command': command, 'timeout': 3}
        # Small local SQLite write. Synchronous ordering avoids late Stop beating a new turn.
        group = {'hooks': [handler]}
        if event == 'PreToolUse':
            group['matcher'] = '.*(request_user_input|requestUserInput).*'
        events[event] = [group]
    return {'hooks': events}


def install_hooks(args):
    target = args.codex_home / 'hooks.json'
    target.parent.mkdir(parents=True, exist_ok=True)
    old = json.loads(target.read_text()) if target.exists() else {'hooks': {}}
    if not isinstance(old, dict) or not isinstance(old.get('hooks', {}), dict):
        raise ValueError('Existing hooks.json has an unsupported structure')
    command = shlex.join([sys.executable, '-m', 'codex_passport', '--state-dir', str(args.state_dir.resolve()), 'hook'])
    merged = json.loads(json.dumps(old))
    hooks = merged.setdefault('hooks', {})
    # Idempotent: replace only entries installed by this application.
    for event, groups in hook_config(command)['hooks'].items():
        previous = hooks.setdefault(event, [])
        cleaned = []
        for group in previous:
            group = dict(group)
            group['hooks'] = [h for h in group.get('hooks', []) if '-m codex_passport ' not in h.get('command', '')]
            if group['hooks']:
                cleaned.append(group)
        hooks[event] = cleaned + groups
    if target.exists():
        backup = target.with_name('hooks.json.passport-backup-' + str(time.time_ns()))
        backup.write_bytes(target.read_bytes())
        backup.chmod(0o600)
    tmp = target.with_suffix('.passport-tmp')
    tmp.write_text(json.dumps(merged, indent=2) + '\n')
    tmp.chmod(0o600)
    tmp.replace(target)
    print(f'Hooks installed: {target}. Restart existing Codex sessions to load them.')


def port_name(explicit):
    if explicit:
        return explicit
    from serial.tools.list_ports import comports
    ports = [p.device for p in comports() if p.vid == 0x303A and p.pid == 0x1001]
    if len(ports) != 1:
        raise RuntimeError('Connect one Passport by USB, or specify --port')
    return ports[0]


def wire_event(event):
    return {'id': event['id'], 'kind': event['kind'], 'project': clipped(event['project'],32),
            'time': int(event['ts']), 'title': clipped(event.get('title') or event['project'],60),
            'body': clipped(event.get('body') or '',120)}


def snapshot(store, seq, event=None):
    now = int(time.time())
    usage = store.get('usage', {})
    windows = []
    for window in usage.get('windows', [None, None]):
        if not window or window['reset'] <= now or now-usage.get('updated',0) > 180:
            windows.append([-1, window['minutes'] if window else 0, 0])
        else:
            windows.append([window['remaining'], window['minutes'], window['reset']])
    frame = {'v': 1, 'seq': seq, 'now': now, **store.summary(), 'windows': windows,
            'tokens': usage.get('tokens') if isinstance(usage.get('tokens'), int) else -1,
            'today': usage.get('today') if isinstance(usage.get('today'), int) else -1,
            'recent': [wire_event(e) for e in store.inbox(4)],
            'event': wire_event(event) if event else None}
    # JSON escaping can expand punctuation; keep the BLE line within its 2048-byte limit.
    while frame['recent'] and len(json.dumps(frame,ensure_ascii=False,separators=(',',':')).encode())>1900:
        frame['recent'].pop()
    return frame


def device_frame(frame):
    """The graphical device needs counts, quota and receipt IDs, not conversation text."""
    result = {k: v for k, v in frame.items() if k != '_link'}
    result['recent'] = []
    event = frame.get('event')
    result['event'] = None if not event else {
        'id': event['id'], 'kind': event['kind'], 'project': '', 'time': event['time']}
    return result


async def run(args, store):
    import serial
    watcher = Sessions(args.codex_home, store)
    client = UsageClient(args.codex)
    lock = (args.state_dir / 'worker.lock').open('a+')
    if os.name == 'posix':
        import fcntl
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise RuntimeError('A Passport worker is already running for this state directory')
    usage_task = None
    link = None
    last_usage = last_scan = last_send = 0.0
    seq = 0
    pending_seq = 0
    pending_event = None
    incoming = bytearray()
    acknowledged = False
    try:
        while True:
            now = time.monotonic()
            if now-last_scan >= 1:
                watcher.poll()
                last_scan = now
            if usage_task is None and now-last_usage >= 60:
                usage_task = asyncio.create_task(client.fetch())
                last_usage = now
            if usage_task and usage_task.done():
                try:
                    store.put('usage', usage_task.result())
                    LOG.info('Account usage refreshed')
                except Exception as exc:
                    LOG.warning('Usage unavailable (%s); retrying in 60s', type(exc).__name__)
                usage_task = None
            if args.dry_run:
                if usage_task:
                    try:
                        store.put('usage', await usage_task)
                    except Exception as exc:
                        LOG.warning('Usage unavailable (%s)', type(exc).__name__)
                    usage_task = None
                print(json.dumps(snapshot(store, 1), ensure_ascii=False, indent=2))
                return
            try:
                if link is None:
                    link = serial.Serial(port_name(args.port), 115200, timeout=0, write_timeout=1)
                    incoming.clear()
                    pending_seq = 0
                    last_send = 0
                    LOG.info('USB connected: %s', link.port)
                data = link.read(min(link.in_waiting, 8192))
                incoming.extend(data)
                while b'\n' in incoming:
                    line, _, rest = incoming.partition(b'\n')
                    incoming = bytearray(rest)
                    try:
                        reply = json.loads(line)
                    except ValueError:
                        continue
                    if not isinstance(reply, dict) or reply.get('v') != 1:
                        continue
                    if reply.get('ack') == pending_seq and pending_seq:
                        if pending_event:
                            store.delivered(pending_event['id'])
                            LOG.info('Notification delivered: #%d %s', pending_event['id'], pending_event['kind'])
                        pending_seq, pending_event = 0, None
                        acknowledged = True
                    if isinstance(reply.get('read'), int):
                        store.mark_read(min(reply['read'], store.summary()['latest']))
                if len(incoming) > 16384:
                    incoming.clear()
                if pending_seq and now-last_send > 5:
                    raise RuntimeError('Passport acknowledgement timed out')
                event = store.pending()
                if not pending_seq and (event or now-last_send >= 2):
                    seq = seq + 1 if seq < 2_000_000_000 else 1
                    payload = json.dumps(snapshot(store, seq, event), ensure_ascii=False, separators=(',', ':')).encode()+b'\n'
                    if len(payload) > 2048:
                        raise ValueError('Snapshot exceeds device frame size')
                    link.write(payload)
                    pending_seq, pending_event, last_send = seq, event, now
                if args.once and acknowledged:
                    return
            except (serial.SerialException, OSError, RuntimeError) as exc:
                LOG.warning('USB disconnected (%s); reconnecting', type(exc).__name__)
                if link:
                    link.close()
                link = None
                await asyncio.sleep(1)
            await asyncio.sleep(0.1)
    finally:
        if link:
            link.close()
        if usage_task:
            usage_task.cancel()
            await asyncio.gather(usage_task, return_exceptions=True)
        await client.close()
        lock.close()


def main():
    parser = argparse.ArgumentParser(description='Codex lifecycle inbox and account usage on AI Passport')
    parser.add_argument('--state-dir', type=Path, default=state_dir())
    parser.add_argument('--codex-home', type=Path, default=Path(os.environ.get('CODEX_HOME', str(Path.home()/'.codex'))))
    sub = parser.add_subparsers(dest='command', required=True)
    worker = sub.add_parser('run')
    worker.add_argument('--port')
    worker.add_argument('--codex', default='codex')
    worker.add_argument('--dry-run', action='store_true')
    worker.add_argument('--once', action='store_true', help='Exit after one device acknowledgement')
    hook = sub.add_parser('hook', help='Consume Codex hook JSON from stdin, or notify JSON argument')
    hook.add_argument('payload', nargs='?')
    sub.add_parser('install-hooks')
    relay = sub.add_parser('serve', help='Expose the authenticated relay to an Android WireGuard peer')
    relay.add_argument('--host', default='127.0.0.1')
    relay.add_argument('--port', type=int, default=18765)
    relay.add_argument('--codex', default='codex')
    relay.add_argument('--ble', metavar='DEVICE', help='Optional Passport BLE name/address; phone fallback on failure')
    bluetooth = sub.add_parser('ble', help='Deliver the existing collector state directly over BLE')
    bluetooth.add_argument('--device', required=True, help='Passport advertising name or OS Bluetooth address')
    bluetooth.add_argument('--once', action='store_true')
    sub.add_parser('status')
    sub.add_parser('events')
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format='%(asctime)s %(message)s')
    store = None
    try:
        store = Store(args.state_dir)
        if args.command == 'run':
            asyncio.run(run(args, store))
        elif args.command == 'serve':
            from .relay import serve
            asyncio.run(serve(args, store))
        elif args.command == 'ble':
            from .ble import worker
            from .routing import Routing
            asyncio.run(worker(args.device, store, Routing(desktop=True), args.once))
        elif args.command == 'hook':
            raw = args.payload if args.payload is not None else sys.stdin.read(4*1024*1024)
            store.ingest_hook(json.loads(raw))
        elif args.command == 'install-hooks':
            install_hooks(args)
        elif args.command == 'status':
            print(json.dumps(snapshot(store, 0), indent=2))
        elif args.command == 'events':
            print(json.dumps(store.recent(100), indent=2))
    except KeyboardInterrupt:
        pass
    except Exception as exc:
        if args.command == 'hook':
            # Notification failures must never block or authorize a Codex tool.
            return
        parser.exit(1, f'{type(exc).__name__}: {exc}\n')
    finally:
        if store is not None:
            store.close()
