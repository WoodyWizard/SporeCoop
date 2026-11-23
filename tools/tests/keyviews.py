"""Cameras for key frames (docs/06-testing.md#testing-key-frames): a key frame must show what its note names, so a
test whose subject is small or far from the game's own camera (an outfit, a planet's animals, a colony's cities) puts
each game's camera there first (`coop.view set`, the test camera of coop_same_view.py) and gives it back after.

  row = nearest_row(con, lambda r: r['key'] in keys)          a census row of the game, the nearest to its camera
  look_at(con, row['pos'], dist=14, height=6)                 this game's camera dist m from it, height m up
  view_off(host, guest)                                       the game's own cameras again (always, also on errors)
"""
import math
import re
import time

import sameview as sv


def look_at(con, place, dist=14.0, height=6.0, k=0, fov=50.0, look_up=1.0):
    """this game's camera dist m away from `place` along the ground, height m up, looking at it; True when set"""
    eye, target = sv.camera_at(place, k, 4, dist, height, look_up)
    return 'now=1' in (con.call(sv.view_cmd(eye, target, fov)).get('out') or '')


def look_both(cons, place, dist=14.0, height=6.0, k=0, fov=50.0, look_up=1.0, settle=1.5):
    """every game's camera at the same place (a same-view frame); True when all were set"""
    ok = all([look_at(c, place, dist, height, k, fov, look_up) for c in cons])
    time.sleep(settle)
    return ok


def view_off(*cons):
    for c in cons:
        try:
            c.call('coop.view off')
        except OSError:
            pass


def nearest_row(con, pred, census='coop.census creatures'):
    """the census row (sameview.parse_census) that `pred` takes, nearest to the game's camera; None when none"""
    rows = [r for r in sv.parse_census(con.call(census).get('out') or '') if pred(r)]
    if not rows:
        return None
    cam = sv.parse_cam(con.call('coop.cam').get('out') or '')
    if cam:
        p = cam['rows'][3]
        rows.sort(key=lambda r: math.dist(r['pos'], p))
    return rows[0]


def burst(tag, note, n=3, gap=1.5, extra_fn=None, **kw):
    """n key frames `gap` s apart (tag, tag_t2, tag_t3, ...), each with extra_fn()'s lines in its manifest: a pose that
    differs in one frame and matches in the next is the relay's lag, one that differs in every frame lasts (BUG-1220)"""
    from twoinst import keyframe
    for i in range(n):
        if i:
            time.sleep(gap)
        try:
            extra = list(extra_fn()) if extra_fn else []
        except (OSError, ValueError, AttributeError) as e:
            extra = [f'extra: {e!r}'[:120]]
        if extra:
            print(f'  ({tag} #{i + 1}: {"; ".join(extra)[:300]})', flush=True)
        keyframe(tag if not i else f'{tag}_t{i + 1}', note, extra=extra, **(dict(kw, settle=0) if i else kw))


# BUG-1459: a same-view camera 12 m from its subject looked through a hut wall (the baby frames of tribe_birth_ask /
# _species / tribe_kid_species: the baby never in the picture), from inside a bush or from inside another member's
# head. clear_view() picks, round the subject, a camera whose line of sight passes no placed game object (`coop.census
# all`: huts, buildings, plants, rocks), no other creature (`coop.census creatures`) and no terrain (`coop.ground`:
# the terrain and the snap surface under sampled points of the line). The objects are taken as upright cylinders of
# a generic size (the census has no bounds): a heuristic, checked by eye in the key frame review.
OBJ_R, OBJ_TOP = 3.0, 6.5    # m: a game object (a hut is ~4 m across and ~5 m high)
CRE_R, CRE_TOP = 1.3, 2.8    # m: a creature or a member
SUBJECT_M = 1.5               # m: rows this close to the subject are the subject
NEAR_END = 2.0                # m of the line next to the target that is not checked (the subject stands there)


def _up(p):
    r = math.sqrt(sum(x * x for x in p)) or 1.0
    return [x / r for x in p]


def _sub(a, b):
    return [a[i] - b[i] for i in range(3)]


def _dot(a, b):
    return sum(a[i] * b[i] for i in range(3))


def line_blockers(eye, target, place, rows):
    """the rows whose upright cylinder the line eye -> target crosses (the last NEAR_END m not checked): ['g 1A2B3C4D
    at 5.1 m', ...]"""
    d = _sub(target, eye)
    length = math.sqrt(_dot(d, d)) or 1.0
    t_max = max(0.0, 1.0 - NEAR_END / length)
    out = []
    for r in rows:
        p = r['pos']
        if math.dist(p, place) < SUBJECT_M or not any(p):
            continue
        rad, top = (CRE_R, CRE_TOP) if r['kind'] == 'c' else (OBJ_R, OBJ_TOP)
        u = _up(p)
        w = _sub(eye, p)
        wh = [w[i] - _dot(w, u) * u[i] for i in range(3)]
        dh = [d[i] - _dot(d, u) * u[i] for i in range(3)]
        dd = _dot(dh, dh)
        t = 0.0 if dd < 1e-9 else min(t_max, max(0.0, -_dot(wh, dh) / dd))
        h = [wh[i] + t * dh[i] for i in range(3)]
        s = [eye[i] + t * d[i] for i in range(3)]
        up_m = _dot(_sub(s, p), u)
        if math.sqrt(_dot(h, h)) < rad and -0.5 < up_m < top:
            out.append(f'{r["kind"]} {r["noun"] or r["key"]} at {t * length:.1f} m')
    return out


def terrain_blocked(con, eye, target, n=5):
    """the first sampled point of the line (from the eye) under the terrain or the snap surface (+0.2 m), as m from the
    eye; None when the line is over the ground everywhere"""
    d = _sub(target, eye)
    length = math.sqrt(_dot(d, d)) or 1.0
    t_max = max(0.0, 1.0 - NEAR_END / length)
    for k in range(n):
        t = t_max * k / max(1, n - 1)
        q = [eye[i] + t * d[i] for i in range(3)]
        out = con.call('coop.ground %.2f %.2f %.2f' % tuple(q)).get('out') or ''
        m = re.search(r'ground ([-\d.]+) point ([-\d.]+)', out)
        if not m:
            continue
        top = float(m.group(1))
        sm = re.search(r'surface ([-\d.]+)', out)
        if sm:
            top = max(top, float(sm.group(1)))
        if float(m.group(2)) < top + 0.2:
            return t * length
    return None


def clear_view(con, place, dist=12.0, height=5.0, look_up=2.0, n=8, tries=None, terrain=True, cam=None):
    """(eye, target, info): the first camera round `place` (n azimuths, then the (dist, height) of `tries`) whose line
    of sight is clear on this game; when none is, the one with the fewest blockers (info says what blocks it).
    terrain=False: the objects only (no coop.ground calls: a moving subject). cam: a dict that gets the chosen camera's
    k, n, dist, height (sameview.camera_at), to set it again round a subject that moved meanwhile"""
    tries = tries or [(dist, height), (dist, height * 1.8), (dist * 0.7, height * 2.4)]
    try:
        rows = sv.parse_census(con.call('coop.census all 40 %.2f %.2f %.2f' % tuple(place)).get('out') or '')
        rows += [r for r in sv.parse_census(con.call('coop.census creatures').get('out') or '')
                 if math.dist(r['pos'], place) < 40]
    except OSError:
        rows = []
    best = None
    for di, he in tries:
        for k in range(n):
            eye, target = sv.camera_at(place, k, n, di, he, look_up)
            bl = line_blockers(eye, target, place, rows)
            if not bl and terrain:
                tb = terrain_blocked(con, eye, target)
                if tb is not None:
                    bl = [f'terrain at {tb:.1f} m']
            if not bl:
                if cam is not None:
                    cam.update(k=k, n=n, dist=di, height=he)
                return eye, target, f'view: clear line of sight (azimuth {k} of {n}, {di:.0f} m away, {he:.0f} m up; ' \
                                    f'{len(rows)} objects near checked)'
            if best is None or len(bl) < len(best[2]):
                best = (eye, target, bl, k, di, he)
    eye, target, bl, k, di, he = best
    if cam is not None:
        cam.update(k=k, n=n, dist=di, height=he)
    return eye, target, f'view: NO clear line of sight found round the subject (best: azimuth {k} of {n}, {di:.0f} m ' \
                        f'away, {he:.0f} m up, blocked by {", ".join(bl[:4])}): the subject may be hidden in this frame'


def look_both_clear(cons, place, dist=12.0, height=5.0, fov=45.0, look_up=2.0, settle=1.5):
    """every game's camera at the same clear camera (chosen on the first game: the host's objects); (ok, info line)"""
    eye, target, info = clear_view(cons[0], place, dist, height, look_up)
    ok = all(['now=1' in (c.call(sv.view_cmd(eye, target, fov)).get('out') or '') for c in cons])
    time.sleep(settle)
    return ok, info
