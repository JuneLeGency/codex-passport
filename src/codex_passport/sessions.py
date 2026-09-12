"""Incremental all-session fallback; hooks are the supported event interface."""
from __future__ import annotations
import json
import time
import sqlite3
from contextlib import closing
from datetime import datetime
from pathlib import Path
from .store import Store

MAX_LINE = 4 * 1024 * 1024

def internal_session(meta):
    source = meta.get('source')
    # These are hidden approval-review workers, not user-visible conversations.
    if meta.get('thread_source') in ('guardian_review', 'memory_consolidation'):
        return True
    if isinstance(source, dict):
        child = source.get('subagent')
        return isinstance(child, dict) and child.get('other') in ('guardian', 'memory_consolidation')
    return False


class Sessions:
    def __init__(self, home: Path, store: Store):
        self.home, self.store = home, store
        self.initialized = bool(store.get('sessions_initialized', False))
        self.baseline = store.get('watch_start', time.time())
        store.put('watch_start', self.baseline)

    def poll(self):
        paths = sorted((self.home / 'sessions').rglob('*.jsonl'))
        # Archived sessions can finish while being moved. Track them too.
        paths += sorted((self.home / 'archived_sessions').rglob('*.jsonl'))
        for path in paths:
            try:
                self._read(path)
            except (OSError, UnicodeError):
                continue
        self.initialized = True
        self.store.put('sessions_initialized', True)

    def _read(self, path):
        stat = path.stat()
        key = f'file:{stat.st_dev}:{stat.st_ino}'
        cursor = self.store.get(key, {})
        if cursor.get('ignored'):
            return
        if not cursor.get('checked'):
            with path.open('rb') as first:
                try: meta=json.loads(first.readline(MAX_LINE)).get('payload',{})
                except (ValueError,AttributeError): meta={}
            if internal_session(meta):
                self.store.hide_thread(meta.get('id',''))
                self.store.put(key,{'ignored':True})
                return
        offset = cursor.get('offset', 0)
        thread, turn = cursor.get('thread', path.stem), cursor.get('turn', '')
        project = cursor.get('project', '')
        historical = not self.initialized and not cursor
        if stat.st_size < offset or cursor.get('inode', stat.st_ino) != stat.st_ino:
            offset = 0
        if offset == stat.st_size and cursor:
            if not cursor.get('checked'):
                cursor['checked']=True
                self.store.put(key,cursor)
            return
        with path.open('rb') as stream:
            if not cursor:
                first = stream.readline(MAX_LINE)
                try:
                    meta = json.loads(first).get('payload', {})
                    if internal_session(meta):
                        self.store.put(key, {'ignored': True})
                        return
                    thread = meta.get('id', thread)
                    project = Path(meta.get('cwd', '')).name
                except (ValueError, AttributeError, TypeError):
                    pass
                if historical:
                    offset = max(0, stat.st_size - 2 * MAX_LINE)
                    stream.seek(offset)
                    if offset:
                        stream.readline()
                        offset = stream.tell()
            stream.seek(offset)
            # Bounded work per file per poll avoids starving the device heartbeat.
            end = min(stat.st_size, offset + 8 * MAX_LINE)
            while stream.tell() < end:
                before = stream.tell()
                raw = stream.readline(MAX_LINE + 1)
                if not raw.endswith(b'\n'):
                    if len(raw) > MAX_LINE:
                        while raw and not raw.endswith(b'\n'):
                            raw = stream.readline(MAX_LINE + 1)
                        offset = stream.tell()
                        continue
                    offset = before  # Never consume a half-written JSON record.
                    break
                offset = stream.tell()
                try:
                    record = json.loads(raw)
                except ValueError:
                    continue
                if not isinstance(record, dict):
                    continue
                p = record.get('payload')
                if not isinstance(p, dict):
                    continue
                stamp = time.time()
                try:
                    stamp = datetime.fromisoformat(record['timestamp'].replace('Z', '+00:00')).timestamp()
                except (KeyError, ValueError, AttributeError):
                    pass
                if record.get('type') == 'session_meta':
                    if internal_session(p):
                        self.store.put(key, {'ignored': True})
                        return
                    thread = p.get('id', thread)
                    project = Path(p.get('cwd', '')).name
                if record.get('type') == 'turn_context':
                    turn = p.get('turn_id', turn)
                kind = None
                identity = ''
                if record.get('type') == 'event_msg':
                    name = p.get('type')
                    turn = p.get('turn_id', turn)
                    kind = {'task_started': 'started', 'task_complete': 'completed',
                            'turn_aborted': 'interrupted', 'task_failed': 'failed',
                            'error': 'failed'}.get(name)
                if record.get('type') == 'response_item':
                    if p.get('type') in ('function_call', 'custom_tool_call') and 'request_user_input' in p.get('name', ''):
                        kind, identity = 'input', p.get('call_id', '')
                    if p.get('type') in ('function_call_output', 'custom_tool_call_output') and not historical:
                        self.store.resume(thread, turn, identity=p.get("call_id", ""))
                if kind:
                    self.store.event(thread, turn, kind, project, identity, stamp, historical or stamp < self.baseline)
                    title=''
                    for database in sorted(self.home.glob('state_*.sqlite'),reverse=True)[:1]:
                        try:
                            with closing(sqlite3.connect(database.resolve().as_uri()+'?mode=ro',uri=True)) as db:
                                row=db.execute('SELECT title FROM threads WHERE id=?',(thread,)).fetchone()
                                if row:title=row[0]
                        except sqlite3.Error:pass
                    body=p.get('last_agent_message','') if kind=='completed' else ''
                    if kind=='input':
                        try:
                            args=json.loads(p.get('arguments','{}'))
                            questions=args.get('questions',[])
                            if questions and isinstance(questions[0],dict):
                                body=questions[0].get('question') or questions[0].get('title') or ''
                        except (ValueError,TypeError,AttributeError):pass
                    self.store.decorate(thread,title=title,body=body)
        self.store.put(key, {'offset': offset, 'thread': thread, 'turn': turn,
                             'project': project, 'inode': stat.st_ino, 'checked':True})
