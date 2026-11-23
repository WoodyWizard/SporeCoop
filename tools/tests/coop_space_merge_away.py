#!/usr/bin/env python3
"""BUG-1414 (audit 0.7.15.5 SPC-2): the switch from Rivals to Allies while the guest is outside Space.

The switch needs only the host in Space. A guest in an editor or an adventure entered from Space keeps its merge
pending (no Space money to read there), but its cEmpire lives on and shared.cpp took the host's shared totals into
it. Back in Space the guest's correction was that total minus the host's credit: the host added its own money (and
the other guests' budgets) once more.

1. Two games in Space; the host switches to Rivals. The guest earns 7000, the host spends 1000, so the two budgets
   differ; the host knows the guest's budget (its K_MONEY).
2. The guest acts as outside Space (`coop.space away 1`: what an editor or an adventure entered from Space gives the
   mod, the Space world stays). The host switches to Allies; the guest stays away for 6 s (the host's totals arrive).
3. The guest is back (`away 0`). Both games' Space money (cEmpire +0xD0, `rivalry` money=) must be the sum of the two
   budgets and stay there.
   0.7.16 (SPORE_FIX_OFF=space.merge_hold_totals): the host's money is its budget twice plus the guest's (FAIL).

  SPORE_LANE=2 python3 tools/tests/coop_space_merge_away.py
"""
import re
import time

from harness import sp, waiter
from twoinst import Checks, keyframe, start


def money(con):
    m = re.search(r'money=(-?\d+)', sp(con, 'rivalry'))
    return int(m.group(1)) if m else None


def field(con, key):
    m = re.search(r'\b' + key + r'=(\d+)', sp(con, 'rivalry'))
    return int(m.group(1)) if m else None


def rivals(con):
    return re.search(r'\brivals=1\b', sp(con, 'rivalry')) is not None


wait = waiter(timeout=20, step=0.5, final=True)


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(3)
    if (money(host) or 0) < 20000:
        host.call('coop.res add money 30000')
        time.sleep(2)
    print('  host:', sp(host, 'rivalry mode rivals'))
    ck(wait(lambda: rivals(host) and rivals(guest), 15), 'rivals mode on both games')
    time.sleep(4)  # the split and the first reports
    guest.call('coop.res add money 7000')
    host.call('coop.res add money -1000')
    time.sleep(4)
    mh, mg = money(host), money(guest)
    print(f'  budgets: host {mh}, guest {mg} | host: {sp(host, "rivalry").splitlines()[0]}', flush=True)
    ck(mh is not None and mg is not None and f'1:{mg}' in sp(host, 'rivalry'), f'the host knows the guest\'s budget {mg}')
    if mh is None or mg is None:
        return ck.result()

    # 2. the switch while the guest is out of Space
    print('  guest:', sp(guest, 'away 1'), flush=True)
    time.sleep(2)
    out = sp(host, 'rivalry mode allies')
    print(f'  host switches to allies: {out}', flush=True)
    ck(out == 'ok', f'the host switched to allies while the guest was away ({out})')
    ck(wait(lambda: not rivals(guest), 15), 'the guest is told allies mode')
    time.sleep(6)  # the host's shared totals (every 2 s) reach the guest while it is away
    print('  guest away:', sp(guest, 'rivalry').splitlines()[-2].strip()[-80:], flush=True)

    # 3. back in Space
    print('  guest:', sp(guest, 'away 0'), flush=True)
    want = mh + mg
    ok = wait(lambda: money(host) == money(guest) == want, 30, 1)
    print(f'  after the merge: host {money(host)} guest {money(guest)} (expected {mh} + {mg} = {want})', flush=True)
    print('  guest:', [l for l in sp(guest, 'rivalry').splitlines() if 'merge' in l][:1], flush=True)
    ck(ok, f'the merge gives both games the two budgets once: host {money(host)}, guest {money(guest)} == {want}')
    time.sleep(6)
    ck(money(host) == money(guest) == want, f'6 s later still {want}: host {money(host)}, guest {money(guest)}')
    keyframe('merge_away', 'after the switch to allies with the guest away: both HUDs show the same Space money, '
             'the sum of the two rival budgets', players={'host': host, 'guest': guest})
    sp(guest, 'away 0')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
