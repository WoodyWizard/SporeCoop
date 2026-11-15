#!/usr/bin/env python3
"""Creature, four players: every puppet and NPC proxy carries an archetype tuning (BUG-0722, class BUG-0431).

Game code reads a creature's archetype tuning (+0xE84, the getter 0xC0C300 and the direct read 0xC115E0)
without a null check: the callers 0xBA43E0, ApplyEvent 0xBA4A00, CreatureSocialSuccess 0xD9BBD0, Create
0xC09B40. Guards exist only for CreatureSelectionUI, CreatureStageEventHandler and the AI tick
(coop.cpp h_SelectUI / h_StageMsg, game.cpp h_NPCTickAI). A puppet (a partner's creature) or a proxy (the
host's NPC on a guest) without a tuning would fault there. This test looks for one, the way players
make them: four players (the host, a guest with its own profile, two guests of the host's world),
walking about, the last guest leaving and joining again (a new puppet on every game).

The readers run with puppets and proxies the way players reach them: two guests hunt an NPC (their hits on
the host with their puppets as attackers, the kill credit and ApplyEvent), then impress NPCs (social acts:
CreatureSocialSuccess on the host with a guest's puppet), then a rejoin. Checked on every game after each
step and every few seconds (`coop.npc untuned`: the game's own creature objects with +0xE84 null): none of
ours (puppets, proxies) without a tuning; at the end every game has some of ours, all tuned; no game faults.

  SPORE_LANE=6 python3 tools/tests/coop_puppet_tuning.py [--seconds 60]
"""
import argparse
import re
import time

from coop_creature_kills import family
from coop_party_creature import hunt, impress
from own_creature import back_to_world, play_own
from party import Party
from twoinst import Checks


def untuned(p):
    out = p.call('coop.npc untuned')
    m = re.search(r'creatures (\d+): untuned (\d+) \(ours (\d+), other (\d+)\)', out)
    ours = [ln.strip() for ln in out.splitlines() if ln.startswith('  creature') and ' ours' in ln]
    return (tuple(int(x) for x in m.groups()) if m else None), ours


def sample(p, label, worst):
    for x in p.all:
        r, ours = untuned(x)
        if r is None:
            continue
        worst[x.name] = max(worst.get(x.name, 0), r[2])
        if r[2]:
            print(f'  [{label}] {x.name}: {r[0]} creatures, ours untuned {r[2]}: {ours[:3]}', flush=True)
    return worst


def ours_tuned(p):
    """(puppets and proxies of ours, how many carry +0xE84) per game, from `coop.npc creatures`"""
    res = {}
    for x in p.all:
        out = x.call('coop.npc creatures')
        lines = [ln for ln in out.splitlines() if ln.startswith('  creature') and ' ours' in ln]
        res[x.name] = (len(lines), sum(1 for ln in lines if ' untuned' not in ln))
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--seconds', type=float, default=60)
    a = ap.parse_args()
    ck = Checks()
    p = Party.start('creature', 4)
    try:
        if not p.check_together(ck, 'join'):
            return ck.result()
        for x in p.all:
            x.call('coop.team hp 1000')  # nests attack now and then: nobody dies by accident
        p.ids()
        # the case of the bug: a guest playing a creature of its own (not the world's species: since BUG-0345 a guest
        # plays the world's species unless it chooses another, so the test chooses one, as the creations menu does)
        own = p.guests[0]
        ck(play_own(own.con), f'{own.name} plays a creature of its own, not the world\'s species')
        time.sleep(3)
        worst = {}
        sample(p, 'start', worst)
        # the game's readers of the tuning with a puppet as the actor: a hunt (two guests' hits, the kill credit,
        # ApplyEvent) and impressing (the guests' social acts, CreatureSocialSuccess on the host with a puppet)
        fam = family(p.host.con)
        aux = Checks()  # the hunt's and impressing's own checks are party_creature_npc's: printed, not counted here
        nid = hunt(aux, p, fam)
        sample(p, 'hunt', worst)
        impress(aux, p, fam, nid)
        sample(p, 'impress', worst)
        t0 = time.time()
        while time.time() - t0 < a.seconds:
            time.sleep(5)
            sample(p, f'{time.time() - t0:3.0f}s', worst)
        print('  ours (all, with a tuning):', ours_tuned(p), flush=True)
        # the last guest leaves and joins again: new puppets on every game, new proxies on it
        d = p.guests[-1]
        ck(p.leave(d) and p.join(d), f'{d.name} left and joined again')
        t0 = time.time()
        while time.time() - t0 < 30:
            time.sleep(5)
            sample(p, f'rejoin {time.time() - t0:3.0f}s', worst)
        res = ours_tuned(p)
        print('  ours after the rejoin (all, with a tuning):', res, flush=True)
        for name, n in worst.items():
            ck(n == 0, f'{name}: no puppet or proxy of ours without an archetype tuning (most seen at once: {n})')
        ck(all(t == n and n > 0 for n, t in res.values()),
           f'every game has puppets / proxies and all of them carry a tuning ({res})')
        p.faults(ck)
        return ck.result()
    finally:
        try:
            back_to_world(p.guests[0].con)
        except Exception:
            pass
        p.stop()


if __name__ == '__main__':
    raise SystemExit(main())
