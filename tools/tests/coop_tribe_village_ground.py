#!/usr/bin/env python3
"""Tribe: the ground round the village the host's live Creature -> Tribe made is the same in the guest's game (BUG-0332).

Campaign probe lines (lab 20261030-210914): after the host's live change to Tribe the host's members and the
animals near the village and their copies on every guest differed only along the planet's radius by 3.9-5.3 m:
the ground there was higher or lower in the guests' games, which load the host's save of the new stage.

1. The players' way from the `creature` fixture to Tribe (coop_tribe_snapshot.py: the evolution bar, History, the
   vote, the editors, the captions), until the guest plays Tribe together with the host.
2. On both games the game's own ground (`coop.ground`: cPlanetModel::GetHeightAt, the terrain radius) at the
   village's centre (the player tribe's members on the host) and on rings of 10, 25, 50 and 100 m round it, 8
   directions each; again 20 s later.
PASS: at every point within 50 m the two grounds differ by less than 0.5 m.

  SPORE_LANE=9 python3 tools/tests/coop_tribe_village_ground.py
"""
import math
import re
import sys
import time

from coop_tribe_gameover import player_members
from coop_tribe_gather_stall import pos_of
from coop_tribe_snapshot import HISTORY_BTN, HISTORY_OK, Player, click, out
from twoinst import Checks, start

LIMIT = 0.5  # m: the two games' ground at the same direction (the fixture: within 0.2 m)


def ground(con, p):
    m = re.search(r'ground ([-\d.]+)', out(con, f'coop.ground {p[0]:.2f} {p[1]:.2f} {p[2]:.2f}'))
    return float(m.group(1)) if m else None


def ring_points(c):
    """the centre and rings round it on the sphere through it (radius from the centre of the planet kept)"""
    r = math.dist(c, (0, 0, 0))
    up = tuple(x / r for x in c)
    a = (0.0, 0.0, 1.0) if abs(up[2]) < 0.9 else (1.0, 0.0, 0.0)
    e1 = (a[1] * up[2] - a[2] * up[1], a[2] * up[0] - a[0] * up[2], a[0] * up[1] - a[1] * up[0])
    l = math.dist(e1, (0, 0, 0))
    e1 = tuple(x / l for x in e1)
    e2 = (up[1] * e1[2] - up[2] * e1[1], up[2] * e1[0] - up[0] * e1[2], up[0] * e1[1] - up[1] * e1[0])
    pts = [(0, c)]
    for d in (10, 25, 50, 100):
        for k in range(8):
            t = k * math.pi / 4
            q = tuple(c[i] + d * (math.cos(t) * e1[i] + math.sin(t) * e2[i]) for i in range(3))
            s = r / math.dist(q, (0, 0, 0))
            pts.append((d, tuple(x * s for x in q)))
    return pts


def villages(con):
    """[(label, (x y z))] of every tribe's village on this game (`coop.tribe`: `tribe N (player) at (x y z)`)"""
    res = []
    for m in re.finditer(r'^tribe (\d+)( \(player\))? at \(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', out(con, 'coop.tribe'), re.M):
        c = tuple(float(m.group(i)) for i in (3, 4, 5))
        if math.dist(c, (0, 0, 0)) > 50:  # a tribe without a place yet reports (0 0 0)
            res.append((f'tribe {m.group(1)}' + (' (player)' if m.group(2) else ''), c))
    return res


def cap_points(c, cap_deg=45.0, step_deg=2.0):
    """a grid on the sphere through c round c's direction (polar angle up to cap_deg)"""
    r = math.dist(c, (0, 0, 0))
    up = tuple(x / r for x in c)
    a = (0.0, 0.0, 1.0) if abs(up[2]) < 0.9 else (1.0, 0.0, 0.0)
    e1 = (a[1] * up[2] - a[2] * up[1], a[2] * up[0] - a[0] * up[2], a[0] * up[1] - a[1] * up[0])
    l = math.dist(e1, (0, 0, 0))
    e1 = tuple(x / l for x in e1)
    e2 = (up[1] * e1[2] - up[2] * e1[1], up[2] * e1[0] - up[0] * e1[2], up[0] * e1[1] - up[1] * e1[0])
    pts = [tuple(x * r for x in up)]
    th = step_deg
    while th <= cap_deg:
        t = math.radians(th)
        n = max(6, int(2 * math.pi * math.sin(t) / math.radians(step_deg)))
        for k in range(n):
            ph = 2 * math.pi * k / n
            d = tuple(math.cos(t) * up[i] + math.sin(t) * (math.cos(ph) * e1[i] + math.sin(ph) * e2[i]) for i in range(3))
            pts.append(tuple(x * r for x in d))
        th += step_deg
    return pts


def ground_map(con, pts):
    return [ground(con, p) for p in pts]


def map_report(label, pts, hm, gm, lim=0.3):
    """the points where the two games' grounds differ, grouped into patches (within 40 m of each other)"""
    bad = [(p, h, g) for p, h, g in zip(pts, hm, gm) if h is not None and g is not None and abs(h - g) > lim]
    patches = []
    for p, h, g in bad:
        for pa in patches:
            if math.dist(pa[0][0], p) < 40:
                pa.append((p, h, g))
                break
        else:
            patches.append([(p, h, g)])
    print(f'  map {label}: {len(pts)} points, {len(bad)} differ by more than {lim} m, {len(patches)} patches', flush=True)
    for pa in patches[:20]:
        w = max(pa, key=lambda x: abs(x[1] - x[2]))
        print(f'    patch of {len(pa)} at ({w[0][0]:.0f} {w[0][1]:.0f} {w[0][2]:.0f}): host {w[1]:.2f} guest {w[2]:.2f} '
              f'(guest - host {w[2] - w[1]:+.2f})', flush=True)
    return bad


def compare(host, guest, pts):
    worst = {}
    for d, p in pts:
        h, g = ground(host, p), ground(guest, p)
        if h is None or g is None:
            continue
        worst[d] = max(worst.get(d, 0.0), abs(h - g))
    return worst


def main():
    fx = next((a.split('=', 1)[1] for a in sys.argv[1:] if a.startswith('--fixture=')), 'creature')
    host, guest = start(fx)
    ck = Checks()
    players = [Player(host, 'host'), Player(guest, 'guest')]
    t0 = time.time()
    do_map = '--map' in sys.argv  # BUG-0332: the whole region's ground on both games, in Creature and in Tribe
    cmap = None
    if do_map:
        a0 = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', out(host, 'coop.status') + out(host, 'coop.avatar'))
        c0 = tuple(float(a0.group(i)) for i in (1, 2, 3)) if a0 else None
        if c0:
            mpts = cap_points(c0)
            cmap = (mpts, ground_map(host, mpts), ground_map(guest, mpts))
            map_report('Creature (before the change)', *cmap)
    host.call('coop.dna add 3000')
    last_kick = time.time()
    tribe_at = None
    while time.time() - t0 < 600:
        h_mode = out(host, 'game.mode')
        if 'Tribe' in h_mode:
            tribe_at = time.time()
            break
        busy = False
        for p in players:
            did = p.service(host)
            if did:
                print(f'   {time.time() - t0:5.0f}s {p.name}: {did}', flush=True)
                busy = True
        busy |= 'movie=1' in out(host, 'coop.status') or out(host, 'coop.vote').startswith('vote active')
        if not busy and 'Creature' in h_mode and time.time() - last_kick > 25:
            last_kick = time.time()
            click(host, HISTORY_BTN)
            time.sleep(3)
            click(host, HISTORY_OK)
            print(f'   {time.time() - t0:5.0f}s host: History and its check', flush=True)
        time.sleep(2)
    if not ck(tribe_at, f'the host entered the Tribe stage ({time.time() - t0:.0f} s)'):
        return ck.result()
    t1, good = time.time(), None
    while time.time() - t1 < 300:
        for p in players:
            p.service(host)
        tog = re.search(r'^together 1 1$', out(host, 'coop.status'), re.M) is not None
        good = (good or time.time()) if tog and 'movie=1' not in out(host, 'coop.status') else None
        if good and time.time() - good > 10:
            break
        time.sleep(3)
    if not ck(good, 'the guest plays the Tribe stage together with the host'):
        return ck.result()
    own, _ = player_members(host)
    ps = [p for p in (pos_of(host, i) for i in sorted(own)) if p]
    ck(bool(ps), f'the host has the player tribe\'s members ({len(ps)})')
    if not ps:
        return ck.result()
    c = tuple(sum(p[i] for p in ps) / len(ps) for i in range(3))
    print(f'  the village (the members\' centre on the host): ({c[0]:.1f} {c[1]:.1f} {c[2]:.1f})', flush=True)
    # BUG-0332: every tribe's village, the NPC tribes' too (in the campaigns the guests' ground was 5-6 m higher at
    # NPC tribe members, not at the player's village)
    # --npc-at=x,y,z;x,y,z: the host's game makes an NPC tribe of 2 there (SpawnNpcTribe, as its controller does
    # during play); the guest makes its copy of it (tribe.npc_tribes) (BUG-0332: the campaigns' new villages)
    for spec in next((a.split('=', 1)[1] for a in sys.argv[1:] if a.startswith('--npc-at=')), '').split(';'):
        if spec.strip():
            q = tuple(float(x) for x in spec.split(','))
            print(f'  host ground at ({spec}) before: {ground(host, q)}; ' + out(host, 'coop.tribe npc_tribe_at 2 %.2f %.2f %.2f' % q).strip(), flush=True)
            time.sleep(8)
            print(f'  guest ground there: {ground(guest, q)}', flush=True)
    places = [('the player tribe\'s members', c)] + villages(host)
    # and the NPC tribe spawn markers: where the host's controller forms the next villages during play (BUG-0332)
    places += [(f'marker {k}', tuple(float(m.group(i)) for i in (1, 2, 3))) for k, m in enumerate(re.finditer(
        r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', out(host, 'coop.tribe markers')))]
    places = [(n, q) for n, q in places if math.dist(q, (0, 0, 0)) > 50]
    if cmap:
        mpts = cmap[0]
        hm, gm = ground_map(host, mpts), ground_map(guest, mpts)
        bad = map_report('Tribe', mpts, hm, gm)
        moved_h = sum(1 for a, b in zip(cmap[1], hm) if a is not None and b is not None and abs(a - b) > 0.3)
        moved_g = sum(1 for a, b in zip(cmap[2], gm) if a is not None and b is not None and abs(a - b) > 0.3)
        print(f'  Creature -> Tribe: the host\'s ground changed at {moved_h} points, the guest\'s at {moved_g}', flush=True)
        for p, h, g in bad[:40]:
            i = mpts.index(p)
            print(f'    ({p[0]:.0f} {p[1]:.0f} {p[2]:.0f}): Creature host {cmap[1][i]} guest {cmap[2][i]}, Tribe host {h} guest {g}',
                  flush=True)
    if '--terrain' in sys.argv:  # the terrain's stamps on both games (terrain_diag.cpp)
        for name, con in (('host', host), ('guest', guest)):
            print(f'  {name} coop.terrain:\n' + out(con, 'coop.terrain')[:6000], flush=True)
    result = {}
    for k in range(2):
        for name, at in places:
            w = compare(host, guest, ring_points(at))
            print(f'  sample {k + 1}, {name} at ({at[0]:.0f} {at[1]:.0f} {at[2]:.0f}): the largest |host - guest| ground '
                  'per ring: ' + ', '.join(f'{d} m: {v:.2f}' for d, v in sorted(w.items())), flush=True)
            for d, v in w.items():
                if d <= 50:
                    result[name] = max(result.get(name, 0.0), v)
        time.sleep(20)
    worst = max(result.items(), key=lambda kv: kv[1], default=(None, None))
    ck(worst[1] is not None and worst[1] < LIMIT, f'the guest\'s ground round every village ({len(places)}) is the '
       f'host\'s (largest difference within 50 m {worst[1] if worst[1] is None else round(worst[1], 2)} m at '
       f'{worst[0]}, < {LIMIT})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
