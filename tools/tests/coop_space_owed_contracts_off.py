#!/usr/bin/env python3
"""Space: an owed reward tool is given also while the host's session has Space contracts off (BUG-1037).

A partner's mission reward tool (spacecoop K_MISSION) or a contract's reward that reaches a player outside Space is
owed (BUG-0694, space_contracts' list) and was paid only by the contracts feature's tick, which does not run while the
host's session settings switch Space contracts off (F3, BUG-0768): the tool was never given.

1. Two games on the space fixture; the host sets `space.contracts off` (the guest plays with it).
2. The guest lacks the Laser 2 tool; its Space modules act as outside Space (`coop.space away 1`) and a reward
   reaches it (`coop.space contracts result 0 6DB0189A`: the owed list, the same one K_MISSION fills).
3. Back in Space (`away 0`): within 8 s the guest's game's inventory has the tool (HasTool).
SPORE_FIX_OFF=space.owed_always: the owed list is not paid while contracts are off (FAIL).

  SPORE_LANE=2 python3 tools/tests/coop_space_owed_contracts_off.py
"""
import re
import time

import settings_lib as sl
import twoinst
from twoinst import Checks, start

LASER2 = '6DB0189A'


def sp(con, *a):
    return con.call('coop.space ' + ' '.join(str(x) for x in a))['out'].strip()


def has(con):
    return bool(re.search(r': have', sp(con, 'contracts tool', LASER2)))


def main():
    twoinst.forget_settings()
    host, guest = start('space')
    ck = Checks()
    try:
        time.sleep(3)
        ck(sl.force(host, [guest], 'space.contracts', 'off'), 'the guest plays with space.contracts off')
        f = [ln for ln in sp(guest, 'features').splitlines() if ln.startswith('contracts')]
        print(f'  guest feature: {f}', flush=True)
        if not ck(not has(guest), f'the guest has no Laser 2 yet ({sp(guest, "contracts tool", LASER2)})'):
            return ck.result()
        print(' ', sp(guest, 'away 1'))
        print(' ', sp(guest, 'contracts result 0', LASER2))
        time.sleep(2)
        print(' ', sp(guest, 'away 0'))
        got = sl.wait(lambda: has(guest), 8)
        print('  guest owed:', sp(guest, 'contracts owed'), flush=True)
        ck(got, "the guest's inventory has the owed reward tool back in Space while contracts are off")
    finally:
        sl.reset([host], ['space.contracts'])
        twoinst.forget_settings()
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
