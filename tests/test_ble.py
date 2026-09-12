import asyncio
import json
import tempfile
import unittest
from pathlib import Path
from codex_passport.ble import Receipts, deliver
from codex_passport.routing import Routing
from codex_passport.store import Store
from codex_passport.cli import device_frame, snapshot

class RouteTests(unittest.TestCase):
    def test_bounded_handoff_and_sticky_fallback(self):
        clock=[0]
        r=Routing(True,lambda:clock[0])
        generation=r.view()['generation']
        r.acknowledged(generation)
        clock[0]=61
        self.assertEqual(r.view()['owner'],'phone')
        clock[0]=1000
        self.assertEqual(r.view()['owner'],'phone')
        r.select('desktop')
        r.failed(generation)  # A late callback cannot cancel a newer handoff.
        self.assertEqual(r.view()['owner'],'desktop')
        r.select('phone')
        r.acknowledged(r.view()['generation'])
        self.assertEqual(r.view()['owner'],'phone')
    def test_no_unconfigured_desktop(self):
        r=Routing()
        with self.assertRaises(ValueError):r.select('desktop')

class DeliveryTests(unittest.IsolatedAsyncioTestCase):
    async def test_fragmented_authenticated_writes_and_receipt(self):
        with tempfile.TemporaryDirectory() as d:
            store=Store(Path(d))
            store.event('test','turn','completed')
            store.decorate('test',title='Private title',body='Private summary')
            pending=store.pending()
            receipts=Receipts(store)
            class Client:
                mtu_size=23
                raw=bytearray()
                async def write_gatt_char(self, uuid, data, response):
                    self.assert_response=response
                    assert response and len(data)<=20
                    self.raw.extend(data)
                    if self.raw.endswith(b'\n'):
                        frame=json.loads(self.raw)
                        assert b'Private' not in self.raw
                        assert not frame['recent']
                        assert len(self.raw)<512
                        wrong=json.dumps(dict(v=1,ack=frame['seq'],event=pending['id']+1)).encode()+b'\n'
                        receipts.receive(None,wrong)
                        assert store.pending() is not None
                        reply=json.dumps(dict(v=1,ack=frame['seq'],event=frame['event']['id'])).encode()+b'\n'
                        receipts.receive(None,reply[:7]);receipts.receive(None,reply[7:])
            await deliver(Client(),receipts,store,12)
            self.assertIsNone(store.pending())
            self.assertEqual(store.summary()['unread'],1)
            receipts.receive(None,b'{"v":1,"read":9999}\n')
            self.assertEqual(store.summary()['unread'],0)
            store.close()
