#!/usr/bin/env python3
"""Tribe: the guest sees the members fight, gather and dance as on the host (BUG-0569).

Player report (guest, 3 times): "I don't see any animations" in Tribe; its screenshot of a village fight shows
the members standing still while the host's show them fighting. A guest's mirror of a member runs no behaviour
tree (the host's member decides, tribe.cpp h_BTTick), and the animations of the members' actions are started by
those trees: the mirrors played only their locomotion's idle and walk.

1. Two games on the tribe fixture, 6 more members; the host's player members attack an NPC tribe's member (the
   player's attack order, CitizenAction 7); every tribe's members also do what their own AI does.
2. For 45 s, twice a second: every member the host's game plays a behaviour animation for now (its current
   animation, Anim::AnimatedCreature GetCurrentAnimation, is the one its behaviour tree started) is looked up on
   the guest by the host's member id: the guest's game's current animation of the mirror.
PASS: at least 60 such samples, and the guest's mirror plays the host's animation in at least 80 % of them
(a start on the host must be seen there for two samples in a row: the relay comes with the next cycle; with the fix 94 %, without 0 %).

SPORE_FIX_OFF=tribe.mirror_anims: the mirrors play their own idle / walk (FAIL).

  SPORE_LANE=4 python3 tools/tests/coop_tribe_mirror_anims.py
"""
import re
import time

from coop_tribe_gameover import player_members
from twoinst import Checks, start

ROW = re.compile(r'cur (\d+) anim=([0-9A-F]{8}) bhv=([0-9A-F]{8})')


def anims(con):
    """{member id: (current anim, last behaviour anim)}"""
    return {int(m.group(1)): (m.group(2), m.group(3)) for m in ROW.finditer(con.call('coop.tribe anims')['out'])}


def main():
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(5)
    print(' ', host.call('coop.tribe spawn 6')['out'].strip())
    time.sleep(6)
    own, enemy = player_members(host)
    ck(own and enemy, f'the player tribe ({len(own)}) and another tribe ({len(enemy)}) have members')
    if not (own and enemy):
        return ck.result()
    for i in own:
        host.call(f'coop.tribe select {i} 1')
    print('  attack:', host.call(f'coop.tribe act 7 {enemy[0]}')['out'].strip())
    samples, same, kinds = 0, 0, {}
    prev = {}
    t0 = time.time()
    while time.time() - t0 < 45:
        h = anims(host)
        g = anims(guest)
        for i, (cur, bhv) in h.items():
            if bhv == '00000000' or cur != bhv or i not in g:
                continue
            # a start the guest cannot have yet (its relay comes with the next state cycle): the same animation
            # on the host's previous sample only
            if prev.get(i) != cur:
                continue
            samples += 1
            kinds[cur] = kinds.get(cur, 0) + 1
            if g[i][0] == cur:
                same += 1
        prev = {i: cur for i, (cur, _) in h.items()}
        time.sleep(0.5)
    rate = same / samples if samples else 0.0
    print(f'  behaviour animations on the host: {samples} samples, {len(kinds)} kinds {sorted(kinds.items(), key=lambda x: -x[1])[:8]}')
    print(f'  the guest plays the same: {same} of {samples} ({100 * rate:.0f} %)')
    print('  host', host.call('coop.tribe anims')['out'].splitlines()[-1])
    print('  guest', guest.call('coop.tribe anims')['out'].splitlines()[-1])
    ck(samples >= 60, f'the host\'s members play behaviour animations ({samples} samples >= 60)')
    ck(rate >= 0.8, f'the guest\'s mirrors play the host\'s behaviour animation ({100 * rate:.0f} % >= 80 %)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
