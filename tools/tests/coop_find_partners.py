#!/usr/bin/env python3
"""F2 of 0.7.12 "Find each other" (BUG-0764, docs/08-ui.md#ui-find-each-other): the player colours, the arrows at the
screen edge to the partners off screen, the partners on the game's minimap.

--stage creature (the default, the full check), two players:
  1. colours: both games show every player in the same palette colour, the colours are distinct; the host picks pink,
     a guest asking for the host's colour is refused, the guest's pick (cyan) shows on both games;
  2. the guest goes 80 m from the host (8 directions tried) until the host is off the guest's screen in front of its
     camera: the guest's game draws an arrow at the screen edge whose direction is the direction to the host's real
     position (the host game's own creature position, projected by the guest's game), within 8 degrees; the
     arrow's pixels in the guest's screenshot have the host's colour; the distance shown is about 80 m;
  3. the guest next to the host with the host on its screen: no arrow, the name tag;
  4. the host switches all.partner_arrows off in play (live): the guest's game gets it and draws no arrow (pixels),
     on again: the arrow is back; Rivals mode on the guest's game: no arrow;
  5. minimap: each game's minimap (the game's own blip list, read back from the game's map) has the partner at the
     partner's real position on the other game, in the partner's colour; all.partner_map off removes it.
--stage tribe | cell | civ | space: the colours, the arrow / tag rows pointing at the partner's real position (Tribe:
  its chief), the minimap (Tribe), and screenshots for the owner's review (out/[laneN/]shots/find_<stage>_*.png).
Twins: SPORE_FIX_OFF=find.arrows fails 2 (no arrow), find.colors fails 1 (each game its own colours), find.map
fails 5 (no blip).

  SPORE_LANE=9 python3 tools/tests/coop_find_partners.py [--stage creature]
"""
import argparse
import math
import re
import sys
import time

from PIL import Image

import twoinst
from twoinst import Checks, start

PALETTE = {'yellow': 0, 'blue': 1, 'pink': 2, 'green': 3, 'orange': 4, 'purple': 5, 'cyan': 6, 'red': 7}


def wait(cond, secs, step=0.5):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return None


def kv(line):
    return dict(re.findall(r'(\w+)=(\S+)', line))


def rows(con):
    """the arrows / tags of the last overlay pass: dicts with kind 'arrow' or 'tag'"""
    out = []
    me = None
    for line in con.call('coop.find')['out'].splitlines():
        w = line.split(' ', 1)
        if w[0] in ('arrow', 'tag'):
            d = kv(line)
            d['kind'] = w[0]
            d['p'] = tuple(float(x) for x in d['pos'].split(','))
            out.append(d)
        elif w[0] == 'me':
            me = tuple(float(x) for x in kv(line)['pos'].split(','))
    return out, me


def row_of(con, pid):
    rs, _ = rows(con)
    for r in rs:
        if int(r['id']) == pid:
            return r
    return None


def me_of(con):
    return rows(con)[1]


def colors(con):
    t = {}
    for line in con.call('coop.find colors')['out'].splitlines():
        if line.startswith('color '):
            d = kv(line)
            t[int(d['id'])] = (int(d['idx']), d['argb'])
    return t


def blips(con):
    t = {}
    for line in con.call('coop.find map')['out'].splitlines():
        if line.startswith('blip '):
            d = kv(line)
            t[int(d['id'])] = (tuple(float(x) for x in d['pos'].split(',')), d['argb'])
    return t


def map_off(b, real):
    """metres between a blip (the game's map stores a unit direction from the planet's centre) and a real position"""
    r = math.sqrt(sum(x * x for x in real))
    bl = math.sqrt(sum(x * x for x in b)) or 1.0
    return math.dist(tuple(x / bl for x in b), tuple(x / r for x in real)) * r


def avatar(con):
    m = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', con.call('coop.avatar')['out'])
    return tuple(float(m.group(i)) for i in (1, 2, 3)) if m else None


def setting(con, key):
    m = re.search(re.escape(key) + r'=(-?\d+)', con.call(f'coop.settings get {key}')['out'])
    return int(m.group(1)) if m else None


def basis(p):
    n = [v / math.sqrt(sum(x * x for x in p)) for v in p]
    ax = (0, 0, 1) if abs(n[2]) < 0.9 else (1, 0, 0)
    u = (n[1] * ax[2] - n[2] * ax[1], n[2] * ax[0] - n[0] * ax[2], n[0] * ax[1] - n[1] * ax[0])
    ul = math.sqrt(sum(x * x for x in u))
    u = [x / ul for x in u]
    v = (n[1] * u[2] - n[2] * u[1], n[2] * u[0] - n[0] * u[2], n[0] * u[1] - n[1] * u[0])
    return u, v


def at(p, u, v, a, d):
    return tuple(p[k] + d * (math.cos(a) * u[k] + math.sin(a) * v[k]) for k in range(3))


def project(con, p):
    d = kv(con.call('coop.find project %.3f %.3f %.3f' % p)['out'])
    return float(d['sx']), float(d['sy']), int(d['sw']), int(d['sh']), int(d['filled'])


def argb_rgb(s):
    v = int(s, 16)
    return ((v >> 16) & 255, (v >> 8) & 255, v & 255)


def arrow_pixels(path, r, sw, rgb, tol=70):
    """pixels of colour rgb in the arrow's body (back from its tip along its direction) in the screenshot"""
    im = Image.open(path).convert('RGB')
    k = im.width / float(sw)
    x, y, a = float(r['x']), float(r['y']), float(r['ang'])
    n = 0
    for back in (6, 9, 12, 15):
        for side in (-3, 0, 3):
            px = (x - math.cos(a) * back - math.sin(a) * side) * k
            py = (y - math.sin(a) * back + math.cos(a) * side) * k
            if 0 <= px < im.width and 0 <= py < im.height:
                c = im.getpixel((int(px), int(py)))
                if sum(abs(c[i] - rgb[i]) for i in range(3)) < tol:
                    n += 1
    return n


def shot(con, name):
    con.call('wait.frames 3 3000')
    return twoinst.shot(con, name)


def check_colors(ck, host, guest):
    th = wait(lambda: (lambda t: t if len(t) >= 2 and all(v[0] >= 0 for v in t.values()) else None)(colors(host)), 20)
    tg = wait(lambda: (lambda t: t if len(t) >= 2 and all(v[0] >= 0 for v in t.values()) else None)(colors(guest)), 20)
    print('  colours host:', th, 'guest:', tg, flush=True)
    ck(th and tg and th == tg, 'every player has the same colour on both games')
    ck(th and len({v[0] for v in th.values()}) == len(th), 'the colours are distinct')
    return th


def creature(ck, host, guest, stage):
    th = check_colors(ck, host, guest)
    # 1. a pick in the lobby's API
    print('  host pink:', host.call('coop.find color pink')['out'].strip(), flush=True)
    wait(lambda: colors(guest).get(0, (None,))[0] == PALETTE['pink'], 8)
    r = guest.call('coop.find color pink')['out'].strip()
    print('  guest pink:', r, flush=True)
    ck(r.startswith('refused'), "a guest cannot take the host's colour")
    print('  guest cyan:', guest.call('coop.find color cyan')['out'].strip(), flush=True)
    ok = wait(lambda: colors(host).get(1, (None,))[0] == PALETTE['cyan'] and colors(guest).get(1, (None,))[0] == PALETTE['cyan']
              and colors(guest).get(0, (None,))[0] == PALETTE['pink'], 8)
    ck(ok, "the guest's pick (cyan) and the host's (pink) show on both games")
    host_rgb = argb_rgb(colors(guest).get(0, (0, 'FFFF5FC8'))[1])
    for c in (host, guest):
        c.call('coop.team off')
    # 2. the guest 80 m away, the host off its screen in front of its camera
    hp = avatar(host)
    u, v = basis(hp)
    found = tag_dir = None
    for k in range(16):
        a = k * math.pi / 8
        guest.call('coop.abilities goto %.3f %.3f %.3f' % at(hp, u, v, a, 80.0))
        time.sleep(2.0)
        r = row_of(guest, 0)
        print(f'  dir {k}: {r and (r["kind"], r["filled"], r["x"], r["y"], r["dist"])}', flush=True)
        if not r or not 65 < float(r['dist']) < 100:  # the goto found no ground there: not 80 m away
            continue
        if r['kind'] == 'tag' and tag_dir is None:  # 3. the host on the guest's screen: the name tag, no arrow
            tag_dir = a
            shot(guest, f'find_{stage}_onscreen.png')
        if r['kind'] == 'arrow' and r['filled'] == '1' and r['walked'] == '0':
            found = a
            if tag_dir is not None:
                break
    ck(found is not None, 'a place 80 m from the host where the host is off the guest\'s screen, in front of it')
    if found is not None:
        time.sleep(1.0)
        r = row_of(guest, 0)
        real = avatar(host)
        sx, sy, sw, sh, filled = project(guest, real)
        ref = math.atan2(sy - sh / 2, sx - sw / 2)
        ang = float(r['ang'])
        dd = abs(math.degrees(math.atan2(math.sin(ang - ref), math.cos(ang - ref))))
        print(f'  arrow ({r["x"]}, {r["y"]}) ang {math.degrees(ang):.1f}, the host\'s real position projected '
              f'({sx:.0f}, {sy:.0f}) of {sw}x{sh}: {math.degrees(ref):.1f} deg, diff {dd:.1f}, dist {r["dist"]}', flush=True)
        ck(filled and dd < 8, f"the arrow points at the host's real position ({dd:.1f} deg)")
        x, y = float(r['x']), float(r['y'])
        ck(min(x, y, sw - x, sh - y) < 60, f'the arrow is at the screen edge ({x:.0f}, {y:.0f})')
        ck(65 < float(r['dist']) < 100, f'the distance shown is about 80 m ({r["dist"]})')
        path = shot(guest, f'find_{stage}_arrow.png')
        n = arrow_pixels(path, r, sw, host_rgb)
        print(f'  arrow pixels in the host\'s colour: {n}', flush=True)
        ck(r['drawn'] == '1' and n >= 3, f"the guest's screen shows the arrow in the host's colour ({n} pixels)")
        # 4. the setting off in play (live), on again; Rivals
        print('  host:', host.call('coop.settings set all.partner_arrows off')['out'].strip(), flush=True)
        ck(wait(lambda: setting(guest, 'all.partner_arrows') == 0, 5), 'the running guest got all.partner_arrows off')
        time.sleep(1.0)
        r2 = row_of(guest, 0)
        n2 = arrow_pixels(shot(guest, f'find_{stage}_arrows_off.png'), r2 or r, sw, host_rgb)
        ck(r2 and r2['drawn'] == '0' and n2 == 0, f'no arrow with the setting off ({n2} pixels)')
        host.call('coop.settings set all.partner_arrows on')
        ck(wait(lambda: setting(guest, 'all.partner_arrows') == 1, 5), 'the setting on again')
        time.sleep(1.0)
        r3 = row_of(guest, 0)
        n3 = arrow_pixels(shot(guest, f'find_{stage}_arrows_on.png'), r3 or r, sw, host_rgb)
        ck(r3 and r3['drawn'] == '1' and n3 >= 3, f'the arrow is back ({n3} pixels)')
        print('  guest:', guest.call('coop.find rivals 1')['out'].strip(), flush=True)
        time.sleep(1.0)
        r4 = row_of(guest, 0)
        n4 = arrow_pixels(shot(guest, f'find_{stage}_rivals.png'), r4 or r, sw, host_rgb)
        guest.call('coop.find rivals 0')
        ck(r4 and r4['drawn'] == '0' and n4 == 0, f'no arrow in Rivals mode ({n4} pixels)')
    # 5. the minimap: the partner at its real position on the other game, in its colour
    time.sleep(1.5)
    gb, hb = blips(guest), blips(host)
    real_h, real_g = avatar(host), avatar(guest)
    print(f'  guest map: {gb}  host real {real_h}', flush=True)
    print(f'  host map: {hb}  guest real {real_g}', flush=True)
    ck(0 in gb and map_off(gb[0][0], real_h) < 4 and gb[0][1] == colors(guest)[0][1],
       "the guest's minimap has the host at its real position, in its colour")
    ck(1 in hb and map_off(hb[1][0], real_g) < 4 and hb[1][1] == colors(host)[1][1],
       "the host's minimap has the guest at its real position, in its colour")
    shot(guest, f'find_{stage}_map.png')
    shot(host, f'find_{stage}_map_host.png')
    host.call('coop.settings set all.partner_map off')
    ck(wait(lambda: 0 not in blips(guest), 5), 'all.partner_map off: the host is no longer on the guest\'s minimap')
    host.call('coop.settings set all.partner_map on')
    ck(wait(lambda: 0 in blips(guest), 5), 'all.partner_map on: back')
    ck(tag_dir is not None, 'the host on the guest\'s screen 80 m away: no arrow (its name tag shows)')


def other(ck, host, guest, stage):
    """Tribe, Cell, Civ, Space: the colours, the rows point at the partner's real position, screenshots"""
    check_colors(ck, host, guest)
    if stage == 'cell':
        guest.call('coop.pool tp 40 0')
        time.sleep(2)
    if stage == 'tribe':
        # the host's chief 150 m from where it stands, in turn in four directions until it is off the guest's view
        for deg in (0, 90, 180, 270):
            print('  host:', host.call(f'coop.tribe chief_away 150 {deg}')['out'].strip(), flush=True)
            time.sleep(3)
            r = row_of(guest, 0)
            if r and r['kind'] == 'arrow':
                break
    if stage in ('civ', 'space'):
        # Civ: the host's pointer (where it plays) pinned 400 m away; Space: the host's ship put over a point 400 m away;
        # four directions in turn until it is off the guest's view
        me = me_of(host)
        if me:
            u, v = basis(me)
            for deg in (0, 90, 180, 270):
                q = at(me, u, v, math.radians(deg), 400.0)
                rq, rm = math.sqrt(sum(x * x for x in q)), math.sqrt(sum(x * x for x in me))
                q = tuple(x * rm / rq for x in q)
                cmd = ('coop.npc marker %.1f %.1f %.1f' if stage == 'civ' else 'coop.space over %.1f %.1f %.1f') % q
                out = host.call(cmd)['out'].strip()
                if stage == 'space' and 'not on a planet' in out:  # in a star system: the ship moved in its plane
                    d = (400.0 * math.cos(math.radians(deg)), 400.0 * math.sin(math.radians(deg)), 0.0)
                    out = host.call('coop.space shipaway %.1f %.1f %.1f' % d)['out'].strip()
                print('  host:', out[:120], flush=True)
                time.sleep(3)
                r = row_of(guest, 0)
                if r and r['kind'] == 'arrow':
                    break
    time.sleep(2)
    r = row_of(guest, 0)
    me_h = me_of(host)
    print(f'  guest row for the host: {r}', flush=True)
    print(f'  host me: {me_h}', flush=True)
    ck(r is not None, f'{stage}: the guest\'s game has the host as an arrow or a tag')
    if r and me_h:
        d = math.dist(r['p'], me_h)
        ck(d < 6, f"{stage}: it points at the host's real position ({d:.1f} m off)")
        if r['kind'] == 'arrow':
            sx, sy, sw, sh, filled = project(guest, me_h)
            if filled:
                ref = math.atan2(sy - sh / 2, sx - sw / 2)
                ang = float(r['ang'])
                dd = abs(math.degrees(math.atan2(math.sin(ang - ref), math.cos(ang - ref))))
                ck(dd < 8, f'{stage}: the arrow direction is the projected real position ({dd:.1f} deg)')
    shot(guest, f'find_{stage}_guest.png')
    shot(host, f'find_{stage}_host.png')
    if stage == 'tribe':
        best = None  # the chief may still walk: the blip (0.25 s updates) against the position read right before it
        for _ in range(4):
            time.sleep(1.0)
            me_h = me_of(host)
            gb = blips(guest)
            if 0 in gb and me_h:
                off = map_off(gb[0][0], me_h)
                best = off if best is None else min(best, off)
        print(f'  guest map: {gb}, the nearest {best} m', flush=True)
        ck(best is not None and best < 6, f"tribe: the guest's minimap has the host's chief at its position ({best} m)")
        if '--icons' in sys.argv:  # looks: the game's blip icons by flags (docs/re/creature.md#creature-minimap)
            for f in ('2', '10', '40', '1', '10000', '0', '4', '20', '80', '100', '200', '400', '800', '1000'):
                guest.call('coop.find map ' + f)
                time.sleep(1.5)
                print('  icon', f, [l for l in guest.call('coop.find map')['out'].splitlines() if l.startswith('blip')], flush=True)
                shot(guest, f'icon_tribe_{f}.png')
            guest.call('coop.find map 2')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--stage', default='creature', choices=('creature', 'tribe', 'cell', 'civ', 'space'))
    ap.add_argument('--icons', action='store_true')
    a = ap.parse_args()
    host, guest = start(a.stage)
    ck = Checks()
    try:
        time.sleep(3)
        if a.stage == 'creature':
            creature(ck, host, guest, a.stage)
        else:
            other(ck, host, guest, a.stage)
    finally:
        for c in (host, guest):
            try:
                c.call('coop.settings force all.partner_arrows on')
                c.call('coop.settings force all.partner_map on')
                c.call('coop.find rivals 0')
                c.call('coop.find color none')
                c.call('coop.npc marker')
                c.call('coop.team on')
            except OSError:
                pass
        twoinst.forget_settings()
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
