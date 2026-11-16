"""Cameras for key frames (docs/06-testing.md#testing-key-frames): a key frame must show what its note names, so a
test whose subject is small or far from the game's own camera (an outfit, a planet's animals, a colony's cities) puts
each game's camera there first (`coop.view set`, the test camera of coop_same_view.py) and gives it back after.

  row = nearest_row(con, lambda r: r['key'] in keys)          a census row of the game, the nearest to its camera
  look_at(con, row['pos'], dist=14, height=6)                 this game's camera dist m from it, height m up
  view_off(host, guest)                                       the game's own cameras again (always, also on errors)
"""
import math
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
