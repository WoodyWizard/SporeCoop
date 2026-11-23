#!/usr/bin/env python3
"""Space: a disabled ship can not jump away (BUG-1490, audit SPC-16).

A lethal hit next to a partner disables the ship (down=1) until the partner repairs it or 20 s pass (then the native
death). The co-op jump (Space page) and travel refuse while disabled (BUG-0063, BUG-1045), but two ways went past it:
the "Jump there" button of a co-op contract's row (space_contracts.cpp jump_to -> `jumpto`) and the in-system step of
the finale's "Fly to", which runs up to 30 s after its own disabled check (travel_to_star).

1. Two ships in the home system; the guest has the interstellar drive; nobody can repair (`repairdist 0`).
2. The guest asks the finale's "Fly to" for a star nearby with its step 6 s later (`finale goto <star> no delay 6`), then
   takes a lethal hit: disabled.
3. While disabled it presses a contract row's "Jump there" (`contracts jump <star>`): refused ("the ship is disabled").
   Over 15 s (the "Fly to" step comes in that time) the guest's ship stays in its home system.
   0.7.16.1 (SPORE_FIX_OFF=space.down_no_jump): the ship jumps to the other star (FAIL).

  SPORE_LANE=2 python3 tools/tests/coop_space_down_jump.py
"""
import re
import time

from coop_space_death import field, ok_until, star_of, stat
from harness import sp
from twoinst import Checks, keyframe, start


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(5)
    _, home = star_of(guest)
    ck(stat(guest, 'partners_here') >= 1, 'both ships are in the same star system')
    print('  guest drive:', sp(guest, 'tool add interstellardrive'), flush=True)
    free = [int(x) for x in re.findall(r'free (\d+)', sp(guest, 'allies free'))]
    ck(bool(free), f'stars near home: {free[:3]}')
    if not free:
        return ck.result()
    other = free[1] if len(free) > 1 else free[0]
    print(' ', sp(guest, 'repairdist', 0), flush=True)

    # 2. "Fly to" queued, then the lethal hit
    print('  guest fly to:', sp(guest, 'finale goto', other, 'no', 'delay', 6), flush=True)
    print('  hit:', sp(guest, 'hit', 100000), flush=True)
    down = field(sp(guest, 'hp'), 'down') == 1
    ck(down, 'a lethal hit next to a partner disables the ship')
    if not down:
        return ck.result()

    # 3. the contract row's "Jump there"
    j = sp(guest, 'contracts jump', other).strip()
    print(f'  guest Jump there ({other}): {j}', flush=True)
    ck('disabled' in j, f'"Jump there" is refused while the ship is disabled ({j})')
    stars = set()
    t0 = time.time()
    while time.time() - t0 < 15 and field(sp(guest, 'hp'), 'down') == 1:
        stars.add(star_of(guest)[1])
        time.sleep(1)
    print(f'  guest stars while disabled: {sorted(stars)} (home {home}) | ' + sp(guest, 'finale').strip()[-160:], flush=True)
    ck(stars == {home}, f'the disabled ship stayed in its home system (seen {sorted(stars)})')
    keyframe('down_jump', 'the guest\'s disabled ship in its home system after "Jump there" and "Fly to": the same '
             'system, the disabled state shown', players={'host': host, 'guest': guest})
    print(' ', sp(guest, 'repairdist', 60), flush=True)
    hp, t = ok_until(guest, lambda h: field(h, 'state') == 0 and field(h, 'down') == 0, 60)
    print(f'  after the disabled state ({t:.0f} s): {hp}', flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
