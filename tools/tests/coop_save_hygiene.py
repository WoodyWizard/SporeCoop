#!/usr/bin/env python3
"""Creature stage: our objects never go into a save (BUG-0020, CLAUDE.md 2).

1. Two games play together: the host has the guest's puppet and the herd made for it.
2. The host saves (`coop.save`, the game's SaveGame: the world through the COM serializer).
3. Both games restart (like after a crash); the host loads that save alone, with the removal of
   such creatures switched off (`coop.npc strays off`), and counts in its game's object list:
   creatures without tuning (+0xE84 = 0: what a saved puppet comes back as) and herds without an
   archetype (+0x88 = 0: the herds made for puppets; every herd of the game has one).
   Both must be 0. Before the fix the puppet came back as an NPC, its herd with it
   (docs/re/creature.md 12; the host crashed on an attack on it, BUG-0008).

SPORE_SAVE_FIX=0 switches the fix off on the host (`coop.savestrip 0`): the run that must FAIL.

--leave (suite save_hygiene_leave, BUG-0255): the guest leaves before the host saves. Its puppet is
gone, the herd made for it stays in the world with no member (herds are reused, never destroyed).
The loaded save must hold no herd without an archetype either, the host's avatar must be there and
nothing faults. SPORE_FIX_OFF=savestrip.empty_herds (the owner rule of 0.7.3.0 kept empty herds):
must FAIL.

  SPORE_LANE=4 python3 tools/tests/coop_save_hygiene.py
"""
import os
import re
import sys
import time

from coop_creature_e84 import archless_herds, close_boxes, restart_and_load, untuned, wait_puppet
from twoinst import Checks, lane_state, start


def main():
    leave = '--leave' in sys.argv
    fix = os.environ.get('SPORE_SAVE_FIX', '1')
    host, guest = start()
    ck = Checks()
    st = lane_state()
    slot = 'Game0' if st.get('slot', 9) == 9 else f'Game{st["slot"]}'
    planet = (st.get('planet') or 'Game0/Scalinus').split('/')[1]
    out = host.call(f'coop.savestrip {fix}')['out']
    ck(f'savestrip={fix}' in out, f'savestrip set to {fix}: {out.strip()}')
    sp = wait_puppet(host)
    ck(sp is not None, f'the host has the guest\'s puppet (species {sp})')
    h0 = archless_herds(host)
    print(f'  before the save: herds without an archetype {h0}', flush=True)
    if leave:
        print('  ' + guest.call('coop.leave')['out'].strip(), flush=True)
        t0 = time.time()
        while time.time() - t0 < 20 and 'puppet pos=' in host.call('coop.status')['out']:
            time.sleep(1)
        time.sleep(3)
        empty = [ln.strip() for ln in host.call('coop.npc herds')['out'].splitlines() if 'ours=1' in ln]
        print(f'  after the leave: herds without an archetype {archless_herds(host)}; ours: {empty[:3]}', flush=True)
        ck(any('members=0' in ln for ln in empty), 'the herd made for the puppet stays in the world empty')
    out = host.call('coop.save')['out']
    close_boxes(host, 12)
    ck('saved' in out, f'the host saved with the puppet in the world ({slot}/{planet}: {out.strip()})')
    print('  ' + host.call('coop.savestrip')['out'].strip(), flush=True)

    host, guest = restart_and_load(slot, planet, before_load=['coop.npc strays off'])
    ours, other, lines = untuned(host)
    for line in lines:
        print('   ', line.strip())
    herds = archless_herds(host)
    ck(other == 0, f'the loaded save holds no creature without tuning (a puppet of the session: {other})')
    ck(herds == 0, f'the loaded save holds no herd without an archetype (made for a puppet: {herds})')
    ck(host.call('coop.avatar')['ok'], 'the host\'s avatar is in the loaded world')
    time.sleep(8)  # the simulator's updates run over the loaded world (a null left in a list faults here)
    print('  ' + host.call('coop.savestrip')['out'].strip(), flush=True)
    host.call('coop.npc strays on')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
