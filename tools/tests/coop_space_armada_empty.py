#!/usr/bin/env python3
"""Space: an armada wave whose raid brings no ships in 30 s starts a new raid, not the same empty one (BUG-1038).

The armada contract's waves are the game's own pirate raids. A fresh (not resumed) wave whose raid brought no ships
in 30 s was logged "again", but its mission was kept: the next ready tick resumed that same raid (30 s more of
waiting on the colony) instead of starting a new one.

1. Both on the space fixture's colony with an armada contract; `contracts hideships 1` before landing: the raid's
   ships never count (the same hook as coop_space_armada_return.py).
2. Wave 1 starts its raid R1 (the game's mission list, `coop.space contracts raids`).
3. 30 s later without ships: PASS when within 10 s R1 is no longer a running raid in the game's mission list and a
   new raid runs on the colony (the wave was not resumed). SPORE_FIX_OFF=space.armada_empty_wave: R1 is resumed and
   stays the colony's raid (FAIL).

  SPORE_LANE=2 python3 tools/tests/coop_space_armada_empty.py
"""
import re
import time

from coop_space_contracts import captions, click_row, sp, st, wait
from coop_ui import Ui
from twoinst import Checks, start


def raids(con):
    return [r[0] for r in re.findall(r'^raid (\S+) state (\d)', sp(con, 'contracts raids'), re.M)]


def wave(con):
    m = re.search(r'wave (\d+)/\d+ on=(\d) ships=(\d) resumed=(\d) .* mission=(\S+) aborted=(\d+) .* resumed_n=(\d+)',
                  sp(con, 'contracts wave'))
    return dict(n=int(m.group(1)), on=int(m.group(2)), ships=int(m.group(3)), resumed=int(m.group(4)),
                mission=m.group(5), aborted=int(m.group(6)), resumed_n=int(m.group(7))) if m else {}


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(5)
    try:
        sp(host, 'contracts period 0')
        sp(host, 'contracts limit 0')
        hstar = re.search(r'star (\d+)', sp(host, 'star')).group(1)
        if re.search(r'star (\d+)', sp(guest, 'star')).group(1) != hstar:
            print('  guest to the host\'s star:', sp(guest, 'jumpto', hstar))
            time.sleep(12)
        uh, ug = Ui(host, 'host'), Ui(guest, 'guest')
        print(' ', sp(host, 'contracts hideships 1'))
        print(' ', sp(host, 'contracts offer armada here'))
        for c in (host, guest):
            sp(c, 'contracts accept')
        ck(wait(lambda: st(host)['state'] == 'active', 15), 'the armada contract is active')
        for ui, c in ((uh, host), (ug, guest)):
            ck(click_row(ui, c, 'Armada at', 'Land'), f'{ui.who}: a click on "Land"')
        ok = wait(lambda: sp(host, 'star').startswith('place 4') and sp(guest, 'star').split()[1] == sp(host, 'star').split()[1], 30)
        ck(ok, f'both on the colony ({sp(host, "star")} / {sp(guest, "star")})')
        t0 = time.time()
        while time.time() - t0 < 40 and not wave(host).get('on'):
            captions(host, uh)
            captions(guest, ug)
            time.sleep(1)
        w0 = wave(host)
        r0 = raids(host)
        print(f'  wave started: {w0} raids {r0}', flush=True)
        ck(w0.get('on') == 1 and w0.get('resumed') == 0 and w0.get('mission') in r0,
           f'wave {w0.get("n")} runs its own fresh raid {w0.get("mission")} (the game\'s raids {r0})')
        first = w0.get('mission')
        # 30 s without ships, then the next ready tick (3 s on the same place)
        t0 = time.time()
        while time.time() - t0 < 45:
            captions(host, uh)
            w = wave(host)
            if w.get('on') and w.get('mission') != first:
                break
            time.sleep(1)
        w1 = wave(host)
        r1 = raids(host)
        print(f'  after the empty raid: {w1} raids {r1} | {sp(host, "contracts wave").strip()}', flush=True)
        ck(first not in r1, f'the empty raid {first} is no longer running in the game ({r1})')
        ck(w1.get('on') == 1 and w1.get('mission') not in (None, first) and w1.get('mission') in r1,
           f'a new raid runs on the colony for the wave ({w1.get("mission")})')
        ck(w1.get('resumed_n') == w0.get('resumed_n'), f'the empty raid was not resumed ({w0.get("resumed_n")} -> {w1.get("resumed_n")})')
        ck(st(host)['state'] == 'active' and w1.get('n') == w0.get('n'), f'the contract goes on at the same wave ({st(host)["state"]})')
    finally:
        sp(host, 'contracts hideships 0')
        sp(host, 'contracts cancel')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
