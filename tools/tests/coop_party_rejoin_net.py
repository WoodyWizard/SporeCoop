#!/usr/bin/env python3
"""Three or four players who leave and join again over their own bad networks (SPORE_NET: every guest
has its own proxy, netem.py), checked in every machine's game (tools/tests/party.py). The invariant
monitor (monitor.py) watches all games all the time (the lab fails the test on a MONITOR-VIOLATION).

Rounds (each ends with the checks below):
  1 one     the last guest leaves and joins again
  2 swap    Bob (who plays his own creature) and the last guest leave; the last guest joins first and
            gets Bob's old player id, then Bob joins (BUG-0076: a newcomer must not inherit the look
            of the id's former player)
  3 lost    a guest's goodbye is lost (its proxy drops everything for 1 s over the leave), it joins
            again at once from the same address (protocol 6: the host must take the new HELLO)
  4 both    every extra guest leaves at the same moment and they join at the same moment

Checks after every round, on every machine's game (not the mod's counters):
  together  every guest plays the host's stage with an avatar and the host has it in its world
  see       each guest's game shows every other player where that player's own game has it
  species   the creature each machine's game draws for a player (the puppet nearest to that player,
            coop.census key) is the species that player's own game plays (its avatar's key)
  shared    food / money the same on every machine
Afterwards no machine has a caught fault.

  SPORE_LANE=9 SPORE_NET=players python3 tools/tests/coop_party_rejoin_net.py --players 4
  BUG-0076's switches (core.id_reuse, core.creations_leave, both) did not fail it (labs 20261101-144314,
  20261101-144852, 20261101-152157: the newcomer was drawn as its own species with Bob's id); the planted
  PARTY_PLANT=lift (Carol's game draws its partners 5 m up after round 2, `coop.track lift 5`) fails it
"""
import argparse
import math
import os
import re
import time

import census
import netem
from coop_multi import sample
from party import NEAR, Party
from twoinst import Checks


def creations(p):
    """(own creature (key, name) or None, {player id: (key, name)} of the others' creatures in this game)"""
    out = p.call('coop.creations')
    m = re.search(r"mine creature: ([0-9A-F]{8}) '([^']*)'", out)
    others = {int(x.group(1)): (x.group(2), x.group(3))
              for x in re.finditer(r"player (\d+) creature: ([0-9A-F]{8})/\S+ '([^']*)'", out)}
    return (m.groups() if m else None), others


def species_view(p, ck, label):
    """each viewer's game draws every other player as the species that player plays. Keys of a creation are
    each game's own (an imported creation gets a new instance key): a player who plays its own creation must
    be drawn as the viewer's import of that creation (same name, the key the viewer's game has for it), a
    player of the world's species as that key (the same save)"""
    own, rows, made = {}, {}, {}
    p.ids()
    for x in p.all:
        c = census.dump(x.con)
        rows[x.name] = c
        own[x.name] = next((r for r in c.rows if r['own'] == 'avatar'), None)
        made[x.name] = creations(x)
    bad, seen = [], []
    near = NEAR.get(p.stage(p.host), 6.0)
    for v in p.all:
        pups = [r for r in rows[v.name].rows if r['own'] == 'puppet']
        for s in p.all:
            if s is v or own[s.name] is None:
                continue
            pid = 0 if s is p.host else s.pid
            at = sample(v.con).get(f'p{pid}')
            if at is None:
                bad.append(f'{v.name} does not show {s.name}')
                continue
            best = min(pups, key=lambda r: math.dist(r['pos'], at), default=None)
            if best is None or math.dist(best['pos'], at) > near:
                bad.append(f'{v.name}: no creature where it shows {s.name}')
                continue
            mine = made[s.name][0]
            if mine and mine[0] == own[s.name]['key']:  # the subject plays its own creation
                imp = made[v.name][1].get(pid)
                want = f"its own '{mine[1]}'"
                ok = imp is not None and imp[1] == mine[1] and imp[0] == best['key']
            else:
                want = own[s.name]['key']
                ok = best['key'] == want
            seen.append(f'{v.name}->{s.name} {best["key"]}')
            if not ok:
                bad.append(f'{v.name} draws {s.name} as {best["key"]} ({made[v.name][1].get(pid)}), {s.name} plays {want}')
    print(f'  [{label}] species drawn: {", ".join(seen)}', flush=True)
    ck(not bad, f'[{label}] every machine draws every other player as the species that player plays: {bad}')


def checks(p, ck, label):
    time.sleep(6)
    if not p.check_together(ck, label):
        return False
    p.check_partners(ck, label)
    if p.fixture == 'creature':
        species_view(p, ck, label)
    p.check_res(ck, label)
    print(f'  [{label}] players {p.ids()}', flush=True)
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--fixture', default='creature')
    ap.add_argument('--players', type=int, default=4)
    ap.add_argument('--rounds', default='one,swap,lost,both')
    a = ap.parse_args()
    print(f'  network per guest: {os.environ.get("SPORE_NET") or "plain"}', flush=True)
    ck = Checks()
    t0 = time.time()
    p = Party.start(a.fixture, a.players)
    try:
        print(f'  players {p.ids()} in {time.time() - t0:.0f} s', flush=True)
        if not checks(p, ck, 'start'):
            return ck.result()
        bob, last = p.byname('Bob'), p.guests[-1]
        extra = p.guests[1:]
        for rnd in filter(None, a.rounds.split(',')):
            t1 = time.time()
            if rnd == 'one':
                ck(p.leave(last), f'[one] {last.name} leaves')
                time.sleep(3)
                ck(p.join(last), f'[one] {last.name} joins again')
            elif rnd == 'swap':
                ids0 = p.ids()
                ck(p.leave(bob), '[swap] Bob leaves')
                ck(p.leave(last), f'[swap] {last.name} leaves')
                time.sleep(3)
                ck(p.join(last), f'[swap] {last.name} joins first')
                ids1 = p.ids()
                print(f'  [swap] ids before {ids0}, {last.name} now {ids1.get(last.netname)}', flush=True)
                # Bob's id went to the newcomer: now, before Bob is back, each game must draw it as its own species
                time.sleep(5)
                gone = p.guests
                p.guests = [g for g in gone if g is not bob]
                try:
                    species_view(p, ck, 'swap, Bob away')
                finally:
                    p.guests = gone
                ck(p.join(bob), '[swap] Bob joins again')
            elif rnd == 'lost':
                px = netem.proxy({'Bob': 'guest', 'Carol': 'extra1', 'Dave': 'extra2'}[last.name])
                if px:
                    px.outage(1.0)
                last.call('coop.leave')
                time.sleep(1.2)
                ck(p.join(last), f'[lost] {last.name} joins again after a lost goodbye' + ('' if px else ' (no proxy)'))
            elif rnd == 'both':
                for gp in extra:
                    gp.call('coop.leave')
                time.sleep(4)
                for gp in extra:
                    gp.call(gp.join)
                ok = all(p.wait_in(gp, 260, stable=4) for gp in extra)
                ck(ok, f'[both] {", ".join(g.name for g in extra)} left and joined at the same moment')
            if os.environ.get('PARTY_PLANT') == 'lift' and rnd == 'swap':
                # a planted problem: Carol's game draws every partner 5 m above where it is (`coop.track lift`)
                print('  PLANTED: Carol: ' + p.byname('Carol').call('coop.track lift 5').strip()[:100], flush=True)
            print(f'  round {rnd}: {time.time() - t1:.0f} s', flush=True)
            checks(p, ck, rnd)
        for name, px in netem.proxies().items():
            print(f'  {name}: {px.describe()}', flush=True)
        p.faults(ck)
        print(f'  ({time.time() - t0:.0f} s)', flush=True)
        return ck.result()
    finally:
        p.stop()


if __name__ == '__main__':
    raise SystemExit(main())
