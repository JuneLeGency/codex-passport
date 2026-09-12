import json
import tempfile
import time
import unittest
from datetime import datetime, timezone
from pathlib import Path
from unittest.mock import patch

from codex_passport.cli import device_frame, snapshot
from codex_passport.sessions import Sessions, interaction_stamp
from codex_passport.store import Store


class InteractionTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.home = Path(self.tmp.name)
        self.store = Store(self.home / 'state')
        self.now = time.time()
        for name, age in [('older', 20), ('newer', 10)]:
            self.store.event(name, 'turn', 'completed', project=name, ts=self.now-age,
                             interaction=True)

    def tearDown(self):
        self.store.close()
        self.tmp.cleanup()

    def assert_order(self, *expected):
        frame = snapshot(self.store, 1)
        self.assertEqual([r['project'] for r in frame['recent']], list(expected))
        self.assertEqual([r['project'] for r in device_frame(frame)['recent']], list(expected))

    def record(self, payload, stamp=None, kind='response_item'):
        return dict(type=kind, timestamp=datetime.fromtimestamp(
            self.now if stamp is None else stamp, timezone.utc).isoformat(), payload=payload)

    def test_background_state_and_reconnect_do_not_reorder(self):
        self.assert_order('newer', 'older')
        self.store.event('older', 'turn', 'input', identity='question', ts=self.now)
        self.store.resume('older', identity='question')
        self.store.event('older', 'turn', 'compacted', ts=self.now+1)
        self.store.decorate('older', title='Renamed')
        self.store.put('usage', {})
        self.store.mark_read(999)
        self.store.close()
        self.store = Store(self.home / 'state')
        self.assert_order('newer', 'older')

    def test_new_interaction_moves_thread_and_old_replay_cannot_move_it_back(self):
        self.store.interaction('older', self.now)
        self.assert_order('older', 'newer')
        self.store.interaction('older', self.now-100)
        self.store.interaction('newer', self.now-100)
        self.assert_order('older', 'newer')
        self.store.interaction('newer', self.now+1)
        self.assert_order('newer', 'older')
        # A timestamp tie remains stable even when lifecycle events arrive later.
        self.store.interaction('older', self.now+1)
        self.store.event('older', 'next', 'failed', ts=self.now+2)
        self.assert_order('newer', 'older')

    def test_hook_message_updates_once_but_subagent_and_empty_stop_do_not(self):
        def hook(name, **kwargs):
            self.store.ingest_hook(dict(hook_event_name=name, session_id='older',
                                        turn_id='next', **kwargs))
        with patch('codex_passport.store.time.time', return_value=self.now):
            hook('UserPromptSubmit')
        self.assert_order('older', 'newer')
        self.store.interaction('newer', self.now+1)
        with patch('codex_passport.store.time.time', return_value=self.now+2):
            hook('UserPromptSubmit')  # duplicate delivery
            hook('SubagentStop')
            hook('Stop')
        self.assert_order('newer', 'older')
        with patch('codex_passport.store.time.time', return_value=self.now+3):
            self.store.ingest_hook(dict(hook_event_name='Stop', session_id='older',
                turn_id='actual-response', last_assistant_message='Done'))
        self.assert_order('older', 'newer')

    def test_only_visible_timestamped_messages_count(self):
        for role in ('user', 'assistant'):
            message = dict(type='message', role=role, content=[dict(type='output_text', text='Hi')])
            self.assertAlmostEqual(interaction_stamp(self.record(message)), self.now, delta=0.000001)
        for payload in (
            dict(type='message', role='developer', content=[dict(type='input_text', text='Policy')]),
            dict(type='message', role='assistant', channel='analysis', content=[dict(type='output_text', text='Thought')]),
            dict(type='message', role='assistant', phase='analysis', content=[dict(type='output_text', text='Thought')]),
            dict(type='function_call', name='exec_command'),
            dict(type='function_call_output', output='Done'),
            dict(type='message', role='assistant', content=[]),
        ):
            self.assertIsNone(interaction_stamp(self.record(payload)))
        record = self.record(dict(type='message', role='user', content=[dict(type='input_text', text='Hi')]))
        del record['timestamp']
        self.assertIsNone(interaction_stamp(record))

    def test_existing_eof_cursor_backfills_without_alerts_or_status_changes(self):
        directory = self.home / 'sessions'
        directory.mkdir()
        path = directory / 'older.jsonl'
        records = [dict(type='session_meta', payload=dict(id='older')),
                   self.record(dict(type='message', role='assistant',
                       content=[dict(type='output_text', text='Fresh reply')]))]
        path.write_text(''.join(json.dumps(r)+'\n' for r in records))
        stat = path.stat()
        key = f'file:{stat.st_dev}:{stat.st_ino}'
        self.store.put(key, dict(offset=stat.st_size, thread='older', turn='turn',
                                 inode=stat.st_ino, checked=True))
        before = self.store.recent()
        watcher = Sessions(self.home, self.store)
        watcher.poll()
        self.assert_order('older', 'newer')
        self.assertEqual(self.store.recent(), before)
        self.assertTrue(self.store.get(key)['interactions_indexed'])
        watcher.poll()
        self.assertEqual(self.store.recent(), before)
        with path.open('a') as stream:
            stream.write(json.dumps(self.record(dict(type='function_call_output', call_id='a'),
                                                 stamp=self.now+10))+'\n')
        self.store.interaction('newer', self.now+1)
        watcher.poll()
        self.assert_order('newer', 'older')
        with path.open('a') as stream:
            stream.write(json.dumps(self.record(dict(type='message', role='user',
                content=[dict(type='input_text', text='Next')]), stamp=self.now+2))+'\n')
        watcher.poll()
        self.assert_order('older', 'newer')

    def test_backfill_walks_back_across_tool_only_blocks(self):
        directory = self.home / 'sessions'
        directory.mkdir()
        path = directory / 'older.jsonl'
        messages = [dict(type='session_meta', payload=dict(id='older')),
                    self.record(dict(type='message', role='assistant',
                        content=[dict(type='output_text', text='Reply')]))]
        messages += [self.record(dict(type='function_call_output', output='x'*200)) for _ in range(20)]
        path.write_text(''.join(json.dumps(r)+'\n' for r in messages))
        stat = path.stat()
        key = f'file:{stat.st_dev}:{stat.st_ino}'
        self.store.put(key, dict(offset=stat.st_size, thread='older', checked=True))
        watcher = Sessions(self.home, self.store)
        with patch('codex_passport.sessions.MAX_LINE', 512):
            watcher.poll()
            self.assertFalse(self.store.get(key)['interactions_indexed'])
            self.assert_order('newer', 'older')
            for _ in range(12):
                watcher.poll()
                if self.store.get(key)['interactions_indexed']:
                    break
        self.assertTrue(self.store.get(key)['interactions_indexed'])
        self.assert_order('older', 'newer')


if __name__ == '__main__':
    unittest.main()
