"""Reconstruct a device rendering from bounded LVGL tile snapshots."""
import argparse
from pathlib import Path
import time
import serial
from PIL import Image
p=argparse.ArgumentParser();p.add_argument('--port',required=True);p.add_argument('--output',default='artifacts/passport-screen.png');args=p.parse_args()
with serial.Serial(args.port,115200,timeout=.5) as s:
 s.reset_input_buffer();s.write(b'CAPTURE\n');data=bytearray();deadline=time.monotonic()+10
 while time.monotonic()<deadline:
  data.extend(s.read(s.in_waiting or 1))
  if b'PASSPORT_CAPTURE_END' in data:break
image=Image.new('RGB',(240,320),'#111a13')
position=data.find(b'PASSPORT_TILES')
if position<0:raise RuntimeError('Device did not return a capture')
position=data.index(b'\n',position)+1
count=0
while position<len(data):
 end=data.find(b'\n',position)
 if end<0:break
 header=bytes(data[position:end]).decode(errors='replace');position=end+1
 if header=='PASSPORT_CAPTURE_END':break
 if not header.startswith('TILE '):continue
 x,y,w,h,stride=map(int,header.split()[1:]);raw=data[position:position+stride*h];position+=stride*h+1
 if len(raw)!=stride*h:raise RuntimeError('Incomplete tile')
 pixels=[]
 for row in range(h):
  for col in range(w):
   offset=row*stride+col*2;v=raw[offset]|raw[offset+1]<<8
   pixels.append(((v>>11)*255//31,((v>>5)&63)*255//63,(v&31)*255//31,255 if v else 0))
 tile=Image.new('RGBA',(w,h));tile.putdata(pixels)
 # LVGL RGB565 snapshots encode transparent extra draw areas as zero.
 # This theme uses no pure black, so restore those pixels as transparent.
 image.paste(tile,(x,y),tile);count+=1
Path(args.output).parent.mkdir(exist_ok=True,parents=True);image.save(args.output)
print('Captured',count,'tiles to',args.output)
