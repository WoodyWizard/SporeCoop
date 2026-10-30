#!/usr/bin/env python3
"""Space: a host ship whose model the guest's game does not have (BUG-0323).

An NPC empire's ships are creations of the host's Sporepedia cache (Pollination.package); a guest
whose cache differs does not have that model. The guest's mirror of such a ship had no model and
the guest's game faulted at 0x00E98DD0 (ship gfx SetVisible on a null model) when it came up
(lab lane 8, 2026-10-30: the host profile's Pollination.package alone made it). Forced here: the
host sends made-up model instances (`coop.fleet fakemodel 1`) and spawns NPC ships next to both
players (a pirate, an escort of the player's empire). The guest's game must not fault; the mirror
takes the model of a ship of that empire the guest's game has, or is not made.

  SPORE_LANE=9 python3 tools/tests/coop_space_mirror_model.py [--without-fix]

--without-fix (or SPORE_FIX_OFF=fleet.model_check): `coop.fix fleet.model_check off` on the
guest: it spawns with the unknown model and faults (FAIL).
"""
import re
import sys
import time

from twoinst import Checks, start


def main():
    without = '--without-fix' in sys.argv
    host, guest = start('space')
    ck = Checks()
    time.sleep(3)
    if without:
        print(' ', guest.call('coop.fix fleet.model_check off')['out'].strip())
    print(' ', host.call('coop.fleet fakemodel 1')['out'].strip())
    for args in ('2', '3 own', '2'):
        print('  host:', host.call(f'coop.fleet spawn {args}')['out'].strip())
        time.sleep(4)
    time.sleep(6)
    st = guest.call('coop.fleet list')['out']
    m = re.search(r'unknown_models=(\d+)', st)
    print('  guest:', (m.group(0) if m else st.strip()[:200]))
    ck(m and int(m.group(1)) > 0, 'the guest got host ships with models it does not have')
    host.call('coop.fleet fakemodel 0')
    ck.faults(host=host, guest=guest)
    if without:
        guest.call('coop.fix fleet.model_check on')
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
