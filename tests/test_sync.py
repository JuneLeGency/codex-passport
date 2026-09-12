import asyncio
import json
from pathlib import Path
import tempfile
import time
import unittest
import threading
from http.server import ThreadingHTTPServer
from urllib.request import Request, urlopen
from urllib.error import HTTPError
from codex_passport.store import Store
from codex_passport.sessions import Sessions
from codex_passport.usage import normalize
from codex_passport.cli import snapshot, hook_config
from codex_passport.relay import handler_factory

class SyncTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory()
        self.home=Path(self.tmp.name)
        self.store=Store(self.home/'state')
    def tearDown(self):
        self.store.close();self.tmp.cleanup()
    def hook(self,event,thread='one',turn='turn',**kwargs):
        self.store.ingest_hook(dict(hook_event_name=event,session_id=thread,turn_id=turn,cwd='/work/project',**kwargs))
    def test_escaped_summaries_fit_ble_frame(self):
        for index in range(4):
            thread=str(index)
            self.store.event(thread,'t','completed',project='"'*32)
            self.store.decorate(thread,title='"'*60,body='"'*120)
        pending=self.store.pending()
        frame=snapshot(self.store,2147483647,pending)
        self.assertLess(len(json.dumps(frame,ensure_ascii=False,separators=(',',':')).encode())+1,2048)
        self.assertEqual(frame['event']['id'],pending['id'])
    def test_guardian_is_hidden_and_old_noise_removed(self):
        self.store.event('hidden','t','completed')
        logs=self.home/'sessions';logs.mkdir()
        path=logs/'internal.jsonl'
        path.write_text(json.dumps({'type':'session_meta','payload':{'id':'hidden','source':{'subagent':{'other':'guardian'}}}})+'\n')
        Sessions(self.home,self.store).poll()
        self.assertEqual(self.store.summary()['unread'],0)
        self.assertEqual(self.store.recent(),[])
        self.hook('Stop',thread='hidden',transcript_path=str(path))
        self.assertEqual(self.store.recent(),[])
        self.hook('Stop',thread='visible')
        self.assertEqual(self.store.pending()['thread'],'visible')
    def test_context_is_bounded_and_inbox_coalesces_threads(self):
        self.hook('Stop',last_assistant_message='完成 BLE 同步。 sk-privatecredential')
        self.store.decorate('one',title='同步通知与用量')
        event=self.store.pending()
        self.assertEqual(event['title'],'同步通知与用量')
        self.assertIn('完成 BLE',event['body'])
        self.assertNotIn('privatecredential',event['body'])
        self.hook('Stop',turn='next',last_assistant_message='新结果')
        self.assertEqual(len(self.store.inbox()),1)
        self.assertEqual(self.store.inbox()[0]['body'],'新结果')
    def test_parallel_sessions_and_dedup(self):
        self.hook('UserPromptSubmit');self.hook('UserPromptSubmit','two')
        self.assertEqual(self.store.summary()['running'],2)
        self.hook('Stop');self.hook('Stop')
        self.store.event('one','turn','completed') # Duplicate log fallback.
        self.assertEqual(self.store.summary()['running'],1)
        self.assertEqual(len(self.store.recent()),3)
    def test_approval_never_decides_and_has_no_private_content(self):
        self.hook('PermissionRequest',tool_name='Bash',tool_input={'command':'SECRET command'})
        self.assertEqual(self.store.summary()['waiting'],1)
        self.assertNotIn('SECRET',json.dumps(self.store.recent()))
        self.assertEqual(self.store.pending()['kind'],'approval')
    def test_parallel_waits_resolve_individually(self):
        self.hook('PreToolUse',tool_name='request_user_input',tool_use_id='a')
        self.hook('PreToolUse',tool_name='request_user_input',tool_use_id='b')
        self.hook('PostToolUse',tool_use_id='a')
        self.assertEqual(self.store.summary()['waiting'],1)
        self.hook('PostToolUse',tool_use_id='b')
        self.assertEqual(self.store.summary()['waiting'],0)
    def test_started_does_not_wake_device(self):
        self.hook('UserPromptSubmit')
        self.assertIsNone(self.store.pending())
        self.assertEqual(self.store.summary()['unread'],0)
    def test_user_input_interruption(self):
        self.hook('PreToolUse',tool_name='functions.request_user_input',tool_use_id='call')
        self.assertEqual(self.store.summary()['waiting'],1)
        self.hook('Interrupt')
        self.assertEqual(self.store.summary()['waiting'],0)
    def test_subagent_does_not_overwrite_parent(self):
        self.hook('UserPromptSubmit')
        self.hook('SubagentStart',agent_id='child')
        self.hook('SubagentStop',agent_id='child')
        self.assertEqual(self.store.summary()['running'],1)
    def test_durable_outbox_and_read(self):
        self.hook('Stop');event=self.store.pending()
        self.store.close();self.store=Store(self.home/'state')
        self.assertEqual(self.store.pending()['id'],event['id'])
        self.store.delivered(event['id'])
        self.assertIsNone(self.store.pending())
        self.assertEqual(self.store.summary()['unread'],1)
        self.store.mark_read(event['id'])
        self.assertEqual(self.store.summary()['unread'],0)
    def test_fragmented_log_and_restart(self):
        sessions=self.home/'sessions';sessions.mkdir()
        file=sessions/'new.jsonl'
        watcher=Sessions(self.home,self.store);watcher.poll()
        event={'type':'event_msg','payload':{'type':'task_started','turn_id':'abc'}}
        raw=json.dumps(event)
        file.write_text(raw[:20]);watcher.poll();self.assertIsNone(self.store.pending())
        with file.open('a') as f:f.write(raw[20:]+'\n')
        watcher.poll();self.assertEqual(self.store.recent()[0]['kind'],'started')
        watcher=Sessions(self.home,self.store);watcher.poll()
        self.assertEqual(len(self.store.recent()),1)
    def test_archive_move_not_replayed(self):
        d=self.home/'sessions';d.mkdir();p=d/'one.jsonl'
        p.write_text(json.dumps({'type':'event_msg','payload':{'type':'task_complete','turn_id':'t'}})+'\n')
        w=Sessions(self.home,self.store);w.poll()
        archive=self.home/'archived_sessions';archive.mkdir();p.rename(archive/p.name)
        w.poll();self.assertIsNone(self.store.pending())
    def test_stale_usage_never_fabricated(self):
        self.store.put('usage',{'updated':int(time.time()),'windows':[{'remaining':1,'minutes':300,'reset':int(time.time())-1},None]})
        self.assertEqual(snapshot(self.store,1)['windows'],[[-1,300,0],[-1,0,0]])
    def test_real_window_shape(self):
        result=normalize({'rateLimits':{'primary':{'usedPercent':86,'windowDurationMins':10080,'resetsAt':2000000000}}})
        self.assertEqual(result['windows'][0]['minutes'],10080)
        self.assertEqual(result['windows'][0]['remaining'],14)
        self.assertIsNone(result['windows'][1])
    def test_invalid_usage(self):
        for bad in (None,'13',float('nan'),-3,True):
            w=normalize({'rateLimits':{'primary':{'usedPercent':bad,'windowDurationMins':300,'resetsAt':2000000000}}})
            self.assertIsNone(w['windows'][0])
    def test_hook_scope(self):
        config=hook_config('python -m codex_passport hook')
        self.assertIn('PermissionRequest',config['hooks'])
        self.assertIn('request_user_input',config['hooks']['PreToolUse'][0]['matcher'])
    def test_server_auth_and_ack(self):
        self.hook('Stop')
        server=ThreadingHTTPServer(('127.0.0.1',0),handler_factory(self.home/'state','test-token'))
        threading.Thread(target=server.serve_forever,daemon=True).start()
        base=f'http://127.0.0.1:{server.server_port}'
        try:
            with self.assertRaises(HTTPError) as error:urlopen(base+'/v1/snapshot')
            self.assertEqual(error.exception.code,401)
            error.exception.close()
            req=Request(base+'/v1/snapshot',headers={'Authorization':'Bearer test-token'})
            with urlopen(req) as r:data=json.load(r)
            self.assertEqual(data['event']['kind'],'completed')
            req=Request(base+'/v1/ack',data=json.dumps({'event':data['event']['id']}).encode(),headers={'Authorization':'Bearer test-token'})
            with urlopen(req) as r:self.assertTrue(json.load(r)['ok'])
            self.assertIsNone(self.store.pending())
        finally:server.shutdown();server.server_close()
if __name__=='__main__':unittest.main()
