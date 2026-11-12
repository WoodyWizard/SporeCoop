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


def measure(png, colour, origin=None):
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
            def red(x, y):
                r, g, b = px[x, y]
                return r > 140 and g < 70 and b < 70 and r > 2.4 * max(g, b, 1)
            bx = []
            for y in range(min(h, by + 30), min(h - 200, by + 370), 2):
                for x in range(max(0, tx - 420), min(w, tx + 420), 2):
                    if red(x, y):
                        bx.append(x)
            bx.sort()
            span = None
            if bx:  # the creature nearest to the tag (the nest has more of the red species): its columns only, a run
                # of red columns without a gap of 40 px around the red column nearest to the tag
                k = min(range(len(bx)), key=lambda i: abs(bx[i] - tx))
                lo = hi = k
                while lo > 0 and bx[lo] - bx[lo - 1] < 40:
                    lo -= 1
                while hi < len(bx) - 1 and bx[hi + 1] - bx[hi] < 40:
                    hi += 1
                bx = bx[lo:hi + 1]
                span = (bx[0], bx[-1])
            # BUG-0929: the tag's lowest row and the top of the red body under it: in the columns of that creature
            # within 80 px of the tag's middle, the first row from the tag down with 4 red pixels that has red rows
            # under it too (8 of the next 12: stray red pixels of the background do not count)
            tag_bottom = max(y for y in range(by, by + 30) if any(abs(x - tx) < 120 for x in rows.get(y, ())))
            top = None
            if origin:  # from the partner's own point (its feet, projected by the game's camera) upwards: the rows
                # with red within 70 px of it, gaps of up to 8 rows; the last one is the top of that creature
                ox, oy = int(origin[0]), int(origin[1])
                found, last, gap = False, None, 0
                for y in range(min(h - 1, oy + 10), max(0, oy - 420), -1):
                    n = sum(1 for x in range(max(0, ox - 70), min(w, ox + 70)) if red(x, y))
                    if n >= 3:
                        found, last, gap = True, y, 0
                    elif found:
                        gap += 1
                        if gap > 8:
                            break
                    elif y < oy - 120:
                        break
                top = last
            return {'tag': (tx, by), 'body': bx[len(bx) // 2] if len(bx) >= 40 else None, 'n': (len(xs), len(bx)),
                    'span': span if len(bx) >= 40 else None, 'tag_bottom': tag_bottom, 'body_top': top}
    except OSError:
        return None


def colour_count(png, colour):
    """pixels of this colour in the region measure() reads (the scene without tags: how much a tag colour is taken by the
    world itself, e.g. a guest creature with yellow stripes)"""
    try:
        from PIL import Image
        with Image.open(png) as im:
            im = im.convert('RGB').resize((im.width // 2, im.height // 2))
            w, h = im.size
            px = im.load()
            n = 0
            for y in range(50, h - 165):
                for x in range(65, w - 65):
                    p = px[x, y]
                    if abs(p[0] - colour[0]) + abs(p[1] - colour[1]) + abs(p[2] - colour[2]) < 48:
                        n += 1
            return n
    except (ImportError, OSError):
        return 0


def pick_colour(host, guest, W, H, drag):
    """the host's colour for the test: of the palette (not red: the creature fixture's species is red), the one the
    guest's scene has least of, in shots with the name tags off at rest and after a turn each way"""
    import settings_lib as sl
    # (not blue: the sky; not pink: the mating call's hearts come and go)
    palette = {'yellow': 'FFD040', 'green': '5CE05C', 'orange': 'FF9030', 'purple': 'B07CFF', 'cyan': '40E0E0'}
    m = re.search(r'key=(\w+) argb=\w+ name=\S+ \(me\)', host.call('coop.find colors').get('out') or '')
    pick_colour.restore = m.group(1) if m else 'yellow'
    if not sl.force(host, [guest], 'all.name_tags', 'off'):
        return None
    counts = dict.fromkeys(palette, 0)
    for k, d in enumerate((0, drag, -drag)):
        if d:
            guest.call(f'drag {W // 2} {H // 2} {W // 2 + d} {H // 2} right')
            time.sleep(1.5)
        png = os.path.join(lane.out_dir('shots'), f'tagcam_calib_{k}.png')
        if guest.call('shot ' + png).get('ok'):
            for key, hx in palette.items():
                counts[key] += colour_count(png, tuple(int(hx[i:i + 2], 16) for i in (0, 2, 4)))
    sl.force(host, [guest], 'all.name_tags', 'on')
    best = min(counts, key=lambda c: counts[c])
    print(f'  the scene\'s pixels per tag colour (tags off): {counts}; the host takes {best}', flush=True)
    out = host.call(f'coop.find color {best}').get('out') or ''
    if 'ok' not in out:
        return None
    time.sleep(1.5)
    hx = palette[best]
    return tuple(int(hx[i:i + 2], 16) for i in (0, 2, 4))


def px_per_metre(con):
    """(screen px per metre of height at the partner, its point on the screen): its position and 1 m over it (the
    planet's up) projected by this game's camera (`coop.find labels`, `coop.find project`); None when not known"""
    m = re.search(r'label kind=player .*pos=(-?[\d.]+),(-?[\d.]+),(-?[\d.]+)', con.call('coop.find labels').get('out') or '')
    if not m:
        return None
    p = [float(v) for v in m.groups()]
    r = sum(v * v for v in p) ** 0.5 or 1.0
    ys = []
    xs = []
    for h in (0.0, 1.0):
        q = [p[i] + p[i] / r * h for i in range(3)]
        mm = re.search(r'sx=(-?[\d.]+) sy=(-?[\d.]+)', con.call('coop.find project %.3f %.3f %.3f' % tuple(q)).get('out') or '')
        if not mm:
            return None
        xs.append(float(mm.group(1)))
        ys.append(float(mm.group(2)))
    return (abs(ys[0] - ys[1]) or None), (xs[0], ys[0])


def shoot(con, name, colour, origin=None):
    png = os.path.join(lane.out_dir('shots'), name + '.png')
    if not con.call('shot ' + png).get('ok'):
        return None, None
    m = measure(png, colour, origin)
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
    ap.add_argument('--colour', default='', help='the tag\'s colour RRGGBB; auto: the host\'s as it is (coop.find); default: the host picks the palette '
                         'colour the scene has least of')
    ap.add_argument('--near', type=float, default=60.0)
    ap.add_argument('--limit', type=int, default=-1)
    ap.add_argument('--gap', type=float, default=-1,
                    help='BUG-0929: FAIL when the tag stands more than this many metres over the top of its body in the '
                         'shots at rest (-1: printed only)')
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
    if len(a.colour) == 6:
        colour = tuple(int(a.colour[i:i + 2], 16) for i in (0, 2, 4))
    elif a.colour == 'auto':
        colour = tag_colour(guest)
    else:  # a colour the scene does not have (a guest creature with yellow stripes was read as the yellow tag)
        colour = pick_colour(host, guest, W, H, a.drag) or tag_colour(guest)
    # the partner on the guest's screen, beside the guest's own creature
    sc = None
    for i in range(16):
        sc = partner_xy(guest)
        if sc and W * 0.2 < sc[0] < W * 0.8 and abs(sc[0] - W / 2) > 130 and 250 < sc[1] < H - 250:
            break
        guest.call(f'drag {W // 2} {H // 2} {W // 2 + 90} {H // 2} right')
        time.sleep(1.5)
    ck(sc is not None, f'the host\'s creature is on the guest\'s screen ({sc})')
    ck(colour is not None, f'the tag\'s colour is known ({colour})')
    if not sc or not colour:
        ck.faults(host=host, guest=guest)
        return ck.result()
    print('  ' + (guest.call('kit.cam reset').get('out') or 'kit.cam: none (an old SporeKit)').strip(), flush=True)
    rest, gaps = [], []
    pm = px_per_metre(guest)
    ppm, origin = pm if pm else (None, None)
    for i in range(3):
        m, p = shoot(guest, f'tagcam_still_{i + 1}', colour, origin)
        if m and m['tag'] and m['body'] is not None:
            rest.append(m['tag'][0] - m['body'])
        if m and m.get('body_top') is not None:
            gaps.append(m['body_top'] - m['tag_bottom'])
        print(f'  still {i + 1}: in the shot: tag {m["tag"] if m else "-"}, body x {m["body"] if m else "-"} '
              f'({m["n"] if m else "-"} px), tag bottom y {m.get("tag_bottom") if m else "-"}, body top y '
              f'{m.get("body_top") if m else "-"} -> {p}', flush=True)
        time.sleep(0.4)
    print('  ' + (guest.call('coop.find tagh').get('out') or '').strip(), flush=True)
    gap_m = sorted(gaps)[len(gaps) // 2] / ppm if gaps and ppm else None
    print(f'  BUG-0929: the tag\'s lowest row is {gaps} px over the top of the body; {ppm or 0:.1f} px a metre there '
          f'(the game\'s camera at the partner, its point at {origin}) -> {gap_m if gap_m is None else round(gap_m, 2)} m', flush=True)
    if a.gap >= 0:
        ck(gap_m is not None and gap_m <= a.gap,
           f'camera at rest: the tag stands just over its creature, not up in the air ({gap_m} m between the tag and the '
           f'top of the body in the shots, at most {a.gap})')
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
            # over: the tag's middle within its creature's columns (a close creature is wide: its middle is not where
            # its head and the tag are) or within --near px of where it is at rest
            x0, x1 = m['span']
            tx = m['tag'][0]
            off = 0 if x0 - 30 <= tx <= x1 + 30 else (tx - (x1 + 30) if tx > x1 else tx - (x0 - 30))
            if abs(m['tag'][0] - m['body'] - base) <= a.near:
                off = 0
            kind = 'over' if off == 0 else 'off'
            worst = max(worst, abs(off))
        kinds[kind] += 1
        print(f'  turn {k + 1} ({sign * a.drag:+d} px, shot {wait + 1} frames after the drag command): {kind}'
              + (f' ({off:+.0f} px)' if off is not None else '')
              + f'; tag {m["tag"] if m else "-"}, body x {m["body"] if m else "-"} -> {path}', flush=True)
        time.sleep(1.5)
    # BUG-0948: how far the viewer moved between the game's draw and Present over the turns (SporeKit's own measure)
    print('  ' + (guest.call('kit.cam').get('out') or 'kit.cam: none').strip(), flush=True)
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
    if getattr(pick_colour, 'restore', None):  # the lane's later tests: the host's colour as it was, none kept in coop.ini
        host.call(f'coop.find color {pick_colour.restore}')
        host.call('coop.find color none')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
