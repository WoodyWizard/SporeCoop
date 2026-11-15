#!/usr/bin/env python3
"""Space Rivals: the host gives a player's kept state back to the same game, not to its name (BUG-1107), and a
reported budget outside the game's money range does not reach the Allies merge (BUG-1110).

The host keeps each guest's colonies, cargo hold, badges and Rivals budget across a rejoin. 0.7.14 keyed them by the
session name, so two players of the same name ("Player", the Spore default) who rejoin in the other order swapped them.
Another game under the same name is made here by the test seam `coop.space keptkey other on` (the next joins of the
guest's game send a fresh rejoin token, as a second "Bob" on another PC does; off: its own token again).

1. Two games in Space, Rivals mode; the guest earns 7000: its budget mg is not the host's.
2. The guest leaves; another game named "Bob" joins (`keptkey other on`): its own empire starts with 0 (a newcomer to
   Rivals), not with Bob's kept budget mg. SPORE_FIX_OFF=space.kept_token: it gets mg (FAIL).
3. The other "Bob" leaves, Bob's own game joins again (`keptkey other off`): its game's money is mg again.
   SPORE_FIX_OFF=space.kept_token: the other game's budget (0) replaced it under the name (FAIL).
4. BUG-1110 (robustness): a budget report above the game's money range (test seam `coop.space rivalry fakemoney`)
   is ignored; the host switches to Allies: both games' own money is host budget + mg.
   SPORE_FIX_OFF=space.money_range: the sum is off (FAIL).

  SPORE_LANE=6 python3 tools/tests/coop_space_kept_key.py
"""
import re
import time

import lane
from coop_space_rival_merge import close_modal, in_space, money, rivals, sp, wait
from twoinst import Checks, start, wait_guest_in

FAKE = 100000000  # one above the game's money range


def budget_on_host(host, gid):
    m = re.search(r'\bbudgets((?: \d+:-?\d+)*)', sp(host, 'rivalry'))
    got = dict(re.findall(r' (\d+):(-?\d+)', m.group(1))) if m else {}
    return int(got[str(gid)]) if str(gid) in got else None


def rejoin(host, guest, ck, what, timeout=240):
    """the guest leaves and joins again (lane.JOIN: the name Bob); True when it is in the host's Space world with its
    budget known (K_SET answered)"""
    guest.call('coop.leave')
    time.sleep(3)
    print(f'  {what}: ' + guest.call(lane.JOIN)['out'].strip()[:120], flush=True)
    t0 = time.time()
    while time.time() - t0 < 60 and in_space(guest):  # the guest's game leaves Space for the host's save
        close_modal(host)
    ok = wait_guest_in(host, guest, timeout)
    ck(ok, f'{what}: the guest is in the host\'s world')
    ok = ok and wait(lambda: in_space(guest) and rivals(guest) and money(guest) is not None, 120, 2)
    time.sleep(6)  # K_ASK -> K_SET, the first reports
    return bool(ok)


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(3)
    if (money(host) or 0) < 20000:
        host.call('coop.res add money 30000')
        time.sleep(2)
    print('  host:', sp(host, 'rivalry mode rivals'), flush=True)
    ck(wait(lambda: rivals(host) and rivals(guest), 15), 'rivals mode on both games')
    time.sleep(4)
    guest.call('coop.res add money 7000')
    time.sleep(4)
    gid = int(re.search(r'\bid=(\d+)', guest.call('coop.status')['out']).group(1))
    mh, mg = money(host), money(guest)
    print(f'  budgets: host {mh}, guest {mg} (id {gid}) | host keys: ' + sp(host, 'keptkey').replace('\n', ' |'), flush=True)
    ck(mh and mg and budget_on_host(host, gid) == mg, f'the host knows the guest\'s budget {mg}')

    # 2. another game named Bob
    print('  ' + sp(guest, 'keptkey other on'), flush=True)
    if not rejoin(host, guest, ck, 'another game named Bob joins'):
        return ck.result()
    mo = money(guest)
    print(f'  the other Bob\'s money {mo} | host: ' + sp(host, 'rivalry').splitlines()[0] + ' | keys: '
          + sp(host, 'keptkey').replace('\n', ' |'), flush=True)
    ck(mo == 0, f'another game under the name Bob starts with its own 0, not Bob\'s kept budget {mg} (it has {mo})')

    # 3. Bob's own game again
    print('  ' + sp(guest, 'keptkey other off'), flush=True)
    if not rejoin(host, guest, ck, 'Bob\'s own game joins again'):
        return ck.result()
    ok = wait(lambda: money(guest) == mg, 20, 1)
    print(f'  Bob\'s money back: {money(guest)} (kept {mg}) | host: ' + sp(host, 'rivalry').splitlines()[0], flush=True)
    ck(ok, f'Bob\'s own game gets its kept budget {mg} back (it has {money(guest)})')

    # 4. BUG-1110: a budget outside the game's range
    mh, mg = money(host), money(guest)
    gid = int(re.search(r'\bid=(\d+)', guest.call('coop.status')['out']).group(1))
    print('  guest: ' + sp(guest, f'rivalry fakemoney {FAKE} 40'), flush=True)
    time.sleep(5)
    hb = budget_on_host(host, gid)
    print(f'  the host\'s copy of the guest\'s budget: {hb} (real {mg}) | '
          + re.search(r'money_refused=\d+', sp(host, 'rivalry')).group(0), flush=True)
    out = sp(host, 'rivalry mode allies')
    ok = wait(lambda: money(host) == money(guest) == mh + mg, 30, 1)
    print(f'  after the merge: host {money(host)} guest {money(guest)} (expected {mh} + {mg} = {mh + mg})', flush=True)
    ck(out == 'ok' and ok, f'the merge counts the guest\'s real budget: host {money(host)}, guest {money(guest)} == {mh + mg}')
    if rivals(host):
        sp(host, 'rivalry mode allies')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
