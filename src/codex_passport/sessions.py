"""Incremental all-session fallback; hooks are the supported event interface."""
from __future__ import annotations
import json
import math
import time
import sqlite3
from contextlib import closing
from datetime import datetime
from pathlib import Path
from .store import Store

MAX_LINE = 4 * 1024 * 1024


def interaction_stamp(record):
    """Only visible messages or explicit questions advance conversation order."""
    if not isinstance(record, dict):
        return None
    p = record.get('payload')
    if not isinstance(p, dict):
        return None
    visible = False
    if record.get('type') == 'event_msg':
        visible = (p.get('type') in ('user_message', 'agent_message') and bool(p.get('message'))) or (
            p.get('type') == 'task_complete' and bool(p.get('last_agent_message')))
    elif record.get('type') == 'response_item':
        content = p.get('content')
        visible = (p.get('type') == 'message' and p.get('role') in ('user', 'assistant') and
                   p.get('channel') in (None, 'commentary', 'final') and isinstance(content, list) and
                   p.get('phase') in (None, 'commentary', 'final_answer', 'final') and
                   any(isinstance(part, dict) and part.get('type') in
                       ('input_text', 'output_text', 'text', 'input_image', 'input_audio') and
                       (part.get('text') or part.get('image_url') or part.get('audio')) for part in content))
        if p.get('type') in ('function_call', 'custom_tool_call'):
            visible = 'request_user_input' in str(p.get('name', ''))
    if not visible:
        return None
    try:
        stamp = datetime.fromisoformat(record['timestamp'].replace('Z', '+00:00')).timestamp()
        return stamp if math.isfinite(stamp) and stamp > 0 else None
    except (KeyError, ValueError, AttributeError, OverflowError):
        return None  # A replay without a timestamp must never become "now".

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
        self.backfill_budget = 8 * MAX_LINE
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

    def _backfill_interactions(self, path, cursor):
        # Upgrade existing EOF cursors without replaying notifications or changing status.
        if not cursor or cursor.get('interactions_indexed') or self.backfill_budget <= 0:
            return
        target = min(cursor.get('interaction_end', cursor.get('offset', 0)), path.stat().st_size)
        start = max(0, target - min(self.backfill_budget, 2 * MAX_LINE))
        latest = 0
        with path.open('rb') as stream:
            # Read newest blocks first; stop after finding the last visible activity.
            # This avoids scanning gigabytes of old tool output during an upgrade.
            if start:
                stream.seek(start-1)
                if stream.read(1) != b'\n':
                    raw = stream.readline(MAX_LINE + 1)
                    while raw and not raw.endswith(b'\n'):
                        raw = stream.readline(MAX_LINE + 1)
            while stream.tell() < target:
                before = stream.tell()
                raw = stream.readline(MAX_LINE + 1)
                if not raw.endswith(b'\n'):
                    if len(raw) > MAX_LINE:
                        while raw and not raw.endswith(b'\n'):
                            raw = stream.readline(MAX_LINE + 1)
                        continue
                    stream.seek(before)
                    break
                try:
                    latest = max(latest, interaction_stamp(json.loads(raw)) or 0)
                except ValueError:
                    pass
        self.backfill_budget -= target - start
        if latest:
            self.store.interaction(cursor.get('thread', path.stem), latest)
        cursor['interaction_end'] = start
        cursor['interactions_indexed'] = bool(latest) or start == 0
        self.store.put(f'file:{path.stat().st_dev}:{path.stat().st_ino}', cursor)

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
        self._backfill_interactions(path, cursor)
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
                activity = interaction_stamp(record)
                if activity:
                    self.store.interaction(thread, activity)
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
        self.store.put(key, {**cursor, 'offset': offset, 'thread': thread, 'turn': turn,
                             'interactions_indexed': cursor.get('interactions_indexed', not historical),
                             'project': project, 'inode': stat.st_ino, 'checked':True})
