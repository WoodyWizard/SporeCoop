#!/usr/bin/env python3
"""BUG-0425 (M1): a guest's copy of every host body that moves stays with the host's body (one test over the stages).

Remote bodies (the host's NPC animals as proxies in Creature and Tribe, the player tribe's members as mirrors,
Civ vehicles) fell 2-7 m behind the host's bodies (BUG-0409, BUG-0440, BUG-0445, BUG-0410, BUG-0438). This test
measures what both players see: both games record every synchronized body as drawn (`coop.bodyrec`, one clock for
both games on one PC) and tools/tests/body_trail.py compares the guest's copy with the host's body AT THE SAME
MOMENT (not with the host's state the guest received, not with the host's path over a lag window).

  coop_body_trail.py --stage creature|tribe|civ [--secs S] [--seed N]

* creature: the host's game walks and runs up to 4 NPCs around the guest's avatar (`coop.npctrack walk`, their own
  locomotion), then hits two of them (`coop.npc hosthit`: they flee or fight by their own AI);
* tribe: the host orders its player tribe's members to walk (`coop.tribe order <id> 4`, the order a click gives) and
  walks animals near them;
* civ: the host's vehicles drive round their city (`coop.civ move`, a new point every second).
SPORE_NET (netem) and SPORE_FPS work as in every test (packet loss, a slow guest).

Checks, per kind over the samples where the host's body moves (body_trail.MOVING):
* the guest's copy is within LIMIT m of the host's body in 90 % of them;
* it is never more than TRAIL_M m off for longer than TRAIL_S s (a copy that falls behind and stays there).
(SPORE_FIX_OFF=drive.lead must FAIL)"""
import argparse
import json
import math
import os
import random
import sys
import time

if '--stage' in sys.argv and sys.argv[sys.argv.index('--stage') + 1] == 'tribe':
    os.environ.setdefault('TRACK_FIXTURE', 'tribe')  # (coop_npc_walk reads it at import)
import body_trail  # noqa: E402
import lane  # noqa: E402
import coop_npc_walk as nw  # noqa: E402
from coop_sync import civ_state, round_city  # noqa: E402
from fixtures import planet_of  # noqa: E402
from play_tribe import tribe_state  # noqa: E402
from twoinst import Checks, start  # noqa: E402

# per stage and kind: (p90 limit m, longest stretch over body_trail.TRAIL_M s). A body walking 2-6 m/s shown 0.2 s late
# (the 0.15-0.2 s interpolation delay plus the one-way latency, 0.7.8) is 0.4-1.2 m behind: measured without the lead
# (SPORE_FIX_OFF=drive.lead, lab 20261106-013022) p90 0.78 m (Creature NPCs), 1.17 m (Tribe animals), 2.04 m (Tribe
# members); with it 0.12, 0.37, 0.64 m (lab 20261106-012156). Civ vehicles drive 10-20 m/s.
LIMITS = {('creature', 'n'): (0.45, 1.0), ('tribe', 'n'): (0.7, 1.0), ('tribe', 't'): (1.0, 1.0), ('civ', 'v'): (2.5, 1.5)}
# over a network with latency spikes and loss bursts (SPORE_NET=player: 45 ms, spikes of +250 ms for 0.7 s every 7 s,
# 30 % loss for 1.5 s every 10 s) a walk's start is known a spike late: off (lab 20261106-014253) Tribe p90 1.73 m
# animals / 2.71 m members, stretches over 2 m of 1.75 / 3.55 s; on 0.83 / 1.28 m, 0.33 / 0.67 s (lab 20261106-014554)
NET_LIMITS = {('creature', 'n'): (0.6, 1.0), ('tribe', 'n'): (1.2, 1.0), ('tribe', 't'): (2.0, 1.0), ('civ', 'v'): (2.5, 1.5)}


def walk_npcs(host, guest, rng, secs, around):
    """the host's game walks up to 4 NPCs the guest mirrors (as coop_npc_walk.walk_phase); around(id) -> centre"""
    hs, gs, party = nw.host_npcs(host), nw.proxies(guest), nw.host_party(host)
    g = nw.avatar_pos(guest)
    lim = 120 if nw.FIXTURE == 'tribe' else 100
    both = sorted((math.dist(gs[i][0], g), i) for i in hs if i in gs and g and math.dist(gs[i][0], g) < lim and i not in party)
    ids = [i for _, i in both[:4]]
    home = {i: hs[i][0] for i in ids}
    return ids, home


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--stage', default='creature', choices=('creature', 'tribe', 'civ'))
    ap.add_argument('--secs', type=float, default=60)
    ap.add_argument('--seed', type=int, default=None)
    a = ap.parse_args()
    seed = a.seed if a.seed is not None else random.randrange(1 << 30)
    print(f'  seed {seed}', flush=True)
    rng = random.Random(seed)
    if a.stage == 'civ':
        host, guest = start('civ', planet_of('civ'))
    else:
        host, guest = start(a.stage)
    ck = Checks()
    if a.stage == 'tribe':
        print('  setup:', host.call('coop.tribe spawn 3')['out'].strip()[:120], flush=True)
    t0 = time.time()
    while a.stage != 'civ' and time.time() - t0 < 40 and not nw.proxies(guest):
        time.sleep(1)
    nw.settle((host, guest))
    time.sleep(3)
    ids, home, members, centre = [], {}, [], None
    if a.stage in ('creature', 'tribe'):
        ids, home = walk_npcs(host, guest, rng, a.secs, None)
        print(f'  walkers {ids}', flush=True)
    if a.stage == 'tribe':
        st = tribe_state(host)
        members = [i for i, v in st.items() if v['tribe'] == 'p' and v['hp'] > 0][:6]
        ps = [st[i]['pos'] for i in members]
        centre = tuple(sum(p[j] for p in ps) / len(ps) for j in range(3)) if ps else None
        print(f'  members {members}', flush=True)
    for c in (host, guest):
        body_trail.record(c, True)
    tt0 = time.time()
    nxt = {}
    hit = False
    while time.time() - tt0 < a.secs:
        now = time.time()
        g = nw.avatar_pos(guest)
        for i in ids:
            if now < nxt.get(('n', i), 0):
                continue
            sp = rng.choice([2.0, 3.0, 4.5, 6.0])
            p = nw.tangent_point(home[i], rng, 3, 10) if a.stage == 'tribe' or not g else nw.tangent_point(g, rng, 4, 12)
            host.call(f'coop.npctrack walk {i} {p[0]:.1f} {p[1]:.1f} {p[2]:.1f} {sp}')
            nxt[('n', i)] = now + rng.uniform(2, 4)
        for i in members:
            if now < nxt.get(('t', i), 0) or not centre:
                continue
            p = nw.tangent_point(centre, rng, 4, 18)
            host.call(f'coop.tribe order {i} 4 {p[0]:.1f} {p[1]:.1f} {p[2]:.1f}')
            nxt[('t', i)] = now + rng.uniform(3, 6)
        if a.stage == 'civ' and now >= nxt.get('v', 0):
            cities, veh = civ_state(host)
            for vid, p in list(veh.items())[:4]:
                if cities:
                    c = min(cities, key=lambda c: math.dist(c, p))
                    host.call('coop.civ move %d %.1f %.1f %.1f' % ((vid,) + round_city(p, c)))
            nxt['v'] = now + 1.0
        if a.stage == 'creature' and not hit and now - tt0 > a.secs * 0.7:
            for i in ids[:2]:
                print(f'  hit #{i}:', host.call(f'coop.npc hosthit {i} 0.5')['out'].strip()[:80], flush=True)
            hit = True
        time.sleep(0.2)
    for c in (host, guest):
        body_trail.record(c, False)
    hr, gr = body_trail.dump(host), body_trail.dump(guest)
    path = os.path.join(lane.out_dir('track'), f'body_trail_{a.stage}_{time.strftime("%Y%m%d-%H%M%S")}.json')
    with open(path, 'w') as f:
        json.dump({'host': hr, 'guest': gr}, f)
    print(f'  rows: host {len(hr)}, guest {len(gr)}: {path}', flush=True)
    t_first = min((r[0] for r in hr), default=0)
    res = body_trail.analyse(hr, gr, kinds={'creature': 'n', 'tribe': 'nt', 'civ': 'v'}[a.stage])
    for line in body_trail.summary(res, t_first):
        print('  ' + line, flush=True)
    for kind in {'creature': 'n', 'tribe': 'nt', 'civ': 'v'}[a.stage]:
        r = res.get(kind)
        lim, tr = (NET_LIMITS if os.environ.get('SPORE_NET') else LIMITS)[(a.stage, kind)]
        ck(r is not None and r['n'] >= 200, f'{kind}: the host\'s bodies moved where the guest has copies '
           f'({r["n"] if r else 0} moving samples >= 200)')
        if not r or not r['n']:
            continue
        p90 = body_trail.pct(r['dist'], 0.9)
        ck(p90 <= lim, f'{kind}: the guest\'s copy within {lim} m of the host\'s body in 90 % of the moving samples '
           f'(p90 {p90:.2f} m, lag p90 {body_trail.pct(r["lag"], 0.9):.3f} s)')
        ck(r['trail'][0] <= tr, f'{kind}: no copy over {body_trail.TRAIL_M:g} m off the host\'s body for more than {tr} s '
           f'(longest {r["trail"][0]:.2f} s, {r["trail"][1]})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
