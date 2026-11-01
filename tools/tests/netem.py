"""A bad network between the guest and the host of a lane: a UDP proxy with delay, jitter, loss,
duplicates and outages (both directions). twoinst.start() puts it in the guest's way when
SPORE_NET is set, so every test and the chaos bot can run over it:

  SPORE_NET="delay=120,jitter=40,loss=0.03"   (ms, ms, fraction; also dup=0.01, outage=10)
  SPORE_NET=dsl | mobile | awful | player      (presets)
  BUG-0366, a home connection's own trouble: spike=0.1,spikems=300,spikelen=800 (a latency spike
  starts with this chance per second: every packet meanwhile is held spikems longer, in order),
  burst=0.1,burstlen=1500,burstloss=0.3 (a loss burst: burstloss of the packets for burstlen ms)

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
    # BUG-0366: the players' report (rtt 70-150 ms, spikes to 400+, bursts of 10-30 % loss)
    'player': 'delay=45,jitter=15,loss=0.01,spike=0.15,spikems=250,spikelen=700,burst=0.1,burstlen=1500,burstloss=0.3',
}


def parse(spec):
    spec = PRESETS.get(spec, spec)
    p = dict(delay=0.0, jitter=0.0, loss=0.0, dup=0.0, spike=0.0, spikems=0.0, spikelen=0.0, burst=0.0,
             burstlen=0.0, burstloss=0.0)
    for part in filter(None, (x.strip() for x in spec.split(','))):
        k, v = part.split('=')
        p[k] = float(v)
    return p


class Proxy:
    def __init__(self, listen_port, host_port, spec, seed=1):
        self.p = parse(spec)
        self.spec = spec
        self.rng = random.Random(seed)
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
        self.down_until = 0.0
        self.spike_until = self.burst_until = self.next_roll = 0.0
        self.stats = dict(up=0, down=0, lost=0, dup=0, spikes=0, bursts=0)
        self.stop = False
        threading.Thread(target=self.pump, daemon=True).start()

    def outage(self, seconds):
        self.down_until = time.time() + seconds

    def roll(self, now):
        """spikes and loss bursts start by chance, rolled every 0.1 s"""
        while self.next_roll <= now:
            if self.next_roll == 0.0:
                self.next_roll = now
            if self.p['spike'] and now >= self.spike_until and self.rng.random() < self.p['spike'] / 10:
                self.spike_until = now + self.p['spikelen'] / 1000
                self.stats['spikes'] += 1
            if self.p['burst'] and now >= self.burst_until and self.rng.random() < self.p['burst'] / 10:
                self.burst_until = now + self.p['burstlen'] / 1000
                self.stats['bursts'] += 1
            self.next_roll += 0.1

    def send_later(self, sock, data, addr):
        now = time.time()
        self.roll(now)
        loss = self.p['burstloss'] if now < self.burst_until else self.p['loss']
        if now < self.down_until or self.rng.random() < loss:
            self.stats['lost'] += 1
            return
        copies = 2 if self.rng.random() < self.p['dup'] else 1
        self.stats['dup'] += copies - 1
        for _ in range(copies):
            d = max(0.0, self.p['delay'] + self.rng.uniform(-self.p['jitter'], self.p['jitter'])) / 1000
            if now < self.spike_until:  # the queue of a congested link: held longer, still in order
                d += self.p['spikems'] * (self.spike_until - now) / (self.p['spikelen'] / 1000) / 1000
            with self.lock:
                heapq.heappush(self.queue, (now + d, id(data) + _, sock, data, addr))

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
                        self.stats['up'] += 1
                        self.send_later(self.b, data, self.host)
                    elif self.guest:
                        self.stats['down'] += 1
                        self.send_later(self.a, data, self.guest)
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


_proxy = None


def from_env(host_port, lane):
    """the guest's join port: through a proxy when SPORE_NET is set, else the host itself"""
    global _proxy
    spec = os.environ.get('SPORE_NET', '')
    if not spec:
        return host_port
    if _proxy is None:
        _proxy = Proxy(47900 + lane, host_port, spec)
        print(f'  (bad network for the guest: {spec} -> {parse(spec)})', flush=True)
    return 47900 + lane


def proxy():
    return _proxy
