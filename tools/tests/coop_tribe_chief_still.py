#!/usr/bin/env python3
"""Tribe: the host's chief holds its staff the same way on both games from the session's start (BUG-1069).

same_view_tribe's stills (lab 20261113-051820-2 views 1 and 4, taken in the first minute after the guest joined): the
host's chief held its staff forward in the hand on the host and upright in the resting grip on the guest. The hold
tests (tribe_hold_only / tribe_hold_guest) start sampling 5 s after the guest runs and passed in the same runs.

Two games on the tribe fixture. From the moment the guest's game runs, both games' `coop.tribe anims` rows of the
host's chief (the game's current animation, `hold=`: the hand's started hold modifier) at ~20 Hz and its hand
(`coop.tribe hand`: the item in the hand) at ~3 Hz, for --secs. Printed: every stretch of over --slack s where the two
games disagree on the hold, the hand item or the animation, with both games' animations around it.
Checked (the game's own state on both machines): no stretch of over --slack s where one game's chief holds its item
in the hand and the other's does not; no stretch of over 2 s where both play the same gesture (not a loop) more than
0.5 s apart in it (the track time `t=`); the hand item the same within 3 s of every change.
Twin: SPORE_FIX_OFF=tribe.mirror_phase_any: the chief's gesture begun at the load (before the session) runs on the
guest from its own start, 10-20 s from the host's moment (FAIL).

  SPORE_LANE=6 python3 tools/tests/coop_tribe_chief_still.py [--secs 90] [--slack 1.0]
"""
import argparse
import re
import threading
import time

from twoinst import Checks, game_runs, keyframe, start  # (first: it puts tools/ on the path)
from coop_tribe_looks import view_both
import lane
from sporectl import Console

LOCO = {'02481DE5', '04866D8E', '04866DAB', '04866DD4'}  # the locomotion's idle / walk


def sample(inst, cid, secs, rows, hands, t_end):
    c = Console(inst)
    nh = 0.0
    while time.time() < t_end:
        m = re.search(r'cur ' + cid + r' anim=(\w+) bhv=\w+ .*hold=(\d) t=([-\d.]+) loop=(-?\d)', c.call('coop.tribe anims')['out'])
        if m:
            rows.append((time.time(), m.group(1), int(m.group(2)), float(m.group(3)), int(m.group(4))))
        if time.time() >= nh:
            nh = time.time() + 0.33
            h = re.search(r'item=(-?\d+)', c.call(f'coop.tribe hand {cid}')['out'])
            if h:
                hands.append((time.time(), int(h.group(1))))
        time.sleep(0.04)


def at(rows, t):
    """the last row at or before t"""
    best = None
    for r in rows:
        if r[0] > t:
            break
        best = r
    return best


def stretches(ts, differ, slack):
    """(start, end) of the runs of sample times where differ(t), longer than slack"""
    out, st, last = [], None, None
    for t in ts:
        if differ(t):
            st = t if st is None else st
            last = t
        elif st is not None:
            if last - st > slack:
                out.append((st, last))
            st = None
    if st is not None and last - st > slack:
        out.append((st, last))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--secs', type=float, default=90)
    ap.add_argument('--slack', type=float, default=1.0)
    a = ap.parse_args()
    host, guest = start('tribe')
    ck = Checks()
    game_runs(guest, 90)
    lead = re.findall(r'#(\d+) \S+ owner=0 .*leader=1', host.call('coop.tribe')['out'])
    cid = lead[0] if lead else '1'
    print(f"  the host's chief #{cid}", flush=True)
    rows = {'host': [], 'guest': []}
    hands = {'host': [], 'guest': []}
    t_end = time.time() + a.secs
    ths = [threading.Thread(target=sample, args=(i, cid, a.secs, rows[r], hands[r], t_end))
           for r, i in (('host', lane.HOST), ('guest', lane.GUEST))]
    for t in ths:
        t.start()
    for t in ths:
        t.join()
    hr, gr = rows['host'], rows['guest']
    if len(hr) < 50 or len(gr) < 50:
        print(f'RESULT: SKIP too few samples ({len(hr)} / {len(gr)})', flush=True)
        return 0
    t0 = hr[0][0]
    ts = [r[0] for r in hr]

    def hold_differs(t):
        h, g = at(hr, t), at(gr, t)
        return bool(h and g and h[2] != g[2])

    def moment_differs(t):  # BUG-1069: the same gesture (not a loop) at another moment of it
        h, g = at(hr, t), at(gr, t)
        # (the host's track not a loop; the mirror's may be one: a start of the relay that loops is still another moment)
        return bool(h and g and h[1] == g[1] and h[4] == 0 and h[1] not in LOCO and abs(h[3] - g[3]) > 0.5)

    def anim_differs(t):
        h, g = at(hr, t), at(gr, t)
        return bool(h and g and h[1] != g[1])
    hts = [x[0] for x in hands['host']]

    def item_differs(t):
        h, g = at(hands['host'], t), at(hands['guest'], t)
        return bool(h and g and h[1] != g[1])

    def runs(rr, s, e, k=1):
        out = []
        for r in rr:
            if s - 2 < r[0] < e + 1 and (not out or out[-1][1:] != r[k:k + 2]):
                out.append((round(r[0] - t0, 2),) + tuple(r[k:k + 2]))
        return out[:14]
    agree = sum(not hold_differs(t) for t in ts)
    print(f'  {len(hr)} / {len(gr)} samples; hold the same in {agree} ({100 * agree / len(ts):.1f} %), animation the same '
          f'in {sum(not anim_differs(t) for t in ts)}', flush=True)
    bad_hold = stretches(ts, hold_differs, a.slack)
    for s, e in bad_hold:
        h = at(hr, s)
        print(f'    hold differs {s - t0:.1f}-{e - t0:.1f} s (host hold={h[2]}): host {runs(hr, s, e)}; guest {runs(gr, s, e)}',
              flush=True)
    bad_moment = stretches(ts, moment_differs, 2.0)
    for s, e in bad_moment:
        h, g = at(hr, s), at(gr, s)
        print(f'    moment differs {s - t0:.1f}-{e - t0:.1f} s: {h[1]} host t={h[3]:.2f} loop={h[4]}, guest t={g[3]:.2f} '
              f'loop={g[4]}', flush=True)
    bad_item = stretches(hts, item_differs, 3.0)
    for s, e in bad_item:
        print(f'    hand item differs {s - t0:.1f}-{e - t0:.1f} s: host {runs(hands["host"], s, e)}; '
              f'guest {runs(hands["guest"], s, e)}', flush=True)
    for s, e in stretches(ts, anim_differs, 3.0)[:6]:
        print(f'    animation differs {s - t0:.1f}-{e - t0:.1f} s: host {runs(hr, s, e)}; guest {runs(gr, s, e)}', flush=True)
    if bad_hold:  # the guest's hold events of the chief (S / E the host's, T bound, t a miss, O / C its own, ...)
        tr = guest.call(f'coop.tribe holdtrace {cid}')['out']
        m_now = re.search(r'now ([\d.]+)', tr)
        if m_now:
            gnow, wnow = float(m_now.group(1)), time.time() - t0
            for s, e in bad_hold[:4]:
                ev = []
                for ln in tr.splitlines():
                    p = ln.split()
                    if p[:1] == ['hev'] and s - t0 - 2 < float(p[1]) - gnow + wnow < e - t0 + 1:
                        ev.append(f'{float(p[1]) - gnow + wnow:.2f} {" ".join(p[3:])}')
                print(f'    guest events {s - t0:.1f}-{e - t0:.1f} s: {"; ".join(ev[:20])}', flush=True)
    m = re.search(r'cur ' + cid + r' anim=\w+ bhv=\w+ pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', host.call('coop.tribe anims')['out'])
    if m and view_both(host, guest, tuple(float(m.group(i)) for i in (1, 2, 3)), 10.0, 3.0):
        keyframe('chief', f"the same camera on both games 10 m from the host's chief #{cid}: its staff held the same "
                          f"way (in the hand or in the resting grip, the same angle) on both screens", kind='sameview')
    ck(not bad_hold, f'no stretch over {a.slack} s with the chief\'s item in the hand on one game only '
                     f'({[(round(s - t0, 1), round(e - s, 1)) for s, e in bad_hold]})')
    ck(not bad_moment, f'no stretch over 2 s with the chief in the same gesture over 0.5 s apart on the two games '
                       f'({[(round(s - t0, 1), round(e - s, 1)) for s, e in bad_moment]})')
    ck(not bad_item, f'the chief\'s hand item the same on both games within 3 s ({[(round(s - t0, 1), round(e - s, 1)) for s, e in bad_item]})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
