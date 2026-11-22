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
PROTO = int(os.environ.get('GALAXY_PROTO', '9'))  # (GALAXY_PROTO=8: a FAIL run on a server before BUG-9336)
(HELLO, WELCOME, LIST, PLANETS, ENTER, ROLE, LEAVE, PING, PONG, REFUSED, HOSTGONE, READY, FETCH, XBEGIN, XCHUNK, XACK, XDONE,
 HOSTMOVE, PARTY, PARTYVIEW, PARTYNOTE, WHO, PLAYERS, WHISPER, CHALLENGE, MOVEON, MOVED, ARRIVE, VISIT, VISITOR, CRTPUT, CRTGET, CRTHAVE, CRTPART, VISITOBJ, ERAS, STARTERA) = range(1, 38)
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
            self.welcome = w
        return w is not None

    def ping(self, planet='', state=0):
        self.send(PING, s8(planet) + bytes([state]))

    def enter(self, planet):
        self.send(ENTER, s8(planet))


def upload(p, xid, planet, data, era=None):
    """An upload of `data` as `planet`'s world; era: the protocol 7 facts (tech 0, no species, the era byte)."""
    head = struct.pack('<I', xid) + s8(planet) + struct.pack('<II', len(data), zlib.crc32(data))
    if era is not None:
        head += bytes([0]) + struct.pack('<III', 0, 0, 0) + bytes([era])
    p.send(XBEGIN, head)
    for i in range(0, len(data), CHUNK):
        p.send(XCHUNK, struct.pack('<II', xid, i // CHUNK) + data[i:i + CHUNK])
    return p.take(XDONE, 2.0)


def blob(planet, n=3000):
    """A world as the game packs it (world.cpp galaxy_pack): "SCW1", str8 the save's planet, then anything."""
    return b'SCW1' + s8(planet) + bytes((i * 5 + 1) & 0xFF for i in range(n))


def moved_of(body):
    r = Reader(body)
    planet = r.s8().decode()
    kind, host = r.take('<B'), r.take('<H')
    return planet, kind, host, r.take('<Q')


def ready(p, planet):
    for _ in range(4):
        p.ping(planet, 2)
        time.sleep(0.05)
    p.pump(0.3)


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
    limits += os.environ.get('GALAXY_CHECK_ARGS', '').split()  # (a FAIL run: a fix's switch off, e.g. --visit-radius 0)
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
            print('BUG-9285')
            name_len = a.welcome[2]
            off = 3 + name_len + 1  # u16 slot, str8 name, u8 the players a planet takes
            check(len(a.welcome) >= off + 4 and struct.unpack_from('<I', a.welcome, off)[0] == 180 * 60,
                  'WELCOME carries the nest refresh (180 min by default)')
            print('BUG-9286')
            check(len(a.welcome) >= off + 8 and struct.unpack_from('<I', a.welcome, off + 4)[0] == 72 * 3600,
                  'WELCOME carries the nest rotation (72 h by default)')
            a.ping()
            pong = a.take(PONG)
            check(pong is not None and len(pong) >= 8 and abs(struct.unpack_from('<Q', pong)[0] - time.time()) < 5,
                  "PONG carries the server's clock")
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

        print('eras (step 4)')
        c.enter('Mars|Creature')
        rc = c.take(ROLE)
        check(rc is not None and role_of(rc)[1] == 1, "the first player in Mars's Creature era hosts it")
        for _ in range(4):
            c.ping('Mars|Creature', 2)
            time.sleep(0.05)
        c.pump(0.3)
        d = upload(c, 50, 'Mars|Creature', blob('Mars'), era=3)
        check(d is not None and d[4] == 0 and b'Tribe stage' in d, 'a Tribe world is refused by the Creature era')
        d = upload(c, 51, 'Mars|Creature', blob('Mars'), era=2)
        check(d is not None and d[4] == 1, 'a Creature world of Mars is stored in its Creature era')
        d = upload(c, 52, 'Mars|Creature', blob('Venus'), era=2)
        check(d is not None and d[4] == 0 and b'another planet' in d, "another planet's save is refused over Mars")
        d = upload(c, 53, 'Mars|Creature', blob('Mars', 3100), era=2)
        keep = [f for f in os.listdir(worlds) if f.endswith('.world.1')]
        check(d is not None and d[4] == 1 and len(keep) == 1, 'a new save of Mars keeps the one before it (.world.1)')
        c.enter('Mars|Tribe')
        rt = c.take(ROLE)
        check(rt is not None and role_of(rt)[0] == 'Mars|Tribe' and role_of(rt)[1] == 1 and role_of(rt)[3] == 0,
              "Mars's Tribe era is its own planet session (hosted, nothing stored yet)")

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

        print('a party crosses to the next stage (step 4 part 3)')
        for p in (a, b, c):
            p.send(LEAVE)
        time.sleep(0.2)
        a.send(PARTY, struct.pack('<BQBI', 1, b.identity, 0, 0))
        time.sleep(0.2)
        b.send(PARTY, struct.pack('<BQBI', 2, a.identity, 1, 0))
        time.sleep(0.3)
        for p in (a, b, c):
            p.pump(0.2)
            p.inbox.clear()
        a.enter('Venus|Creature')
        ra = a.take(ROLE)
        ready(a, 'Venus|Creature')
        b.enter('Venus|Creature')
        c.enter('Venus|Creature')
        check(ra is not None and role_of(ra)[1] == 1 and b.take(ROLE) is not None and c.take(ROLE) is not None,
              'Host hosts Venus (Creature), Guest (its party) and the third player join')
        a.send(MOVEON)
        ma, mb = a.take(MOVED), b.take(MOVED)
        rc = c.take(ROLE)
        ok = ma is not None and mb is not None and moved_of(ma)[1] == 1 and moved_of(ma)[0] == moved_of(mb)[0]
        check(ok and '~' in moved_of(ma)[0] and moved_of(mb)[2] == a.slot and moved_of(ma)[3] == moved_of(mb)[3] != 0,
              "MOVEON: the party goes into its crossing, its leader hosts it (warm, one token)")
        check(rc is not None and role_of(rc)[0] == 'Venus|Creature' and role_of(rc)[1] == 1 and c.take(MOVED, 0.2) is None,
              'the player who stays takes Venus (Creature) over')
        cross = moved_of(ma)[0] if ma else ''
        c.send(LIST)
        lst = c.take(PLANETS)
        check(lst is not None and b'~' not in lst, 'a crossing is not listed')
        b.inbox.clear()
        b.send(MOVEON)
        check(b.take(REFUSED) is not None, 'only the leader moves the party on')
        c.inbox.clear()
        c.enter(cross)
        check(c.take(REFUSED) is not None, 'nobody enters a crossing')
        d = upload(a, 60, cross, blob('Venus'), era=2)
        check(d is not None and d[4] == 0, 'a crossing is never stored')
        a.send(ARRIVE, bytes([2]))
        check(a.take(REFUSED) is not None, 'ARRIVE in the stage it left is refused')
        a.send(ARRIVE, bytes([3]))
        ma, mb = a.take(MOVED), b.take(MOVED)
        check(ma is not None and mb is not None and moved_of(ma)[:2] == ('Venus|Tribe', 2) and moved_of(mb)[:2] == ('Venus|Tribe', 2),
              'ARRIVE in Tribe, nobody hosts Venus (Tribe): the crossing becomes it (no reload)')
        c.send(LIST)
        lst = c.take(PLANETS)
        check(lst is not None and b'Venus|Tribe' in lst, 'Venus (Tribe) is listed')
        # the second party to arrive joins the era's host: the party moves on again from Tribe while Civ has a host
        c.enter('Venus|Civ')
        rc = c.take(ROLE)
        ready(c, 'Venus|Civ')
        a.send(MOVEON)
        a.take(MOVED)
        b.take(MOVED)
        a.send(ARRIVE, bytes([4]))
        ma, mb = a.take(MOVED), b.take(MOVED)
        check(rc is not None and ma is not None and mb is not None and moved_of(ma)[:3] == ('Venus|Civ', 3, c.slot)
              and moved_of(mb)[1] == 3, 'ARRIVE where the era has a host: the party is told to join it')

        print('a player starts a stage its planet has none of (BUG-9336)')
        for p in (a, b, c):
            p.send(LEAVE)
        time.sleep(0.2)
        for p in (a, b, c):
            p.pump(0.2)
            p.inbox.clear()
        a.enter('Mars|Creature')
        a.take(ROLE)
        ready(a, 'Mars|Creature')
        mars = blob('Mars', 2500)
        d = upload(a, 70, 'Mars|Creature', mars, era=2)
        check(d is not None and d[4] == 1, 'Mars (Creature) stored')
        c.send(STARTERA, s8('Mars|Creature') + bytes([4]))
        check(c.take(REFUSED) is not None, 'STARTERA two stages up (Civ from Creature) is refused')
        c.send(STARTERA, s8('Mars|Creature') + bytes([3]))
        rc = c.take(ROLE)
        start = role_of(rc) if rc else ('', 0, 0, 0)
        check(rc is not None and '~' in start[0] and start[0].endswith('|Creature') and start[1] == 1 and start[2] == c.slot
              and start[3] == len(mars), 'STARTERA Tribe on Mars: a crossing of its own from the Creature world, hosted by the player')
        c.send(FETCH)
        xb = c.take(XBEGIN, 2.0)
        check(xb is not None and struct.unpack_from('<I', xb, 4 + 1 + len(start[0]))[0] == len(mars),
              'FETCH in the start crossing sends the stored world of Mars (Creature)')
        a.send(LIST)
        lst = a.take(PLANETS)
        check(lst is not None and b'~' not in lst, 'the start crossing is not listed')
        c.send(ARRIVE, bytes([3]))
        mc = c.take(MOVED)
        check(mc is not None and moved_of(mc)[:2] == ('Mars|Tribe', 2), 'ARRIVE: the start crossing becomes Mars (Tribe)')
        b.send(STARTERA, s8('Mars|Creature') + bytes([3]))
        check(b.take(REFUSED) is not None and b.take(ROLE, 0.2) is None, 'STARTERA Tribe again once Mars (Tribe) has a player: refused')
        b.send(STARTERA, s8('Nowhere|Creature') + bytes([3]))
        check(b.take(REFUSED) is not None, 'STARTERA from an era with no stored world: refused')

        if not os.environ.get('GALAXY_CHECK_OLD'):
            print('visitors across eras (BUG-9287)')
            for p in (a, b):
                p.send(LEAVE)
            time.sleep(0.2)
            a.enter('Pluto|Creature')
            a.take(ROLE)
            b.enter('Pluto|Tribe')
            b.take(ROLE)
            time.sleep(0.2)
            for p in (a, b, c):
                p.pump(0.2)
                p.inbox.clear()
            body = bytes([2]) + struct.pack('<III', 0x7C0B35F6, 0x2B978C46, 0x40626200) + struct.pack('<fff', 1.0, 2.0, 3.0)
            a.send(VISIT, body)
            v = b.take(VISITOR)
            check(v is not None and struct.unpack_from('<H', v)[0] == a.slot and v.endswith(body),
                  "a VISIT reaches the planet's other era with the sender's slot")
            c.pump(0.3)
            a.pump(0.1)
            check(c.take(VISITOR, 0.2) is None and a.take(VISITOR, 0.2) is None,
                  'not another planet, not the sender')
            time.sleep(0.5)
            b.inbox.clear()
            a.send(VISIT, body)
            a.send(VISIT, body)
            b.pump(0.4)
            check(sum(1 for d in b.inbox if d[0:1] == b'G' and d[1] == VISITOR) == 1, 'a VISIT sooner than 0.5 s after the last is dropped')
            time.sleep(0.5)
            b.inbox.clear()
            a.send(VISIT, body[:-1])
            check(b.take(VISITOR, 0.3) is None, 'a VISIT of the wrong size is dropped')

            print('only near players (BUG-9288)')
            def vbody(x, y, z):
                return bytes([2]) + struct.pack('<III', 0x7C0B35F6, 0x2B978C46, 0x40626200) + struct.pack('<fff', x, y, z)
            time.sleep(0.5)
            b.send(VISIT, vbody(400.0, 0.0, 0.0))  # b's creature 400 m away
            a.take(VISITOR, 0.3)
            b.inbox.clear()
            a.send(VISIT, vbody(0.0, 0.0, 0.0))
            check(b.take(VISITOR, 0.4) is None, 'a VISIT does not reach a player 400 m away (300 m radius)')
            time.sleep(0.5)
            b.send(VISIT, vbody(100.0, 0.0, 0.0))
            time.sleep(0.5)
            a.send(VISIT, vbody(0.0, 0.0, 0.0))
            check(b.take(VISITOR, 0.6) is not None, 'it reaches a player 100 m away')

            print('other eras hidden by the player (BUG-9294)')
            b.send(ERAS, bytes([0]))  # b hides other eras: it gets none, and its own go to nobody
            time.sleep(0.6)
            b.inbox.clear()
            a.send(VISIT, vbody(0.0, 0.0, 0.0))
            check(b.take(VISITOR, 0.5) is None, 'a player that hides other eras gets no VISITOR')
            a.pump(0.6)  # (the earlier VISITs' VISITORs still on the way)
            a.inbox.clear()
            b.send(VISIT, vbody(100.0, 0.0, 0.0))
            check(a.take(VISITOR, 0.5) is None, "and its own VISIT reaches nobody")
            b.send(ERAS, bytes([1]))
            time.sleep(0.6)
            b.inbox.clear()
            a.send(VISIT, vbody(0.0, 0.0, 0.0))
            check(b.take(VISITOR, 0.6) is not None, 'shown again: it gets them again')

            print('the creation store (BUG-9287, BUG-9290)')
            CT, CG = 0x2B978C46, 0x40626200
            crt = bytes((i * 7 + 3) & 0xFF for i in range(3000))
            def put(p, data, skip=()):
                c = zlib.crc32(data) & 0xFFFFFFFF
                for k, off in enumerate(range(0, len(data), 1100)):
                    if k not in skip:
                        p.send(CRTPUT, struct.pack('<IIIIII', 0x29AA0001, CT, CG, len(data), c, off) + data[off:off + 1100])
            def get(p, owner, inst=0x29AA0001):
                p.inbox.clear()
                p.send(CRTGET, struct.pack('<QIII', owner, inst, CT, CG))
                p.pump(0.5)
                parts = [d[2:] for d in p.inbox if d[0:1] == b'G' and d[1] == CRTPART]
                if not parts:
                    return None
                total = struct.unpack_from('<I', parts[0], 20)[0]
                got = bytearray(total)
                for d in parts:
                    off = struct.unpack_from('<I', d, 28)[0]
                    got[off:off + len(d) - 32] = d[32:]
                return bytes(got)
            put(a, crt, skip=(1,))
            check(a.take(CRTHAVE, 0.4) is None, 'a creation with a piece missing is not kept')
            put(a, crt)
            h = a.take(CRTHAVE)
            check(h is not None and h == struct.pack('<QIIIB', a.identity, 0x29AA0001, CT, CG, 1), "a whole creation is kept as its sender's (CRTHAVE)")
            check(get(b, a.identity) == crt, 'CRTGET of its owner and key sends every piece of it back')
            b.send(CRTGET, struct.pack('<QIII', a.identity, 0x29AA0002, CT, CG))
            h = b.take(CRTHAVE)
            check(h is not None and h[-1] == 0, 'CRTGET of a creation nobody put: CRTHAVE 0')
            other = bytes(2500)
            put(b, other)
            h = b.take(CRTHAVE)
            check(h is not None and h[-1] == 1 and get(c, b.identity) == other and get(c, a.identity) == crt,
                  "another player's creation with the same key is its own (BUG-9290)")
            a.inbox.clear()
            put(a, crt[:1100] + bytes(1900))
            h = a.take(CRTHAVE)
            check(h is not None and h[-1] == 1 and get(b, a.identity) == crt, 'a key kept already stays as it was')

            print("a Civ era's objects (BUG-9290)")
            obj = struct.pack('<IIII', 7, 0x0654CCD8, 0x24682294, 0x40646200) + struct.pack('<10f', 1, 2, 3, 0, 0, 0, 1, 0, 0, 0) + bytes([1])
            a.inbox.clear()
            b.send(VISITOBJ, bytes([1]) + obj)
            o = a.take(VISITOBJ)
            check(o is not None and struct.unpack_from('<HQ', o) == (b.slot, b.identity) and o[10:] == bytes([1]) + obj,
                  "VISITOBJ reaches the planet's other era with the sender's slot and identity")
            time.sleep(1.1)
            a.inbox.clear()
            for _ in range(12):
                b.send(VISITOBJ, bytes([1]) + obj)
            a.pump(0.5)
            check(sum(1 for d in a.inbox if d[0:1] == b'G' and d[1] == VISITOBJ) == 8, 'at most 8 VISITOBJ a second from one player')

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
