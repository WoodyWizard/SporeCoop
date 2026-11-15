#!/usr/bin/env python3
"""BUG-0932 (probe): a family animal standing at the nest stays where the host has it on a guest that comes back from
far away (Creature).

long_creature (lab 20261111-071518, 14 min in) had a family animal of the host's nest standing still (speed 0) 8.4 m
from its copy on the guest in two sync-checks 3 s apart, while the guest walked towards the nest from 80 m. Seen once.

Two games on the creature fixture, the host's creature at the nest. ROUNDS times: the guest's creature walks
(`coop.npc gopos`, its own walk) about FAR m away from the nest, waits there, and walks back. All the while (every
0.5 s) each family NPC that stands still on the host (speed 0, the host's own `coop.npc list`) is compared with the
place its copy has in the guest's game (`at=`, read from the copy itself). Checked: no standing family animal is
more than 3 m from its copy in two samples 1 s apart or more (the sync-check's rule: 2 checks 3 s apart), and no
fault.

  SPORE_LANE=4 python3 tools/tests/coop_creature_far_family.py [--rounds 4] [--far 80] [--host-off 23]
"""
import argparse
import math
import re
import time

from coop_creature_kills import family
from twoinst import Checks, start


def host_rows(con):
    """{id: (pos, speed, family?)} of the host's NPCs"""
    out = {}
    for line in con.call('coop.npc list')['out'].splitlines():
        m = re.match(r'\s+#(\d+) \S+ species=(\w+) hp=([\d.]+) .*?pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\) spd=([\d.]+)', line)
        if m and float(m.group(3)) > 0:
            out[m.group(1)] = (tuple(float(m.group(i)) for i in (4, 5, 6)), float(m.group(7)), ' family' in line)
    return out


def guest_rows(con):
    """{id: pos} of the guest's copies as its game has them"""
    out = {}
    for line in con.call('coop.npc list')['out'].splitlines():
        m = re.match(r'\s+#(\d+) species=\w+ spawned hp=([\d.]+) .* at=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', line)
        if m and float(m.group(2)) > 0:
            out[m.group(1)] = tuple(float(m.group(i)) for i in (3, 4, 5))
    return out


def avatar_pos(con):
    m = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', con.call('coop.avatar')['out'])
    return tuple(float(m.group(i)) for i in (1, 2, 3)) if m else None


def away_point(nest, far):
    """a point `far` m from the nest along the planet's surface (a tangent step, same radius)"""
    r = math.sqrt(sum(x * x for x in nest))
    up = [x / r for x in nest]
    ref = [0.0, 0.0, 1.0] if abs(up[2]) < 0.9 else [1.0, 0.0, 0.0]
    t = [ref[1] * up[2] - ref[2] * up[1], ref[2] * up[0] - ref[0] * up[2], ref[0] * up[1] - ref[1] * up[0]]
    n = math.sqrt(sum(x * x for x in t))
    p = [nest[i] + far * t[i] / n for i in range(3)]
    k = r / math.sqrt(sum(x * x for x in p))
    return [x * k for x in p]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rounds', type=int, default=4)
    ap.add_argument('--far', type=float, default=80.0)
    ap.add_argument('--host-off', type=float, default=0.0, help='the host stands this far from the nest (the report: 23 m)')
    a = ap.parse_args()
    host, guest = start('creature')
    ck = Checks()
    time.sleep(3)
    fam = family(host)
    nest = avatar_pos(host)
    print(f'  host family {fam}, nest (host) at {nest}', flush=True)
    if a.host_off:
        hp = away_point(nest, -a.host_off)
        host.call('coop.npc gopos %.2f %.2f %.2f' % tuple(hp))
        time.sleep(8)
        print(f'  the host {math.dist(avatar_pos(host) or nest, nest):.0f} m from the nest', flush=True)
    rows = host_rows(host)
    fams = [i for i, r in rows.items() if r[2]]
    ck(fams, f'family NPCs on the host ({fams})')
    worst, held, samples = 0.0, [], 0
    off_since = {}
    for rnd in range(a.rounds):
        far = away_point(nest, a.far * (0.8 + 0.1 * rnd))
        for leg, goal, secs in (('out', far, 30), ('there', None, 6), ('back', nest, 35), ('home', None, 8)):
            if goal is not None:
                guest.call('coop.npc gopos %.2f %.2f %.2f' % tuple(goal))
            t0 = time.time()
            while time.time() - t0 < secs:
                hr, gr = host_rows(host), guest_rows(guest)
                now = time.time()
                for i, (p, spd, isfam) in hr.items():
                    if not isfam or spd > 0.05 or i not in gr:
                        off_since.pop(i, None)
                        continue
                    d = math.dist(p, gr[i])
                    samples += 1
                    worst = max(worst, d)
                    if d > 3.0:
                        s = off_since.setdefault(i, now)
                        if now - s >= 1.0 and (rnd, leg, i) not in [(h[0], h[1], h[2]) for h in held]:
                            held.append((rnd, leg, i, round(d, 1), round(now - s, 1)))
                    else:
                        off_since.pop(i, None)
                if goal is not None and math.dist(avatar_pos(guest) or goal, goal) < 6:
                    break
                time.sleep(0.5)
            gp = avatar_pos(guest)
            print(f'  round {rnd} {leg}: the guest {math.dist(gp, nest) if gp else -1:.0f} m from the nest; '
                  f'worst standing family copy off so far {worst:.1f} m ({samples} samples)', flush=True)
    ck(samples > 50, f'standing family animals compared ({samples} samples)')
    ck(not held, f'no standing family animal 3 m or more from its copy for 1 s or more ({held}; worst {worst:.1f} m)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
