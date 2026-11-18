#!/usr/bin/env python3
"""BUG-1260: a game that left the galaxy menu before its start-up video ended shows its main menu panel when it comes
back to the galaxy menu.

The galaxy menu shows its main menu panel (Galactic Adventures, Sporepedia, Co-op) when its intro state
([GalaxyGameEntry_ptr] +0x1EC) reaches 6: the start-up video's end (0x49731AD) starts the menu's GGE_intro (state 5)
and the intro's end (0x44F1189) or a skip (0x66F6B6F) sets 6. A load from the menu before the video's end (the lab's
quick fixture load; a guest that joins from its menu at once) had the video's end come in the stage: the handler set
state 5, BUG-0626 refused the intro there, and nothing ever ended it. Back in the galaxy menu the panel stayed hidden.

1. start('creature'): both games load the fixture from their menu at once (before the video's end, ~16 s);
2. the host, then the guest, leaves for the galaxy menu with the game's own quit (`coop.menu`);
3. on each game, for up to 25 s: the panel's "Galactic Adventures" entry is shown (`coop.ui find`), the intro state;
   a key frame of both menus.
PASS: both games show the panel. INCONCLUSIVE: no game had its menu intro refused in the stage (`coop.status`
"not started" 0: the start was slower than the video). Without the fix (SPORE_FIX_OFF=world.menu_intro_return) the
panel stays hidden: FAIL.

  SPORE_LANE=N python3 tools/tests/coop_menu_panel.py
"""
import re
import struct
import time

import uifind
from coop_creature_kills import read
from twoinst import Checks, keyframe, start

GGE_PTR = 0x016A1344  # SporeCoop::GalaxyGameEntry_ptr (data/symbols/overrides_dc04aee5.sym)


def call(c, line):
    try:
        return c.call(line)['out'].strip()
    except Exception as e:  # noqa: BLE001 (a game that died answers nothing)
        return f'no answer ({e.__class__.__name__})'


def mode(c):
    m = call(c, 'game.mode').split()
    return m[1] if len(m) > 1 else 'gone'


def intro_state(c):
    """the galaxy menu's intro state ([GalaxyGameEntry_ptr] +0x1EC), None when unreadable"""
    b = read(c, GGE_PTR, 4)
    gge = struct.unpack('<I', b)[0] if len(b) == 4 else 0
    b = read(c, gge + 0x1EC, 4) if gge else b''
    return struct.unpack('<I', b)[0] if len(b) == 4 else None


def refused(c):
    """how often the menu intro was not started outside the menu (BUG-0626), and how often the mod ended it at the
    menu's return (BUG-1260)"""
    s = call(c, 'coop.status')
    m = re.search(r'not started (\d+)', s)
    r = re.search(r'menu intro ended at the return (\d+)', s)
    return (int(m.group(1)) if m else 0), (int(r.group(1)) if r else 0)


def main():
    host, guest = start('creature')
    ck = Checks()
    games = (('host', host), ('guest', guest))
    time.sleep(3)
    before = {who: refused(c)[0] for who, c in games}
    print(f'  in the stage: the menu intro not started (BUG-0626) {before}; intro state '
          f'host {intro_state(host)}, guest {intro_state(guest)}', flush=True)
    # 2. the galaxy menu through the game's own quit: the host first, then the guest
    for who, c in games:
        print(f'  {who}: ' + call(c, 'coop.menu'), flush=True)
        for _ in range(120):
            if mode(c) == 'GalaxyGameEntry':
                break
            time.sleep(1)
        print(f'  {who}: mode {mode(c)}, intro state {intro_state(c)}', flush=True)
    ck(mode(host) == 'GalaxyGameEntry' and mode(guest) == 'GalaxyGameEntry', 'both games are at the galaxy menu')
    # 3. the panel (a player's normal return shows it at once; the menu settles a few seconds)
    seen = {}
    t0 = time.time()
    while time.time() - t0 < 25 and len(seen) < 2:
        for who, c in games:
            if who not in seen and uifind.shown(c, 'Galactic Adventures'):
                seen[who] = round(time.time() - t0, 1)
        time.sleep(1)
    for who, c in games:
        print(f'  {who}: the main menu panel shown after {seen.get(who)} s; intro state {intro_state(c)}; '
              f'(not started, ended at the return) {refused(c)}; {uifind.find(c, "Galactic Adventures")}', flush=True)
    keyframe('menu', 'both games back at the galaxy menu after the Creature stage: the main menu panel (Galactic '
             'Adventures, Sporepedia, Co-op) on the left', {'host': host, 'guest': guest}, settle=0)
    if not any(before.values()):
        return ck.inconclusive('no game had its menu intro refused in the stage (the start was slower than the video)')
    for who, _ in games:
        if before[who]:
            ck(who in seen, f'the {who} shows its main menu panel back in the galaxy menu ("Galactic Adventures" shown '
                            f'after {seen.get(who)} s)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
