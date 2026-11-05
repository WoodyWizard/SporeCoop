#!/usr/bin/env python3
"""BUG-0592 / BUG-0504: a player's own standing creature pushed by the partner's creature is never left in the ground.

The long-session bot's ground gate (both own creatures 0.5-0.7 m under the ground for ~20 s) came right after the
guest's world load: the guest's avatar comes back where the host's save had the host's creature, the two overlap,
the game separates them sideways and the host's standing creature, moved 0.7 m along a slope at the same distance
from the planet's centre, stood 0.69 m under the terrain until it walked (lab 20261105-015350).

1. Two games on the creature fixture, team off. Both go to the report's spot (--at); the steepest point within 6 m
   of it (GetHeightAt sampled 1 m around); the host's creature stands there on its ground.
2. A: the guest's creature is put 0.3 m downhill of it (overlapping it), so the game pushes the host's creature uphill;
3. for SECS s, every 0.25 s, each game's own creature's height over its terrain (`coop.abilities`: above=).
   A: observed and judged (the game does not always push far enough to sink it).
4. B: on each game in turn its standing creature is put where the report's push left it: 0.7 m uphill, 0.69 m under
   the ground there (`coop.abilities put`, a teleport; 0.3 m under, or moved by SetPosition, the game grounds it
   again by itself, lab 20261105-113116 / 113250), 4 s of samples.
Checks: B's push put it in the ground at first (a test without that proves nothing); after the first second neither
own creature is more than UNDER m under its ground on 2 samples in a row (the monitor's rule is 0.5 m on 2 trace
lines: this is stricter). SPORE_FIX_OFF=creature.own_ground must FAIL.

  SPORE_LANE=4 python3 tools/tests/coop_own_ground.py [--secs 8]
"""
import argparse
import math
import re
import time

from twoinst import Checks, start

UNDER = 0.25  # m


def avatar(con):
    m = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', con.call('coop.avatar')['out'])
    return tuple(float(m.group(i)) for i in (1, 2, 3)) if m else None


def above(con):
    m = re.search(r'above=([-\d.]+)', con.call('coop.abilities')['out'])
    return float(m.group(1)) if m else None


def ground(con, p):
    m = re.search(r'ground ([-\d.]+)', con.call('coop.abilities ground %.3f %.3f %.3f' % p)['out'])
    return float(m.group(1)) if m else 0.0


def basis(p):
    n = [v / math.sqrt(sum(x * x for x in p)) for v in p]
    ax = (0, 0, 1) if abs(n[2]) < 0.9 else (1, 0, 0)
    u = (n[1] * ax[2] - n[2] * ax[1], n[2] * ax[0] - n[0] * ax[2], n[0] * ax[1] - n[1] * ax[0])
    ul = math.sqrt(sum(x * x for x in u))
    u = [x / ul for x in u]
    v = (n[1] * u[2] - n[2] * u[1], n[2] * u[0] - n[0] * u[2], n[0] * u[1] - n[1] * u[0])
    return u, v


def at(p, u, v, a, d):
    return tuple(p[k] + d * (math.cos(a) * u[k] + math.sin(a) * v[k]) for k in range(3))


def steepest(con, p0):
    """(point, uphill angle, rise over 1 m) of the steepest spot near p0"""
    u, v = basis(p0)
    best = (p0, 0.0, -1.0)
    for d in (0, 3, 6, 9, 12):
        for ai in range(8 if d else 1):
            c = at(p0, u, v, ai * math.pi / 4, d)
            g0 = ground(con, c)
            for bi in range(12):
                b = bi * math.pi / 6
                rise = ground(con, at(c, u, v, b, 1.0)) - g0
                if rise > best[2]:
                    best = (c, b, rise)
    return best


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--secs', type=float, default=8)
    # the spot of the report (long_creature 20261105-015350: agl -0.69 after a 0.7 m push), on the fixture's planet
    ap.add_argument('--at', default='-219.8,-454.1,-48.6')
    ap.add_argument('--radial', type=float, default=0)  # experiments: B this deep under the ground (m; 0: 0.69)
    a = ap.parse_args()
    host, guest = start('creature')
    ck = Checks()
    for c in (host, guest):
        c.call('coop.team off')
    p0 = tuple(float(x) for x in a.at.split(','))
    ck(avatar(host) is not None, 'the host\'s creature')
    host.call('coop.abilities goto %.3f %.3f %.3f' % p0)
    guest.call('coop.npc gopos %.3f %.3f %.3f' % at(p0, *basis(p0), 0.0, 6.0))
    time.sleep(4)
    spot, up, rise = steepest(host, p0)
    print(f'  steepest spot {tuple(round(x, 1) for x in spot)}: {rise:.2f} m rise over 1 m', flush=True)
    ck(rise >= 0.3, f'a slope near the spot ({rise:.2f} m over 1 m)')

    def watch(secs):
        rows, t0 = [], time.time()
        while time.time() - t0 < secs:
            rows.append((time.time() - t0, above(host), above(guest), avatar(host)))
            time.sleep(0.25)
        print('  ' + ' '.join(f'{r[0]:.1f}:{r[1]:.2f}/{r[2]:.2f}' for r in rows if r[1] is not None and r[2] is not None),
              flush=True)
        return rows

    def judge(rows, what, k, who):
        late = [r[k] for r in rows if r[0] > 1.0 and r[k] is not None]
        runs = sum(1 for x, y in zip(late, late[1:]) if x < -UNDER and y < -UNDER)
        ck(runs == 0, f'{what}: the {who}\'s own creature never {UNDER} m under its ground on 2 samples in a row '
                      f'(min {min(late or [0]):.2f} m, {runs} pairs)')

    # A: the partner's creature put onto the host's (the report's arrival), observed and judged
    print('  host: ' + host.call('coop.abilities goto %.3f %.3f %.3f' % spot)['out'].strip(), flush=True)
    time.sleep(3)
    hp = avatar(host)
    h0 = above(host)
    ck(h0 is not None and abs(h0) < 0.2, f'the host\'s creature stands on its ground ({h0})')
    u, v = basis(hp)
    print('  guest: ' + guest.call('coop.npc gopos %.3f %.3f %.3f' % at(hp, u, v, up + math.pi, 0.3))['out'].strip(),
          flush=True)
    rows = watch(a.secs)
    moved = max((math.dist(r[3], hp) for r in rows if r[3]), default=0)
    print(f'  A: the partner on the host\'s creature: host moved up to {moved:.2f} m', flush=True)
    judge(rows, 'A (overlap)', 1, 'host')
    judge(rows, 'A (overlap)', 2, 'guest')
    # B: the push as the game's separation did it in the report: the standing creature 0.7 m uphill at the same
    # distance from the planet's centre (SetPosition), on each game in turn
    guest.call('coop.npc gopos %.3f %.3f %.3f' % at(spot, u, v, up + math.pi / 2, 8.0))
    for con, k, who in ((host, 1, 'host'), (guest, 2, 'guest')):
        con.call('coop.abilities goto %.3f %.3f %.3f' % spot)
        time.sleep(2)
        p = avatar(con)
        q = at(p, u, v, up, 0.7)
        r0, rq = math.sqrt(sum(x * x for x in p)), math.sqrt(sum(x * x for x in q))
        q = tuple(x * r0 / rq for x in q)
        gq = ground(con, q)
        # the report's end state: 0.7 m to the side, 0.69 m under the ground there (lab 20261105-015350). Less deep
        # (0.32 m) the game put the standing creature back onto its ground; 0.6 m under it stayed (lab 20261105-113250
        # and the --radial 0.6 experiment)
        depth = a.radial or 0.69
        q = tuple(x * (gq - depth) / r0 for x in q)
        print(f'  B {who}: ' + con.call('coop.abilities put %.3f %.3f %.3f' % q)['out'].strip(), flush=True)
        rows = watch(4)
        first = next((r[k] for r in rows if r[k] is not None), 0)
        ck(first < -UNDER, f'B: the push put the {who}\'s creature in the ground at first ({first:.2f} m)')
        judge(rows, 'B (push)', k, who)
        con.call('coop.npc gopos %.3f %.3f %.3f' % at(spot, u, v, up - math.pi / 2, 8.0))
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
