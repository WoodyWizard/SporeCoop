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
* tribe: no member steps over 5 m in one frame on either game (BUG-0610: the arrival's chief placing teleported a
  walking member; SPORE_FIX_OFF=tribe.place_still must FAIL).
(SPORE_FIX_OFF=drive.lead must FAIL)"""
import argparse
import collections
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
# Both Tribe games at 18-20 fps (a loaded lab, load 10-12 on 6 cores): with the lead p90 0.68-0.77 m animals, 1.11-1.21 m
# members (labs 20261106-021152, 20261106-032341); without it 1.20-1.22 / 2.07-2.08 m (20261106-020354, 20261106-031151).
LIMITS = {('creature', 'n'): (0.45, 1.0), ('tribe', 'n'): (0.9, 1.0), ('tribe', 't'): (1.5, 1.0), ('civ', 'v'): (2.5, 1.5)}
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


NT0 = {}
HIT_EVERY = 5.0  # s between two hits (--hits)
HIT_WIN = 3.0  # s after a hit that are judged
# m, s: the copy of a hit NPC within HIT_MAX of the host's body, over TRAIL_M m at most HIT_TRAIL s. A push throws the host's
# NPC at 13-31 m/s for 0.3-0.9 s: shown one NPC state period (0.1 s) plus the latency late, the copy is up to ~4-5.5 m
# off for under 0.2 s (docs/re/creature.md#creature-hit-throw); BUG-0409 was a copy HELD 4.8 m off for 1.1 s
HIT_MAX, HIT_TRAIL = 6.0, 0.75


def hit_checks(ck, host, guest, hr, gr, hits, t_first):
    """BUG-0409: the guest's copy of a hit NPC against the host's body in the seconds after each hit, and the
    locomotion mode (+0x2B0, 3 = thrown by a hit) the guest's game gave the proxy"""
    def ntrack(con):
        con.call('coop.npctrack stop')
        return nw.dump(con)
    gt, ht = ntrack(guest), ntrack(host)
    path = os.path.join(lane.out_dir('track'), f'body_trail_hits_npctrack_{time.strftime("%Y%m%d-%H%M%S")}.json')
    with open(path, 'w') as f:
        json.dump({'host': ht, 'guest': gt, 'hits': hits, 't_first': t_first, 'nt0_host': NT0.get(id(host)),
                   'nt0_guest': NT0.get(id(guest))}, f)
    print(f'  npctrack rows: {path}', flush=True)
    worst, longest, n = [], [], 0
    for key, th in hits:
        win = lambda t: th - body_trail.GAP <= t <= th + HIT_WIN + body_trail.GAP  # noqa: E731
        h = [x for x in hr if x[1] == key and win(x[0])]
        g = [x for x in gr if x[1] == key and th <= x[0] <= th + HIT_WIN]
        r = body_trail.analyse(h, g, kinds='n').get('n')
        nid = int(key[1:])
        g_lm = collections.Counter(x.get('lm') for x in gt if x['id'] == nid)
        h_lm = collections.Counter(x.get('lm') for x in ht if x['id'] == nid)
        if not r or not r['n']:
            print(f'  hit {key} at {th - t_first:.1f} s: no moving samples (guest lm {dict(g_lm)}, host lm {dict(h_lm)})')
            continue
        n += r['n']
        mx = max(r['dist'])
        worst.append((mx, key, th - t_first))
        longest.append((r['trail'][0], key, th - t_first))
        print(f'  hit {key} at {th - t_first:.1f} s: off p90 {body_trail.pct(r["dist"], .9):.2f} max {mx:.2f} m, over '
              f'{body_trail.TRAIL_M:g} m {r["trail"][0]:.2f} s; guest lm {dict(g_lm)}, host lm {dict(h_lm)}', flush=True)
    ck(len(hits) >= 4 and n >= 100, f'hits: {len(hits)} hits of walking NPCs with {n} moving samples after them (>= 4, >= 100)')
    if worst:
        w = max(worst)
        ck(w[0] <= HIT_MAX, f'hits: the guest\'s copy of a hit NPC within {HIT_MAX} m of the host\'s body in the {HIT_WIN:g} s '
           f'after the hit (max {w[0]:.2f} m, {w[1]} hit at {w[2]:.1f} s)')
        lg = max(longest)
        ck(lg[0] <= HIT_TRAIL, f'hits: no copy of a hit NPC over {body_trail.TRAIL_M:g} m off for more than {HIT_TRAIL} s '
           f'(longest {lg[0]:.2f} s, {lg[1]} hit at {lg[2]:.1f} s)')


HOLD_EVERY = 8.0  # s between two planted holds (--hold)
HOLD_S = 2.5  # s the guest's game undoes the held mirror's placings
# m, s: the held mirror within HOLD_MAX of the host's member, over TRAIL_M m at most HOLD_TRAIL s (BUG-0386: it fell 8 m
# behind in 0.9 s and was teleported back, again and again while the hold lasted)
HOLD_MAX, HOLD_TRAIL = 4.0, 0.5


def hold_checks(ck, hr, gr, holds, t_first):
    """BUG-0386: the guest's copy of a walking member whose placings the guest's game undoes (coop.tribe undo)"""
    worst, longest, n = [], [], 0
    for key, th in holds:
        h = [x for x in hr if x[1] == key and th - body_trail.GAP <= x[0] <= th + HOLD_S + body_trail.GAP]
        g = [x for x in gr if x[1] == key and th <= x[0] <= th + HOLD_S]
        r = body_trail.analyse(h, g, kinds='t').get('t')
        if not r or not r['n']:
            print(f'  hold {key} at {th - t_first:.1f} s: no moving samples')
            continue
        n += r['n']
        worst.append((max(r['dist']), key, th - t_first))
        longest.append((r['trail'][0], key, th - t_first))
        print(f'  hold {key} at {th - t_first:.1f} s: off p90 {body_trail.pct(r["dist"], .9):.2f} max {max(r["dist"]):.2f} m, '
              f'over {body_trail.TRAIL_M:g} m {r["trail"][0]:.2f} s', flush=True)
    ck(len(worst) >= 4 and n >= 100, f'holds: {len(worst)} held walking members with {n} moving samples (>= 4, >= 100)')
    if worst:
        w, lg = max(worst), max(longest)
        ck(w[0] <= HOLD_MAX, f'holds: the guest\'s copy of a held member within {HOLD_MAX} m of the host\'s member '
           f'(max {w[0]:.2f} m, {w[1]} held at {w[2]:.1f} s)')
        ck(lg[0] <= HOLD_TRAIL, f'holds: no held copy over {body_trail.TRAIL_M:g} m off for more than {HOLD_TRAIL} s '
           f'(longest {lg[0]:.2f} s, {lg[1]} held at {lg[2]:.1f} s)')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--stage', default='creature', choices=('creature', 'tribe', 'civ'))
    ap.add_argument('--secs', type=float, default=60)
    ap.add_argument('--seed', type=int, default=None)
    ap.add_argument('--hold', action='store_true', help='tribe: every HOLD_EVERY s the guest\'s game undoes the placings '
                    'of one walking member\'s mirror for HOLD_S s (coop.tribe undo, BUG-0386) and those seconds are judged')
    ap.add_argument('--hits', action='store_true', help='creature: hit a walking NPC every HIT_EVERY s and judge the '
                    'seconds after each hit (BUG-0409)')
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
        if a.hits:
            c.call('coop.npctrack start 150')
            NT0[id(c)] = body_trail.now(c)  # (npctrack's t counts from its start)
    tt0 = time.time()
    nxt = {}
    hit = False
    hits = []  # (key, host time) of every hit (--hits)
    holds = []  # (key, guest time) of every planted hold (--hold)
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
        if a.hold and members and centre and now >= nxt.get('hold', tt0 + 6):
            i = members[len(holds) % len(members)]
            p = nw.tangent_point(centre, rng, 25, 35)  # a long walk: 3-4 s at 9 m/s
            host.call(f'coop.tribe order {i} 4 {p[0]:.1f} {p[1]:.1f} {p[2]:.1f}')
            nxt[('t', i)] = now + 7
            time.sleep(0.7)  # it walks on the guest too
            out = guest.call(f'coop.tribe undo {i} {HOLD_S}')['out'].strip()
            if 'undone' in out:
                holds.append((f't{i}', body_trail.now(guest)))
            print(f'  hold #{i}: {out[:70]}', flush=True)
            nxt['hold'] = time.time() + HOLD_EVERY
        if a.stage == 'civ' and now >= nxt.get('v', 0):
            cities, veh = civ_state(host)
            for vid, p in list(veh.items())[:4]:
                if cities:
                    c = min(cities, key=lambda c: math.dist(c, p))
                    host.call('coop.civ move %d %.1f %.1f %.1f' % ((vid,) + round_city(p, c)))
            nxt['v'] = now + 1.0
        if a.hits and ids and now >= nxt.get('hit', tt0 + 8):
            i = ids[len(hits) % len(ids)]
            push = 3.0 if len(hits) % 2 else 0.0
            out = host.call(f'coop.npc hosthit {i} 0.5 {push}')['out'].strip()
            th = body_trail.now(host)
            if 'applied' in out:
                hits.append((f'n{i}', th))
            print(f'  hit #{i} push {push}: {out[:60]}', flush=True)
            nxt['hit'] = now + HIT_EVERY
        if a.stage == 'creature' and not a.hits and not hit and now - tt0 > a.secs * 0.7:
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
    if a.hits:
        hit_checks(ck, host, guest, hr, gr, hits, t_first)
    if a.hold:
        hold_checks(ck, hr, gr, holds, t_first)
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
    if a.stage == 'tribe':
        # BUG-0610: the arrival's chief placing (tribe.place_chiefs) teleported a walking member 9-22 m on the host,
        # and the guest's copy followed (SPORE_FIX_OFF=tribe.place_still must FAIL)
        for side, rows in (('host', hr), ('guest', gr)):
            st = body_trail.steps(rows, 't')
            ck(not st, f'{side}: no Tribe member steps over 5 m in one frame of its own game ({len(st)}'
               + (': ' + ', '.join(f'{k} {d:.1f} m at {t - t_first:.1f} s' for d, k, t, _ in st[:3]) if st else '') + ')')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
