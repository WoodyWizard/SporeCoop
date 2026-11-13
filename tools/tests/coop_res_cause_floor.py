#!/usr/bin/env python3
"""BUG-1009 (audit 0.7.14 NET-5): a guest's change cut at 0 by the shared floor must not show up as the host's earning.

Tribe fixture (the Tribe food: no income every frame, like coop_res_cause.py), the guest behind a proxy with a
300 ms one-way delay (SPORE_NET="delay=300"), so both players spend the shared food before either hears of the
other's purchase:
1. the shared food is set to 400 on both games;
2. the host spends 150 (its game: 250) and at once the guest spends 350 (its game: 50); the guest's change reaches the
   host when the pool holds 250: the floor takes 250 of it, the food is 0 on both games.
3. Over the next 8 s the guest's game must not say that the host earned food (the notification toasts on screen,
   `coop.status`), and both games show 0.
0.7.13 (SPORE_FIX_OFF=shared.cause_clamped): the host counted the guest's -350 in full, its next total's own part was
0 - 250 + 350 = +100: "<host> earned 100 food (shared)" on the guest.
"""
import os
import re
import threading
import time

from twoinst import Checks, start


def food(con):
    m = re.search(r'shared food=(-?[\d.]+)', con.call('coop.res')['out'])
    return float(m.group(1)) if m else None


def toasts(con):
    """the texts of the notification toasts on screen (a toast's box moves as others come and go: the text only)"""
    return {ln.split(' | ', 1)[-1] for ln in con.call('coop.status')['out'].splitlines() if ln.startswith('toast ')}


def main():
    if not os.environ.get('SPORE_NET'):
        print('RESULT: FAIL (needs SPORE_NET="delay=300": the purchases must cross on the network)')
        return 1
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(3)
    f0 = food(host) or 0
    setup = round(400 - f0)
    host.call(f'coop.res add food {setup}')
    t0 = time.time()
    while time.time() - t0 < 10 and not (food(host) == 400 and food(guest) == 400):
        time.sleep(0.3)
    ck(food(host) == 400 and food(guest) == 400, f'setup: 400 food on both (host {food(host)}, guest {food(guest)})')
    time.sleep(3)  # the setup's notices are gone from the toasts we look at below
    before = set(toasts(guest))
    th = threading.Thread(target=lambda: print('  host:', host.call('coop.res add food -150')['out'].strip()))
    th.start()
    time.sleep(0.05)
    print('  guest:', guest.call('coop.res add food -350')['out'].strip(), flush=True)
    th.join()
    earned = []
    t0 = time.time()
    while time.time() - t0 < 8:
        for ln in toasts(guest):
            m = re.search(r'earned (\d+) food', ln)
            if ln not in before and m and (int(m.group(1)) != setup or setup == 100) and ln not in earned:
                earned.append(ln)  # (the setup's own earning may still be on screen)
        time.sleep(0.2)
    fh, fg = food(host), food(guest)
    print(f'  food after both purchases: host {fh}, guest {fg}', flush=True)
    print('  guest toasts:', [t for t in toasts(guest) if t not in before], flush=True)
    print('  host toasts:', sorted(toasts(host)), flush=True)
    if fh is not None and fh > 1:
        print('  the guest\'s purchase came before the host\'s total: no floor was reached')
        print('RESULT: SKIP (the purchases did not cross)')
        return 0
    ck(fh is not None and fg is not None and fh <= 1 and fg <= 1, f'the food is 0 on both (host {fh}, guest {fg})')
    ck(not earned, f'the guest\'s game names no earning of the host ({earned})')
    host.call('coop.res add food 300')  # leave food for the next test of the lane
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
