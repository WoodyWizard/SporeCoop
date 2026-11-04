#!/usr/bin/env python3
"""BUG-0506 (GitHub issue #190): the Cell editor opened from a mate when the game could not make its
Documents save areas.

The editor's play-mode background load (SporeCoop::PlayModeBackgrounds_LoadPictures 0x633750) calls
Resource::GetDatabaseByID(0x11AC196, the Pictures save area) and uses it without a null check. The areas
are folders of Documents\\<PlayerDir> made once by the game's startup; a ru-ru Steam install writes
PlayerDir "??? ????????", a name that cannot be made, and the editor faulted at 0x633765 reading 0.
Since the fix, the mod gives every missing area a fallback folder right after the game's startup.

Two games in the Cell fixture. Before they start, the guest profile's Documents\\My Spore Creations
becomes a plain file (the same effect as the broken PlayerDir: the folder cannot be made). The test
checks that the guest's game has the three save areas, then the guest's own mating call opens its Cell
editor (real clicks, coop_editor_desync.py's way). The guest must not fault and must reach the editor and
stay in it; then it cancels. The profile folder is put back at the end.

  coop_save_areas_editor.py   (SPORE_FIX_OFF=game.save_areas: the startup check is off too, through the
  profile's SporeCoop\\fixes_off.txt; the fault at 0x633765 comes back, it FAILS)"""
import os
import re
import shutil
import time

import lane
from coop_editor_desync import CANCEL, mating_call, mode, wait
from twoinst import Checks, start

AREAS = (0x011AC196, 0x011AC197, 0x011AC198)


def area(con, aid):
    m = re.search(r'eax=0x([0-9A-F]+)', con.call(f'call Resource::GetDatabaseByID cdecl 0x{aid:X}')['out'])
    return int(m.group(1), 16) if m else None


def main():
    off = [n for n in (n.strip() for n in os.environ.get('SPORE_FIX_OFF', '').split(',')) if n]
    docs = os.path.join(os.path.dirname(lane.GUEST_DIR), 'Documents')
    msc = os.path.join(docs, 'My Spore Creations')
    keep = os.path.join(docs, 'My Spore Creations.w-save-areas')
    offfile = os.path.join(lane.GUEST_DIR, 'SporeCoop', 'fixes_off.txt')
    lane.sh('kill')  # the folder is changed while no game of the lane runs
    time.sleep(2)
    os.makedirs(docs, exist_ok=True)
    if os.path.isdir(msc) and not os.path.exists(keep):
        os.rename(msc, keep)
    elif os.path.isdir(msc):
        shutil.rmtree(msc)
    if not os.path.isfile(msc):
        with open(msc, 'w') as f:
            f.write('not a folder (coop_save_areas_editor.py)\n')
    os.makedirs(os.path.dirname(offfile), exist_ok=True)
    if 'game.save_areas' in off:
        with open(offfile, 'w') as f:
            f.write('game.save_areas\n')
    ck = Checks()
    try:
        host, guest = start('cell', guest_creations=False)
        got = [area(guest, a) for a in AREAS]
        print('  guest save areas:', [f'{v:08X}' if v is not None else None for v in got])
        ck(all(got), 'the guest\'s game has the Pictures / Movies / Animations save areas')
        _, reached = mating_call(guest, 'guest', lambda: 'Editor' in mode(guest))
        ck(reached, f"the guest's own cell editor opens ({mode(guest)})")
        time.sleep(4)
        try:
            exc = guest.call('exc')['out'].splitlines()
            m = mode(guest)
        except OSError:
            exc, m = ['the guest does not answer'], ''
        print('  guest faults:', exc[:3])
        ck('total faults: 0' in exc[0], 'no fault while the editor loaded its backgrounds (0x633765)')
        ck('Editor' in m, f'the guest is still in its editor 4 s later ({m})')
        if 'Editor' in m:
            guest.call('click %d %d' % CANCEL)
            wait(lambda: 'Cell' in mode(guest), 30)
        ck.faults(host=host, guest=guest)
    finally:
        lane.sh('kill')
        time.sleep(2)
        if os.path.exists(offfile):
            os.remove(offfile)
        if os.path.isfile(msc):
            os.remove(msc)
        if os.path.isdir(keep) and not os.path.exists(msc):
            os.rename(keep, msc)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
