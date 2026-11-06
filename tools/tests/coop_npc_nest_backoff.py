#!/usr/bin/env python3
"""BUG-0562: an NPC creature backing away from the guest gets stuck inside its nest (player report, video).

The host's NPCs that live at a nest stand next to its mound; the guest comes to them and roars (the game's roar:
they back away, behaviour node 0x02D852E0, docs/re/creature.md#creature-roar-relay), from the side away from the
mound, so they back away onto / into it. The host's own player stays far from that nest (the case of the report:
the guest plays at a nest the host's camera is not at).

Both games are sampled for 14 s, every NPC of that nest within 25 m of the guest: on the host the NPC's height over
its game's surface (`coop.ground`: cPlanetModel_SurfacePoint, the terrain or the mound where the collision is loaded),
on the guest the proxy's drawn height over the guest's surface and how far it is from the host's NPC.

Checks (the game's own state on both machines):
  * the NPCs backed away (they moved: the roar worked; else nothing is tested)
  * on the guest no proxy of them is drawn more than SUNK m inside its surface (the mound) for more than 1 s
  * on the guest every proxy keeps within FOLLOW m of the host's NPC (not stuck at the mound)

  coop_npc_nest_backoff.py [--host-near]   (--host-near: the host's player stays at the nest too, the control)
"""
import argparse
import math
import re
import time

import worlddiff
from coop_creature_roar import npcs, puppet_pos
from twoinst import Checks, start

NEST_NOUN = 0x52AA6122
SUNK = 0.30     # m inside the surface: drawn in the mound (a standing creature is 0-0.05 m in it)
FOLLOW = 4.0    # m from the host's NPC (a proxy follows 0.3-1.5 m behind, npc_walk)
SECONDS = 14


def over_surface(con, p):
    m = re.search(r'above ([-\d.]+) surface [\d.]+ over_surface ([-\d.]+)', con.call('coop.ground %.2f %.2f %.2f' % p)['out'])
    return (float(m.group(1)), float(m.group(2))) if m else (None, None)


def proxies(guest):
    out = {}
    for line in guest.call('coop.npc list')['out'].splitlines():
        m = re.match(r'\s*#(\d+) species=\S+ spawned .*?want=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\) at=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', line)
        if m:
            out[m.group(1)] = dict(want=tuple(map(float, m.groups()[1:4])), at=tuple(map(float, m.groups()[4:7])))
    return out


def away(p, frm, d):
    v = [p[i] - frm[i] for i in range(3)]
    n = math.sqrt(sum(x * x for x in v)) or 1.0
    return tuple(p[i] + v[i] / n * d for i in range(3))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--host-near', action='store_true')
    a = ap.parse_args()
    host, guest = start('creature')
    ck = Checks()
    for c in (host, guest):
        c.call('coop.team off')
    gsp = re.search(r'key=0x([0-9A-F]+)', guest.call('coop.avatar')['out'])
    gsp = gsp.group(1).zfill(8) if gsp else ''
    hw = worlddiff.dump(host, 3000)
    nests = [x['pos'] for x in hw.rows if x['kind'] == 'g' and x['noun'] == NEST_NOUN]
    mirrored = set()
    for _ in range(10):
        mirrored = set(re.findall(r'#(\d+) .*spawned', guest.call('coop.npc list')['out']))
        if mirrored:
            break
        time.sleep(1)
    wild = [n for n in npcs(host) if n['species'] != gsp and not n['family'] and n['id'] in mirrored]
    # the nest with the most wild NPCs within 12 m of it
    best = max(nests, key=lambda q: sum(math.dist(n['pos'], q) < 12 for n in wild), default=None)
    at_nest = [n for n in wild if best and math.dist(n['pos'], best) < 12]
    ck(len(at_nest) >= 1, f'wild NPCs at a nest of the fixture ({len(at_nest)} at {worlddiff._fmt(best) if best else "-"}, '
                          f'{len(nests)} nests)')
    if not at_nest:
        return ck.result()
    # the guest 5 m beyond the NPCs from the mound: backing away from it takes them onto the mound
    mid = tuple(sum(n['pos'][i] for n in at_nest) / len(at_nest) for i in range(3))
    gp = away(mid, best, 5.0)
    if not a.host_near:  # the host's player (and camera) far from that nest
        host.call('coop.npc gopos %.2f %.2f %.2f' % away(best, mid, 120.0))
    print('  ' + guest.call('coop.npc gopos %.2f %.2f %.2f' % gp)['out'].strip(), flush=True)
    time.sleep(4)
    ids = [n['id'] for n in at_nest]
    print(f'  nest at {worlddiff._fmt(best)}; its NPCs {ids}; the host player {"near" if a.host_near else "120 m away"}',
          flush=True)
    print('  ' + guest.call('coop.rules roar 31')['out'].strip(), flush=True)
    t0 = time.time()
    rows = []   # (t, id, host over_surface, guest over_surface, guest-host distance, host dist to nest)
    start_pos = {n['id']: n['pos'] for n in at_nest}
    while time.time() - t0 < SECONDS:
        hn = {n['id']: n for n in npcs(host)}
        gp_ = proxies(guest)
        for i in ids:
            if i not in hn or i not in gp_:
                continue
            hp_ = hn[i]['pos']
            _, hs = over_surface(host, hp_)
            _, gs = over_surface(guest, gp_[i]['at'])
            rows.append((time.time() - t0, i, hs, gs, math.dist(hp_, gp_[i]['at']), math.dist(hp_, best)))
        time.sleep(0.25)
    moved = {i: math.dist(start_pos[i], npcs_pos) for i, npcs_pos in
             ((n['id'], n['pos']) for n in npcs(host)) if i in start_pos}
    for i in ids:
        r = [x for x in rows if x[1] == i]
        if not r:
            continue
        print(f'  #{i}: moved {moved.get(i, -1):.1f} m; host over surface min {min((x[2] for x in r if x[2] is not None), default=0):.2f}; '
              f'guest over surface min {min((x[3] for x in r if x[3] is not None), default=0):.2f}; '
              f'guest from host max {max(x[4] for x in r):.1f} m; nearest the nest {min(x[5] for x in r):.1f} m', flush=True)
        for x in r[::6]:
            print(f'      {x[0]:5.1f} s host {x[2]} guest {x[3]} apart {x[4]:.2f} nest {x[5]:.1f}', flush=True)
    dt = SECONDS / max(1, len(rows) / max(1, len(ids)))
    sunk = {}
    for i in ids:
        r = [x for x in rows if x[1] == i]
        sunk[i] = sum(1 for x in r if x[3] is not None and x[3] < -SUNK) * dt
    far = {i: max((x[4] for x in rows if x[1] == i), default=0) for i in ids}
    ck(any(v > 1.5 for v in moved.values()), f'the NPCs backed away from the guest ({ {i: round(v, 1) for i, v in moved.items()} } m)')
    ck(all(v <= 1.0 for v in sunk.values()), f'on the guest no proxy is drawn more than {SUNK} m inside the surface for over 1 s '
                                              f'({ {i: round(v, 1) for i, v in sunk.items()} } s)')
    ck(all(v <= FOLLOW for v in far.values()), f'on the guest every proxy keeps within {FOLLOW} m of the host\'s NPC '
                                                f'({ {i: round(v, 1) for i, v in far.items()} } m)')
    for c in (host, guest):
        c.call('coop.team on')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
