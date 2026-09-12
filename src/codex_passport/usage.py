"""Read-only JSON-RPC adapter. No authentication files or tokens are inspected."""
import asyncio
import json
import math
import time
from . import __version__


def normalize(response):
    buckets = response.get('rateLimitsByLimitId') or {}
    value = buckets.get('codex') or response.get('rateLimits') or {}
    windows = []
    for name in ('primary', 'secondary'):
        w = value.get(name)
        if not isinstance(w, dict):
            windows.append(None)
            continue
        used, duration, reset = w.get('usedPercent'), w.get('windowDurationMins'), w.get('resetsAt')
        if (isinstance(used, bool) or not isinstance(used, (float, int)) or not math.isfinite(used)
                or not 0 <= used <= 100 or not isinstance(duration, int) or duration <= 0
                or not isinstance(reset, int) or reset <= 0):
            windows.append(None)
        else:
            windows.append({'remaining': round(100-used), 'minutes': duration, 'reset': reset})
    return {'windows': windows, 'updated': int(time.time()), 'plan': str(value.get('planType') or 'unknown')[:24]}


class UsageClient:
    def __init__(self, executable='codex'):
        self.executable = executable
        self.process = None
        self.serial = 0

    async def close(self):
        if self.process is not None:
            if self.process.returncode is None:
                self.process.terminate()
                try:
                    await asyncio.wait_for(self.process.wait(), 2)
                except asyncio.TimeoutError:
                    self.process.kill()
                    await self.process.wait()
            self.process = None

    async def request(self, method, params=None):
        self.serial += 1
        message = {'id': self.serial, 'method': method}
        if params is not None:
            message['params'] = params
        self.process.stdin.write((json.dumps(message)+'\n').encode())
        await self.process.stdin.drain()
        async def response():
            while True:
                line = await self.process.stdout.readline()
                if not line:
                    raise RuntimeError('Codex app-server closed')
                data = json.loads(line)
                if data.get('id') != self.serial or 'method' in data:
                    continue
                if 'error' in data:
                    raise RuntimeError('Codex request unavailable: ' + method)
                return data.get('result', {})
        return await asyncio.wait_for(response(), 12)

    async def fetch(self):
        try:
            if self.process is None or self.process.returncode is not None:
                self.process = await asyncio.create_subprocess_exec(
                    self.executable, 'app-server', stdin=asyncio.subprocess.PIPE,
                    stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.DEVNULL,
                    limit=4*1024*1024)
                await self.request('initialize', {'clientInfo': {'name': 'codex-passport-sync', 'version': __version__}})
                self.process.stdin.write(b'{"method":"initialized"}\n')
                await self.process.stdin.drain()
            limits = normalize(await self.request('account/rateLimits/read'))
            try:
                activity = await self.request('account/usage/read')
                limits['tokens'] = (activity.get('summary') or {}).get('lifetimeTokens')
                today = time.strftime('%Y-%m-%d')
                limits['today'] = next((x.get('tokens') for x in activity.get('dailyUsageBuckets') or []
                                       if x.get('startDate') == today), None)
            except RuntimeError:
                pass  # Older Codex versions may not expose token-activity summaries.
            return limits
        except BaseException:
            await self.close()
            raise
