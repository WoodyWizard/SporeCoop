#!/usr/bin/env python3
"""BUG-1247 (players' video): in the guest's game the host's NPC cells around spin / dance in place, while in the
host's game at the same spot they swim evenly. Cell fixture, both cells in one place.

What a player sees of a cell is its soft body (docs/re/cell.md 10): `coop.pool cloud` gives, for every NPC cell on the
screen, the angle of its first soft-body particle about the particles' mean (the picture turns with it), on the host
for its own cells and on the guest for its mirrors (the same key: the host's index). Both games are sampled at 10 Hz;
per cell seen on both screens the total turn of that angle on each game is compared:
  * a mirror must not turn much more on the guest's screen than the same cell on the host's (spinning in place).

  coop_cell_mirror_turn.py [--seconds 25] [--fps N] [--crowd N] [--swim]
     --fps N: the guest at ~N fps (frame.limit); --crowd N: N more cells around the host; --swim: the host swims
     around (its cells come and go on both screens). Bad network: SPORE_NET="delay=250,loss=0.05".
"""
import argparse
import math
import re
import threading
import time

from twoinst import Checks, keyframe, start  # (first: it puts tools/ on the path for sporectl)
from sporectl import Console

CLOUD_RE = re.compile(r'^cloud c(\d+) host=([-\d.]+) rot=([-\d.]+) cloud=([-\d.]+) soft_age=([-\d.]+) soft_d=([-\d.]+) d=([-\d.]+)'
                      r'(?: level=\S+ fg=\S+ anim=([0-9A-F]+) state=([0-9A-F]+) speed=([-\d.]+) ailock=\d+(?: bare=(\d))?)?', re.M)


class Sampler(threading.Thread):
    def __init__(self, con):
        super().__init__(daemon=True)
        self.port = con.port - 47799
        self.rows = {}  # key -> [(t, cloud, host heading, soft_age, soft_d)]
        self.stop_ev = threading.Event()

    def run(self):
        c = Console(self.port, timeout=20)
        try:
            while not self.stop_ev.is_set():
                t = time.time()
                for m in CLOUD_RE.finditer(c.call('coop.pool cloud 80')['out']):
                    self.rows.setdefault(m.group(1), []).append(
                        (t, float(m.group(4)), float(m.group(2)), float(m.group(5)), float(m.group(6)), m.group(8) or '-',
                         m.group(9) or '-', float(m.group(10) or 0), m.group(11) == '1'))
                self.stop_ev.wait(0.1)
        finally:
            c.sock and c.sock.close()

    def stop(self):
        self.stop_ev.set()
        self.join(10)


def after_editor(ck, host, guest):
    """the host's mating call, the vote, the editors (each player's own when it has one), then the guest's reload"""
    from coop_editor_desync import READY, edit_cell, mating_call, mode, wait, wait_reload
    host_vote = lambda: 'vote active' in host.call('coop.vote')['out'] or 'Editor' in mode(host)
    _, reached = mating_call(host, 'host', host_vote)
    ck(reached, 'the host reaches its mate: the evolution vote starts')
    wait(lambda: guest.call('coop.uistate')['out'], 10)
    guest.call('click %d %d' % READY)
    ck(wait(lambda: 'Editor' in mode(host), 30), 'the host editor opens after the vote')
    time.sleep(4)
    if 'Editor' in mode(guest):
        edit_cell(guest, 'Turnguest')
    edit_cell(host, 'Turncell')
    ck(wait(lambda: 'Cell' in mode(host), 30), 'the host is back in the pool')
    print(f'  guest reloaded the host\'s world: {wait_reload(guest, 60)}', flush=True)
    wait(lambda: 'Cell' in mode(guest) and 'Cell' in mode(host), 30)
    time.sleep(8)


def own_pos(con):
    m = re.search(r'^anim avatar \S+ anim=\S+ head=\S+ angv=\S+ pos=\(([-\d.]+) ([-\d.]+)\)', con.call('coop.pool anims')['out'], re.M)
    return (float(m.group(1)), float(m.group(2))) if m else None


def turned(v):
    return sum(abs((b - a + math.pi) % (2 * math.pi) - math.pi) for a, b in zip(v, v[1:]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--seconds', type=float, default=25)
    ap.add_argument('--fps', type=int, default=0)
    ap.add_argument('--crowd', type=int, default=0)
    ap.add_argument('--swim', action='store_true')
    ap.add_argument('--editor', action='store_true', help='first the host\'s evolution (mating call, vote, editors) and the reload')
    ap.add_argument('--bare', action='store_true', help='the guest\'s particle pool full: its mirrors made again come without a soft body (BUG-0355)')
    ap.add_argument('--far', type=float, default=0, help='the guest that far from the host: its mirrors there are cells off the host\'s screen')
    args = ap.parse_args()
    host, guest = start('cell', **({'guest_creations': False} if args.editor else {}))
    ck = Checks()
    time.sleep(3)
    if args.editor:
        after_editor(ck, host, guest)
    if args.fps:
        print('  guest: ' + guest.call(f'frame.limit {int(1000 / args.fps)}')['out'].strip(), flush=True)
    if args.far:  # (the join after a load puts a new own cell back next to the host for 30 s, BUG-0352: again until it holds)
        t0 = time.time()
        while time.time() - t0 < 50:
            guest.call(f'coop.pool tp {args.far:.0f} 0')
            time.sleep(6)
            hp_, gp_ = own_pos(host), own_pos(guest)
            if hp_ and gp_ and math.hypot(hp_[0] - gp_[0], hp_[1] - gp_[1]) > args.far * 0.7:
                break
        hp_, gp_ = own_pos(host), own_pos(guest)
        print(f'  guest {math.hypot(hp_[0] - gp_[0], hp_[1] - gp_[1]):.1f} m from the host', flush=True)
    if args.bare:  # the guest's mirrors made again from a full particle pool (coop_cell_particles.py's recipe)
        for _ in range(2):
            print('  guest: ' + guest.call('coop.pool particles 1499')['out'].strip()[:80], flush=True)
            print('  guest: ' + guest.call('coop.pool zap 5000')['out'].strip()[:80], flush=True)
            time.sleep(1.5)
        guest.call('coop.pool particles 1499')
        time.sleep(3)
    if args.crowd:
        print('  host: ' + host.call(f'coop.pool crowd {args.crowd} 12')['out'].strip()[:100], flush=True)
    time.sleep(3)
    sh, sg = Sampler(host), Sampler(guest)
    sh.start()
    sg.start()
    try:
        t0 = time.time()
        k = 0
        while time.time() - t0 < args.seconds:
            if args.swim:
                a = k * 1.3
                host.call(f'coop.pool goto {12 * math.cos(a):.1f} {12 * math.sin(a):.1f}')
                k += 1
            time.sleep(2)
        keyframe('mirrors', "Cell: the host's NPC cells on both screens: on the guest they swim as on the host, none spins in place", settle=0)
    finally:
        sh.stop()
        sg.stop()
    if args.fps:
        guest.call('frame.limit auto')
    rows = []
    for key, g in sg.rows.items():
        h = sh.rows.get(key)
        if args.far:  # the host does not draw them: its heading as the guest got it is the reference
            h = [(r[0], r[2]) + r[2:] for r in g]
        if not h or len(g) < 40 or len(h) < 40:
            continue
        ht, gt = turned([r[1] for r in h]), turned([r[1] for r in g])
        hd = turned([r[2] for r in g])  # the host's heading as the guest got it
        span_h, span_g = h[-1][0] - h[0][0], g[-1][0] - g[0][0]
        ages = [r[3] for r in g if r[3] >= 0]
        big = sum(1 for r in g if abs(r[4]) > 0.5)
        rows.append((key, ht / max(1, span_h), gt / max(1, span_g), hd / max(1, span_g), len(h), len(g),
                     max(ages) if ages else -1, big, sum(1 for r in g if len(r) > 8 and r[8])))
    rows.sort(key=lambda r: r[2] - r[1], reverse=True)
    from collections import Counter
    for name, smp in (('host', sh), ('guest', sg)):  # what the cells play (+0x1B0 / the state +0x18C) on each screen
        an = Counter(r[5] for v in smp.rows.values() for r in v)
        st = Counter(r[6] for v in smp.rows.values() for r in v)
        print(f'  {name}: animations {dict(an.most_common(8))} states {dict(st.most_common(8))}', flush=True)
    print(f'  {len(rows)} cells on both screens (of {len(sh.rows)} on the host\'s, {len(sg.rows)} on the guest\'s)', flush=True)
    for r in rows[:12]:
        print(f'    c{r[0]}: turn rate host {r[1]:.2f} rad/s, guest {r[2]:.2f} rad/s (host heading as received {r[3]:.2f}), '
              f'samples {r[4]}/{r[5]}, soft age max {r[6]:.2f}, corrections > 0.5 rad {r[7]}, bare samples {r[8]}', flush=True)
    spin = [r for r in rows if r[2] > 2 * r[1] + 0.6]
    ck(len(rows) >= 3, f'cells seen on both screens ({len(rows)})')
    if args.bare:
        nb = sum(1 for r in rows if r[8] >= 20)
        ck(nb >= 2, f'mirrors without a soft body among them ({nb})')
    ck(not spin, f'no mirror spins on the guest\'s screen ({len(spin)} of {len(rows)} turn over twice the host\'s rate + 0.6 rad/s: '
       f'{[(r[0], round(r[1], 2), round(r[2], 2)) for r in spin[:6]]})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
