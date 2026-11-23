#!/usr/bin/env python3
"""A guest leaving its Tribe world does not stand for seconds in the world reset (BUG-9312, the analogue of BUG-9272
that IltonBJSilva found and fixed in their fork, PR #212).

The destroy hook's stale-reference fixes (BUG-0456 forget_sender, BUG-0930 forget_last_attacker) walk every object of
the world for each creature or citizen destroyed, with readable() (VirtualQuery: a system call on Windows) for each
live animal or citizen: a world reset that destroys D of A creatures costs ~1.5 * D * A readable() calls. Under Wine a
VirtualQuery is cheap, so the guest's readable() is slowed to ~50 us a call for the leave (`coop.perf readable extra
<n>`, BUG-0791's cost knob: n more VirtualQuery per call), as on a slow Windows PC; the fork measured 13-16 s there.

tribe fixture, the guest:
  1. its readable() is made ~50 us a call; it leaves for the galaxy menu (`coop.menu`: the game's simulator reset,
     SporeCoop::GalaxyMenu_SimReset, tears the world down);
  2. its `coop.status` reports the reset (hooked, timed with the performance counter): at least 30 creatures or
     citizens destroyed in it (the world had a population);
  3. the time the destroy hook's walks took inside the reset is under 0.5 s and the whole reset under 10 s.
SPORE_FIX_OFF=game.reset_no_walks (suite reset_stall_off, the twin): a walk for every creature destroyed: the walks
take seconds: check 3 FAILs.

  SPORE_LANE=4 python3 tools/tests/coop_reset_stall.py
"""
import re
import time

from twoinst import Checks, start

US_PER_CALL = 50.0  # the readable() cost to emulate (a slow Windows PC)


def last_reset(con):
    try:
        out = con.call('coop.status').get('out') or ''
    except OSError:
        return None
    m = re.search(r'world resets (\d+) \(hooked (\d)\), the last: secs=(-?[\d.]+) creatures=(\d+) walked=(\d+) '
                  r'walk_secs=([\d.]+) skipped=(\d+) readable=(-?\d+)', out)
    if not m:
        return None
    k = ('n', 'hooked', 'secs', 'creatures', 'walked', 'walk_secs', 'skipped', 'readable')
    return {a: float(v) if '.' in v else int(v) for a, v in zip(k, m.groups())}


def main():
    host, guest = start('tribe')
    ck = Checks()
    r0 = last_reset(guest)
    if not ck(r0 is not None and r0['hooked'] == 1, f'the guest\'s world reset is hooked ({r0})'):
        return ck.result()
    m = re.search(r'us_per_call=([\d.]+)', guest.call('coop.perf readable')['out'])
    base = float(m.group(1)) if m else 1.0
    extra = max(0, min(2000, int(US_PER_CALL / max(base, 0.02))))
    out = guest.call(f'coop.perf readable extra {extra}')['out'].strip()
    print(f'  readable() {base:.3f} us a call here; extra {extra}: {out}', flush=True)
    r = None
    try:
        print('  guest: ' + guest.call('coop.menu')['out'].strip(), flush=True)
        t0 = time.time()
        while time.time() - t0 < 180:
            r = last_reset(guest)
            if r and r['n'] > r0['n']:
                break
            time.sleep(1)
    finally:
        try:
            guest.call('coop.perf readable extra 0')
        except OSError:
            pass
    print(f'  the reset: {r}', flush=True)
    ok = r is not None and r['n'] > r0['n']
    ck(ok, '1. the guest left its world through the game\'s simulator reset')
    if ok:
        ck(r['creatures'] >= 30, f'2. the reset destroyed {r["creatures"]} creatures or citizens (at least 30)')
        ck(r['walk_secs'] < 0.5 and r['secs'] < 10,
           f'3. the destroy hook\'s walks took {r["walk_secs"]:.2f} s of the reset\'s {r["secs"]:.2f} s '
           f'({r["walked"]} destroys walked, {r["skipped"]} not, {r["readable"]} readable calls)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
