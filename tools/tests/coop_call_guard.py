#!/usr/bin/env python3
"""BUG-1047: a module's call guard left set by a fault the game caught does not switch that module's relay off.

Modules hold a flag around their own game calls ("we are replaying a partner's change: send nothing"). A fault inside
the call that the game's handler catches unwinds past the clear: the flag stayed set and the module sent nothing for
the rest of the session. call_guard.h registers every such guard; the frame's swap guard tick clears one still set.

Two games on the space_empire fixture (Space diplomacy, as coop_space_diplomacy.py):
1. Guest: `coop.guard unwind space_diplomacy.applying`: the next replay of a partner's change ends as a caught fault
   ends it (the guard stays set).
2. Host: a gift to an empire both have met; the guest's game replays it (the guarded call, cut short).
3. Guest: flies to the home star of an empire nobody has met yet: its own game makes first contact (the game's
   contact call through the module's hook, no command of ours sends it). The host's game must have met that empire
   too (the host's relation flags, the game's own record), which needs the guest's relay.
SPORE_FIX_OFF=core.call_guard_reset must FAIL (the guest's guard stays set, the contact is never sent).

  SPORE_LANE=6 python3 tools/tests/coop_call_guard.py"""
import re
import time

from coop_space_diplomacy import FIXTURE, GIFT, flags, near, rec, sp, space_planet, wait
from twoinst import Checks, start

GUARD = 'space_diplomacy.applying'


def guard(con):
    out = con.call('coop.guard')['out']
    m = re.search(r'^' + re.escape(GUARD) + r' set=(\d) cleared=(\d+)( unwind=\d+)?$', out, re.M)
    return (int(m.group(1)), int(m.group(2)), bool(m.group(3))) if m else None


def main():
    host, guest = start(FIXTURE, planet=space_planet())
    ck = Checks()
    time.sleep(4)
    ck('diplomacy: on' in sp(guest, 'features'), 'guest: the diplomacy feature is on')
    g0 = guard(guest)
    print(f'  guest {GUARD}: set/cleared/armed {g0}', flush=True)
    if not ck(g0 is not None and g0[0] == 0, f'the guest has the guard {GUARD} registered and clear'):
        return ck.result()
    hn = near(host)
    known = [x for x in hn if int(x[3])]
    unmet = [x for x in near(guest) if not int(x[3])]
    if not ck(known and unmet, f'an empire both have met ({len(known)}) and one nobody has met ({len(unmet)})'):
        return ck.result()
    p = known[0][0]
    q, qhome = unmet[0][0], unmet[0][1]

    # 1-2: the guest's replay of the host's gift ends like a caught fault
    print('  guest:', guest.call('coop.guard unwind ' + GUARD)['out'].strip(), flush=True)
    before = rec(guest, p)
    print('  host gift:', sp(host, 'diplomacy apply', p, GIFT, 1), flush=True)
    ck(wait(lambda: rec(guest, p) != before and rec(guest, p) == rec(host, p)),
       f'the host\'s gift to {p} was replayed in the guest\'s game')
    ck(wait(lambda: guard(guest) and not guard(guest)[2], 10), 'the replay was cut short like a fault (the unwind used)')
    time.sleep(1.0)
    g1 = guard(guest)
    print(f'  guest {GUARD} 1 s later: set/cleared/armed {g1}', flush=True)

    # 3: the guest's own first contact (the game's call through the hook) reaches the host's game
    for _ in range(5):  # a badge or caption cinematic blocks the jump: confirm it
        r = sp(guest, 'jumpto', qhome)
        print(f'  guest flies to {q} at star {qhome}:', r, flush=True)
        if 'cinematic' not in r:
            break
        guest.call('key enter')
        time.sleep(2)
    met = wait(lambda: flags(guest, q) & 16, 40)
    ck(met, f'guest: its game made first contact with {q} on arriving at its star')
    ok = met and wait(lambda: flags(host, q) & 16, 20)
    print(f'  host {q}: {rec(host, q)} | guest: {rec(guest, q)}', flush=True)
    print('  guest diplomacy log:', sp(guest, 'diplomacy log').splitlines()[-3:], flush=True)
    ck(ok, f'the host\'s game has met {q} too: the guest\'s diplomacy relay works after the fault')
    print(f'  guest {GUARD} at the end: set/cleared/armed {guard(guest)}', flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
