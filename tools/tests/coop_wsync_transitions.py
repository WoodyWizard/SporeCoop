#!/usr/bin/env python3
"""Tribe: every host-owned world state row is the host's in the guest's game after each transition (W1 class test,
BUG-0419 class; BUG-0572, BUG-0565, BUG-0564 goal, BUG-0579, BUG-0577).

The rows (mods/coop/wsync.cpp, read from each game with `coop.wsync dump`): the sky (time of day) and the day's
length, the stages done, how every village feels about the player tribe, the Tribe goal card and goal progress; and
the shared food (`coop.res`, the tribe's own bundles).

1. Tribe fixture. The host's world moves away from its last save: a village turns hostile (ApplyRelationship), the
   goal card goes to it (SetTargetTribe), a stage done (a consequence trait), 40 food. The guest's game has it all.
2. Transition 1, the host's game over and the game's reload of its last save (as coop_tribe_bounce_world.py): the
   host's world is the save's again. PASS: within 40 s every row of the guest's game is the host's (no row kept from
   before the reload), with the sky.
3. Transition 2, the guest leaves and joins again (a rejoin). PASS: within 40 s every row is the host's again.
4. Transition 3, the guest's game spends all its food and its village feelings drift (the game's own events on the
   guest): PASS: the host's values come back within 15 s.

  SPORE_LANE=4 python3 tools/tests/coop_wsync_transitions.py
"""
import time

import lane
import twoinst
from coop_tribe_food_zero import food
from coop_tribe_gameover import frames_go_on, player_members
from twoinst import Checks, start
from wsync_util import differences, dump


def settle(host, guest, secs, food_too=True):
    """the guest's differences from the host after at most `secs` (the last sample's)"""
    t0 = time.time()
    while True:
        h, g = dump(host), dump(guest)
        bad = differences(h, g)
        if food_too:
            hf, gf = food(host), food(guest)
            if hf is None or gf is None or abs(hf - gf) > 1:
                bad.append(f'food: host {hf} guest {gf}')
        if not bad or time.time() - t0 > secs:
            return bad, h, g
        time.sleep(2)


def show(h, g):
    for row in ('tod', 'day', 'stages', 'tribe_rel', 'tribe_goal'):
        print(f'    {row}: host {[e["raw"] for e in h.get(row, [])]} guest {[e["raw"] for e in g.get(row, [])]}',
              flush=True)


def main():
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(6)
    h = dump(host)
    rel = h.get('tribe_rel', [])
    ck(bool(rel), 'the host\'s game has an NPC village')
    if not rel:
        return ck.result()
    v = rel[0]['key']
    for c in (f'coop.wsync rel {v} -7', f'coop.wsync goal {v}', 'coop.wsync stage 0 0BB2D2A7', 'coop.res add food 40'):
        print('  host: ' + host.call(c)['out'].strip().splitlines()[0], flush=True)
    bad, h, g = settle(host, guest, 15)
    show(h, g)
    ck(not bad, f'before the transitions the guest\'s game has the host\'s world state ({bad})')

    # 1. the host's game over and reload of its last save
    own, enemy = player_members(host)
    ck(own and enemy, 'the player tribe and another tribe have members')
    t0 = time.time()
    while own and enemy and time.time() - t0 < 60:
        own, enemy = player_members(host)
        alive = [i for i, hp in own.items() if hp > 0]
        if not alive or not enemy:
            break
        for i in alive:
            host.call(f'coop.tribe pincer {i} {enemy[0]} 300')
        time.sleep(1)
    t2, alas = time.time(), False
    while time.time() - t2 < 40 and not alas:
        time.sleep(3)
        alas = 'no window' not in host.call('coop.ui find "Alas"')['out']
    ck(alas, 'the game-over caption is up on the host')
    host.call('key enter')
    ck(frames_go_on(host, 15), 'the host bounces back and keeps drawing')
    time.sleep(5)
    hr = dump(host)
    print(f'  the host\'s reloaded world: {[(r, [e["raw"] for e in es]) for r, es in hr.items()]}', flush=True)
    bad, h, g = settle(host, guest, 40)
    show(h, g)
    ck(not bad, f'after the host\'s game over and reload every row of the guest\'s game is the host\'s ({bad})')

    # 2. the guest's rejoin
    print('  guest: ' + guest.call('coop.leave')['out'].strip()[:80], flush=True)
    time.sleep(2)
    guest.call(lane.JOIN)
    ck(twoinst.wait_guest_in(host, guest, 120), 'the guest is back in the host\'s world after its rejoin')
    bad, h, g = settle(host, guest, 40)
    show(h, g)
    ck(not bad, f'after the guest\'s rejoin every row is the host\'s ({bad})')

    # 3. the guest's own game changes them (its spending, a relation event of its own)
    h = dump(host)
    m = guest.call('coop.tribe')['out']
    import re
    pt = re.search(r'player_tribe=([0-9A-F]+)', m)
    gf = food(guest)
    if pt and gf and gf >= 1:
        print('  guest spends: ' + guest.call(f'call SporeCoop::cTribe_AddFood thiscall 0x{pt.group(1)} -{int(gf)}')['out']
              .strip().splitlines()[0], flush=True)
    if h.get('tribe_rel'):
        print('  guest: ' + guest.call(f'coop.wsync rel {h["tribe_rel"][0]["key"]} 4')['out'].strip(), flush=True)
    host.call('coop.res add food 10')  # the host's total moves on after the guest's spend reached it
    bad, h, g = settle(host, guest, 15)
    show(h, g)
    ck(not bad, f'the guest\'s own changes are replaced by the host\'s values within 15 s ({bad})')
    print('  guest wsync: ' + guest.call('coop.wsync')['out'].strip().replace('\n', ' | '), flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
