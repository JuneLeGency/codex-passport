"""Bounded, ephemeral device commands. Secrets never enter the event database."""
import copy
import ipaddress
import secrets
import threading
import time
from urllib.parse import urlsplit


def private_endpoint(value):
    if not isinstance(value, str) or len(value) > 96:
        raise ValueError('Use a private IPv4 HTTP relay address')
    url = urlsplit(value)
    try:
        address = ipaddress.IPv4Address(url.hostname)
        port = url.port or 80
    except (ValueError, TypeError):
        raise ValueError('Use a private IPv4 HTTP relay address') from None
    private = any(address in ipaddress.ip_network(n) for n in ('10.0.0.0/8', '172.16.0.0/12', '192.168.0.0/16'))
    if not private or url.scheme != 'http' or url.username or url.password or url.query or url.fragment or url.path not in ('', '/') or not 1 <= port <= 65535:
        raise ValueError('Use a private IPv4 HTTP relay address')
    return f'http://{address}:{port}'


class DeviceCommands:
    def __init__(self, token, routing, clock=time.time):
        self.token, self.routing, self.clock = token, routing, clock
        self.lock = threading.RLock()
        self.pending = None
        self.expires = 0
        self.status = {}
        self.device = {}
        self.seen = 0

    def issue(self, payload):
        if not isinstance(payload, dict):
            raise ValueError('Invalid command')
        op = payload.get('op')
        command = {'op': op}
        if op == 'settings':
            for name, low, high in (('brightness', 10, 80), ('idle', 15, 120), ('volume', 20, 80)):
                value = payload.get(name)
                if type(value) is not int or not low <= value <= high:
                    raise ValueError('Invalid device setting')
                command[name] = value
            if type(payload.get('muted')) is not bool:
                raise ValueError('Invalid mute setting')
            command['muted'] = payload['muted']
        elif op == 'wifi':
            if type(payload.get('enabled')) is not bool:
                raise ValueError('Invalid Wi-Fi mode')
            command['enabled'] = payload['enabled']
            if command['enabled']:
                # Reuse already provisioned credentials only with an explicit request.
                if payload.get('saved') is True:
                    command['saved'] = True
                else:
                    ssid, password = payload.get('ssid'), payload.get('password')
                    if not isinstance(ssid, str) or not 1 <= len(ssid.encode()) <= 32 or any(ord(c) < 32 for c in ssid):
                        raise ValueError('Wi-Fi name must be 1–32 UTF-8 bytes')
                    if not isinstance(password, str) or not 8 <= len(password.encode()) <= 63 or any(ord(c) < 32 for c in password):
                        raise ValueError('Wi-Fi password must be 8–63 UTF-8 bytes')
                    command.update(ssid=ssid, password=password, endpoint=private_endpoint(payload.get('endpoint')), token=self.token)
        elif op not in ('identify', 'forget_wifi'):
            raise ValueError('Unsupported command')
        with self.lock:
            self.current()
            if self.pending:
                raise ValueError('Wait for the previous device command')
            command['id'] = secrets.randbelow(2_000_000_000) + 1
            self.pending, self.expires = command, self.clock() + 90
            self.status = {'id': command['id'], 'state': 'pending'}
            return dict(self.status)

    def current(self):
        with self.lock:
            if self.pending and self.clock() > self.expires:
                self.pending = None
                self.status['state'] = 'expired'
            return copy.deepcopy(self.pending)

    def acknowledge(self, reply):
        with self.lock:
            self.current()
            meta = reply.get('device')
            if isinstance(meta, dict):
                clean = {}
                for key, lo, hi in (('battery', -1, 100), ('brightness', 10, 80), ('idle', 15, 120), ('volume', 20, 80), ('wifi', 0, 4)):
                    value = meta.get(key)
                    if type(value) is int and lo <= value <= hi:
                        clean[key] = value
                for key in ('muted', 'configured'):
                    if type(meta.get(key)) is bool:
                        clean[key] = meta[key]
                if isinstance(meta.get('fw'), str) and len(meta['fw']) <= 24 and all(c in '0123456789.-dev' for c in meta['fw']):
                    clean['fw'] = meta['fw']
                self.device, self.seen = clean, self.clock()
            if self.pending and reply.get('cmd') == self.pending['id'] and type(reply.get('cmd_ok')) is bool:
                command, self.pending = self.pending, None
                self.status['state'] = 'applied' if reply['cmd_ok'] else 'failed'
                if reply['cmd_ok']:
                    if command['op'] == 'wifi' and command['enabled']:
                        self.routing.select('wifi')
                    elif command['op'] in ('wifi', 'forget_wifi'):
                        self.routing.select('phone')

    def view(self):
        with self.lock:
            self.current()
            return dict(settings=dict(self.device), seenAt=self.seen, command=dict(self.status))
