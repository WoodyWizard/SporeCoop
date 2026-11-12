#!/usr/bin/env python3
"""BUG-0948: is a partner's name tag drawn over its creature while the camera turns?

Two players stand in the Creature stage a few metres apart (the host walks --walk s away from the nest), nobody
moves after that. The guest's camera is turned with right-drags (the way the play bots look around; SporeKit's
`drag`: 8 mouse steps in 8 frames) and the guest's game is shot inside the turn. The measure is the picture itself,
what the player sees: the name tag in the shot (the pixels of the tag's colour, the densest 30 rows of them) against
the partner's body in the shot (the red of the fixture's creature in the 340 px under the tag, within 420 px of it),
compared with the same two in shots of the camera at rest. Per shot of a turn:
  over   the tag within --near px of where it is over the body at rest,
  off    the tag further from its body,
  empty  a tag and no body under it (the frame of BUG-0948),
  none   no tag in the picture (an arrow at the edge, or the partner outside the view).
Nothing but `shot`, `drag`, `key`, `coop.targets` and `coop.avatar` is used, so the test runs on old builds too
(--colour 80FF80: the tags before 0.7.12 were green).

The shots stay in the run's frames (tagcam_still_N, tagcam_turn_N; without tools/tests/frames.py: the lane's shots).

  coop_tag_camera.py [--drag 400] [--turns 12] [--walk 1.6] [--colour RRGGBB] [--near 60] [--limit -1]
--limit N: FAIL when more than N turn shots are `off` or `empty` (-1: a measurement, the test fails only on a fault).
"""
import argparse
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lane  # noqa: E402
from twoinst import Checks, start  # noqa: E402

try:
    import frames
except ImportError:  # (before 0.7.12)
    frames = None


def partner_xy(con):
    """the partner's place on this game's screen (`coop.targets all`: player <id> <d> <x> <y>), None when not there"""
    for line in (con.call('coop.targets all').get('out') or '').splitlines():
        f = line.split()
        if len(f) == 5 and f[0] == 'player':
            return int(f[3]), int(f[4])
    return None


def tag_colour(con):
    """the partner's colour on this game (`coop.find`: argb= of its row, 0.7.12 on), None when not known"""
    m = re.search(r'^(?:tag|arrow) id=\d+ .* argb=([0-9A-Fa-f]{8})', con.call('coop.find').get('out') or '', re.M)
    if not m:
        return None
    v = int(m.group(1), 16)
    return (v >> 16) & 255, (v >> 8) & 255, v & 255


def measure(png, colour):
    """{'tag': (x, y) | None, 'body': x | None, 'n': (tag px, body px)} of a shot; None without Pillow / a picture.
    The HUD (the status block top right, the bars at the bottom) and where the arrows' labels are (the 130 px at the
    left and right edges, the 330 px at the bottom) are left out."""
    try:
        from PIL import Image
    except ImportError:
        return None
    try:
        with Image.open(png) as im:
            im = im.convert('RGB')
            w, h = im.size
            px = im.load()
            rows = {}
            for y in range(100, h - 330):
                for x in range(130, w - 130):
                    if y < 190 and x > w * 0.6:
                        continue
                    p = px[x, y]
                    if abs(p[0] - colour[0]) + abs(p[1] - colour[1]) + abs(p[2] - colour[2]) < 48:
                        rows.setdefault(y, []).append(x)
            best, by = 0, None
            for y in rows:
                n = sum(len(rows.get(y + k, ())) for k in range(30))
                if n > best:
                    best, by = n, y
            if best < 40:
                return {'tag': None, 'body': None, 'n': (best, 0)}
            xs = sorted(x for k in range(30) for x in rows.get(by + k, ()))
            tx = xs[len(xs) // 2]
            xs = [x for x in xs if abs(x - tx) < 120]  # (the text is short: stray pixels elsewhere do not count)
            if len(xs) < 40:
                return {'tag': None, 'body': None, 'n': (len(xs), 0)}
            bx = []
            for y in range(min(h, by + 30), min(h - 200, by + 370), 2):
                for x in range(max(0, tx - 420), min(w, tx + 420), 2):
                    r, g, b = px[x, y]
                    if r > 140 and g < 70 and b < 70 and r > 2.4 * max(g, b, 1):
                        bx.append(x)
            bx.sort()
            return {'tag': (tx, by), 'body': bx[len(bx) // 2] if len(bx) >= 40 else None, 'n': (len(xs), len(bx))}
    except OSError:
        return None


def shoot(con, name, colour):
    png = os.path.join(lane.out_dir('shots'), name + '.png')
    if not con.call('shot ' + png).get('ok'):
        return None, None
    m = measure(png, colour)
    out = png
    if frames is not None:
        d = frames.frames_dir(lane.out_dir())
        if frames.to_jpeg(png, os.path.join(d, name + '.jpg')):
            out = os.path.join(d, name + '.jpg')
            try:
                os.remove(png)
            except OSError:
                pass
    return m, out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--drag', type=int, default=400, help='mouse px of one turn (play_creature.py: 300-450)')
    ap.add_argument('--turns', type=int, default=12)
    ap.add_argument('--walk', type=float, default=1.6, help='s the host walks away first')
    ap.add_argument('--colour', default='', help='the tag\'s colour RRGGBB (default: asked from coop.find)')
    ap.add_argument('--near', type=float, default=60.0)
    ap.add_argument('--limit', type=int, default=-1)
    a = ap.parse_args()
    host, guest = start('creature')
    ck = Checks()
    time.sleep(3)
    m = re.search(r'viewport x=\d+ y=\d+ w=(\d+) h=(\d+)', guest.call('coop.cam').get('out') or '')
    ck(m is not None, 'the guest has a camera')
    if not m:
        return ck.result()
    W, H = int(m.group(1)), int(m.group(2))
    if a.walk > 0:
        host.call('key 0x57 down')
        time.sleep(a.walk)
        host.call('key 0x57 up')
        time.sleep(2.5)

    def place(con):
        mm = re.search(r'pos=\(\s*(-?[\d.]+)\s+(-?[\d.]+)\s+(-?[\d.]+)\)', con.call('coop.avatar').get('out') or '')
        return [float(v) for v in mm.groups()] if mm else None
    hp, gp = place(host), place(guest)
    dist = sum((hp[i] - gp[i]) ** 2 for i in range(3)) ** 0.5 if hp and gp else -1
    print(f'  the players stand {dist:.1f} m apart', flush=True)
    # the partner on the guest's screen, beside the guest's own creature
    sc = None
    for i in range(16):
        sc = partner_xy(guest)
        if sc and W * 0.2 < sc[0] < W * 0.8 and abs(sc[0] - W / 2) > 130 and 250 < sc[1] < H - 250:
            break
        guest.call(f'drag {W // 2} {H // 2} {W // 2 + 90} {H // 2} right')
        time.sleep(1.5)
    ck(sc is not None, f'the host\'s creature is on the guest\'s screen ({sc})')
    colour = tuple(int(a.colour[i:i + 2], 16) for i in (0, 2, 4)) if len(a.colour) == 6 else tag_colour(guest)
    ck(colour is not None, f'the tag\'s colour is known ({colour})')
    if not sc or not colour:
        ck.faults(host=host, guest=guest)
        return ck.result()
    rest = []
    for i in range(3):
        m, p = shoot(guest, f'tagcam_still_{i + 1}', colour)
        if m and m['tag'] and m['body'] is not None:
            rest.append(m['tag'][0] - m['body'])
        print(f'  still {i + 1}: in the shot: tag {m["tag"] if m else "-"}, body x {m["body"] if m else "-"} '
              f'({m["n"] if m else "-"} px) -> {p}', flush=True)
        time.sleep(0.4)
    ck(len(rest) >= 2, f'camera at rest: the tag and the body under it are found in the shots (tag - body: {rest} px)')
    base = sorted(rest)[len(rest) // 2] if rest else 0
    kinds = {'over': 0, 'off': 0, 'empty': 0, 'none': 0}
    worst = 0.0
    for k in range(a.turns):
        sign = 1 if k % 2 == 0 else -1
        wait = 5 + k % 6  # commands before the shot: one command a frame, the mouse moves in frames 5..12
        guest.call(f'drag {W // 2} {H // 2} {W // 2 + sign * a.drag} {H // 2} right')
        for f in range(wait):
            guest.call('coop.cam')
        m, path = shoot(guest, f'tagcam_turn_{k + 1}', colour)
        if not m or not m['tag']:
            kind, off = 'none', None
        elif m['body'] is None:
            kind, off = 'empty', None
        else:
            off = m['tag'][0] - m['body'] - base
            kind = 'over' if abs(off) <= a.near else 'off'
            worst = max(worst, abs(off))
        kinds[kind] += 1
        print(f'  turn {k + 1} ({sign * a.drag:+d} px, shot {wait + 1} frames after the drag command): {kind}'
              + (f' ({off:+.0f} px)' if off is not None else '')
              + f'; tag {m["tag"] if m else "-"}, body x {m["body"] if m else "-"} -> {path}', flush=True)
        time.sleep(1.5)
    after = []
    for i in range(2):
        m, p = shoot(guest, f'tagcam_after_{i + 1}', colour)
        if m and m['tag'] and m['body'] is not None:
            after.append(m['tag'][0] - m['body'])
        time.sleep(0.4)
    print(f'  camera at rest: the tag {base:+.0f} px beside the body\'s middle before, {after} after the turns; '
          f'{a.turns} shots inside a turn: {kinds["over"]} over the body, {kinds["off"]} off it (up to {worst:.0f} px), '
          f'{kinds["empty"]} over empty ground, {kinds["none"]} without a tag (screen {W} px wide)', flush=True)
    if a.limit >= 0:
        bad = kinds['off'] + kinds['empty']
        ck(bad <= a.limit, f'the tag is over its body while the camera turns ({bad} shots off or over empty ground, '
                           f'limit {a.limit})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
