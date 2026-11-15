#!/usr/bin/env python3
"""The co-op window and the overlay's status block on the screen (one game of the lane, main menu):

  recenter   BUG-0113: the window is opened once, the resolution goes down to 1280x720 through the
             game's own Renderer_SetVideoMode (as the options do); opened again, every widget of
             the window lies inside the new screen
  block      BUG-0155: hosting, the status block ("CO-OP host ...") is not drawn while the co-op
             window is open; with the window closed, a notification panel starts below the block
             (at 1280x720, where they met)

  --old      the fixes off (`coop.fix core.window_recenter off`, `core.status_block off`): both cases FAIL

  SPORE_LANE=3 python3 tools/tests/coop_window_layout.py [--old]
"""
import re
import sys
import time

import lane
from coop_ui import Ui
from sporectl import Console
from twoinst import Checks, wait_up


def start_host(ck):
    lane.sh('kill')
    time.sleep(2)
    lane.sh('deploy')
    lane.run_host()
    ck(wait_up(lane.HOST, 180), 'the game starts')
    c = Console(lane.HOST)
    for _ in range(120):
        if 'GalaxyGameEntry' in c.call('game.mode')['out']:
            break
        time.sleep(1)
    c.call('wait.frames 300 60000')
    return c


def screen(c):
    m = re.search(r'^screen (\d+) (\d+)', c.call('coop.uistate')['out'], re.M)
    return (int(m.group(1)), int(m.group(2))) if m else (0, 0)


def renderer(c):
    r = re.search(r'eax=0x([0-9A-F]+)', c.call('call Graphics::IRenderer::Get cdecl')['out'])
    return int(r.group(1), 16) if r else 0


def set_mode(c, w, h):
    r = renderer(c)
    full = c.call(f'mem.read 0x{r + 0x6C:X} 1')['out'].split()
    fs = int(full[1], 16) if len(full) > 1 else 0
    print('  ', c.call(f'call SporeCoop::Renderer_SetVideoMode thiscall 0x{r:X} {fs} {w} {h} -1')['out'].strip())
    c.call('wait.frames 60 30000')


def widgets(st):
    return {k: v for k, v in st.items() if isinstance(v, dict) and v.get('vis')}


def case_recenter(ck, c, ui):
    c.call('coop.uistate 1')
    st = ui.wait(lambda s: s.get('open') == '1' and s.get('report', {}).get('vis'), 20, 'the co-op window')
    sw0, sh0 = screen(c)
    print(f'  screen {sw0}x{sh0}, close at {st["close"]["rect"] if st else "?"}')
    c.call('coop.uistate 0')
    time.sleep(1)
    set_mode(c, 1280, 720)
    sw, sh = screen(c)
    print(f'  after the mode change: {sw}x{sh}')
    ck((sw, sh) == (1280, 720), 'the game runs at 1280x720 now')
    c.call('coop.uistate 1')
    st = ui.wait(lambda s: s.get('open') == '1' and s.get('report', {}).get('vis'), 20, 'the co-op window again')
    out = [(k, v['rect']) for k, v in widgets(st or {}).items()
           if v['rect'][0] < 0 or v['rect'][1] < 0 or v['rect'][2] > sw or v['rect'][3] > sh]
    print(f'  widgets outside the screen: {out}; close {st["close"]["rect"] if st else "?"}, '
          f'report {st.get("report", {}).get("rect") if st else "?"}')
    ck(st and not out, 'every widget of the window is on the screen')
    c.call('coop.uistate 0')
    return sw0, sh0


def block(c):
    m = re.search(r'status block \((-?\d+) (-?\d+) (-?\d+) (-?\d+)\)', c.call('coop.status')['out'])
    return tuple(int(v) for v in m.groups()) if m else None


def overlap(a, b):
    return a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]


def case_block(ck, c, ui):
    print('  ', c.call('coop.host').get('out', '').strip())
    c.call('wait.frames 30 10000')
    b = block(c)
    print(f'  block with the window closed: {b}')
    ck(b and b[3] > b[1], 'the status block is drawn while hosting')
    c.call('coop.notify a notice to stack under the block')
    time.sleep(1.5)
    out = c.call('coop.status')['out']
    rects = [tuple(int(float(v)) for v in r) for r in
             re.findall(r'toast \w+ \((-?[\d.]+) (-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\)', out)]
    print(f'  panels: {rects}')
    ck(rects and b and not any(overlap(r, b) for r in rects), 'no notification panel under the status block')
    c.call('coop.uistate 1')
    st = ui.wait(lambda s: s.get('open') == '1' and s.get('report', {}).get('vis'), 20, 'the co-op window')
    c.call('wait.frames 10 5000')
    b2 = block(c)
    hits = [k for k, v in widgets(st or {}).items() if b2 and b2[3] > b2[1] and overlap(v['rect'], b2)]
    print(f'  block with the window open: {b2}, widgets under it: {hits}')
    ck(b2 is not None and b2[3] <= b2[1], 'the status block is not drawn over the co-op window')
    c.call('coop.uistate 0')
    c.call('coop.leave')


def main():
    ck = Checks()
    old = '--old' in sys.argv
    c = start_host(ck)
    for sw in ('core.window_recenter', 'core.status_block'):
        print('  ', c.call(f'coop.fix {sw} {"off" if old else "on"}')['out'].strip().splitlines()[-1:])
    ui = Ui(c, 'host')
    res = {}
    first = None
    # the block case runs at 1280x720 (left by the recenter case): there the first panel (15 % of
    # the height = y 108) and the window (x 260..1020) are where the block is (x from 800)
    for case, fn in (('recenter', case_recenter), ('block', case_block)):
        print(f'== {case}' + (' (fix off)' if old else ''))
        before = ck.ok
        ck.ok = True
        r = fn(ck, c, ui)
        first = first or r
        res[case] = ck.ok
        ck.ok &= before
    print('cases:', ', '.join(f'{k}={"PASS" if v else "FAIL"}' for k, v in res.items()))
    if first:
        set_mode(c, *first)
    ck.faults(game=c)
    lane.sh('kill')
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
