#!/usr/bin/env python3
"""Creature: the attack key's walk-and-use moves the creature to its target (BUG-0964, BUG-0965).

For the animals near the players (not of the players' family), one at a time: the avatar is put 6 m from the animal
(`coop.npc gopos`, as coop_creature_partner_pass.py and other tests put creatures), its combat target set to the animal
(`coop.abilities target <ptr> any`: the game's SetTarget, as a click does) and the attack key pressed. Checked on the
game's own state: the avatar's position in its own game must move (0.3 m within 4 s) or the game's gap reach the
bite's reach. A press that does not move it prints what the game does (`coop.abilities walk raw`: the behaviour tree's
node, the locomotion's request, the facing, the steering, the nearest nest, the terrain and the surface radius), then
a plain walk 3 m to the side / back / ahead (the game's WalkTo) and a 2 m lift with another press.

What it found (2026-11-12, docs/re/creature.md#creature-walk-held-in-mound): a creature put at the terrain's height
(GetHeightAt) inside a nest mound's or a rock's collision is held by Havok in every direction: the walk-and-use node
runs, the request and the steering point at the target, the velocity stays 0, a plain walk does nothing; 2 m higher it
falls onto the mound and walks. `coop.npc gopos` now puts the creature onto the game's surface (tools.gopos_surface).
SPORE_FIX_OFF=tools.gopos_surface must FAIL (about 1 round in 5 near the fixture's nests).

  --who guest|host|solo   whose avatar walks (solo: the guest leaves first; the host plays alone, no session)
  --fixoff                every fix of the mod off on that game (`coop.fix all off`): the game's own code paths
  --mode teleport|walkin  walkin: no teleport, the game walks the creature there (WalkTo) before the press
  --rounds N              animals tried
  --dumps                 the walk detail for every round (not only the failing ones)

  SPORE_LANE=2 python3 tools/tests/coop_creature_walk_start.py [--who guest] [--rounds 20]
"""
import argparse
import math
import re
import time

from coop_creature_partner_pass import npcs, place, reach
from coop_sync import sample
from twoinst import Checks, set_stance, start


def walk_detail(con, tag):
    """the lines of `coop.abilities walk raw` that say why (not the raw dwords)"""
    out = con.call('coop.abilities walk raw')['out']
    keep = [x for x in out.splitlines() if x.startswith(('node=', 'stim', 'speed', 'facing', 'steer', 'near', 'nest', 'anim'))]
    print(f'    [{tag}] ' + '\n      '.join(keep), flush=True)


def side_walks(con, me_key, an):
    """a plain walk 3 m to the side, back and ahead (the game's WalkTo): is the creature held, or only that way?"""
    con.call('coop.abilities target none')
    q0 = (sample(con).get(me_key) or (None,))[0]
    moved = []
    if q0:
        r0 = math.sqrt(sum(c * c for c in q0))
        up0 = [c / r0 for c in q0]
        g = [a - b for a, b in zip(an, q0)]
        side = [up0[1] * g[2] - up0[2] * g[1], up0[2] * g[0] - up0[0] * g[2], up0[0] * g[1] - up0[1] * g[0]]
        sl = math.sqrt(sum(c * c for c in side)) or 1
        gl = math.sqrt(sum(c * c for c in g)) or 1
        for v in ([c / sl * 3 for c in side], [-c / gl * 3 for c in g], [c / gl * 3 for c in g]):
            q1 = (sample(con).get(me_key) or (None,))[0]
            con.call('coop.abilities walkto %.3f %.3f %.3f' % tuple(x + y for x, y in zip(q1, v)))
            time.sleep(2.0)
            q2 = (sample(con).get(me_key) or (None,))[0]
            moved.append(round(math.dist(q1, q2), 2) if q1 and q2 else -1)
    print(f'    a plain walk 3 m to the side / back / ahead: moved {moved} m', flush=True)
    return moved


def one(con, nid, ptr, me_key, label, dumps, mode):
    s = sample(con)
    an = (s.get(f'n{nid}') or (None,))[0]
    me = (s.get(me_key) or (None,))[0]
    if not an or not me:
        return None
    r = math.sqrt(sum(c * c for c in an)) or 1
    up = [c / r for c in an]
    d = [m - a for m, a in zip(me, an)]
    k = sum(x * y for x, y in zip(d, up))
    d = [x - k * u for x, u in zip(d, up)]
    ln = math.sqrt(sum(c * c for c in d)) or 1
    goal = [a + c / ln * 6.0 for a, c in zip(an, d)]
    if mode == 'walkin':  # (no teleport: the game's own walk there, WalkTo as a click on the ground orders it)
        if math.dist(me, goal) > 80:
            print(f'  [{label}] n{nid}: {math.dist(me, goal):.0f} m away, not walked', flush=True)
            return None
        con.call('coop.abilities target none')
        con.call('coop.abilities walkto %.3f %.3f %.3f' % tuple(goal))
        t1, last, still = time.time(), None, 0
        while time.time() - t1 < 30:
            time.sleep(0.5)
            q = (sample(con).get(me_key) or (None,))[0]
            if q and math.dist(q, goal) < 0.8:
                break
            still = still + 1 if q and last and math.dist(q, last) < 0.05 else 0
            last = q
            if still >= 6:
                print(f'  [{label}] n{nid}: the walk there stopped {math.dist(q, goal):.1f} m short (3 s still)', flush=True)
                break
        time.sleep(1.0)
    else:
        place(con, goal)
    out = con.call(f'coop.abilities target {ptr:08X} any')['out'].strip()
    tg, gap, rch, st = reach(con)
    if st != 0 or gap is None or gap <= rch or gap > 30:
        print(f'  [{label}] n{nid}: target {out!r}, key 1 status {st} gap {gap}: not a round', flush=True)
        return None
    p0 = (sample(con).get(me_key) or (None,))[0]
    con.call('key 1')  # the attack key (1): the game's walk-and-use
    t0, moved, gaps, early = time.time(), 0.0, [], False
    while time.time() - t0 < 4.0:
        time.sleep(0.25)
        p = (sample(con).get(me_key) or (None,))[0]
        g = reach(con)[1]
        gaps.append(None if g is None else round(g, 1))
        if p and p0:
            moved = max(moved, math.dist(p, p0))
        if dumps and not early and time.time() - t0 > 0.5:
            early = True
            walk_detail(con, '0.5 s')
        if g is not None and g <= rch:
            break
    ok = moved >= 0.3 or bool(gaps and gaps[-1] is not None and gaps[-1] <= rch)
    s2 = sample(con)
    others = sorted((round(math.dist(p0, v[0]), 1), k) for k, v in s2.items() if p0 and k.startswith(('n', 'p'))
                    and k not in (f'n{nid}', me_key))[:3]
    print(f'  [{label}] n{nid} {ptr:08X}: gap {gap:.1f} -> {gaps[-1] if gaps else None}, moved {moved:.2f} m: '
          f'{"walked" if ok else "NO START"}; nearest others {others}', flush=True)
    if not ok:
        walk_detail(con, 'held')
        con.call('key 1')  # (a second press, as a player would)
        time.sleep(2)
        p = (sample(con).get(me_key) or (None,))[0]
        print(f'    a second press: moved {math.dist(p, p0) if p and p0 else -1:.2f} m', flush=True)
        side_walks(con, me_key, an)
        # put 2 m higher at the same spot: it falls onto what is there; then the key again
        con.call(f'coop.abilities target {ptr:08X} any')
        q1 = (sample(con).get(me_key) or (None,))[0]
        con.call('coop.abilities lift 2')
        time.sleep(1.5)
        con.call('key 1')
        time.sleep(2.5)
        q2 = (sample(con).get(me_key) or (None,))[0]
        print(f'    2 m higher, then a press: moved {math.dist(q1, q2) if q1 and q2 else -1:.2f} m, key 1 {reach(con)}',
              flush=True)
    con.call('coop.abilities target none')
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--who', default='guest', choices=('guest', 'host', 'solo'))
    ap.add_argument('--rounds', type=int, default=20)
    ap.add_argument('--dumps', action='store_true')
    ap.add_argument('--fixoff', action='store_true')
    ap.add_argument('--mode', default='teleport', choices=('teleport', 'walkin'))
    a = ap.parse_args()
    host, guest = start('creature')
    ck = Checks()
    time.sleep(3)
    gl = guest.call('coop.npc list')['out']
    family = set(re.findall(r'species=([0-9A-F]{8}) .*?fam=1', gl))
    hid = (re.search(r'client id=(\d+)', guest.call('coop.status')['out']) or [None, '1'])[1]
    if a.who == 'solo':  # (the host's game alone: no session, no partner, no puppets or proxies)
        guest.call('coop.leave')
        time.sleep(3)
    con = guest if a.who == 'guest' else host
    me_key = f'p{hid}' if a.who == 'guest' else 'p0'
    if a.fixoff:
        print('  ' + con.call('coop.fix all off')['out'].strip(), flush=True)
    set_stance(con, 'combat')
    res, used = [], set()
    for r in range(a.rounds * 2):
        if len(res) >= a.rounds:
            break
        hl, cl = npcs(host), npcs(con)
        cand = sorted((v['dist'], k) for k, v in hl.items() if not v['dead'] and k not in used and v['dist'] < 150
                      and v.get('species') not in family and cl.get(k, {}).get('ptr'))
        if not cand:
            break
        nid = cand[0][1]
        used.add(nid)
        x = one(con, nid, cl[nid]['ptr'], me_key, f'r{r + 1}', a.dumps, a.mode)
        if x is not None:
            res.append(x)
    print(f'  {a.who} {a.mode}{" fixes off" if a.fixoff else ""}: {len(res)} rounds, walked {sum(res)}, '
          f'no start {len(res) - sum(res)}', flush=True)
    ck(len(res) >= min(10, a.rounds), f'rounds: {len(res)} >= {min(10, a.rounds)}')
    ck(bool(res) and all(res), f'every press of the attack key moves the creature ({sum(res)} of {len(res)})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
