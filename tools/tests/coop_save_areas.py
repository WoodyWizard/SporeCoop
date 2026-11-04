#!/usr/bin/env python3
"""BUG-0506: the game settings window opened from the galaxy menu when the game could not make its
Documents\\My Spore Creations folder.

Players (offline, galaxy menu) crashed at SporeApp.exe+0x2025A9: the settings window's builder
(SporeCoop::GameSettings_Build) calls Resource::GetDatabaseByID(0x11AC197) (the Movies save area)
and uses the result without a null check. The app's init makes the Pictures / Movies / Animations
areas as folders in My Spore Creations; when that folder cannot be made (Documents missing,
redirected or blocked, or a file of that name) the areas are not there.

One game (the lane's guest instance, its own test profile): before it starts, the profile's
Documents\\My Spore Creations becomes a plain file, so the game cannot make the folders. In the
galaxy menu the test asks the game for the three save areas (the fix makes them right after the game's
startup), then opens Options -> Settings with real clicks. The game must not fault (`exc`), the three
save areas must exist and the settings window must be on screen (the window manager's modal window).
The profile folder is put back at the end.

  coop_save_areas.py   (SPORE_FIX_OFF=game.save_areas: also written to the profile's SporeCoop\fixes_off.txt,
  as the fix acts while the game starts; the read of 0 at 0x6025A9 comes back, it FAILS)"""
import os
import re
import shutil
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE)))
import lane  # noqa: E402
from sporectl import Console  # noqa: E402
from twoinst import Checks, fix_off_ok  # noqa: E402

AREAS = {0x011AC196: 'Pictures', 0x011AC197: 'Movies', 0x011AC198: 'Animations'}
# 1440x1080 test window (spore.sh run -p): the Spore button at the bottom left opens Options,
# its third entry is Settings
SPORE_BUTTON = (16, 1062)
SETTINGS = (660, 560)


def area(con, aid):
    m = re.search(r'eax=0x([0-9A-F]+)', con.call(f'call Resource::GetDatabaseByID cdecl 0x{aid:X}')['out'])
    return int(m.group(1), 16) if m else None


def faults(con):
    return con.call('exc')['out'].splitlines()


def main():
    ck = Checks()
    docs = os.path.join(os.path.dirname(lane.GUEST_DIR), 'Documents')
    msc = os.path.join(docs, 'My Spore Creations')
    keep = os.path.join(docs, 'My Spore Creations.w-save-areas')
    lane.sh('kill')
    time.sleep(2)
    lane.sh('deploy')
    os.makedirs(docs, exist_ok=True)
    if os.path.isdir(msc) and not os.path.exists(keep):
        os.rename(msc, keep)
    elif os.path.isdir(msc):
        shutil.rmtree(msc)
    if not os.path.isfile(msc):
        with open(msc, 'w') as f:
            f.write('not a folder (coop_save_areas.py)\n')
    offfile = os.path.join(lane.GUEST_DIR, 'SporeCoop', 'fixes_off.txt')
    os.makedirs(os.path.dirname(offfile), exist_ok=True)
    if 'game.save_areas' in os.environ.get('SPORE_FIX_OFF', ''):
        with open(offfile, 'w') as f:
            f.write('game.save_areas\n')
    try:
        lane.run_guest()
        con = Console(lane.GUEST, timeout=60)
        for _ in range(120):
            try:
                if 'GalaxyGameEntry' in con.call('game.mode')['out']:
                    break
            except OSError:
                pass
            time.sleep(1)
        con.call('wait.frames 600 90000')  # the galaxy settles; the menu intro cinematic starts ~16 s in
        t0 = time.time()
        while time.time() - t0 < 90 and (time.time() - t0 < 30 or
                                         '(menu intro)' in con.call('coop.status')['out']):
            time.sleep(2)  # the intro hides the menu buttons (~10 s)
        con.call('wait.frames 120 20000')
        for name in filter(None, (n.strip() for n in os.environ.get('SPORE_FIX_OFF', '').split(','))):
            out = con.call(f'coop.fix {name} off')['out'].strip()
            print('  SPORE_FIX_OFF: ' + out, flush=True)
            if not fix_off_ok(name, out):
                print(f'RESULT: FAIL (SPORE_FIX_OFF: unknown fix {name})', flush=True)
                return 1
        before = {n: area(con, a) for a, n in AREAS.items()}
        print('  save areas before:', before)
        log = os.path.join(lane.game_dir(), 'SporebinEP1', 'mods', 'logs', f'sporekit.{lane.GUEST}.log')
        try:
            with open(log, errors='replace') as f:
                seen = [l.strip() for l in f if 'save areas after the game' in l]
        except OSError:
            seen = []
        print('  the game\'s startup:', seen[-1] if seen else 'no line')
        ck(bool(seen) and 'Movies=00000000' in seen[-1],
           'the game\'s own startup left the Movies save area out (My Spore Creations could not be made)')
        con.call('click %d %d' % SPORE_BUTTON)
        con.call('wait.frames 90 20000')
        time.sleep(2)  # the Options window slides in
        con.call('shot ' + os.path.join(lane.out_dir('shots'), 'save_areas_options.png'))
        con.call('click %d %d' % SETTINGS)
        con.call('wait.frames 90 20000')
        time.sleep(2)
        alive = True
        try:
            f = faults(con)
        except OSError:
            alive, f = False, ['the game does not answer']
        print('  faults:', f[:3])
        ck(alive and 'total faults: 0' in f[0], 'no fault while the settings window opened (0x6025A9)')
        if alive:
            after = {n: area(con, a) for a, n in AREAS.items()}
            print('  save areas after:', after)
            ck(all(v for v in after.values()), 'the game has the three save areas now')
            m = re.search(r'eax=0x([0-9A-F]+)', con.call('call UTFWin::IWindowManager::Get cdecl')['out'])
            wm = int(m.group(1), 16) if m else 0
            modal = con.call(f'mem.ptr 0x{wm + 0x82C:X}')['out'].strip() if wm else ''
            print('  modal window:', modal)
            ck(bool(re.search(r'0x(?!0+\b)[0-9A-F]+', modal)), 'a modal window (the settings) is open')
            con.call('key ESCAPE')
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
