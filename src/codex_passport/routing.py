"""One active BLE owner, with a bounded desktop handoff and sticky fallback.

No location inference: a reachable VPN server does not imply Bluetooth proximity.
"""
import threading
import time


class Routing:
    def __init__(self, desktop=False, clock=time.monotonic):
        self.clock = clock
        self.lock = threading.Lock()
        self.desktop = desktop
        self.owner = 'desktop' if desktop else 'phone'
        self.state = 'connecting' if desktop else 'idle'
        self.deadline = clock() + 180 if desktop else 0
        self.generation = 0

    def view(self):
        with self.lock:
            if self.owner == 'desktop' and self.clock() > self.deadline:
                self.owner, self.state = 'phone', 'fallback'
                self.generation += 1
            return dict(owner=self.owner, state=self.state,
                        desktopAvailable=self.desktop, generation=self.generation)

    def select(self, owner):
        if owner not in ('phone', 'desktop') or (owner == 'desktop' and not self.desktop):
            raise ValueError('Desktop BLE is not configured' if owner == 'desktop' else 'Invalid route')
        with self.lock:
            self.owner = owner
            self.state = 'connecting' if owner == 'desktop' else 'idle'
            self.deadline = self.clock() + 180
            self.generation += 1
        return self.view()

    def acknowledged(self, generation):
        with self.lock:
            if generation == self.generation and self.owner == 'desktop':
                self.state, self.deadline = 'connected', self.clock() + 60

    def failed(self, generation):
        with self.lock:
            if generation == self.generation:
                self.owner, self.state = 'phone', 'fallback'
                self.generation += 1
