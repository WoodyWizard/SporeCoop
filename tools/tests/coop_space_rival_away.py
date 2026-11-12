#!/usr/bin/env python3
"""BUG-0813: rivals mode, a guest's own budget after a trip out of Space (an editor, an adventure).

Rivals mode (`coop.space rivalry mode rivals`): each player has its own budget, the guest's is its game's Space
money. The guest acts as outside Space (`coop.space away 1`: what an editor or an adventure entered from Space gives
the mod; the Space world lives on) and wins a contract meanwhile (`coop.space contracts result 5000 0`: the reward is
owed, BUG-0694, and paid in the first Space frame back). Back in Space (`away 0`) the guest's game money must be its
money before plus the 5000, and stay there.

Before the fix every return to Space asked the host for the guest's budget again, and the host's copy (the guest's
last report, from before the trip) replaced it: the 5000 were gone (`restored` of `coop.space rivalry` rose).

  coop_space_rival_away.py   (SPORE_FIX_OFF=space.rival_budget_world: FAILS, the money is back to the old value)
"""
import re
import time

from twoinst import Checks, start


def sp(con, *a):
    return con.call('coop.space ' + ' '.join(str(x) for x in a))['out'].strip()


def money(con):
    m = re.search(r'money=(-?\d+)', sp(con, 'rivalry'))
    return int(m.group(1)) if m else None


def field(con, key):
    m = re.search(r'\b' + key + r'=(\d+)', sp(con, 'rivalry'))
    return int(m.group(1)) if m else None


def rivals(con):
    return re.search(r'\brivals=1\b', sp(con, 'rivalry')) is not None


def wait(pred, timeout=20, step=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        v = pred()
        if v:
            return v
        time.sleep(step)
    return pred()


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(3)
    if (money(host) or 0) < 10000:
        host.call('coop.res add money 20000')
        time.sleep(2)
    print('  host:', sp(host, 'rivalry mode rivals'))
    ck(wait(lambda: rivals(host) and rivals(guest), 15), 'rivals mode on both games')
    time.sleep(4)  # the split and the first reports
    m0 = money(guest)
    print(f'  guest money in rivals mode: {m0}; host {money(host)}', flush=True)
    ck(m0 is not None, f'the guest has its own budget ({m0})')
    restored0 = field(guest, 'restored') or 0
    print('  guest:', sp(guest, 'away 1'))
    time.sleep(2)
    print('  guest wins a contract while away:', sp(guest, 'contracts result 5000 0'))
    time.sleep(2)
    print('  guest:', sp(guest, 'away 0'))
    ok = wait(lambda: money(guest) == m0 + 5000, 10)
    print(f'  guest back: money {money(guest)}', flush=True)
    ck(ok, f'back in Space the guest has the reward: {m0} + 5000 ({money(guest)})')
    time.sleep(6)  # an ask of the host would be answered by now
    m1 = money(guest)
    print('  guest:', sp(guest, 'rivalry').splitlines()[-1], flush=True)
    ck(m1 == m0 + 5000, f'6 s later the guest still has {m0 + 5000} ({m1}): its budget was not reset to the host\'s copy')
    ck((field(guest, 'restored') or 0) == restored0, f'no budget was put back by the host '
                                                    f'(restored {restored0} -> {field(guest, "restored")})')
    ck(wait(lambda: abs((money(guest) or 0) - m1) < 1, 5), 'the guest\'s money is steady')

    print('  host:', sp(host, 'rivalry mode allies'))
    wait(lambda: not rivals(host) and not rivals(guest), 15)
    sp(guest, 'away 0')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
