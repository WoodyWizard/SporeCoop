#!/usr/bin/env python3
"""BUG-1243 (players' video, second look): the GUEST fights creatures FAR from the host; the HOST's own cell, swimming
alone in empty water, is spun in the combat whirl ("I fought the air"). Cell fixture.

The guest goes 70 m from the host (`coop.pool tp`), the host does nothing. First an idle baseline, then the guest
fights the creatures around it (swims into them, the game's Bite, with the fixture's mouth and with borrowed parts:
jaw, spike, electric, poison). Sampled on the host every 0.1 s (`coop.pool anims`: its own cell's animation +0x1B0,
heading from +0x5C and position) and the world effects each game sent / played (`coop.effects recent`, with the
distance of each to the local player's own cell). Checks:
  * no effect of the guest is played on the host within 6 m of the host's own cell while the guest is 70 m away;
  * the host's own cell plays none of the attack / hurt animations and does not turn more than in the baseline;
  * the guest is also bitten (`coop.pool hurt`, as by a creature) until it is knocked out (part `hurt`);
  * key frames of both games (the host's screen must show its cell alone, without the fight's effects).

  coop_cell_far_fight.py [--parts none,jaw,spike,electric,poison,hurt] [--dist 70]
"""
import math
import re
import sys
import threading
import time

from twoinst import Checks, keyframe, start  # (first: it puts tools/ on the path for sporectl)
from sporectl import Console

ANIM_RE = re.compile(r'^anim avatar \S+ anim=([0-9A-F]+) head=([-\d.]+) angv=([-\d.naif]+) pos=\(([-\d.]+) ([-\d.]+)\) hp=(-?\d+)', re.M)
RECENT_RE = re.compile(r'^recent (\w) from=(\d+) id=([0-9A-F]{8}) at=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\) local_d=([-\d.]+) age=([\d.]+)', re.M)


def arg(name, default):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default


class Sampler(threading.Thread):
    """the own cell of one game: (t, anim, heading, x, y, hp) every 0.1 s"""

    def __init__(self, con):
        super().__init__(daemon=True)
        self.port = con.port - 47799
        self.rows = []
        self.stop_ev = threading.Event()

    def run(self):
        c = Console(self.port, timeout=20)
        try:
            while not self.stop_ev.is_set():
                m = ANIM_RE.search(c.call('coop.pool anims')['out'])
                if m:
                    self.rows.append((time.time(), m.group(1), float(m.group(2)), float(m.group(4)), float(m.group(5)), int(m.group(6))))
                self.stop_ev.wait(0.1)
        finally:
            c.sock and c.sock.close()

    def stop(self):
        self.stop_ev.set()
        self.join(10)


def turned(rows, t0, t1):
    """the total turn of the heading (rad) and its largest step over the window"""
    hs = [r[2] for r in rows if t0 <= r[0] <= t1]
    tot, big = 0.0, 0.0
    for a, b in zip(hs, hs[1:]):
        d = abs((b - a + math.pi) % (2 * math.pi) - math.pi)
        tot += d
        big = max(big, d)
    return tot, big, len(hs)


def anims(rows, t0, t1):
    return sorted({r[1] for r in rows if t0 <= r[0] <= t1})


def recent(con):
    return [(m.group(1), int(m.group(2)), m.group(3), float(m.group(7)), float(m.group(8)))
            for m in RECENT_RE.finditer(con.call('coop.effects recent')['out'])]


def own_pos(con):
    m = ANIM_RE.search(con.call('coop.pool anims')['out'])
    return (float(m.group(4)), float(m.group(5))) if m else None


def hurt(host, guest, seconds):
    """the guest's own cell is bitten (as by a creature) until it is knocked out, far from the host"""
    gid = re.search(r'client id=(\d+)', guest.call('coop.status')['out'])
    gid = gid.group(1) if gid else '1'
    n = 0
    t0 = time.time()
    while time.time() - t0 < seconds:
        host.call(f'coop.pool hurt {gid} 2')
        n += 1
        time.sleep(1.5)
    return n, 0


def fight(guest, part, seconds):
    """the guest swims into the creatures around it and bites the nearest; returns how many swims / bites"""
    if part != 'none':
        print(f'  guest borrow {part}: ' + guest.call(f'coop.pool borrow {part} graft')['out'].strip()[:90], flush=True)
    swims = bites = 0
    t0 = time.time()
    while time.time() - t0 < seconds:
        m = re.search(r'^see (c\d+) creature ', guest.call('coop.pool see 30')['out'], re.M)
        if m:
            guest.call(f'coop.pool swimto {m.group(1)}')
            swims += 1
        if swims % 3 == 0:
            r = guest.call('coop.pool kill -')['out']
            bites += r.startswith('bit')
        time.sleep(0.6)
    if part != 'none':
        guest.call('coop.pool borrow off')
    return swims, bites


def main():
    parts = arg('--parts', 'none,jaw,spike,electric,poison,hurt').split(',')
    dist = float(arg('--dist', '70'))
    host, guest = start('cell')
    ck = Checks()
    time.sleep(3)
    for con in (host, guest):
        con.call('coop.effects log on')
    # (the guest's join after a load puts a new own cell back next to the host for 30 s, BUG-0352: try again)
    apart, t0 = -1, time.time()
    while time.time() - t0 < 50:
        guest.call(f'coop.pool tp {dist:.0f} 0')
        time.sleep(4)
        hp, gp = own_pos(host), own_pos(guest)
        apart = math.hypot(hp[0] - gp[0], hp[1] - gp[1]) if hp and gp else -1
        if apart > dist * 0.7:
            time.sleep(4)
            hp, gp = own_pos(host), own_pos(guest)
            apart = math.hypot(hp[0] - gp[0], hp[1] - gp[1]) if hp and gp else -1
            if apart > dist * 0.7:
                break
    print(f'  host at {hp}, guest at {gp}: {apart:.1f} m apart', flush=True)
    ck(apart > dist * 0.7, f'the guest is far from the host ({apart:.1f} m)')
    sh = Sampler(host)
    sh.start()
    try:
        tb0 = time.time()
        time.sleep(12)  # baseline: nobody fights
        tb1 = time.time()
        base_turn, base_big, base_n = turned(sh.rows, tb0, tb1)
        base_anims = anims(sh.rows, tb0, tb1)
        print(f'  baseline: host turned {base_turn:.2f} rad (largest step {base_big:.2f}) in {base_n} samples, anims {base_anims}', flush=True)
        recent(host)  # (read once: the baseline's own effects stay in the ring with their age)
        tf0 = time.time()
        for part in parts:
            tp0 = time.time()
            swims, bites = hurt(host, guest, 20) if part == 'hurt' else fight(guest, part, 20)
            tp1 = time.time()
            t, big, n = turned(sh.rows, tp0, tp1)
            print(f'  guest fought with {part}: {swims} swims, {bites} bites | host turned {t:.2f} rad (largest step {big:.2f}) '
                  f'in {n} samples, anims {anims(sh.rows, tp0, tp1)}', flush=True)
            keyframe(f'fight_{part}', f"Cell: the guest fights creatures {dist:.0f} m away with {part}; the host's cell swims alone, "
                     "no fight effect or whirl on the host's screen", settle=0)
            gp = own_pos(guest)
            hp = own_pos(host)
            if gp and hp:
                print(f'    apart {math.hypot(hp[0] - gp[0], hp[1] - gp[1]):.1f} m', flush=True)
        tf1 = time.time()
    finally:
        sh.stop()
    played = [r for r in recent(host) if r[0] == 'p' and r[4] <= tf1 - tf0 + 2]
    sent = [r for r in recent(guest) if r[0] == 's']
    near = [r for r in played if 0 <= r[3] < 6.0]
    print(f'  guest sent {len(sent)} world effects (ids {sorted({r[2] for r in sent})})', flush=True)
    print(f'  host played {len(played)} of the guest\'s world effects; within 6 m of its own cell: {len(near)} '
          f'(ids {sorted({r[2] for r in near})}); distances {sorted(round(r[3], 1) for r in played)[:20]}', flush=True)
    for name, con in (('host', host), ('guest', guest)):
        print(f'  {name} effects: ' + ' | '.join(con.call('coop.effects')['out'].splitlines()[:1]), flush=True)
    t, big, n = turned(sh.rows, tf0, tf1)
    fight_anims = anims(sh.rows, tf0, tf1)
    rate_b = base_turn / max(1e-3, tb1 - tb0)
    rate_f = t / max(1e-3, tf1 - tf0)
    print(f'  host turn rate: baseline {rate_b:.3f} rad/s, while the guest fought {rate_f:.3f} rad/s (largest step {big:.2f})', flush=True)
    ck(not near, f"no effect of the guest's fight {dist:.0f} m away is played on the host's own cell ({len(near)} within 6 m)")
    ck(not (set(fight_anims) - set(base_anims) - {'0'}),
       f"the host's own cell plays no attack / hurt animation while the guest fights far away ({fight_anims}, baseline {base_anims})")
    ck(rate_f <= max(2 * rate_b, 0.35), f"the host's own cell does not spin while the guest fights far away ({rate_f:.3f} rad/s, baseline {rate_b:.3f})")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
