"""Install a persistent macOS user relay with an explicit private bind address."""
import argparse
import ipaddress
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',required=True,help='This computer\'s private LAN/VPN IP (not an ADB address)')
    parser.add_argument('--port',type=int,default=18765)
    parser.add_argument('--ble',help='Optional Passport advertising name/address for direct BLE')
    parser.add_argument('--codex',default=shutil.which('codex'))
    parser.add_argument('--state-dir',type=Path)
    args=parser.parse_args()
    if sys.platform!='darwin':parser.error('This installer requires macOS; run the relay CLI on other platforms')
    address=ipaddress.ip_address(args.host)
    if address.is_unspecified or not (address.is_private or address.is_loopback):
        parser.error('--host must be a specific private or loopback address')
    if not 1<=args.port<=65535:parser.error('Invalid port')
    if not args.codex:parser.error('Codex not found; supply --codex /path/to/codex')
    if args.ble:
        import importlib.util
        if not importlib.util.find_spec('bleak'):parser.error('Install BLE support with uv sync --extra ble first')
    root=Path(__file__).resolve().parents[1]
    target=Path.home()/'Library/LaunchAgents/dev.codex-passport.relay.plist'
    target.parent.mkdir(parents=True,exist_ok=True)
    state=(args.state_dir or root/'.runtime/live').resolve()
    state.mkdir(parents=True,exist_ok=True,mode=0o700)
    arguments=[sys.executable,'-m','codex_passport','--state-dir',str(state),
               '--codex-home',str(Path(os.environ.get('CODEX_HOME',str(Path.home()/'.codex')))),
               'serve','--host',args.host,'--port',str(args.port),'--codex',str(Path(args.codex).resolve())]
    if args.ble:arguments.extend(['--ble',args.ble])
    plist={'Label':'dev.codex-passport.relay','ProgramArguments':arguments,'WorkingDirectory':str(root),
           'EnvironmentVariables':{'PATH':os.environ.get('PATH','/usr/local/bin:/usr/bin:/bin'),'PYTHONUNBUFFERED':'1'},
           'RunAtLoad':True,'KeepAlive':True,'ThrottleInterval':10,
           'StandardOutPath':str(state/'service.log'),'StandardErrorPath':str(state/'service-error.log')}
    if target.exists():
        previous=plistlib.loads(target.read_bytes())
        if previous.get('Label')!='dev.codex-passport.relay':raise RuntimeError('Existing service is unrelated')
    temporary=target.with_suffix('.tmp')
    temporary.write_bytes(plistlib.dumps(plist));temporary.chmod(0o644);temporary.replace(target)
    domain=f'gui/{os.getuid()}'
    subprocess.run(['launchctl','bootout',domain+'/dev.codex-passport.relay'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    subprocess.run(['launchctl','bootstrap',domain,str(target)],check=True)
    print('Installed service:',target)

if __name__=='__main__':main()
