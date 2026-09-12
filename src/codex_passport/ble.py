"""Cross-platform BLE delivery; paired GATT writes and explicit device receipts."""
import asyncio
import json
import logging
import time

from .cli import snapshot, device_frame

SERVICE = 'fac06f64-6578-2d70-6173-73706f727401'
RX = 'fac06f64-6578-2d70-6173-73706f727402'
TX = 'fac06f64-6578-2d70-6173-73706f727403'
LOG = logging.getLogger('codex-passport')


class Receipts:
    def __init__(self, store, commands=None):
        self.store = store
        self.commands = commands
        self.buffer = bytearray()
        self.pending = None
        self.ack = asyncio.Event()

    def receive(self, _, data):
        self.buffer.extend(data)
        while b'\n' in self.buffer:
            line, _, rest = self.buffer.partition(b'\n')
            self.buffer = bytearray(rest)
            try:
                reply = json.loads(line)
            except ValueError:
                continue
            if not isinstance(reply, dict) or reply.get('v') != 1:
                continue
            if self.commands:
                self.commands.acknowledge(reply)
            if self.pending and reply.get('ack') == self.pending[0]:
                event = self.pending[1]
                if event and reply.get('event') == event['id']:
                    self.store.delivered(event['id'])
                elif event:
                    continue
                self.ack.set()
            read = reply.get('read')
            if isinstance(read, int) and not isinstance(read, bool) and read >= 0:
                self.store.mark_read(min(read, self.store.summary()['latest']))
        if len(self.buffer) > 4096:
            self.buffer.clear()


async def deliver(client, receipts, store, seq):
    event = store.pending()
    frame = snapshot(store, seq, event)
    if receipts.commands:
        frame['cmd'] = receipts.commands.current()
    frame = device_frame(frame)
    raw = json.dumps(frame, ensure_ascii=False, separators=(',', ':')).encode() + b'\n'
    if len(raw) > 2048:
        raise ValueError('Snapshot exceeds the device frame limit')
    receipts.pending = (seq, event)
    receipts.ack.clear()
    size = max(20, min(180, client.mtu_size - 3))
    security_deadline = time.monotonic() + 110
    for offset in range(0, len(raw), size):
        while True:
            try:
                await client.write_gatt_char(RX, raw[offset:offset+size], response=True)
                break
            except Exception as exc:
                # CoreBluetooth can return an ATT security error while its PIN dialog
                # is still open. Keep this connection alive; never downgrade security.
                security_error = any(s in str(exc).lower() for s in
                                     ('insufficient encryption', 'insufficient authentication'))
                if not security_error or not client.is_connected or time.monotonic() >= security_deadline:
                    raise
                await asyncio.sleep(1)
    await asyncio.wait_for(receipts.ack.wait(), timeout=15)


async def connect_and_sync(device, store, routing, generation, once=False, commands=None):
    from bleak import BleakClient, BleakScanner
    # Select explicitly; never pair with an arbitrary nearby Passport.
    found = await BleakScanner.find_device_by_filter(
        lambda d, ad: d.address.lower() == device.lower() or ad.local_name == device,
        timeout=30, service_uuids=[SERVICE])
    if found is None:
        raise TimeoutError('Selected Passport is not advertising nearby')
    if routing.view()['generation'] != generation:
        return False
    async with BleakClient(found, pair=True, timeout=45) as client:
        receipts = Receipts(store, commands)
        await client.start_notify(TX, receipts.receive)
        seq = int(time.time()*1000) % 2_000_000_000
        last_send, signature = 0, ''
        while client.is_connected:
            route = routing.view()
            if route['owner'] != 'desktop' or route['generation'] != generation:
                return False
            frame = device_frame(snapshot(store, 0, store.pending()))
            if commands:
                frame['cmd'] = commands.current()
            frame.pop('now')
            current = json.dumps(frame, sort_keys=True)
            if current != signature or time.monotonic() - last_send >= 10:
                seq = seq % 2_000_000_000 + 1
                if last_send:
                    await asyncio.wait_for(deliver(client, receipts, store, seq), timeout=35)
                else:
                    await deliver(client, receipts, store, seq)
                routing.acknowledged(generation)
                (LOG.info if not last_send else LOG.debug)('Desktop BLE device ACK confirmed (sequence %d)', seq)
                last_send, signature = time.monotonic(), current
                if once:
                    return True
            await asyncio.sleep(1)
        raise ConnectionError('Passport disconnected')


async def worker(device, store, routing, once=False, commands=None):
    while True:
        route = routing.view()
        if route['owner'] == 'desktop':
            generation = route['generation']
            try:
                operation = connect_and_sync(device, store, routing, generation, once, commands)
                acknowledged = await asyncio.wait_for(operation, 170) if once else await operation
                if once:
                    if not acknowledged:
                        raise TimeoutError('Handoff expired before a device acknowledgement')
                    return
            except Exception as exc:
                LOG.warning('Desktop BLE unavailable (%s); returning ownership to phone', type(exc).__name__)
                routing.failed(generation)
                if once:
                    raise
        await asyncio.sleep(1)
