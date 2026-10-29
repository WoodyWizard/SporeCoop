#!/usr/bin/env python3
"""Civilization: the shared money and the roles across a crossing spend and a rejoin
(BUG-0106, BUG-0049, BUG-0064).

1. Crossing spends (BUG-0106): the host sets the shared money to 150, then host and guest spend
   100 each in the same step (`coop.res add money -100`, the same r.set path as a purchase). The
   game's own money (cCivilization::mWealth as `coop.res` reads it) must never go below 0 on
   either machine, and both end equal.
2. Rejoin (BUG-0049, BUG-0064): the guest picks the military role, leaves, earns 40000 alone
   (`coop.res add money 40000` while offline) and joins again (it gets the same id). The host's
   money must not jump by the offline earning, and the host must not keep the old player's role
   for the newcomer (`coop.role` on the host lists it as none, like the guest's own window).
3. A spend at another player's join (BUG-0220): both totals at 1000, then on the guest
   `coop.res joinsim add money -500` (what a third player's join runs on a guest, then the spend,
   in one step). After 3 s the game's money on both machines must be 500 (0.7.3.0: the guest's
   purchase was undone by the next host total, the host never counted it: both 1000).

--nofix: the code before the fixes (`coop.res fix reset 0`, `coop.res fix floor 0`,
`coop.role fix 0`, `coop.fix res.join_keep off`): must FAIL.
--only-join: step 3 only (with --nofix: only res.join_keep off, the twin run of BUG-0220).

  SPORE_LANE=7 python3 tools/tests/coop_civ_shared.py [--nofix]
"""
import argparse
import re
import threading
import time

import lane
from twoinst import Checks, start, wait_guest_in


def money(con):
    m = re.search(r'shared money=(-?[\d.]+)', con.call('coop.res')['out'])
    return float(m.group(1)) if m else None


def role_lines(con):
    return {int(i): r for i, r in re.findall(r'^role (\d+) \S+ (\w+)', con.call('coop.role')['out'], re.M)}


def both(host, guest, cmd):
    """the same command on both machines in the same moment"""
    out = {}
    ts = [threading.Thread(target=lambda c=c, k=k: out.__setitem__(k, c.call(cmd)['out'])) for k, c in
          (('host', host), ('guest', guest))]
    for t in ts:
        t.start()
    for t in ts:
        t.join()
    return out


def sample_min(host, guest, seconds):
    lo = {'host': 1e18, 'guest': 1e18}
    t0 = time.time()
    while time.time() - t0 < seconds:
        for k, c in (('host', host), ('guest', guest)):
            v = money(c)
            if v is not None:
                lo[k] = min(lo[k], v)
        time.sleep(0.05)
    return lo


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nofix', action='store_true')
    ap.add_argument('--only-join', action='store_true')
    a = ap.parse_args()
    host, guest = start('civ')
    ck = Checks()
    for c in (host, guest):
        if not a.only_join:
            for f in ('coop.res fix reset', 'coop.res fix floor', 'coop.role fix'):
                print(' ', c.call(f'{f} {0 if a.nofix else 1}')['out'].strip())
        print(' ', c.call(f'coop.fix res.join_keep {"off" if a.nofix else "on"}')['out'].strip())
    time.sleep(2)
    if not a.only_join:
        steps_1_2(host, guest, ck)
    join_spend(host, guest, ck)
    for c in (host, guest):
        c.call('coop.fix res.join_keep on')
    ck.faults(host=host, guest=guest)
    return ck.result()


def join_spend(host, guest, ck):
    """3. a guest's spend right at another player's join is counted once (BUG-0220)"""
    h = money(host)
    ck(h is not None, f'the host has a shared money total ({h})')
    if h is None:
        return
    host.call(f'coop.res add money {1000 - h:.1f}')
    time.sleep(3)
    h, g = money(host), money(guest)
    print(f'  shared money set: host {h} guest {g}', flush=True)
    ck(g is not None and abs(h - 1000) < 5 and abs(g - 1000) < 5, f'both start from 1000 (host {h}, guest {g})')
    print(' ', guest.call('coop.res joinsim add money -500')['out'].strip().replace('\n', ' | '))
    time.sleep(3)
    h, g = money(host), money(guest)
    print(f'  after the guest spent 500 at a join: host {h} guest {g}', flush=True)
    ck(g is not None and abs(g - 500) < 5, f'the guest\'s purchase stays made ({g})')
    ck(h is not None and abs(h - 500) < 5, f'the host counted the guest\'s spend once ({h})')


def steps_1_2(host, guest, ck):

    # 1. crossing spends
    h = money(host)
    ck(h is not None, f'the host has a shared money total ({h})')
    if h is None:
        return
    host.call(f'coop.res add money {150 - h:.1f}')
    time.sleep(2.5)
    h, g = money(host), money(guest)
    print(f'  shared money set: host {h} guest {g}', flush=True)
    ck(g is not None and abs(h - g) < 5 and h < 160, f'both start from about 150 (host {h}, guest {g})')
    both(host, guest, 'coop.res add money -100')
    lo = sample_min(host, guest, 2.0)
    h, g = money(host), money(guest)
    print(f'  after both spent 100: host {h} guest {g}, lowest seen host {lo["host"]} guest {lo["guest"]}', flush=True)
    ck(lo['host'] >= 0 and lo['guest'] >= 0, f'the money never went below 0 (lowest host {lo["host"]}, guest {lo["guest"]})')
    ck(h is not None and g is not None and abs(h - g) < 5, f'both machines show the same money ({h}, {g})')

    # 2. rejoin: role and offline earnings
    print(' ', guest.call('coop.role 1')['out'].strip().replace('\n', ' | '))
    time.sleep(1.5)
    roles = role_lines(host)
    print('  host roles before the leave:', roles, flush=True)
    ck(roles.get(1) == 'military', 'the host knows the guest governs as military')
    guest.call('coop.leave')
    time.sleep(2)
    h0 = money(host)
    print(' ', guest.call('coop.res add money 40000')['out'].strip().replace('\n', ' | '))
    guest.call(lane.JOIN)
    t0 = time.time()
    hmax = h0
    while time.time() - t0 < 12:  # the offline change would arrive right after HELLO
        v = money(host)
        if v is not None:
            hmax = max(hmax, v)
        time.sleep(0.1)
    print(f'  host money before the rejoin {h0}, highest after it {hmax}', flush=True)
    ck(hmax - h0 < 10000, f'the guest\'s offline earning of 40000 did not reach the host\'s money (+{hmax - h0:.0f})')
    wait_guest_in(host, guest, 150)
    time.sleep(3)
    hr, gr = role_lines(host), role_lines(guest)
    print('  roles after the rejoin: host', hr, 'guest', gr, flush=True)
    ck(hr.get(1, 'none') == 'none', f'the newcomer with id 1 has no role on the host ({hr.get(1)})')
    ck(hr.get(1, 'none') == gr.get(1, 'none'), 'host and guest agree on the newcomer\'s role')
    h, g = money(host), money(guest)
    print(f'  money after the rejoin: host {h} guest {g}', flush=True)
    ck(h is not None and g is not None and abs(h - g) < 5, f'the guest took the host\'s total ({h}, {g})')


if __name__ == '__main__':
    raise SystemExit(main())
