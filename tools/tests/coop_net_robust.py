#!/usr/bin/env python3
"""Malformed messages from a joined player (BUG-0201): two scripted guests (tools/fakepeer.py's
protocol) join the host of a two-game session. Guest A sends Space messages whose header does not
fit their sender and then a few hundred cut or random messages of every type a guest sends to the
host; guest B listens to what the host passes on. Checks: B gets A's own Space message but none of
the ones that do not fit A, the host keeps running without a fault, the real guest stays in the
session."""
import importlib.util
import os
import random
import socket
import struct
import threading
import time

import lane
from twoinst import Checks, host_frame, start

spec = importlib.util.spec_from_file_location('fakepeer', os.path.join(lane.ROOT, 'tools', 'fakepeer.py'))
fp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fp)

MSG_SPACE, MSG_CREATION = 48, 45
# message types a guest sends to the host (protocol.h)
GUEST_TYPES = [7, 11, 21, 24, 26, 27, 28, 30, 32, 34, 35, 36, 38, 41, 44, 45, 46, 48, 49, 51]


class Peer:
    def __init__(self, name):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(('127.0.0.1', 0))
        self.sock.setblocking(False)
        self.addr = ('127.0.0.1', lane.PORT)
        self.c = fp.Conn(self.sock, self.addr)
        self.id = None
        self.got = []  # (type, payload)
        self.lock = threading.Lock()
        self.stop = False
        nm = name if isinstance(name, bytes) else name.encode()  # bytes: sent as they are (coop_net_names.py)
        self.c.send_reliable(fp.HELLO, struct.pack('<HIB', self.c.protocol, 0xdc04aee5, len(nm)) + nm)
        threading.Thread(target=self.run, daemon=True).start()

    def deliver(self, typ, p):
        if typ == fp.WELCOME:
            self.id = p[0]
        self.got.append((typ, bytes(p)))

    def run(self):
        while not self.stop:
            with self.lock:
                try:
                    while True:
                        data, frm = self.sock.recvfrom(4096)
                        if frm == self.addr:
                            self.c.on_packet(data, self.deliver)
                except BlockingIOError:
                    pass
                self.c.flush(time.time())
            time.sleep(0.01)

    def send(self, typ, payload):
        with self.lock:
            self.c.send_reliable(typ, payload)

    def pending(self):
        with self.lock:
            return len(self.c.rel_out)

    def bye(self):
        with self.lock:
            for _ in range(3):
                self.c.send_unreliable(fp.BYE, b'\0')
                self.c.flush(time.time() + 1)
        self.stop = True


def keep_frames(host, seconds):
    """a guest joining makes the host save (a modal box stops its frames): close it"""
    t0 = time.time()
    while time.time() - t0 < seconds:
        f1 = host_frame(host)
        time.sleep(0.7)
        if host_frame(host) == f1:
            host.call('key enter')


def space(kind, origin, body):
    return struct.pack('<BB', kind, origin) + body


def main():
    host, guest = start()
    ck = Checks()
    a, b = Peer('FuzzA'), Peer('FuzzB')
    t0 = time.time()
    while time.time() - t0 < 20 and (a.id is None or b.id is None):
        time.sleep(0.2)
    ck(a.id is not None and b.id is not None, f'both scripted guests joined (ids {a.id}, {b.id})')
    if a.id is None or b.id is None:
        return ck.result()
    keep_frames(host, 15)

    # Space: K_DOWN (kind 1) {u8 state, u8 seconds}; the seconds byte tells the messages apart
    a.send(MSG_SPACE, space(1, a.id, bytes([2, 101])))     # A's own: passed on
    a.send(MSG_SPACE, space(1, 0, bytes([2, 102])))        # not A
    a.send(MSG_SPACE, space(1, b.id, bytes([2, 103])))     # not A
    a.send(MSG_SPACE, space(20, a.id, bytes(40)))          # a kind only the host sends
    a.send(MSG_SPACE, space(57, 0, bytes([b.id, 0, 0, 0, 0])))
    a.send(MSG_SPACE, space(1, a.id, bytes([2, 104])))     # A's own again: the order is kept
    t0 = time.time()
    while time.time() - t0 < 15 and not any(t == MSG_SPACE and p[-1:] == bytes([104]) for t, p in b.got):
        time.sleep(0.2)
    relayed = [p for t, p in b.got if t == MSG_SPACE]
    marks = [p[-1] for p in relayed if len(p) == 4 and p[0] == 1]
    print(f'  B got {len(relayed)} Space message(s), K_DOWN marks {marks}')
    ck(101 in marks and 104 in marks, "the host passes on A's own Space messages")
    ck(102 not in marks and 103 not in marks, 'the host does not pass on messages that claim another sender')
    ck(not any(p[:1] in (b'\x14', b'\x39') for p in relayed), 'the host does not pass on host-only kinds from a guest')

    # creations: a piece that does not fit the buffer it belongs to, then random and cut messages
    a.send(MSG_CREATION, struct.pack('<BBIIIH', a.id, 0, 0x5EED, 1000, 0, 900) + bytes(900))
    a.send(MSG_CREATION, struct.pack('<BBIIIH', a.id, 0, 0x5EED, 4000, 1800, 900) + bytes(900))
    rng = random.Random(201)
    sent = 0
    for rnd in range(12):
        for typ in GUEST_TYPES:
            n = rng.choice([0, 1, 2, 3, 5, 8, 13, 20, 40, 120])
            body = bytes(rng.randrange(256) for _ in range(n))
            if typ == MSG_SPACE and n >= 2 and rnd % 2 == 0:
                body = bytes([body[0], a.id]) + body[2:]  # a header that fits: the payload is cut
            if typ == MSG_CREATION and n >= 1:
                body = bytes([a.id]) + body[1:]
            # fakepeer packs every unacknowledged message into one datagram: keep it small
            t1 = time.time()
            while a.pending() > 3 and time.time() - t1 < 10:
                time.sleep(0.02)
            if time.time() - a.c.last_recv > 8:
                break  # the host dropped A
            a.send(typ, body)
            sent += 1
    t0 = time.time()
    while time.time() - t0 < 20 and a.pending():
        time.sleep(0.2)
    print(f'  A sent {sent} random messages, {a.pending()} not acknowledged')
    keep_frames(host, 6)
    a.bye()
    b.bye()
    time.sleep(3)
    hs = host.call('coop.status')
    ck(hs['ok'], 'the host answers')
    f1 = host_frame(host)
    time.sleep(2)
    ck(host_frame(host) != f1, 'the host game runs (frames go on)')
    gs = guest.call('coop.status')['out']
    ck("remote 0 'Alice'" in gs, 'the real guest is still in the session')
    sp = host.call('coop.space')['out']
    print('  host:', ' '.join(ln for ln in sp.splitlines() if 'bad_origin' in ln)[:200])
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
