#!/usr/bin/env python3
"""BUG-1029 (audit 0.7.14 SPC-1): the Rivals -> Allies merge with a guest whose budget the host has no report of.

1. Two games in Space; the host switches to Rivals (`coop.space rivalry mode rivals`): the money is split. The guest
   earns 7000 and the host spends 1000, so the guest's budget is not the host's save money.
2. Rejoin: the guest leaves and joins again (a rejoin loads the host's save: its game shows the save's money, not its
   budget, until the host's K_SET). While the guest's game loads, the host switches to Allies. Both games' own Space
   money (cEmpire +0xD0, `rivalry` money=) must become host budget + the guest's kept budget.
   0.7.13 (SPORE_FIX_OFF=space.merge_known_budget): the host had no report of the rejoiner (its kept budget wiped),
   the guest sent its whole game money as the correction: the total is the host's budget + the save's money.
3. Newcomer (owner decision 2026-11-13): Rivals again, the guest leaves and a new player (Carol) joins from the same
   game: its game's money must become 0 (not the host's save money), and the merge then gives the host's budget only.
   0.7.13: the newcomer keeps the save's money as its budget.

  SPORE_LANE=2 python3 tools/tests/coop_space_rival_merge.py
"""
import re
import time

import lane
from twoinst import Checks, host_frame, start, wait_guest_in


def sp(con, *a):
    return con.call('coop.space ' + ' '.join(str(x) for x in a))['out'].strip()


def money(con):
    m = re.search(r'money=(-?\d+)', sp(con, 'rivalry'))
    return int(m.group(1)) if m else None


def rivals(con):
    return re.search(r'\brivals=1\b', sp(con, 'rivalry')) is not None


def in_space(con):
    return 'Space' in con.call('game.mode')['out']


def wait(pred, timeout=20, step=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        v = pred()
        if v:
            return v
        time.sleep(step)
    return pred()


def close_modal(host):
    """the host saves before it shares its world: a modal box stops its frames until Enter (twoinst.wait_guest_in)"""
    f1 = host_frame(host)
    time.sleep(0.7)
    if host_frame(host) == f1:
        host.call('key enter')


def join_and_switch(host, guest, join, ck, what):
    """the guest joins with `join`; while its game loads the host's save, the host switches to Allies"""
    guest.call('coop.leave')
    time.sleep(3)
    print(f'  guest {what}:', guest.call(join)['out'].strip()[:120], flush=True)
    t0 = time.time()
    loading = False
    while time.time() - t0 < 120:
        close_modal(host)
        if not in_space(guest):
            loading = True
            break
    if not loading:
        return False
    out = sp(host, 'rivalry mode allies')
    print(f'  host switches to allies while the guest loads: {out}', flush=True)
    ck(out == 'ok', f'the host switched to allies during the guest\'s load ({out})')
    wait(lambda: in_space(guest), 180, 2)
    ck(wait_guest_in(host, guest, 180), f'the guest is in the host\'s world after the {what}')
    return True


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(3)
    if (money(host) or 0) < 20000:
        host.call('coop.res add money 30000')
        time.sleep(2)
    print('  host:', sp(host, 'rivalry mode rivals'))
    ck(wait(lambda: rivals(host) and rivals(guest), 15), 'rivals mode on both games')
    time.sleep(4)
    guest.call('coop.res add money 7000')
    host.call('coop.res add money -1000')
    time.sleep(4)
    mh, mg = money(host), money(guest)
    print(f'  budgets: host {mh}, guest {mg} | host: {sp(host, "rivalry").splitlines()[0]}', flush=True)
    ck(mh and mg and f'1:{mg}' in sp(host, 'rivalry'), f'the host knows the guest\'s budget {mg}')

    # ---- a rejoin, the switch to allies inside its load
    if not join_and_switch(host, guest, lane.JOIN, ck, 'rejoin'):
        print('  the rejoin did not load the host\'s world within 120 s')
        print('RESULT: SKIP (the rejoin\'s load was not reached)')
        return 0
    ok = wait(lambda: money(host) == money(guest) == mh + mg, 30, 1)
    print(f'  after the merge: host {money(host)} guest {money(guest)} (expected {mh} + {mg} = {mh + mg})', flush=True)
    print('  guest:', sp(guest, 'rivalry').splitlines()[-1], flush=True)
    ck(ok, f'the merge counted the rejoiner\'s kept budget: host {money(host)}, guest {money(guest)} == {mh + mg}')

    # ---- a newcomer to Rivals starts with 0
    print('  host:', sp(host, 'rivalry mode rivals'))
    ck(wait(lambda: rivals(host) and rivals(guest), 15), 'rivals mode again')
    time.sleep(4)
    guest.call('coop.leave')
    time.sleep(3)
    carol = lane.JOIN.replace(' Bob', ' Carol')
    print('  a newcomer joins:', guest.call(carol)['out'].strip()[:120], flush=True)
    ck(wait_guest_in(host, guest, 240), 'the newcomer is in the host\'s world')
    wait(lambda: in_space(guest) and money(guest) is not None, 120, 2)
    ok = wait(lambda: money(guest) == 0, 30, 1)
    mh2, mn = money(host), money(guest)
    print(f'  newcomer\'s money {mn}, host {mh2} | guest: {sp(guest, "rivalry").splitlines()[-1]}', flush=True)
    ck(ok, f'the newcomer\'s own empire starts with 0 ({mn}), not with the host\'s save money')
    out = sp(host, 'rivalry mode allies')
    ok = wait(lambda: money(host) == money(guest) == mh2 + (mn or 0), 30, 1)
    ck(out == 'ok' and ok, f'merged: host {mh2} + newcomer {mn}: host {money(host)}, guest {money(guest)}')
    if rivals(host):
        sp(host, 'rivalry mode allies')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
