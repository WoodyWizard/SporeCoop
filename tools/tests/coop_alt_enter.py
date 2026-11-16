#!/usr/bin/env python3
"""Creature stage: the fullscreen keys while two games play together (a players' report, 2026-10-25,
the host on Windows: "Game puts me underground after I use Alt + Enter to get fullscreen", and two
crashes at 0x7777BE reading 0xFFFE0310).

Alt+Enter toggles fullscreen through a device Reset. Alt+Shift+Enter is the game's own "next display
adapter" key: with two or more adapters (the headless test display has two monitors, so Wine reports
two) the game recreates the D3D9 device on the next one, and its code for that faults at 0x7777BE
(cMaterialManager::ReleaseShaders visits shaders its list holds twice; docs/re/graphics.md). The fix
turns that key into the fullscreen toggle.

1. The host presses Alt+Enter twice (real key messages, the game reads Alt from GetKeyState): two
   device Resets, fullscreen on and off, no fault, the avatar stays on the ground while it walks.
2. The host and then the guest press Alt+Shift+Enter: before the fix the game faulted at 0x7777BE
   in a loop (on Windows: the crash report); now fullscreen toggles, no fault, both still play.

  SPORE_LANE=2 python3 tools/tests/coop_alt_enter.py [--without-fix]
  (--without-fix: `coop.fix game.adapter_key off` on both games, the test must FAIL; BUG-0012)
"""
import re
import sys
import time

from harness import faults
import lane
from twoinst import Checks, start


def fullscreen(con):
    """the renderer's fullscreen flag (Renderer_SetVideoMode writes it at +0x6C)"""
    r = re.search(r'eax=0x([0-9A-F]+)', con.call('call Graphics::IRenderer::Get cdecl')['out'])
    out = con.call(f'mem.read 0x{int(r.group(1), 16) + 0x6C:X} 1')['out'] if r else ''
    m = re.match(r'[0-9A-F]+\s+([0-9A-F]{2})', out.strip())
    return int(m.group(1), 16) if m else None


def above(con):
    """the avatar's height over the terrain (coop.avatar: cPlanetModel::GetHeightAt under it)"""
    m = re.search(r'above=(-?[\d.]+)', con.call('coop.avatar')['out'])
    return float(m.group(1)) if m else None


def frame(con):
    m = re.search(r'frame=(\d+)', con.call('frame')['out'])
    return int(m.group(1)) if m else -1


def keys(con, shift):
    """Alt(+Shift)+Enter as key messages; SporeKit keeps the held keys down for GetKeyState"""
    if shift:
        con.call('key shift down')
    con.call('key alt down')
    time.sleep(0.2)
    con.call('key enter')
    time.sleep(0.5)
    con.call('key alt up')
    if shift:
        con.call('key shift up')


def press(ck, con, name, shift):
    """press, wait for the switch; True when the game toggled fullscreen and runs on"""
    fs0, f0, e0 = fullscreen(con), frame(con), faults(con)
    keys(con, shift)
    time.sleep(4)
    fs1, f1, e1 = fullscreen(con), frame(con), faults(con)
    what = 'Alt+Shift+Enter' if shift else 'Alt+Enter'
    ok = ck(fs0 is not None and fs1 == (fs0 ^ 1) and e1 == e0 and f1 > f0 + 30,
            f'{name}: {what} toggles fullscreen {fs0} -> {fs1}, no fault ({e0} -> {e1}), frames go on ({f0} -> {f1})')
    return ok


def walk(ck, con, label):
    """a few clicks on the terrain; the lowest height over it while the avatar walks"""
    low = 99.0
    for x, y in ((1100, 500), (400, 500), (720, 800)):
        con.call(f'click {x} {y}')
        for _ in range(5):
            time.sleep(0.5)
            a = above(con)
            if a is not None:
                low = min(low, a)
    ck(-0.5 < low < 50, f'{label}: the avatar walks on the ground (lowest height over it {low:.2f} m)')


def main():
    host, guest = start()
    ck = Checks()
    if '--without-fix' in sys.argv:
        for con in (host, guest):
            print('  ' + con.call('coop.fix game.adapter_key off')['out'].strip())
    a0 = above(host)
    ck(a0 is not None and abs(a0) < 2, f'the host\'s avatar stands on the ground ({a0} m over it)')

    # 1. Alt+Enter twice: fullscreen and back through device Resets
    for i in range(2):
        if not press(ck, host, 'host', False):
            return ck.result()
        walk(ck, host, f'host after Alt+Enter #{i + 1}')

    # 2. Alt+Shift+Enter: the device recreation that crashed
    for name, con in (('host', host), ('guest', guest)):
        if not press(ck, con, name, True):
            print('  (the game faulted: see "exc" / the game log)', flush=True)
            ck.faults(host=host, guest=guest)
            return ck.result()
        keys(con, False)  # back to windowed (the next tests expect the window)
        time.sleep(4)
        ck(fullscreen(con) == 0, f'{name}: windowed again')
    st = host.call('coop.status')['out']
    m = re.search(r'Alt\+Shift\+Enter turned into Alt\+Enter (\d+)', st)
    ck(m and int(m.group(1)) >= 1, f'the host\'s fix took the key ({m.group(0) if m else "no counter"})')
    walk(ck, host, 'host after Alt+Shift+Enter')
    ck('Bob' in st, 'the guest is still in the host\'s session')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
