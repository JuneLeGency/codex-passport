"""Fetch a verified OFL font and generate the bounded device bitmap font.

Requires Node.js/npm. Generated files are build inputs, not application source.
"""
from pathlib import Path
import hashlib
import re
import subprocess
from urllib.request import urlopen

ROOT=Path(__file__).resolve().parents[1]
COMMIT='f8d157532fbfaeda587e826d4cd5b21a49186f7c'
URL=f'https://raw.githubusercontent.com/notofonts/noto-cjk/{COMMIT}/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf'
SHA256='2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b'

def main():
    directory=ROOT/'firmware/generated';directory.mkdir(parents=True,exist_ok=True)
    font=directory/'NotoSansCJKsc-Regular.otf'
    if not font.exists():
        with urlopen(URL,timeout=60) as response:raw=response.read(32*1024*1024)
        if hashlib.sha256(raw).hexdigest()!=SHA256:raise RuntimeError('Font checksum mismatch')
        font.write_bytes(raw)
    if hashlib.sha256(font.read_bytes()).hexdigest()!=SHA256:raise RuntimeError('Font checksum mismatch')
    subprocess.run(['npx','--yes','lv_font_conv@1.5.3','--font','firmware/generated/NotoSansCJKsc-Regular.otf',
                    '--size','16','--bpp','2','--range','0x20-0xff,0x2000-0x206f,0x2190-0x21ff,0x3000-0x30ff,0x4e00-0x9fff,0xff00-0xffef',
                    '--format','lvgl','--no-compress','--no-kerning','--lv-include','lvgl.h',
                    '--output','firmware/generated/passport_cjk16.c'],cwd=ROOT,check=True)
    output=directory/'passport_cjk16.c'
    value=re.sub(r'\.line_height = \d+', '.line_height = 24', output.read_text())
    output.write_text(re.sub(r'\.base_line = \d+', '.base_line = 5', value))
    print('Generated 16 px Noto CJK font; OFL license: firmware/fonts/OFL.txt')

if __name__=='__main__':main()
