#!/usr/bin/env python3
"""The same view on both games (docs/06-testing.md#testing-same-view): both games' cameras at the same place looking at
the same point with the same field of view (`coop.view set`, a test command), from --dist m round the host's place in
--dirs directions. Each view is a key frame of kind `sameview` (the sheets of tools/frames_review.py show the two
screens side by side for the review by eye), and one automatic check: the creatures each game has in that picture
(its census rows projected with its own camera) must match the other game's, the same species key within 120 px.

  coop_same_view.py [--fixture creature|tribe] [--dist 25] [--height 8] [--dirs 4] [--fov 50]
Twin: SAMEVIEW_SHIFT=<m> moves the guest's camera that many m sideways: the views differ and the check must FAIL.
"""
import argparse
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sameview as sv  # noqa: E402
import twoinst  # noqa: E402
from twoinst import Checks, keyframe, start  # noqa: E402


def place_of(con, mode='Creature'):
    """the player's place: its creature, in Tribe its tribe (coop.avatar is the mouse pointer there)"""
    if mode == 'Tribe':
        m = re.search(r'tribe \d+ \(player\) at \((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\)', con.call('coop.tribe').get('out') or '')
        return [float(v) for v in m.groups()] if m else None
    m = re.search(r'pos=\(\s*(-?[\d.]+)\s+(-?[\d.]+)\s+(-?[\d.]+)\)', con.call('coop.avatar').get('out') or '')
    return [float(v) for v in m.groups()] if m else None


def census(con, mode, place):
    """the creature rows of the game (`creatures`: also outside the Creature stage)"""
    return con.call('coop.census' if mode == 'Creature' else 'coop.census creatures').get('out') or ''


def seen_by(con, mode, place):
    cam = sv.parse_cam(con.call('coop.cam').get('out') or '')
    if not cam:
        return None, None
    rows = sv.parse_census(census(con, mode, place))
    alias = sv.aliases(con.call('coop.creations').get('out') or '')
    return cam, sv.in_view(cam, rows, alias=alias)


def brightness(con, name):
    """the mean brightness (0-255) of a shot of the game, None without Pillow or a shot"""
    try:
        from PIL import Image, ImageStat
    except ImportError:
        return None
    path = twoinst.shot(con, name)
    try:
        with Image.open(path) as im:
            return sum(ImageStat.Stat(im.convert('RGB')).mean) / 3
    except OSError:
        return None


def chief_rows(host, guest):
    """the host's chief's `coop.tribe anims` animation / hold / track time and its hand item on both games"""
    lead = re.findall(r'#(\d+) \S+ owner=0 .*leader=1', host.call('coop.tribe').get('out') or '')
    if not lead:
        return 'no chief'
    out = []
    for name, c in (('host', host), ('guest', guest)):
        m = re.search(r'cur ' + lead[0] + r' anim=(\w+) bhv=(\w+) .*hold=(\d) t=([-\d.]+)', c.call('coop.tribe anims').get('out') or '')
        h = re.search(r'item=(-?\d+)', c.call(f'coop.tribe hand {lead[0]}').get('out') or '')
        out.append(f'{name} anim={m.group(1)} bhv={m.group(2)} hold={m.group(3)} t={m.group(4)} item={h.group(1) if h else "?"}'
                   if m else f'{name} -')
    return f'#{lead[0]} ' + '; '.join(out)


def wait_drawn(players, secs=90):
    """until every game draws its world (a guest's world is black for a while after its load in Tribe): the mean
    brightness of a shot over 25; seconds waited, None when one never drew"""
    t0 = time.time()
    while time.time() - t0 < secs:
        b = {n: brightness(c, f'same_view_drawn_{n}.png') for n, c in players.items()}
        if any(v is None for v in b.values()):
            time.sleep(10)  # no picture to judge (no Pillow): a fixed wait
            return time.time() - t0
        if all(v > 25 for v in b.values()):
            return time.time() - t0
        time.sleep(3)
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--fixture', default='creature')
    ap.add_argument('--dist', type=float, default=25.0)
    ap.add_argument('--height', type=float, default=8.0)
    ap.add_argument('--dirs', type=int, default=4)
    ap.add_argument('--fov', type=float, default=50.0)
    a = ap.parse_args()
    shift = float(os.environ.get('SAMEVIEW_SHIFT') or 0)
    host, guest = start(a.fixture)
    ck = Checks()
    time.sleep(3)
    w = wait_drawn({'host': host, 'guest': guest})
    ck(w is not None, f'both games draw their world ({"after %.0f s" % w if w is not None else "not within 90 s"})')
    mode = (host.call('game.mode').get('out') or '').split()[-1]
    place = place_of(host, mode)
    gplace = place_of(guest, mode)
    print(f'  {mode}: host at {place}, guest at {gplace}' + (f'; TWIN: the guest\'s camera {shift} m aside' if shift else ''),
          flush=True)
    ck(place is not None, 'the host has a place')
    if place is None:
        ck.faults(host=host, guest=guest)
        return ck.result()
    if gplace and sum((place[i] - gplace[i]) ** 2 for i in range(3)) < 60 ** 2:  # both players in the views
        place = [(place[i] + gplace[i]) / 2 for i in range(3)]
    good = 0
    last_eye = None
    try:
        for k in range(a.dirs):
            eye, target = sv.camera_at(place, k, a.dirs, a.dist, a.height)
            last_eye = eye
            geye, gtarget = eye, target
            if shift:
                e1, _, _ = sv.tangent_basis(place)
                geye = [eye[i] + shift * e1[i] for i in range(3)]
                gtarget = [target[i] + shift * e1[i] for i in range(3)]
            ah = host.call(sv.view_cmd(eye, target, a.fov)).get('out') or ''
            ag = guest.call(sv.view_cmd(geye, gtarget, a.fov)).get('out') or ''
            if not ck('now=1' in ah and 'now=1' in ag, f'view {k + 1}: both cameras set ({ah.strip()[:60]} | {ag.strip()[:60]})'):
                continue
            time.sleep(1.5)  # a few frames drawn from there
            hcam, hs = seen_by(host, mode, place)
            gcam, gs = seen_by(guest, mode, place)
            if not ck(hcam and gcam, f'view {k + 1}: both cameras read back'):
                continue
            hpos, gpos = hcam['rows'][3], gcam['rows'][3]
            off = max(abs(hpos[i] - eye[i]) for i in range(3))
            ck(off < 0.05, f'view {k + 1}: the host\'s viewer is where it was put ({off:.3f} m off)')
            r = sv.compare(hs, gs)
            keys = sorted({x['key'] for x in hs + gs})
            keyframe(f'view{k + 1}', f'the same camera on both games ({a.dist:g} m from the players\' place, direction '
                     f'{k + 1} of {a.dirs}, fov {a.fov:g}): the same creatures at the same places in the picture, with '
                     f'the same models, paints and poses', kind='sameview',
                     extra=[f'check: {r["line"]}', 'keys: ' + ' '.join(keys)[:200]])
            print(f'  view {k + 1}: host {len(hs)}, guest {len(gs)}: {r["line"]}', flush=True)
            if mode == 'Tribe':  # BUG-1069: the host's chief at the still on both games (animation, hold, hand item)
                print(f'    host\'s chief at the still: {chief_rows(host, guest)}', flush=True)
            for x in r['only_a'][:4]:
                print(f'    only the host: {x}', flush=True)
            for x in r['only_b'][:4]:
                print(f'    only the guest: {x}', flush=True)
            good += r['ok']
            ck(r['ok'], f'view {k + 1}: the two pictures hold the same creatures ({r["line"]})')
    finally:
        for c in (host, guest):
            c.call('coop.view off')
    time.sleep(1.5)
    back = []
    for name, c in (('host', host), ('guest', guest)):
        st = c.call('coop.view').get('out') or ''
        cam = sv.parse_cam(c.call('coop.cam').get('out') or '')
        d = (sum((cam['rows'][3][i] - last_eye[i]) ** 2 for i in range(3)) ** 0.5) if cam and last_eye else -1
        back.append(f'{name} {d:.1f} m')
        ck(st.startswith('view off') and d > 1, f'{name}: the game\'s own camera is back after coop.view off '
                                                f'(the viewer {d:.1f} m from the last fixed eye)')
    print(f'  {good} of {a.dirs} views matched; cameras back: {", ".join(back)}', flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
