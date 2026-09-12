"""Durable metadata-only inbox shared by hooks and the device worker."""
from __future__ import annotations
import hashlib
import re
import json
import math
import sqlite3
import time
from pathlib import Path

ALERT_KINDS = {"completed", "failed", "interrupted", "approval", "input"}

LABELS = {
    "started": "Task started", "completed": "Task complete", "failed": "Task failed",
    "interrupted": "Task interrupted", "approval": "Approval needed",
    "input": "Your input needed", "session": "Session opened", "closed": "Session closed",
    "compacting": "Context compacting", "compacted": "Context compacted",
    "unknown": "Status unavailable",
}


def clipped(value, limit=60):
    # Bound UTF-8 bytes without splitting a character.
    return ''.join(c for c in str(value) if ord(c)>=32).encode('utf-8')[:limit].decode('utf-8',errors='ignore')


class Store:
    def __init__(self, directory: Path):
        directory.mkdir(parents=True, exist_ok=True, mode=0o700)
        self.db = sqlite3.connect(directory / 'state.sqlite3', timeout=2)
        self.db.row_factory = sqlite3.Row
        self.db.execute('PRAGMA journal_mode=WAL')
        self.db.executescript('''
          CREATE TABLE IF NOT EXISTS events(
            id INTEGER PRIMARY KEY AUTOINCREMENT, key TEXT UNIQUE, thread TEXT,
            kind TEXT, project TEXT, ts REAL, delivered INTEGER DEFAULT 0,
            read INTEGER DEFAULT 0);
          CREATE TABLE IF NOT EXISTS sessions(
            thread TEXT PRIMARY KEY, turn TEXT, status TEXT, project TEXT, updated REAL);
          CREATE TABLE IF NOT EXISTS waits(thread TEXT, turn TEXT, identity TEXT,
              PRIMARY KEY(thread,turn,identity));
          CREATE TABLE IF NOT EXISTS kv(key TEXT PRIMARY KEY, value TEXT);
          CREATE TABLE IF NOT EXISTS interactions(thread TEXT PRIMARY KEY, ts REAL NOT NULL);
          CREATE INDEX IF NOT EXISTS events_thread_id ON events(thread,id);
        ''')
        columns={r[1] for r in self.db.execute('PRAGMA table_info(events)')}
        for col in ('title','body'):
            if col not in columns:
                self.db.execute(f"ALTER TABLE events ADD COLUMN {col} TEXT DEFAULT ''")
        if self.get('notification_policy',0)<2:
            self.db.execute("UPDATE events SET delivered=1,read=1 WHERE kind NOT IN ('completed','failed','interrupted','approval','input')")
            self.put('notification_policy',2)
        self.db.commit()

    def hide_thread(self, thread):
        self.db.execute('DELETE FROM events WHERE thread=?',(thread,))
        self.db.execute('DELETE FROM sessions WHERE thread=?',(thread,))
        self.db.execute('DELETE FROM waits WHERE thread=?',(thread,))
        self.db.execute('DELETE FROM interactions WHERE thread=?',(thread,))
        self.db.commit()

    def interaction(self, thread, ts):
        """Index real conversation activity independently of lifecycle updates."""
        if not thread or not isinstance(ts, (int, float)) or not math.isfinite(ts) or ts <= 0:
            return
        self.db.execute('''INSERT INTO interactions VALUES(?,?)
            ON CONFLICT(thread) DO UPDATE SET ts=excluded.ts
            WHERE excluded.ts>interactions.ts''', (thread, ts))
        self.db.commit()

    def decorate(self, thread, title='', body=''):
        # Retain human context, redact obvious credentials and omit code blocks.
        def clean(text,limit):
            text=re.sub(r'(?s)```.*?```', '', str(text))
            text=re.sub(r'(?i)(?:sk-|pypi-|ghp_)[A-Za-z0-9_-]+','[redacted]',text)
            return clipped(' '.join(text.split()),limit)
        row=self.db.execute('SELECT id FROM events WHERE thread=? ORDER BY id DESC LIMIT 1',(thread,)).fetchone()
        if row:
            if title:self.db.execute('UPDATE events SET title=? WHERE id=?',(clean(title,60),row[0]))
            if body:self.db.execute('UPDATE events SET body=? WHERE id=?',(clean(body,120),row[0]))
            self.db.commit()

    def get(self, key, default=None):
        row = self.db.execute('SELECT value FROM kv WHERE key=?', (key,)).fetchone()
        return json.loads(row[0]) if row else default

    def put(self, key, value):
        self.db.execute('INSERT OR REPLACE INTO kv VALUES(?,?)', (key, json.dumps(value)))
        self.db.commit()

    def event(self, thread, turn, kind, project='', identity='', ts=None, silent=False, interaction=False):
        if kind not in LABELS or not thread:
            return
        ts = time.time() if ts is None else ts
        old = self.db.execute('SELECT * FROM sessions WHERE thread=?', (thread,)).fetchone()
        turn = turn or (old['turn'] if old else '')
        project = clipped(project or (old['project'] if old else thread[:8]), 32)
        key = hashlib.sha256(f'{thread}|{turn}|{kind}|{identity}'.encode()).hexdigest()
        # Historical tail recovery must never overwrite a newer live hook.
        if not old or ts >= old['updated']:
            status = kind
            if kind == 'session':
                status = old['status'] if old else 'idle'
            if kind == 'compacted':
                status = 'started'
            if kind in ('approval', 'input'):
                self.db.execute('INSERT OR IGNORE INTO waits VALUES(?,?,?)',(thread,turn,identity))
            elif kind in ('completed', 'failed', 'interrupted', 'closed', 'started'):
                self.db.execute('DELETE FROM waits WHERE thread=? AND turn!=?',(thread,turn))
                if kind != 'started':
                    self.db.execute('DELETE FROM waits WHERE thread=?',(thread,))
            self.db.execute('INSERT OR REPLACE INTO sessions VALUES(?,?,?,?,?)',
                            (thread, turn, status, project, ts))
        if not silent:
            quiet = int(kind not in ALERT_KINDS)
            inserted = self.db.execute('INSERT OR IGNORE INTO events(key,thread,kind,project,ts,delivered,read) VALUES(?,?,?,?,?,?,?)',
                            (key, thread, kind, project, ts, quiet, quiet))
            if interaction and inserted.rowcount:
                self.interaction(thread, ts)
        self.db.commit()

    def resume(self, thread, turn='', project='', identity=''):
        row = self.db.execute('SELECT * FROM sessions WHERE thread=?', (thread,)).fetchone()
        if identity:
            self.db.execute('DELETE FROM waits WHERE thread=? AND identity=?',(thread,identity))
        pending = self.db.execute('SELECT count(*) FROM waits WHERE thread=?',(thread,)).fetchone()[0]
        if not pending and row and (not turn or turn == row['turn']) and row['status'] in ('approval', 'input', 'compacting'):
            self.db.execute("UPDATE sessions SET status='started',updated=? WHERE thread=?",
                            (time.time(), thread))
            self.db.commit()

    def ingest_hook(self, value):
        if not isinstance(value, dict):
            return
        from .sessions import internal_session
        transcript=value.get('transcript_path')
        if transcript:
            try:
                with Path(transcript).open('rb') as stream:
                    meta=json.loads(stream.readline(4*1024*1024)).get('payload',{})
                if internal_session(meta):
                    self.hide_thread(meta.get('id',''))
                    return
            except (OSError, ValueError, AttributeError):
                pass
        if value.get('agent_type') in ('guardian','guardian_review'):
            return
        event = value.get('hook_event_name', value.get('type', ''))
        thread = str(value.get('agent_id') or value.get('thread-id') or value.get('session_id') or '')[:128]
        turn = str(value.get('turn_id') or value.get('turn-id') or '')[:128]
        project = Path(str(value.get('cwd') or '')).name
        tool = str(value.get('tool_name') or '')
        mapping = {'SessionStart': 'session', 'UserPromptSubmit': 'started', 'Stop': 'completed',
                   'SubagentStart': 'started', 'SubagentStop': 'completed', 'Interrupt': 'interrupted',
                   'SessionEnd': 'closed', 'PermissionRequest': 'approval',
                   'PreCompact': 'compacting', 'PostCompact': 'compacted',
                   'agent-turn-complete': 'completed'}
        if event == 'PostToolUse':
            identity = str(value.get('tool_use_id') or '')
            hashed = hashlib.sha256(json.dumps(value.get('tool_input', {}), sort_keys=True).encode()).hexdigest()
            self.resume(thread, turn, project, identity)
            self.resume(thread, turn, project, hashed)
            return
        kind = mapping.get(event)
        if event == 'PreToolUse' and ('request_user_input' in tool or 'requestUserInput' in tool):
            kind = 'input'
        if not kind:
            return
        identity = ''
        if kind in ('approval', 'input'):
            # Raw tool arguments never enter storage, only their one-way identity hash.
            identity = str(value.get('tool_use_id') or hashlib.sha256(
                json.dumps(value.get('tool_input', {}), sort_keys=True).encode()).hexdigest())
        if kind in ('session', 'closed'):
            identity = str(int(time.time()) // 5)
        body=value.get('last_assistant_message') or value.get('last-assistant-message') or ''
        self.event(thread, turn, kind, project, identity,
                   interaction=event == 'UserPromptSubmit' or kind in ('approval', 'input') or
                   (event in ('Stop', 'agent-turn-complete') and bool(body)))
        if kind=='input':
            tool_input=value.get('tool_input')
            questions=tool_input.get('questions',[]) if isinstance(tool_input,dict) else []
            if questions and isinstance(questions[0],dict):
                body=questions[0].get('question') or questions[0].get('title') or ''
        self.decorate(thread,body=body)

    def pending(self):
        row = self.db.execute('SELECT * FROM events WHERE delivered=0 ORDER BY id LIMIT 1').fetchone()
        return dict(row) if row else None

    def delivered(self, event_id):
        self.db.execute('UPDATE events SET delivered=1 WHERE id=?', (event_id,))
        self.db.commit()

    def mark_read(self, event_id):
        self.db.execute('UPDATE events SET read=1 WHERE id<=?', (event_id,))
        self.db.commit()

    def summary(self):
        counts = dict(self.db.execute('SELECT status,count(*) FROM sessions WHERE updated>? GROUP BY status', (time.time()-86400,)))
        unread = self.db.execute('SELECT count(*) FROM events WHERE read=0').fetchone()[0]
        latest = self.db.execute('SELECT coalesce(max(id),0) FROM events').fetchone()[0]
        return {'running': counts.get('started', 0) + counts.get('compacting', 0),
                'waiting': counts.get('approval', 0) + counts.get('input', 0),
                'unread': unread, 'latest': latest}

    def inbox(self, limit=4):
        return [dict(r) for r in self.db.execute(
            '''SELECT e.* FROM events e LEFT JOIN interactions a ON a.thread=e.thread
               WHERE e.id IN (SELECT max(id) FROM events WHERE kind IN
                 ('completed','failed','interrupted','approval','input') GROUP BY thread)
               ORDER BY coalesce(a.ts,(SELECT min(ts) FROM events WHERE thread=e.thread)) DESC,
                        e.thread ASC LIMIT ?''',(limit,))]

    def progress(self, limit=3):
        """Recent visible sessions, with current state rather than an old alert's state."""
        rows = self.db.execute('''
            SELECT s.*, e.id, e.kind AS event_kind, e.body,
              coalesce(a.ts,(SELECT min(ts) FROM events WHERE thread=s.thread),s.updated) AS interaction_ts,
              (SELECT title FROM events WHERE thread=s.thread AND title!='' ORDER BY id DESC LIMIT 1) AS title
            FROM sessions s JOIN events e ON e.id=(SELECT max(id) FROM events WHERE thread=s.thread)
            LEFT JOIN interactions a ON a.thread=s.thread
            WHERE s.updated>? AND s.status NOT IN ('closed','idle','session')
            ORDER BY interaction_ts DESC, s.thread ASC LIMIT ?
        ''', (time.time()-86400, max(0, min(limit, 3))))
        return [dict(id=r['id'], kind=r['status'], project=r['project'], ts=r['interaction_ts'],
                     title=r['title'] or r['project'],
                     body=clipped(r['body'],90) if r['status']==r['event_kind'] and
                     r['status'] in ALERT_KINDS else '') for r in rows]

    def recent(self, limit=4):
        return [dict(r) for r in self.db.execute('SELECT * FROM events ORDER BY id DESC LIMIT ?', (limit,))]

    def close(self):
        self.db.close()
