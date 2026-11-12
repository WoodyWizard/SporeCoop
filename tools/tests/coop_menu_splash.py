#!/usr/bin/env python3
"""BUG-0938: the galaxy menu's legal lines ("Online Interactions Not Rated by the ESRB", the logo, the copyright) do
not stay on a guest's screen in the stage after a join from the menu.

The galaxy menu shows a splash step by step (GalaxyGameEntry +0x204: logos, then the legal lines' window 0x05B82B38
for 4.1 s); its update runs only in the menu. The guest here joins from its menu the moment the legal lines come up
(polled with `coop.ui where 05B82B38`, bounded), so its load leaves the menu while they stand. Checked in the guest's
own window tree in the stage, three times over 20 s: no window of the splash is drawn (every window from the game's
own splash table, through visible parents), and a key frame of both games. Without the fix
(SPORE_FIX_OFF=world.menu_splash) the legal lines stay over the creature stage.

  SPORE_LANE=4 python3 tools/tests/coop_menu_splash.py
"""
import struct
import time

from coop_creature_kills import read
from twoinst import Checks, keyframe, start

LEGAL = '05B82B38'
GROUPS = ('06776FE0', '06776FE1')
seen_legal = []


def wait_legal(guest):
    """before the join: until the menu's legal lines are up (at most 30 s)"""
    t0 = time.time()
    while time.time() - t0 < 30:
        if 'none' not in guest.call(f'coop.ui where {LEGAL}')['out']:
            seen_legal.append(round(time.time() - t0, 2))
            return
        time.sleep(0.1)


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
    print(f'  the guest joined {seen_legal[0] if seen_legal else "-"} s after its menu\'s legal lines came up', flush=True)
    ck(bool(seen_legal), 'the guest\'s menu showed its legal lines before the join')
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
