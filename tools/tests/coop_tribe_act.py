#!/usr/bin/env python3
"""Tribe: the `coop.tribe act` test command does not fault the host (BUG-0260).

`coop.tribe act 15 <member of an NPC tribe>` (CitizenAction 15 = RaidHuts, a hut / tool action)
made the host's selected members fault in cCreatureCitizen::DoAction (+0x78C, data 0x39C): the
game's UI never gives an object action with a citizen as the target (attack is 7,
docs/re/tribe.md 2). The command now refuses the object actions (3, 12, 15, 24) on a member.

1. Tribe fixture; the host selects its player tribe's members (`coop.tribe select <id> 1`).
2. `coop.tribe act 15 <NPC member>`: no fault in the host's game (exc), a refusal answer.
3. `coop.tribe act 7 <NPC member>` (attack): ordered, no fault.

--nofix: `coop.fix tribe.act_check off` (the old command): must FAIL (the host faults).

  SPORE_LANE=8 python3 tools/tests/coop_tribe_act.py [--nofix]
"""
import argparse
import re
import time

from coop_tribe_gameover import player_members
from twoinst import Checks, start


def faults(con):
    m = re.search(r'total faults: (\d+)', con.call('exc')['out'])
    return int(m.group(1)) if m else -1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nofix', action='store_true')
    a = ap.parse_args()
    host, guest = start('tribe')
    ck = Checks()
    if a.nofix:
        print(' ', host.call('coop.fix tribe.act_check off')['out'].strip())
    time.sleep(5)
    print(' ', host.call('coop.tribe spawn 4')['out'].strip())
    time.sleep(4)
    own, enemy = player_members(host)
    ck(own and enemy, f'the player tribe ({len(own)}) and another tribe ({len(enemy)}) have members')
    if not (own and enemy):
        return ck.result()
    for i in own:
        host.call(f'coop.tribe select {i} 1')
    f0 = faults(host)
    out = host.call(f'coop.tribe act 15 {enemy[0]}')['out'].strip()
    print('  act 15:', out)
    time.sleep(6)
    f1 = faults(host)
    ck(f1 == f0, f'a hut action on a member does not fault the host (faults {f0} -> {f1})')
    out = host.call(f'coop.tribe act 7 {enemy[0]}')['out'].strip()
    print('  act 7:', out)
    time.sleep(6)
    f2 = faults(host)
    ck('action ordered' in out and f2 == f1, f'an attack order on a member is given without a fault ({f1} -> {f2})')
    print(' ', host.call('exc')['out'].splitlines()[:3])
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
