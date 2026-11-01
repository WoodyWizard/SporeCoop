"""A bad network between each guest and the host of a lane (docs/06-testing.md#testing-degraded): a UDP
proxy per guest with delay, jitter, loss, duplicates, outages, burst loss, a bandwidth cap and latency
steps / spikes (both directions). twoinst.start() puts one in the guest's way and coop_multi.join_cmd()
one in each extra guest's way (its own port, its own random stream) when SPORE_NET is set, so every
test, party and the chaos bot can run over it:

  SPORE_NET="delay=120,jitter=40,loss=0.03"   (one-way ms, ms, fraction; also dup=0.01)
  SPORE_NET=dsl | mobile | awful | player | players | players_bad | burst | lowband | step   (PRESETS)

  burst_every=S,burst_s=S,burst_loss=F   Gilbert-Elliott burst loss: a good state of S s on average
                                         (exponential), then a burst of burst_s s on average losing F
                                         of the packets (loss= stays the good state's loss)
  spike_every=S,spike_s=S,spike_ms=MS    latency spikes: every ~S s (+-30 %) MS ms more for spike_s s
  step=AT:MS[/AT:MS...]                  the base delay becomes MS ms AT s after the proxy started
  rate=KBIT[,queue=MS]                   a bandwidth cap per direction (kbit/s); a packet that would
                                         wait in the queue longer than MS (default 300 ms) is dropped

proxy.outage(seconds) drops everything for a while (a short disconnect).
"""
import heapq
import os
import random
import socket
import threading
import time

PRESETS = {
    'dsl': 'delay=40,jitter=10,loss=0.005',
    'mobile': 'delay=120,jitter=60,loss=0.03,dup=0.01',
    'awful': 'delay=250,jitter=120,loss=0.10,dup=0.02',
    # what the players' own logs show (2026-11 audit): rtt 70-150 ms, spikes to 400+ ms, bursts of
    # 10-30 % loss lasting seconds
    'players': 'delay=50,jitter=25,loss=0.005,spike_every=60,spike_s=3,spike_ms=200,'
               'burst_every=45,burst_s=4,burst_loss=0.2',
    'players_bad': 'delay=75,jitter=35,loss=0.01,dup=0.005,spike_every=30,spike_s=4,spike_ms=250,'
                   'burst_every=25,burst_s=5,burst_loss=0.3,rate=2000',
    'burst': 'delay=35,jitter=10,burst_every=20,burst_s=3,burst_loss=0.3',
    'lowband': 'delay=40,jitter=10,rate=512,queue=400',
    'step': 'delay=35,jitter=10,step=30:200/60:35',
    # BUG-0366: the 2026-11-01 report as a home connection: ~0.15 latency spikes/s of 250 ms for 0.7 s,
    # ~0.1 loss bursts/s of 30 % for 1.5 s
    'player': 'delay=45,jitter=15,loss=0.01,spike_every=7,spike_s=0.7,spike_ms=250,'
              'burst_every=10,burst_s=1.5,burst_loss=0.3',
}
KEYS = ('delay', 'jitter', 'loss', 'dup', 'outage', 'rate', 'queue', 'burst_every', 'burst_s', 'burst_loss',
        'spike_every', 'spike_s', 'spike_ms', 'step')
# the proxy port of each guest of lane N (console ports 47800-47839, co-op ports 47810-47849)
PORT_BASE = {'guest': 47900, 'Bob': 47900, 'extra1': 47920, 'Carol': 47920, 'extra2': 47930, 'Dave': 47930}


def parse(spec):
    spec = PRESETS.get(spec, spec)
    p = dict(delay=0.0, jitter=0.0, loss=0.0, dup=0.0, rate=0.0, queue=300.0, burst_every=0.0, burst_s=0.0,
             burst_loss=0.0, spike_every=0.0, spike_s=0.0, spike_ms=0.0, step=[])
    for part in filter(None, (x.strip() for x in spec.split(','))):
        k, v = part.split('=')
        k = k.strip()
        if k not in KEYS:
            raise ValueError(f'netem: unknown key {k!r} (known: {", ".join(KEYS)})')
        if k == 'step':
            p['step'] = sorted((float(a), float(b)) for a, b in (x.split(':') for x in v.split('/')))
        else:
            p[k] = float(v)
    return p


class Model:
    """what happens to each packet, without sockets (tests drive it with their own clock):
    decide(direction, size, now) -> [delivery times] (empty: lost)"""

    def __init__(self, spec, seed=1, now=0.0):
        self.p = parse(spec) if isinstance(spec, str) else spec
        self.rng = random.Random(seed)
        self.t0 = now
        self.down_until = 0.0
        self.bad = False  # Gilbert-Elliott state
        self.switch_at = now + self._good_len() if self.p['burst_every'] > 0 else float('inf')
        self.spike_on = False
        self.spike_at = now + self._spike_gap() if self.p['spike_every'] > 0 else float('inf')
        self.busy = {0: now, 1: now}  # the bandwidth queue of each direction: when it is free again
        self.stats = dict(up=0, down=0, lost=0, dup=0, burst_lost=0, bursts=0, spikes=0, rate_drop=0)

    def _good_len(self):
        return self.rng.expovariate(1.0 / self.p['burst_every'])

    def _bad_len(self):
        return self.rng.expovariate(1.0 / max(self.p['burst_s'], 1e-3))

    def _spike_gap(self):
        return self.p['spike_every'] * self.rng.uniform(0.7, 1.3)

    def advance(self, now):
        while now >= self.switch_at:
            self.bad = not self.bad
            self.stats['bursts'] += self.bad
            self.switch_at += self._bad_len() if self.bad else self._good_len()
        while now >= self.spike_at:
            self.spike_on = not self.spike_on
            self.stats['spikes'] += self.spike_on
            self.spike_at += self.p['spike_s'] if self.spike_on else self._spike_gap()

    def base_delay(self, now):
        d = self.p['delay']
        for at, ms in self.p['step']:
            if now - self.t0 >= at:
                d = ms
        return d + (self.p['spike_ms'] if self.spike_on else 0.0)

    def decide(self, direction, size, now):
        self.advance(now)
        self.stats['up' if direction == 0 else 'down'] += 1
        if now < self.down_until:
            self.stats['lost'] += 1
            return []
        if self.bad and self.rng.random() < self.p['burst_loss']:
            self.stats['lost'] += 1
            self.stats['burst_lost'] += 1
            return []
        if self.rng.random() < self.p['loss']:
            self.stats['lost'] += 1
            return []
        depart = now
        if self.p['rate'] > 0:
            depart = max(now, self.busy[direction]) + size * 8 / (self.p['rate'] * 1000)
            if depart - now > self.p['queue'] / 1000:
                self.stats['rate_drop'] += 1
                self.stats['lost'] += 1
                return []
            self.busy[direction] = depart
        copies = 2 if self.rng.random() < self.p['dup'] else 1
        self.stats['dup'] += copies - 1
        base = self.base_delay(now)
        return [depart + max(0.0, base + self.rng.uniform(-self.p['jitter'], self.p['jitter'])) / 1000
                for _ in range(copies)]


class Proxy:
    def __init__(self, listen_port, host_port, spec, seed=1):
        self.spec = spec
        self.model = Model(spec, seed, time.time())
        self.p = self.model.p
        self.host = ('127.0.0.1', host_port)
        self.guest = None
        self.a = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)  # faces the guest
        self.a.bind(('127.0.0.1', listen_port))
        self.b = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)  # faces the host
        self.b.bind(('127.0.0.1', 0))
        for s in (self.a, self.b):
            s.setblocking(False)
            s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 << 20)
        self.queue, self.lock = [], threading.Lock()
        self.seq = 0
        self.stop = False
        threading.Thread(target=self.pump, daemon=True).start()

    @property
    def stats(self):
        return self.model.stats

    def outage(self, seconds):
        self.model.down_until = time.time() + seconds

    def send_later(self, sock, data, addr, direction):
        for when in self.model.decide(direction, len(data) + 28, time.time()):
            with self.lock:
                self.seq += 1
                heapq.heappush(self.queue, (when, self.seq, sock, data, addr))

    def pump(self):
        # select() over both sockets, everything that is waiting is read at once: the old loop waited
        # up to 5 ms on each idle socket per packet and so capped the link at ~200 packets/s (~200 KB/s),
        # which made a world transfer take ~21 s on "mobile" whatever the netcode did
        import select
        while not self.stop:
            with self.lock:
                wait = max(0.0, min(0.02, self.queue[0][0] - time.time())) if self.queue else 0.02
            try:
                ready, _, _ = select.select([self.a, self.b], [], [], wait)
            except (OSError, ValueError):
                ready = []
            for s in ready:
                for _ in range(256):
                    try:
                        data, addr = s.recvfrom(4096)
                    except (socket.timeout, BlockingIOError, OSError):
                        break
                    if s is self.a:
                        self.guest = addr
                        self.send_later(self.b, data, self.host, 0)
                    elif self.guest:
                        self.send_later(self.a, data, self.guest, 1)
            now = time.time()
            with self.lock:
                while self.queue and self.queue[0][0] <= now:
                    _, _, sock, data, addr = heapq.heappop(self.queue)
                    try:
                        sock.sendto(data, addr)
                    except OSError:
                        pass

    def describe(self):
        return f'netem {self.spec}: {self.stats}'


_proxies = {}


def port_of(lane, who='guest'):
    return PORT_BASE.get(who, 47900) + lane


def from_env(host_port, lane, who='guest'):
    """a guest's join port: through its own proxy when SPORE_NET is set, else the host itself"""
    spec = os.environ.get('SPORE_NET', '')
    if not spec:
        return host_port
    key = {'Bob': 'guest', 'Carol': 'extra1', 'Dave': 'extra2'}.get(who, who)
    if key not in _proxies:
        seed = 1 + sorted(set(PORT_BASE.values())).index(PORT_BASE.get(key, 47900))
        try:
            _proxies[key] = Proxy(port_of(lane, key), host_port, spec, seed)
        except OSError as e:
            import errno
            if e.errno != errno.EADDRINUSE:
                raise
            # a helper of the test (load_game.py run by coop_two_instances.py, ...) imports lane while the
            # test's own process runs this proxy: use it (every creature_* players variant died here at its
            # host's load, lab 20261101-160125)
            return port_of(lane, key)
        print(f'  (bad network for {key}: {spec} -> {parse(spec)})', flush=True)
    return port_of(lane, key)


def proxy(who='guest'):
    return _proxies.get(who)


def proxies():
    return dict(_proxies)
