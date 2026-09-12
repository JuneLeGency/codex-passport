import json
import tempfile
import threading
import unittest
from pathlib import Path
from http.server import ThreadingHTTPServer
from urllib.request import Request, urlopen
from urllib.error import HTTPError
from codex_passport.device import DeviceCommands, private_endpoint
from codex_passport.routing import Routing
from codex_passport.relay import handler_factory
from codex_passport.cli import device_frame, snapshot
from codex_passport.store import Store

class DeviceTests(unittest.TestCase):
    def test_private_destination_validation(self):
        self.assertEqual(private_endpoint('http://192.168.1.3:18765/'),'http://192.168.1.3:18765')
        for bad in ('http://127.0.0.1','https://192.168.1.3','http://10.0.0.1/x','http://user@10.0.0.1','http://8.8.8.8','http://example.com','http://10.0.0.1:99999'):
            with self.subTest(bad=bad),self.assertRaises(ValueError):private_endpoint(bad)

    def test_ephemeral_secret_and_acknowledged_handoff(self):
        clock=[100.0];route=Routing(clock=lambda:clock[0]);commands=DeviceCommands('test-secret-token-value',route,clock=lambda:clock[0])
        request=dict(op='wifi',enabled=True,ssid='Test network',password='not-real-password',endpoint='http://192.168.1.3:18765')
        issued=commands.issue(request)
        self.assertNotIn('password',json.dumps(commands.view()))
        self.assertNotIn('token',json.dumps(commands.view()))
        self.assertEqual(route.view()['owner'],'phone')
        with self.assertRaises(ValueError):commands.issue({'op':'identify'})
        commands.acknowledge(dict(cmd=issued['id']+1,cmd_ok=True))
        self.assertEqual(route.view()['owner'],'phone')
        commands.acknowledge(dict(cmd=issued['id'],cmd_ok=True,device={'fw':'0.3.0-dev','muted':True,'battery':90,'ssid':'private'}))
        self.assertEqual(route.view()['owner'],'wifi')
        self.assertIsNone(commands.current())
        self.assertNotIn('ssid',commands.view()['settings'])
        clock[0]+=46
        self.assertEqual(route.view()['owner'],'phone')
        commands.issue({'op':'identify'});clock[0]+=91
        self.assertIsNone(commands.current())
        self.assertEqual(commands.view()['command']['state'],'expired')

    def test_invalid_settings_and_bounded_provisioning_frame(self):
        commands=DeviceCommands('x'*43,Routing())
        for bad in ({'op':'settings','brightness':True,'idle':60,'volume':50,'muted':False}, {'op':'wifi','enabled':True,'saved':False}, {'op':'wifi','enabled':'true'}, {'op':'erase_bonds'}):
            with self.assertRaises(ValueError):commands.issue(bad)
        with tempfile.TemporaryDirectory() as folder:
            store=Store(Path(folder))
            for i in range(4):
                store.event(str(i),'t','input');store.decorate(str(i),title='中"\\'*30,body='文"\\'*60)
            request=dict(op='wifi',enabled=True,ssid='中'*10,password='a'*63,endpoint='http://192.168.100.100:18765')
            commands.issue(request)
            frame=snapshot(store,1,store.pending());frame['cmd']=commands.current();frame['_device']=commands.view()
            result=device_frame(frame)
            self.assertLessEqual(len(json.dumps(result,ensure_ascii=False,separators=(',',':')).encode()),1900)
            self.assertNotIn('_device',result)
            self.assertEqual(result['cmd']['ssid'],request['ssid'])
            self.assertEqual(store.get('cmd',None),None)
            store.close()

    def test_wifi_api_gates_generation_and_requires_auth(self):
        with tempfile.TemporaryDirectory() as folder:
            route=Routing();commands=DeviceCommands('test-secret-token-value',route)
            server=ThreadingHTTPServer(('127.0.0.1',0),handler_factory(Path(folder),'test-secret-token-value',route,commands))
            threading.Thread(target=server.serve_forever,daemon=True).start()
            base=f'http://127.0.0.1:{server.server_port}'
            def call(path,body=None,auth=True):
                req=Request(base+path,data=None if body is None else json.dumps(body).encode(),headers={'Authorization':'Bearer test-secret-token-value'} if auth else {})
                with urlopen(req) as r:return json.load(r)
            try:
                with self.assertRaises(HTTPError) as e:call('/v1/device/command',{'op':'identify'},False)
                self.assertEqual(e.exception.code,401);e.exception.close()
                self.assertTrue(call('/v1/ping')['wifi'])
                with self.assertRaises(HTTPError) as e:call('/v1/device/snapshot')
                self.assertEqual(e.exception.code,409);e.exception.close()
                generation=route.select('wifi')['generation']
                frame=call('/v1/device/snapshot')
                self.assertEqual(frame['generation'],generation)
                self.assertNotIn('_threads',frame)
                call('/v1/device/ack',{'generation':generation,'device':{'fw':'0.3.0'}})
                self.assertEqual(route.view()['state'],'connected')
                route.select('phone')
                with self.assertRaises(HTTPError) as e:call('/v1/device/ack',{'generation':generation})
                self.assertEqual(e.exception.code,409);e.exception.close()
            finally:server.shutdown();server.server_close()

if __name__=='__main__':unittest.main()
