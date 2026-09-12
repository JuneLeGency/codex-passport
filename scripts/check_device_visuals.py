"""Render labeled diagnostic fixtures on Passport, then restore live state.

No event is enqueued or acknowledged. Screenshots are diagnostic samples, not usage evidence.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import time
import serial
from codex_passport.cli import snapshot
from codex_passport.store import Store

root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port',required=True)
parser.add_argument('--state-dir',type=Path,required=True)
args=parser.parse_args()
store=Store(args.state_dir)
port=args.port

def send(frame):
    with serial.Serial(port,115200,timeout=.1) as link:
        link.reset_input_buffer()
        link.write(json.dumps(frame,ensure_ascii=False,separators=(',',':')).encode()+b'\n')
        deadline=time.monotonic()+5
        while time.monotonic()<deadline:
            try: reply=json.loads(link.readline())
            except ValueError: continue
            if reply.get('ack')==frame['seq']:return
        raise RuntimeError('Diagnostic frame was not rendered')

try:
    for index,(name,remaining) in enumerate([('fast',20),('balanced',50),('slow',80)]):
        now=int(time.time())
        frame={'v':1,'seq':1900000000+index,'now':now,'running':3,'waiting':1,'unread':2,'latest':2,
               'tokens':-1,'today':-1,'windows':[[remaining,100,now+3000],[-1,0,0]],'recent':[],'event':None}
        send(frame)
        subprocess.run([sys.executable,str(root/'scripts/capture_device.py'),'--port',port,'--output',str(root/f'.runtime/pace-{name}.png')],check=True)
finally:
    send(snapshot(store,1900000010))
    store.close()
