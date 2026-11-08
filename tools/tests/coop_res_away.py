#!/usr/bin/env python3
"""Shared money while a game is away from the stage: a guest's credit while the host cannot hold it, a guest's
purchase right after an editor visit (BUG-0691, BUG-0692; class BUG-0423: credits dropped or undone).

1. BUG-0691: both games at 1000 Civ money. The host's resources read as unavailable for 8 s (`coop.res block 8`:
   what the host's game gives in a load or the galaxy menu, no player civilization to read); the guest earns 300
   (`coop.res add money 300`, the r.set path of an earning). 6 s after the block ends the game's money (cCivilization
   mWealth) is 1300 on both games. 0.7.10: the host returned before counting and acking it: host 1000, guest 1300.
2. BUG-0692: both at 1000. The guest's shared tick sees an editor's mode for 3 s (`coop.res away 3 money -200`, the
   mode an editor visit gives), and in the first frame back the guest buys for 200. 5 s later both games hold 800.
   0.7.10: the tick dropped the sync state in the editor, the host's next total undid the purchase: both 1000.

--tribe: the same with the Tribe food on the tribe fixture (300 and 200 food; start at 500). In Civ the host's
money changes in every frame (the cities' income), so its total reaches the guest in every frame and the window of
step 2 is one frame: step 2 shows the bug with the Tribe food, whose total comes on a change or every 2 s.

  SPORE_FIX_OFF=res.owed coop_res_away.py [--tribe]       (step 1 fails)
  SPORE_FIX_OFF=res.mode_keep coop_res_away.py --tribe    (step 2 fails)
"""
import re
import sys
import time

from twoinst import Checks, start


RES = 'food' if '--tribe' in sys.argv else 'money'
BASE = 500 if RES == 'food' else 1000


def money(con):
    m = re.search(rf'shared {RES}=(-?[\d.]+)', con.call('coop.res')['out'])
    return float(m.group(1)) if m else None


def both_at(host, guest, v, ck):
    h = money(host)
    if h is None:
        ck(False, 'the host has a shared money total')
        return False
    host.call(f'coop.res add {RES} {v - h:.1f}')
    time.sleep(3)
    h, g = money(host), money(guest)
    print(f'  shared money set: host {h} guest {g}', flush=True)
    return ck(g is not None and abs(h - v) < 5 and abs(g - v) < 5, f'both start from {v} (host {h}, guest {g})')


def main():
    host, guest = start('tribe' if RES == 'food' else 'civ')
    ck = Checks()
    time.sleep(2)
    # 1. a guest's credit while the host's game cannot hold it (BUG-0691)
    if both_at(host, guest, BASE, ck):
        print(' ', host.call('coop.res block 8')['out'].strip())
        time.sleep(0.5)
        print(' ', guest.call(f'coop.res add {RES} 300')['out'].strip().replace('\n', ' | '))
        time.sleep(8 + 6)
        h, g = money(host), money(guest)
        print(f'  6 s after the host could hold it again: host {h} guest {g}', flush=True)
        print('  host:', host.call('coop.res')['out'].strip().replace('\n', ' | '))
        ck(h is not None and abs(h - BASE - 300) < 5, f"the host's game counted the guest's 300 ({h})")
        ck(g is not None and h is not None and abs(g - h) < 5, f'both games hold the same money ({h}, {g})')
    # 2. a purchase in the first frame after an editor visit (BUG-0692)
    if both_at(host, guest, BASE, ck):
        # the host sends its total on a change and every 2 s after it: both_at's change came 3 s ago, so the 3 s editor
        # visit ends 1 s away from the host's next total (ended on it, the total of that frame decides the outcome)
        time.sleep(1.0)
        print(' ', guest.call(f'coop.res away 3 {RES} -200')['out'].strip())
        time.sleep(3 + 5)
        h, g = money(host), money(guest)
        print(f'  5 s after the guest bought for 200 right after the editor: host {h} guest {g}', flush=True)
        ck(g is not None and abs(g - BASE + 200) < 5, f"the guest's purchase stays paid on its game ({g})")
        ck(h is not None and abs(h - BASE + 200) < 5, f"the host's game counted it ({h})")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
