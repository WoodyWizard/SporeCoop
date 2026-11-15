#!/usr/bin/env python3
"""Space: a contract's reward tool that arrives while the player is outside Space is given when it is back (BUG-0694;
class BUG-0423: credits and effects dropped or undone).

1. Two games on the space fixture. The guest lacks the Laser 2 tool (`coop.space contracts tool 6DB0189A`: the
   game's inventory HasTool).
2. The guest's Space modules act as outside Space (`coop.space away 1`: as an editor or an adventure entered from
   Space, the world stays, no inventory) and a won contract's result reaches it (`coop.space contracts result 0
   6DB0189A`: on_result with the guest in the crew, the path of MSG_SPACE kind 22).
3. Back in Space (`coop.space away 0`): within 5 s the guest's inventory has the tool. 0.7.10
   (SPORE_FIX_OFF=space.reward_owed): give_tool found no inventory and the reward was spent: no tool.
"""
import re
import time

from twoinst import Checks, start

LASER2 = '6DB0189A'


def sp(con, *a):
    return con.call('coop.space ' + ' '.join(str(x) for x in a))['out'].strip()


def has(con):
    return bool(re.search(r': have', sp(con, 'contracts tool', LASER2)))


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(3)
    if not ck(not has(guest), f'the guest has no Laser 2 yet ({sp(guest, "contracts tool", LASER2)})'):
        return ck.result()
    print(' ', sp(guest, 'away 1'))
    print(' ', sp(guest, 'contracts result 0', LASER2))
    time.sleep(2)
    away = has(guest)
    print(' ', sp(guest, 'away 0'))
    t0 = time.time()
    while time.time() - t0 < 5 and not has(guest):
        time.sleep(0.5)
    got = has(guest)
    print(f'  tool while away: {away}, back in space: {got}; notices:', sp(guest, 'contracts notices').replace('\n', ' | ')[-300:])
    ck(got, "the guest's inventory has the reward tool once it is back in space")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
