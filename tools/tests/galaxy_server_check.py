#!/usr/bin/env python3
"""The galaxy server alone (no game): its protocol rules checked with plain UDP sockets.

    py tools/tests/galaxy_server_check.py [path to galaxy_server(.exe)]

Starts build/galaxy_server(.exe) --local on a free port with an empty worlds folder and checks what the 2026-11-18
review fixed (docs/13-alpha-review-2026-11-18.md): no allocation for an upload of a player that is not the planet's
host, the world size limit, a stored world sent back intact, each player once in a fan-out, a lost LEAVE and a lost
READY followed from the pings, names cleaned, the players-per-address limit, no note for an answer to no invite.
Exit code 0: all passed. Wire format: mods/coop/net/galaxy.h. GALAXY_CHECK_OLD=1: against a server built before
the fixes (no limit switches): 9 of the 15 checks fail there (2026-11-18).
"""
import os
import socket
import struct
import subprocess
import sys
import tempfile
import time
import zlib

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
EXE = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'build', 'galaxy_server.exe' if os.name == 'nt' else 'galaxy_server')
PROTO = 6
(HELLO, WELCOME, LIST, PLANETS, ENTER, ROLE, LEAVE, PING, PONG, REFUSED, HOSTGONE, READY, FETCH, XBEGIN, XCHUNK, XACK, XDONE,
 HOSTMOVE, PARTY, PARTYVIEW, PARTYNOTE, WHO, PLAYERS, WHISPER, CHALLENGE) = range(1, 26)
CHUNK = 1100
fails = []


def check(ok, what):
    print(('  ok    ' if ok else '  FAIL  ') + what)
    if not ok:
        fails.append(what)


def s8(t):
    b = t.encode() if isinstance(t, str) else t
    return bytes([len(b)]) + b


class Reader:
    def __init__(self, b):
        self.b, self.i = b, 0

    def take(self, fmt):
        n = struct.calcsize(fmt)
        v = struct.unpack_from(fmt, self.b, self.i)[0]
        self.i += n
        return v

    def s8(self):
        n = self.take('<B')
        v = self.b[self.i:self.i + n]
        self.i += n
        return v


class Player:
    def __init__(self, port, name, identity):
        self.s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.s.bind(('127.0.0.1', 0))
        self.s.settimeout(0.05)
        self.to = ('127.0.0.1', port)
        self.name, self.identity, self.slot, self.inbox = name, identity, 0, []

    def send(self, t, body=b''):
        self.s.sendto(b'G' + bytes([t]) + body, self.to)

    def raw(self, data):
        self.s.sendto(data, self.to)

    def pump(self, secs=0.3):
        end = time.time() + secs
        while time.time() < end:
            try:
                d, _ = self.s.recvfrom(65536)
                self.inbox.append(d)
            except socket.timeout:
                pass

    def take(self, t, secs=1.0):
        """the first control message of type t (removed from the inbox), or None"""
        end = time.time() + secs
        while True:
            for k, d in enumerate(self.inbox):
                if len(d) >= 2 and d[0:1] == b'G' and d[1] == t:
                    return self.inbox.pop(k)[2:]
            if time.time() >= end:
                return None
            self.pump(0.1)

    def hello(self, key=None, cookie=0):
        k = self.identity * 7 + 1 if key is None else key  # (protocol 6: the identity's key, then the address cookie)
        self.send(HELLO, struct.pack('<IQ', PROTO, self.identity) + s8(self.name) + s8('check') + struct.pack('<QQ', k, cookie))
        ch = self.take(CHALLENGE, 0.5)
        if ch is not None:
            self.send(HELLO, struct.pack('<IQ', PROTO, self.identity) + s8(self.name) + s8('check') +
                      struct.pack('<QQ', k, struct.unpack_from('<Q', ch)[0]))
        w = self.take(WELCOME)
        if w is not None:
            self.slot = struct.unpack_from('<H', w)[0]
        return w is not None

    def ping(self, planet='', state=0):
        self.send(PING, s8(planet) + bytes([state]))

    def enter(self, planet):
        self.send(ENTER, s8(planet))


def role_of(body):
    r = Reader(body)
    planet = r.s8().decode()
    role, host, stored = r.take('<B'), r.take('<H'), r.take('<I')
    r.take('<Q')
    return planet, role, host, stored


def main():
    if not os.path.exists(EXE):
        print('no galaxy server at', EXE)
        return 2
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    probe.bind(('127.0.0.1', 0))
    port = probe.getsockname()[1]
    probe.close()
    worlds = tempfile.mkdtemp(prefix='galaxy_check_')
    limits = [] if os.environ.get('GALAXY_CHECK_OLD') else ['--max-per-address', '6', '--max-world-mb', '1']  # (a server before the limits)
    srv = subprocess.Popen([EXE, '--local', '--worlds', worlds] + limits + [str(port), 'check'],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        time.sleep(0.5)
        a, b, c = Player(port, 'Host', 0xA1), Player(port, 'Guest', 0xB2), Player(port, 'Od\nd\x07 name', 0xC3)
        print('connect')
        stranger = Player(port, 'x', 1)
        stranger.send(LIST)
        check(stranger.take(REFUSED) is not None, 'a datagram from an unknown address is refused')
        if not os.environ.get('GALAXY_CHECK_OLD'):
            print('6S')
            probe2 = Player(port, 'Probe', 0xD4)
            probe2.send(HELLO, struct.pack('<IQ', PROTO, probe2.identity) + s8('Probe') + s8('check') + struct.pack('<QQ', 5, 0))
            check(probe2.take(CHALLENGE) is not None and probe2.take(WELCOME, 0.3) is None,
                  'a HELLO from a new address without its cookie gets a CHALLENGE, no slot')
            probe2.send(HELLO, struct.pack('<IQ', PROTO, probe2.identity) + s8('Probe') + s8('check') + struct.pack('<QQ', 5, 12345))
            check(probe2.take(WELCOME, 0.5) is None, 'a wrong cookie is no proof: no slot')
        check(a.hello() and b.hello() and c.hello(), 'three players are welcomed')
        if not os.environ.get('GALAXY_CHECK_OLD'):
            thief = Player(port, 'Thief', a.identity)
            ok = thief.hello(key=999)
            ref = thief.take(REFUSED, 0.5)
            check(not ok and ref is not None and b'another key' in ref, "another game with Host's identity and another key is refused")

        print('names')
        b.send(WHO)
        pl = b.take(PLAYERS)
        check(pl is not None and b'Odd name' in pl and b'\n' not in pl and b'\x07' not in pl, 'a name with a line break and a control character arrives cleaned')

        print('a lost LEAVE')
        c.enter('Lost')
        check((c.take(ROLE) or b'') != b'' , 'the first player on a planet hosts it')
        for _ in range(4):
            c.ping('', 0)
            time.sleep(0.05)
        c.pump(0.3)
        b.send(LIST)
        lst = b.take(PLANETS)
        check(lst is not None and b'Lost' not in lst, 'pings that say "on no planet" take the player off it (the LEAVE was lost)')

        print('a lost READY')
        a.enter('Earth')
        ra = a.take(ROLE)
        check(ra is not None and role_of(ra)[1] == 1, 'Host hosts Earth')
        b.enter('Earth')
        check(b.take(REFUSED) is not None, 'Earth is "being loaded" before its host is ready')
        for _ in range(4):
            a.ping('Earth', 2)
            time.sleep(0.05)
        a.pump(0.3)
        b.enter('Earth')
        rb = b.take(ROLE)
        check(rb is not None and role_of(rb)[1] == 2 and role_of(rb)[2] == a.slot, 'pings that say "hosting" make the planet ready (the READY was lost): Guest joins')

        print('uploads')
        t0 = time.time()
        for i in range(200):  # (128 MB each before the fix: 25 GB of allocations)
            b.send(XBEGIN, struct.pack('<I', 1000 + i) + s8('Earth') + struct.pack('<II', 128 << 20, 0))
        done = b.take(XDONE, 3.0)
        check(done is not None and done[4] == 0 and time.time() - t0 < 3.0, 'an upload of a player that is not the host is refused at once (nothing allocated)')
        b.inbox.clear()
        a.send(XBEGIN, struct.pack('<I', 7) + s8('Earth') + struct.pack('<II', 2 << 20, 0))
        big = a.take(XDONE)
        check(big is not None and big[4] == 0, 'a world over the size limit is refused')
        world = bytes((i * 7 + 3) & 0xFF for i in range(2500))
        a.send(XBEGIN, struct.pack('<I', 8) + s8('Earth') + struct.pack('<II', len(world), zlib.crc32(world)))
        for i in range(0, len(world), CHUNK):
            a.send(XCHUNK, struct.pack('<II', 8, i // CHUNK) + world[i:i + CHUNK])
        ok = a.take(XDONE, 2.0)
        check(ok is not None and ok[4] == 1, 'the host stores its world')

        print('downloads')
        b.send(FETCH)
        xb = b.take(XBEGIN, 2.0)
        got = {}
        if xb is not None:
            r = Reader(xb)
            xid = r.take('<I')
            r.s8()
            size, crc = r.take('<I'), r.take('<I')
            end = time.time() + 3
            while len(got) < (size + CHUNK - 1) // CHUNK and time.time() < end:
                ch = b.take(XCHUNK, 0.3)
                if ch is None:
                    continue
                idx = struct.unpack_from('<I', ch, 4)[0]
                got[idx] = ch[8:]
                b.send(XACK, struct.pack('<II', xid, idx))
            data = b''.join(got[k] for k in sorted(got))
            check(data == world and crc == zlib.crc32(world), 'a guest gets the stored world back intact')
        else:
            check(False, 'a guest gets the stored world back intact')

        print('fan-out')
        b.inbox.clear()
        a.raw(b'F' + bytes([3]) + struct.pack('<HHH', b.slot, b.slot, b.slot) + bytes([42]) + b'hello')
        b.pump(0.5)
        copies = [d for d in b.inbox if d[0:1] == b'F']
        check(len(copies) == 1 and copies[0][3:] == bytes([42]) + b'hello', 'a player listed three times in a fan-out gets one copy')

        print('parties')
        a.inbox.clear()
        b.send(PARTY, struct.pack('<BQBI', 2, a.identity, 1, 0))  # an answer to an invite nobody sent
        b.pump(0.3)
        a.pump(0.3)
        check(a.take(PARTYNOTE, 0.2) is None, 'an answer to no invite sends its "inviter" nothing')

        print('limits')
        more = [Player(port, 'p%d' % i, 0x100 + i) for i in range(5)]
        res = [p.hello() for p in more]
        check(sum(res) == 3 and more[-1].take(REFUSED, 0.3) is not None, 'the players-per-address limit refuses the seventh (6 allowed)')
    finally:
        srv.kill()
    print('galaxy_server_check: %s' % ('all passed' if not fails else '%d FAILED' % len(fails)))
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
