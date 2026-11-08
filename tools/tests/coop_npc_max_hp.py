#!/usr/bin/env python3
"""BUG-0710 (class BUG-0419): an animal's maximum health on the guest is the host's.

NpcSpawn.max_health was sent and never applied: the guest's proxy had its species' adult maximum (a baby of the
creature fixture: max 5 on the host, 10 on the guest; detector Creature:max_hp in 20 of 23 comparisons, lab
20261107-231824). Four comparisons of the two games' worlds (tools/tests/worlddiff.py, the game's own max health of
every matched creature, cCombatant vt 0x58): no max_hp row.

  SPORE_FIX_OFF=npc.max_health coop_npc_max_hp.py   (max_hp rows: must FAIL)
"""
import time

import worlddiff
from twoinst import Checks, start


def main():
    host, guest = start('creature')
    ck = Checks()
    time.sleep(12)
    rows, matched = [], 0
    for i in range(4):
        hw, gw = worlddiff.pair_dump(host, guest)
        found = worlddiff.diff(hw, gw)
        rows += [f for f in found if f['sig'].startswith('Creature:max_hp')]
        time.sleep(5)
    for f in rows[:4]:
        print('  ', f['text'])
    print('  guest:', ' '.join(x for x in guest.call('coop.status')['out'].split() if x.startswith('max_hp_applied=')))
    ck(not rows, f'every matched animal has the host\'s maximum health on the guest ({len(rows)} max_hp findings in 4 comparisons)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
