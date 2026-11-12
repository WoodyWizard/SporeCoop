#!/usr/bin/env python3
"""Partner arrows and name tags are kept apart and off the game's HUD (BUG-0949).

Key-frame review of the 0.7.12 final check: two partners in one direction had their edge arrows' labels written
over each other, an arrow sat inside a notification card, Civ unit tags stood on the minimap and the bottom bar.

On the guest's game of the creature fixture, made-up partners (`coop.find ghost`: an arrow or a name tag through the
same path as a real partner's) are put where the cases of the report happen, from the guest's own camera
(`coop.cam`: its axes and the game's screen ray of a pixel):
  A, B  two partners 70 and 76 m away to the left, nearly in one direction: two arrows at the left edge;
  C     a partner whose point is under the minimap (the game's window 0x0190722E);
  D     a partner whose point is under a notification card (the host says something: the guest's card);
  E     a partner just above the bottom bar: its tag's box would reach into the bar.
Then the labels of the last overlay pass with their boxes as drawn (`coop.find boxes`) and the game's UI windows
(visible windows with a picture, read from the game's window tree) are compared:
  * no two drawn labels touch, no drawn label touches a UI window;
  * C and D are not drawn (their point is behind the HUD), A, B and E are drawn;
  * the picture: no pixel of C's colour inside the minimap's rectangle in a shot.
SPORE_FIX_OFF=find.layout must FAIL (the labels where they fall).

  SPORE_LANE=4 python3 tools/tests/coop_find_layout.py
"""
import os
import re
import time

import lane
import twoinst
from twoinst import Checks, start

PURPLE = (0xB0, 0x7C, 0xFF)  # palette 5: the colour of player id 5 (C) while it has none of its own


def cam(con):
    out = con.call('coop.cam').get('out') or ''
    rows = [[float(v) for v in m.groups()] for m in re.finditer(r'view\[\d\] =\s+(-?[\d.]+)\s+(-?[\d.]+)\s+(-?[\d.]+)',
                                                                out)]
    return rows if len(rows) == 4 else None


def ray_point(con, x, y, dist):
    m = re.search(r'ok=1 pos=\((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\) dir=\((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\)',
                  con.call(f'coop.cam {x} {y}').get('out') or '')
    if not m:
        return None
    v = [float(g) for g in m.groups()]
    return [v[i] + v[3 + i] * dist for i in range(3)]


def where(con, wid):
    m = re.search(wid + r' (-?\d+) (-?\d+) (-?\d+) (-?\d+)', con.call('coop.ui where ' + wid).get('out') or '')
    return [int(v) for v in m.groups()] if m else None


def boxes(con):
    out = con.call('coop.find boxes').get('out') or ''
    labels = [dict(kind=m.group(1), id=int(m.group(2)), box=[float(m.group(i)) for i in range(3, 7)],
                   hidden=m.group(7) == '1', text=m.group(8))
              for m in re.finditer(r'^box kind=(\S+) id=(\d+) x0=(-?\d+) y0=(-?\d+) x1=(-?\d+) y1=(-?\d+) hidden=(\d) '
                                   r'text=(.*)$', out, re.M)]
    hud = [[float(m.group(i)) for i in range(1, 5)]
           for m in re.finditer(r'^hud x0=(-?\d+) y0=(-?\d+) x1=(-?\d+) y1=(-?\d+)', out, re.M)]
    return labels, hud


def touch(a, b, pad=0.0):
    return a[0] < b[2] + pad and b[0] < a[2] + pad and a[1] < b[3] + pad and b[1] < a[3] + pad


def colour_in(png, rect, colour):
    try:
        from PIL import Image
        with Image.open(png) as im:
            im = im.convert('RGB')
            px = im.load()
            n = 0
            for y in range(max(0, rect[1]), min(im.height, rect[3])):
                for x in range(max(0, rect[0]), min(im.width, rect[2])):
                    p = px[x, y]
                    if abs(p[0] - colour[0]) + abs(p[1] - colour[1]) + abs(p[2] - colour[2]) < 60:
                        n += 1
            return n
    except (ImportError, OSError):
        return None


def main():
    host, guest = start('creature')
    ck = Checks()
    twoinst.game_runs(guest)
    time.sleep(2)
    c = cam(guest)
    ck(c is not None, 'the guest has a camera')
    mini = where(guest, '0190722E')
    ck(mini is not None, f'the guest\'s minimap window is up ({mini})')
    if not c or not mini:
        ck.faults(host=host, guest=guest)
        return ck.result()
    right, fwd, up, eye = c
    W, H = 1440, 1080
    m = re.search(r'viewport x=\d+ y=\d+ w=(\d+) h=(\d+)', guest.call('coop.cam').get('out') or '')
    if m:
        W, H = int(m.group(1)), int(m.group(2))

    def at(f, r, u):
        return [eye[i] + fwd[i] * f + right[i] * r + up[i] * u for i in range(3)]

    host.call('coop.say hello from the host')  # D: a card on the guest's screen
    time.sleep(2.5)
    labels, hud = boxes(guest)
    cards = [b for b in hud if b[0] > W * 0.5 and b[1] > 120 and b[1] < 400 and b[2] - b[0] > 300]
    print(f'  the guest\'s UI windows: {len(hud)}; notification cards: {cards[:2]}', flush=True)
    ck(bool(cards), 'the guest shows the host\'s message on a card')
    ghosts = {201: at(25, -70, 0), 202: at(27, -76, 1.5)}
    p = ray_point(guest, (mini[0] + mini[2]) // 2, (mini[1] + mini[3]) // 2, 12)
    if p:
        ghosts[5] = p
    if cards:
        b = cards[0]
        p = ray_point(guest, int((b[0] + b[2]) / 2), int((b[1] + b[3]) / 2), 12)
        if p:
            ghosts[206] = p
    bar = min((b[1] for b in hud if b[1] > H * 0.9 and b[2] - b[0] > W * 0.3), default=H - 56)
    p = ray_point(guest, W // 2 + 200, int(bar) - 6, 10)
    if p:
        ghosts[207] = p
    for gid, q in ghosts.items():
        guest.call('coop.find ghost %d %.3f %.3f %.3f G%d' % (gid, q[0], q[1], q[2], gid))
    time.sleep(1.0)
    labels, hud = boxes(guest)
    png = os.path.join(lane.out_dir('shots'), 'find_layout.png')
    shot_ok = guest.call('shot ' + png).get('ok')
    twoinst.keyframe('layout', 'the guest: two arrows at the left edge with their labels apart, no label on the minimap, '
                               'the bottom bar or the message card at the top right')
    for l in labels:
        print(f'  {l["kind"]} {l["text"]!r} id={l["id"]} box={[int(v) for v in l["box"]]} hidden={int(l["hidden"])}',
              flush=True)
    drawn = [l for l in labels if not l['hidden']]
    pairs = [(a['text'], b['text']) for i, a in enumerate(drawn) for b in drawn[i + 1:] if touch(a['box'], b['box'])]
    over = [(l['text'], [int(v) for v in h]) for l in drawn for h in hud if touch(l['box'], h)]
    print(f'  drawn labels touching each other: {pairs}; touching a UI window: {over[:6]}', flush=True)
    by = {l['id']: l for l in labels}
    ck(201 in by and 202 in by and not by[201]['hidden'] and not by[202]['hidden'],
       'both arrows at the left edge are drawn')
    ck(not pairs, f'no two partner labels are drawn over each other ({pairs})')
    ck(not over, f'no partner label is drawn over the game\'s UI windows ({over[:4]})')
    ck(by.get(5) is None or by[5]['hidden'], 'the tag of the partner under the minimap is not drawn over it')
    ck(206 not in ghosts or by.get(206) is None or by[206]['hidden'],
       'the tag of the partner under the message card is not drawn over it')
    ck(207 not in by or not by[207]['hidden'], 'the tag just above the bottom bar is drawn (moved off the bar)')
    if shot_ok:
        n = colour_in(png, mini, PURPLE)
        print(f'  pixels of the colour of the partner under the minimap inside the minimap: {n}', flush=True)
        ck(n is not None and n < 15, f'the picture: no tag on the minimap ({n} px of its colour)')
    guest.call('coop.find ghost clear')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
