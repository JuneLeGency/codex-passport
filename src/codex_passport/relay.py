"""Authenticated LAN relay intended to be reached over the user's WireGuard tunnel."""
import asyncio
import hmac
import json
import logging
import secrets
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from .store import Store
from .sessions import Sessions
from .usage import UsageClient
from .routing import Routing

LOG = logging.getLogger('codex-passport')


def token_file(directory):
    path = directory / 'relay-token'
    if not path.exists():
        with path.open('x') as stream:
            path.chmod(0o600)
            stream.write(secrets.token_urlsafe(32))
    return path


def handler_factory(directory, token, routing=None):
    routing = routing or Routing()
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def authorized(self):
            return hmac.compare_digest(self.headers.get('Authorization', ''), 'Bearer ' + token)

        def reply(self, status, value):
            raw = json.dumps(value, ensure_ascii=False, separators=(',', ':')).encode()
            self.send_response(status)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(raw)))
            self.send_header('Cache-Control', 'no-store')
            self.end_headers()
            self.wfile.write(raw)

        def do_GET(self):
            if not self.authorized():
                return self.reply(401, {'error': 'unauthorized'})
            if self.path != '/v1/snapshot':
                return self.reply(404, {'error': 'not found'})
            from .cli import snapshot
            store = Store(directory)
            try:
                seq = int(time.time() * 1000) % 2_000_000_000 + 1
                frame = snapshot(store, seq, store.pending())
                frame['_link'] = routing.view()
                self.reply(200, frame)
            finally:
                store.close()

        def do_POST(self):
            if not self.authorized():
                return self.reply(401, {'error': 'unauthorized'})
            if self.path not in ('/v1/ack', '/v1/route'):
                return self.reply(404, {'error': 'not found'})
            try:
                length = int(self.headers.get('Content-Length', '-1'))
                if not 0 <= length <= 1024:
                    return self.reply(413, {'error': 'invalid length'})
                self.connection.settimeout(5)
                payload = json.loads(self.rfile.read(length))
                if not isinstance(payload, dict):
                    raise ValueError()
            except (ValueError, OSError):
                return self.reply(400, {'error': 'invalid JSON'})
            if self.path == '/v1/route':
                try:
                    return self.reply(200, routing.select(payload.get('owner')))
                except ValueError as exc:
                    return self.reply(400, {'error': str(exc)})
            store = Store(directory)
            try:
                event = payload.get('event')
                read = payload.get('read')
                if isinstance(event, int) and not isinstance(event, bool) and event > 0:
                    store.delivered(event)
                if isinstance(read, int) and not isinstance(read, bool) and read >= 0:
                    store.mark_read(min(read, store.summary()['latest']))
                self.reply(200, {'ok': True})
            finally:
                store.close()
    return Handler


async def serve(args, store):
    token = token_file(args.state_dir).read_text().strip()
    routing = Routing(desktop=bool(args.ble))
    server = ThreadingHTTPServer((args.host, args.port), handler_factory(args.state_dir, token, routing))
    server.daemon_threads = True
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    LOG.info('Relay listening at %s:%d; pairing token file: %s', args.host, args.port, token_file(args.state_dir))
    watcher = Sessions(args.codex_home, store)
    usage = UsageClient(args.codex)
    task = None
    ble_task = None
    if args.ble:
        from .ble import worker
        ble_task = asyncio.create_task(worker(args.ble, store, routing))
    last = 0
    try:
        while True:
            watcher.poll()
            if task is None and time.monotonic() - last >= 60:
                task = asyncio.create_task(usage.fetch())
                last = time.monotonic()
            if task and task.done():
                try:
                    store.put('usage', task.result())
                    LOG.info('Account usage refreshed')
                except Exception as exc:
                    LOG.warning('Usage refresh unavailable (%s)', type(exc).__name__)
                task = None
            await asyncio.sleep(1)
    finally:
        if ble_task:
            ble_task.cancel()
            await asyncio.gather(ble_task, return_exceptions=True)
        if task:
            task.cancel()
            await asyncio.gather(task, return_exceptions=True)
        await usage.close()
        server.shutdown()
        server.server_close()
