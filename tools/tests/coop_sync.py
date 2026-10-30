#!/usr/bin/env python3
"""How exactly the guest shows what the host has, in every stage: position and facing of every
synchronized object (players, NPC creatures/cells, citizens, vehicles, ships), host vs guest.

For each stage: start host + guest, wait until neither game is paused (the GOG 2024 guest keeps
its game time paused for ~7 s after its world load, docs/06-testing.md), let things move (the host
swims/walks, vehicles drive, ships fly), then take rounds of host / host / guest / host samples
(coop.sync) and compare by key. The guest shows remote objects on purpose a little late (players:
0.10 s interpolation delay + 20 Hz snapshots), so a guest pose counts as right when the host had it
within LAG_WINDOW before the guest was read: its position is compared with the host's path over
that window, its facing with the host's turn over it. A desync (wrong pose, a stuck or drifting
object) is far from that whole path. Prints per kind: count, median / 90% / max position error (m)
and facing error (deg), and the 90% without the window ("raw", the old measure, lag included).

usage: coop_sync.py [--fps N] [stage ...]   (default: cell creature tribe civ space)

--fps N: the guest runs at ~N frames a second (frame.limit 1000/N), a weak PC or four games on
one PC (BUG-0175: drive::step's pull towards the host's pose was per frame, so a mirror that
cannot walk trailed 4x as far at 15 fps as at 60; the fix's FAIL/PASS is npc_tests: in game, with
the game time running, the 0.15 s lag window hides the difference, see the ticket).
"""
import math
import os
import re
import sys
import time

from fixtures import planet_of
from twoinst import Checks, shot, start

FIXTURES = {
    'cell': ('cell', planet_of('cell')),
    'creature': ('creature', None),
    'tribe': ('tribe', planet_of('tribe')),
    'civ': ('civ', planet_of('civ')),
    'space': ('space', planet_of('space')),
}
KINDS = {'p': 'players', 'n': 'NPC creatures', 'c': 'NPC cells', 't': 'citizens', 'v': 'vehicles', 's': 'ships'}
# the host pose the guest is compared with may be up to this old (s): interpolation delay 0.10 s
# plus one 20 Hz snapshot interval (net/interp.h, coop.cpp send_state)
LAG_WINDOW = 0.15
# what counts as "in sync": position (m, relative to the kind) and facing (deg)
LIMITS = {'p': (1.5, 15), 'n': (1.5, 25), 'c': (0.6, 20), 't': (1.5, 30), 'v': (3.0, 20), 's': (10.0, 20)}  # ships fly ~100 m/s


def sample(con):
    d = {}
    for line in con.call('coop.sync')['out'].splitlines():
        f = line.split()
        if len(f) == 8:
            try:
                d[f[0]] = (tuple(map(float, f[1:4])), tuple(map(float, f[4:8])))
            except ValueError:
                pass
    return d


def seg_dist(p, a, b):
    """distance from p to the segment a-b"""
    ab = [y - x for x, y in zip(a, b)]
    ll = sum(c * c for c in ab)
    t = 0 if ll == 0 else max(0.0, min(1.0, sum((pc - ac) * c for pc, ac, c in zip(p, a, ab)) / ll))
    return math.dist(p, [ac + t * c for ac, c in zip(a, ab)])


def nlerp(a, b, t):
    if sum(x * y for x, y in zip(a, b)) < 0:
        b = [-x for x in b]
    q = [x + (y - x) * t for x, y in zip(a, b)]
    n = math.sqrt(sum(x * x for x in q)) or 1
    return [x / n for x in q]


def arc_angle(q, a, b, steps=8):
    """facing error of q against the host's turn from a to b"""
    return min(angle(q, nlerp(a, b, i / steps)) for i in range(steps + 1))


def game_paused(con):
    """the game time is paused (cGameTimeManager +0x48 bit 0, what the game's own checks test)"""
    m = re.search(r'eax=0x([0-9A-Fa-f]+)', con.call('call Simulator::cGameTimeManager::Get cdecl').get('out', ''))
    if not m or int(m.group(1), 16) == 0:
        return None
    m = re.search(r'^\S+\s+([0-9A-Fa-f]{2})', con.call('mem.read 0x%X 1' % (int(m.group(1), 16) + 0x48)).get('out', ''))
    return bool(int(m.group(1), 16) & 1) if m else None


def wait_running(host, guest, timeout=40):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if not game_paused(host) and not game_paused(guest):
            return time.time() - t0
        time.sleep(0.5)
    return None


def angle(q1, q2):
    dot = abs(sum(a * b for a, b in zip(q1, q2)))
    n = math.sqrt(sum(a * a for a in q1)) * math.sqrt(sum(b * b for b in q2)) or 1
    return math.degrees(2 * math.acos(min(1.0, dot / n)))


def pct(v, p):
    if not v:
        return float('nan')
    v = sorted(v)
    return v[min(len(v) - 1, int(len(v) * p))]


def civ_state(con):
    cities, veh = [], {}
    for line in con.call('coop.civ')['out'].splitlines():
        m = re.match(r'city .* pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', line)
        if m:
            cities.append(tuple(float(m.group(i)) for i in (1, 2, 3)))
        m = re.match(r'\s+veh #(\d+) .* pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', line)
        if m and m.group(1) != '0':
            veh[int(m.group(1))] = tuple(float(m.group(i)) for i in (2, 3, 4))
    return cities, veh


def round_city(p, c, step=35.0, radius=45.0):
    """the next point of a drive round city c (planet centre at the origin): the vehicle's offset
    from the city turned by `step` degrees about the city's up axis, at `radius` m. With a new order
    every sample (--fps) the old away / back targets reversed the vehicle every sample: the host's
    vehicle flipped 180 deg within 0.15 s each time and the facing check compared a mid-turn guest
    pose with a host turn whose direction it cannot know (BUG-0326)"""
    n = math.sqrt(sum(x * x for x in c)) or 1
    up = [x / n for x in c]
    r = [pp - cc for pp, cc in zip(p, c)]
    r = [x - sum(a * b for a, b in zip(r, up)) * u for x, u in zip(r, up)]  # in the city's ground plane
    rl = math.sqrt(sum(x * x for x in r)) or 1
    r = [x / rl * radius for x in r]
    t = math.radians(step)
    cr = [up[1] * r[2] - up[2] * r[1], up[2] * r[0] - up[0] * r[2], up[0] * r[1] - up[1] * r[0]]
    return tuple(cc + x * math.cos(t) + y * math.sin(t) for cc, x, y in zip(c, r, cr))


def stir(stage, host, guest, i):
    """keep things moving between rounds"""
    if stage == 'space' and i == 0:
        for _ in range(3):
            host.call('coop.fleet spawn 2')
    if stage == 'civ':  # drive the vehicles 40 m away from / back towards the nearest city
        cities, veh = civ_state(host)
        for vid, p in list(veh.items())[:4]:
            if not cities:
                break
            c = min(cities, key=lambda c: math.dist(c, p))
            if FPS:  # BUG-0326: a new order every sample: drive round the city (no 180 deg flips)
                tgt = round_city(p, c)
            else:
                d = math.dist(p, c) or 1
                k = (d + (40 if i % 2 == 0 else -25)) / d
                tgt = tuple(cc + (pp - cc) * k for pp, cc in zip(p, c))
            host.call('coop.civ move %d %.1f %.1f %.1f' % ((vid,) + tgt))
    if stage == 'cell':
        host.call('coop.pool goto %d %d' % ([20, 0, -20, 0][i % 4], [0, 20, 0, -20][i % 4]))


def measure(stage):
    fx, planet = FIXTURES[stage]
    host, guest = start(fx, planet)
    if FPS:
        print('  guest:', guest.call(f'frame.limit {round(1000 / FPS)}')['out'].strip(), flush=True)
    w = wait_running(host, guest)
    print(f'  (game time running {w:.1f} s after the start)' if w is not None
          else '  (a game stayed paused: measuring anyway)', flush=True)
    time.sleep(5)
    pos, rot, raw_pos, raw_rot, keys = {}, {}, {}, {}, {}
    for i in range(16):
        if i % 4 == 0 or (FPS and stage == 'civ'):  # --fps: the vehicles drive all the time (BUG-0175)
            stir(stage, host, guest, i if FPS and stage == 'civ' else i // 4)
        h0 = sample(host)
        time.sleep(LAG_WINDOW - 0.03)  # a console call takes ~0.03 s: h0 is ~LAG_WINDOW older than g
        h1, g, h2 = sample(host), sample(guest), sample(host)
        for k, (gp, gq) in g.items():
            if k not in h1 or k not in h2:
                continue
            kind = k[0]
            e1, e2 = math.dist(gp, h1[k][0]), math.dist(gp, h2[k][0])
            hq = h1[k][1] if e1 <= e2 else h2[k][1]
            # the guest was read between the two host reads: compare with the host's path between
            # them (fast ships move metres between two console calls)
            raw = seg_dist(gp, h1[k][0], h2[k][0])
            e, a = raw, arc_angle(gq, h1[k][1], h2[k][1])
            if k in h0:  # and with where the host was over the lag window before
                e = min(e, seg_dist(gp, h0[k][0], h1[k][0]))
                a = min(a, arc_angle(gq, h0[k][1], h1[k][1]))
            pos.setdefault(kind, []).append(e)
            raw_pos.setdefault(kind, []).append(raw)
            if e > 50:  # print the big ones: which object, when, where on both sides
                print(f'    big: {k} {e:.0f} m at sample {i}: host {h1[k][0]} / {h2[k][0]}, guest {gp}', flush=True)
            rot.setdefault(kind, []).append(a)
            if os.environ.get('SYNC_VERBOSE') and kind == 'v':  # BUG-0326: how far the host turned meanwhile
                turn = angle(h0[k][1], h2[k][1]) if k in h0 else -1
                print(f'    v {k} sample {i}: facing err {a:5.1f} deg, host turned {turn:5.1f} deg over the window, '
                      f'pos err {e:.2f} m', flush=True)
            raw_rot.setdefault(kind, []).append(angle(gq, hq))
            keys.setdefault(kind, set()).add(k)
        time.sleep(0.4)
    shot(host, f'sync_{stage}_host.png')
    shot(guest, f'sync_{stage}_guest.png')
    return pos, rot, raw_pos, raw_rot, keys, host, guest


FPS = 0


def main():
    global FPS
    args = sys.argv[1:]
    if args[:1] == ['--fps']:
        FPS = float(args[1])
        args = args[2:]
    stages = args or ['cell', 'creature', 'tribe', 'civ', 'space']
    ck = Checks()
    for st in stages:
        print(f'== {st}', flush=True)
        pos, rot, raw_pos, raw_rot, keys, host, guest = measure(st)
        if not pos:
            print('  nothing in common: host', list(sample(host))[:6], 'guest', list(sample(guest))[:6])
        for kind in sorted(pos):
            p, r = pos[kind], rot[kind]
            lp, lr = LIMITS.get(kind, (2, 20))
            print(f'  {KINDS.get(kind, kind):14} n={len(keys[kind]):3}  pos med {pct(p, .5):5.2f} p90 {pct(p, .9):5.2f} '
                  f'max {max(p):6.2f} m | facing med {pct(r, .5):5.1f} p90 {pct(r, .9):5.1f} max {max(r):5.1f} deg'
                  f' | raw p90 {pct(raw_pos[kind], .9):.2f} m {pct(raw_rot[kind], .9):.1f} deg', flush=True)
            ck(pct(p, .9) <= lp, f'{st}/{KINDS.get(kind, kind)}: 90% within {lp} m')
            ck(pct(r, .9) <= lr, f'{st}/{KINDS.get(kind, kind)}: 90% facing within {lr} deg')
        if FPS:
            guest.call('frame.limit auto')
        ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
