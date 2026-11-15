"""The same view on two games: the creatures each game has in a camera's picture, compared
(docs/06-testing.md#testing-same-view). No game code here: the parsing, the projection and the match are unit-tested by
visual_selftest.py.

  cam = parse_cam(<coop.cam answer>)                 the viewer's rows (right, forward, up, position), view window, viewport
  rows = parse_census(<coop.census [all ...] answer>)  every object row: kind, key (species or noun), alive, own, pos
  seen = in_view(cam, rows, alias=aliases(<coop.creations>))   [{key, own, x, y, dist}] of the rows on the screen
  r = compare(seen_a, seen_b)                         matched pairs, the unmatched of each side, a verdict
"""
import math
import re

NUM = r'(-?[\d.]+(?:e-?\d+)?)'


def parse_cam(text):
    """coop.cam -> {'rows': [right, forward, up, pos], 'window': (wx, wy), 'viewport': (x, y, w, h)}, None when it
    lacks a part (no viewer)"""
    rows = {}
    for m in re.finditer(r'view\[(\d)\] =\s+' + r'\s+'.join([NUM] * 4), text or ''):
        rows[int(m.group(1))] = [float(m.group(i)) for i in range(2, 5)]
    w = re.search(r'viewWindow=\(' + NUM + r'\s+' + NUM + r'\)', text or '')
    vp = re.search(r'viewport x=(\d+) y=(\d+) w=(\d+) h=(\d+)', text or '')
    if len(rows) < 4 or not w or not vp:
        return None
    return {'rows': [rows[i] for i in range(4)], 'window': (float(w.group(1)), float(w.group(2))),
            'viewport': tuple(int(vp.group(i)) for i in range(1, 5))}


def project(cam, p):
    """a world point -> (x, y, depth) on the screen, the way SporeKit's world_to_screen does (sporekit/gamehooks.cpp);
    None behind the camera"""
    right, fwd, up, pos = cam['rows']
    d = [p[i] - pos[i] for i in range(3)]
    x = sum(d[i] * right[i] for i in range(3))
    z = sum(d[i] * fwd[i] for i in range(3))
    y = sum(d[i] * up[i] for i in range(3))
    if z < 0.1:
        return None
    wx, wy = cam['window']
    vx, vy, vw, vh = cam['viewport']
    if wx <= 0 or wy <= 0 or not vw or not vh:
        return None
    nx, ny = x / (z * wx), y / (z * wy)
    return vx + (nx + 1) * 0.5 * vw, vy + (1 - ny) * 0.5 * vh, z


ROW = re.compile(r'obj ([cg])(-?\d+) key=([0-9A-Fa-f]{8}) alive=(\d) hp=\S+ ai=(\d) own=(\S+) owner=(-?\d+) pos=\('
                 + NUM + r' ' + NUM + r' ' + NUM + r'\)(?: noun=([0-9A-Fa-f]{8}))?')


def parse_census(text):
    """coop.census rows: [{'kind', 'id', 'key', 'alive', 'own', 'owner', 'pos', 'noun'}]"""
    out = []
    for m in ROW.finditer(text or ''):
        out.append({'kind': m.group(1), 'id': int(m.group(2)), 'key': m.group(3).upper(), 'alive': int(m.group(4)),
                    'own': m.group(6), 'owner': int(m.group(7)),
                    'pos': (float(m.group(8)), float(m.group(9)), float(m.group(10))),
                    'noun': (m.group(11) or '').upper()})
    return out


def aliases(creations_text):
    """coop.creations -> {key: key}: a player's own creation has its own key in its own game ('mine creature: K
    'name'') and the session's key in the player table ('player N creature: K2/... 'name''); the own key is read as
    the session's, so both games name that creature alike"""
    out = {}
    mine = re.search(r"mine creature: ([0-9A-Fa-f]{8}) '([^']*)'", creations_text or '')
    if not mine or not mine.group(2):
        return out
    for m in re.finditer(r"player \d+ creature: ([0-9A-Fa-f]{8})/\S+ '([^']*)'", creations_text or ''):
        if m.group(2) == mine.group(2) and m.group(1).upper() != mine.group(1).upper():
            out[mine.group(1).upper()] = m.group(1).upper()
    return out


def row_key(r, alias=None):
    """what has to be the same on both games: a creature's species key (and dead or alive); another object's noun"""
    if r['kind'] == 'c':
        k = (alias or {}).get(r['key'], r['key'])
        return f'{k}{"" if r["alive"] else ":dead"}'
    return 'n' + (r['noun'] or r['key'])


def in_view(cam, rows, max_dist=70.0, margin=0, alias=None):
    """the rows the camera's picture holds: [{'key', 'own', 'x', 'y', 'dist'}] (depth under max_dist m)"""
    vx, vy, vw, vh = cam['viewport']
    out = []
    for r in rows:
        s = project(cam, r['pos'])
        if not s:
            continue
        x, y, z = s
        if z > max_dist or not (vx - margin <= x <= vx + vw + margin and vy - margin <= y <= vy + vh + margin):
            continue
        out.append({'key': row_key(r, alias), 'own': r['own'], 'x': round(x, 1), 'y': round(y, 1), 'dist': round(z, 1)})
    return out


def compare(a, b, tol_px=120.0, need=0.75):
    """match the two games' creatures in the picture: the same key within tol_px pixels (closest pairs first).
    ok: both pictures hold something and the matched pairs are at least `need` of the bigger list."""
    pairs = []
    for i, ra in enumerate(a):
        for j, rb in enumerate(b):
            if ra['key'] == rb['key']:
                d = math.hypot(ra['x'] - rb['x'], ra['y'] - rb['y'])
                if d <= tol_px:
                    pairs.append((d, i, j))
    pairs.sort()
    used_a, used_b, matched = set(), set(), []
    for d, i, j in pairs:
        if i in used_a or j in used_b:
            continue
        used_a.add(i)
        used_b.add(j)
        matched.append((a[i], b[j], round(d, 1)))
    only_a = [r for i, r in enumerate(a) if i not in used_a]
    only_b = [r for j, r in enumerate(b) if j not in used_b]
    big = max(len(a), len(b))
    ok = bool(a) and bool(b) and len(matched) >= need * big
    return {'matched': matched, 'only_a': only_a, 'only_b': only_b, 'ok': ok,
            'line': f'{len(a)} / {len(b)} in the picture, {len(matched)} matched, '
                    f'{len(only_a)} only on the first, {len(only_b)} only on the second'}


def tangent_basis(p):
    """two unit vectors along the ground at p on a planet (centre at 0) and its up"""
    r = math.sqrt(sum(x * x for x in p)) or 1.0
    up = [x / r for x in p]
    ref = (0.0, 0.0, 1.0) if abs(up[2]) < 0.9 else (1.0, 0.0, 0.0)
    e1 = [up[1] * ref[2] - up[2] * ref[1], up[2] * ref[0] - up[0] * ref[2], up[0] * ref[1] - up[1] * ref[0]]
    n = math.sqrt(sum(x * x for x in e1))
    e1 = [x / n for x in e1]
    e2 = [up[1] * e1[2] - up[2] * e1[1], up[2] * e1[0] - up[0] * e1[2], up[0] * e1[1] - up[1] * e1[0]]
    return e1, e2, up


def camera_at(place, k, n=4, dist=25.0, height=8.0, look_up=1.0):
    """the k-th of n cameras round a place: (eye, target), dist m away along the ground, height m up, looking at the
    point look_up m over the place"""
    e1, e2, up = tangent_basis(place)
    a = 2 * math.pi * k / n
    d = [math.cos(a) * e1[i] + math.sin(a) * e2[i] for i in range(3)]
    eye = [place[i] + dist * d[i] + height * up[i] for i in range(3)]
    target = [place[i] + look_up * up[i] for i in range(3)]
    return eye, target


def view_cmd(eye, target, fov=50.0):
    return 'coop.view set ' + ' '.join(f'{v:.3f}' for v in list(eye) + list(target)) + f' {fov:g}'
