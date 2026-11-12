#!/usr/bin/env python3
"""BUG-0938: the galaxy menu's legal lines ("Online Interactions Not Rated by the ESRB", the logo, the copyright) do
not stay on a guest's screen in the stage after a join from the menu.

The galaxy menu shows a splash step by step (GalaxyGameEntry +0x204: logos, then the legal lines' window 0x05B82B38
for 4.1 s); its update runs only in the menu, and leaving the menu does not hide a step that is up. The lab's guests
reach their menu after the splash (state 2, the legal lines never drawn in 28 s of polling, lab 2026-11-12), so the
natural moment (a join within the 4.1 s of the legal lines) is not reached here: the guest waits for its menu, the
legal lines are put up as their step puts them up (`coop.ui show <0x05B82B38> 1`, the game's SetFlag(visible)), and
it joins at once. Checked in the guest's own window tree in the stage, three times over 20 s: no window of the splash
is drawn (every window from the game's own splash table, through visible parents), and a key frame of both games.
Without the fix (SPORE_FIX_OFF=world.menu_splash) the legal lines stay over the creature stage.

  SPORE_LANE=4 python3 tools/tests/coop_menu_splash.py
"""
import re
import struct
import time

from coop_creature_kills import read
from twoinst import Checks, keyframe, start

LEGAL = '05B82B38'
GROUPS = ('06776FE0', '06776FE1')
ESRB = 'Online Interactions Not Rated by the ESRB'
seen_legal = []


def wait_legal(guest):
    """before the join: the menu's legal lines up (as their splash step shows them), then the join"""
    t0 = time.time()
    while time.time() - t0 < 20 and 'GalaxyGameEntry' not in guest.call('game.mode')['out']:
        time.sleep(0.3)
    m = re.search(r'Window ([0-9A-F]{8}) id=0x' + LEGAL, guest.call(f'coop.ui find {ESRB}')['out'])
    if m:
        guest.call(f'coop.ui show {m.group(1)} 1')
    up = 'none' not in guest.call(f'coop.ui where {LEGAL}')['out']
    print(f'  guest menu: the legal lines {"up" if up else "NOT up"} before the join ({m and m.group(1)})', flush=True)
    if up:
        seen_legal.append(round(time.time() - t0, 2))


def splash_ids(con):
    gp = struct.unpack('<I', read(con, 0x016A1344, 4))[0]
    b = read(con, gp + 0x204, 6 * 12) if gp else b''
    ids = [f'{struct.unpack_from("<I", b, 12 * k)[0]:08X}' for k in range(len(b) // 12)]
    return sorted({i for i in ids if i != '00000000'} | set(GROUPS))


def drawn(con, ids):
    """the splash windows drawn now (`coop.ui where` walks visible parents only)"""
    out = con.call('coop.ui where ' + ' '.join(ids))['out']
    return [line.split()[0] for line in out.splitlines() if line.strip() and 'none' not in line]


def main():
    host, guest = start('creature', join_at=0.5, before_join=wait_legal)
    ck = Checks()
    ck(bool(seen_legal), 'the guest\'s menu had its legal lines up at the join')
    ids = splash_ids(guest)
    print(f'  the menu\'s splash windows: {ids}', flush=True)
    ck(LEGAL in ids, f'the game\'s splash table names the legal lines ({LEGAL})')
    for k in range(3):
        time.sleep(7 if k else 3)
        g, h = drawn(guest, ids), drawn(host, ids)
        st = guest.call('coop.status')['out']
        print(f'  {3 + 7 * k} s in the stage: drawn on the guest {g}, on the host {h}; '
              f'{next((l.strip() for l in st.splitlines() if "menu intro" in l), "")}', flush=True)
        ck(not g, f'{3 + 7 * k} s in the stage: no splash window of the galaxy menu is drawn on the guest ({g})')
        ck(not h, f'{3 + 7 * k} s in the stage: none on the host ({h})')
        if k == 0:
            keyframe('stage', 'both games in Creature play after a join from the menu: no logo, no "ESRB" or '
                              'copyright lines over the picture')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
