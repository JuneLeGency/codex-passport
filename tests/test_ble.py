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
                        assert frame['recent'][0]['title']=='Private title'
                        assert 'title' not in frame['event']
                        assert '_threads' not in frame and '_link' not in frame
                        assert len(self.raw)<2048
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

class ProgressTests(unittest.TestCase):
    def test_three_current_threads_keep_title_but_not_previous_completion(self):
        with tempfile.TemporaryDirectory() as d:
            store=Store(Path(d))
            for i in range(5):
                store.event(str(i),'first','completed')
                store.decorate(str(i),title='会话 '+str(i),body='旧一轮已完成')
            store.event('0','second','started',interaction=True)
            rows=store.progress()
            self.assertEqual(len(rows),3)
            self.assertEqual(rows[0]['kind'],'started')
            self.assertEqual(rows[0]['title'],'会话 0')
            self.assertEqual(rows[0]['body'],'')
            # Silent progress is displayable but never becomes a pending alert.
            self.assertEqual(store.pending()['kind'],'completed')
            store.event('0','second','closed')
            self.assertNotIn('会话 0',[r['title'] for r in store.progress()])
            store.close()

    def test_progress_frame_bounded_utf8_with_escaping(self):
        with tempfile.TemporaryDirectory() as d:
            store=Store(Path(d))
            for i in range(6):
                store.event(str(i),'t','input',project='引号"\\'*10)
                store.decorate(str(i),title='中文"\\'*30,body='问题"\\'*80)
            wire=device_frame(snapshot(store,2147483647,store.pending()))
            raw=json.dumps(wire,ensure_ascii=False,separators=(',',':')).encode()+b'\n'
            self.assertEqual(len(wire['recent']),3)
            self.assertLessEqual(len(raw),2048)
            self.assertNotIn('_threads',wire)
            self.assertNotIn('body',wire['event'])
            for row in wire['recent']:
                self.assertLessEqual(len(row['title'].encode()),60)
                self.assertLessEqual(len(row['body'].encode()),90)
            store.close()
