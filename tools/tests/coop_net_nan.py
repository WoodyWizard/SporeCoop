#!/usr/bin/env python3
"""BUG-0614: a joined player's state with NaN / Inf in its position, rotation or velocity never reaches a body.

A scripted guest (tools/fakepeer.py's protocol) joins the host of a two-game session (Creature) and walks next to
the host's avatar, so the host and the real guest make a puppet of it; then it sends states whose position,
rotation and velocity are NaN or Inf for a few seconds, then good ones again. Checks on the game's own positions
(`coop.bodyrec`: the puppet as both games drew it): the puppet of the scripted guest is finite in every row on the
host and on the real guest, it is still drawn after the bad states, both games run on without a fault.
(SPORE_FIX_OFF=game.finite_pose must FAIL)"""
import math
import struct
import time

import body_trail
from coop_net_robust import Peer, fp, keep_frames
from twoinst import Checks, host_frame, start

NAN, INF = float('nan'), float('inf')


def host_state(peer):
    for typ, p in reversed(peer.got):
        if typ == fp.STATE and len(p) >= struct.calcsize('<BI3f4f3fBBffIII'):
            st = fp.unpack_state(p)
            if st['id'] == 0:
                return st
    return None


def send_states(peer, st, secs, bad=None):
    t0 = time.time()
    k = 0
    while time.time() - t0 < secs:
        a = (time.time() - t0) * 0.8
        pos = tuple(st['pos'][j] + (4 * math.cos(a) if j == 0 else 4 * math.sin(a) if j == 2 else 0) for j in range(3))
        rot, vel = st['rot'], (0.0, 0.0, 0.0)
        if bad:
            v = bad[k % len(bad)]
            pos, rot, vel = (pos[0], v, pos[2]), (v, rot[1], rot[2], rot[3]), (v, v, v)
        with peer.lock:
            peer.c.send_unreliable(fp.STATE, fp.pack_state(peer.id, int(time.time() * 1000), pos, rot, vel, fp.PF_IN_WORLD,
                                                           10.0, 10.0, 0, st['mode'], st['world']))
        k += 1
        time.sleep(0.05)


def rows_of(con, key):
    return [r for r in body_trail.dump(con) if r[1] == key]


def main():
    host, guest = start()
    ck = Checks()
    a = Peer('NanBot')
    t0 = time.time()
    while time.time() - t0 < 20 and (a.id is None or host_state(a) is None):
        time.sleep(0.2)
    st = host_state(a)
    ck(a.id is not None and st is not None, f'the scripted guest joined and hears the host (id {a.id})')
    if a.id is None or st is None:
        return ck.result()
    keep_frames(host, 8)
    key = f'p{a.id}'
    for c in (host, guest):
        body_trail.record(c, True)
    send_states(a, st, 6)
    made = {n: len(rows_of(c, key)) for n, c in (('host', host), ('guest', guest))}
    print(f'  puppet rows before the bad states: {made}', flush=True)
    ck(made['host'] > 20, f'the host draws a puppet of the scripted guest ({made["host"]} rows)')
    t_bad = body_trail.now(host)
    send_states(a, st, 4, bad=[NAN, INF, -INF])
    send_states(a, st, 5)
    t_end = body_trail.now(host)
    for c in (host, guest):
        body_trail.record(c, False)
    for name, c in (('host', host), ('guest', guest)):
        rows = rows_of(c, key)
        bad = [r for r in rows if not all(math.isfinite(x) for x in r[2])]
        after = [r for r in rows if r[0] > t_bad + 4.5 and all(math.isfinite(x) for x in r[2])]
        print(f'  {name}: {len(rows)} puppet rows, {len(bad)} not finite, {len(after)} finite after the bad states'
              + (f' (first bad at {bad[0][0] - t_bad:.2f} s: {bad[0][2]})' if bad else ''), flush=True)
        if name == 'host' or made['guest'] > 20:
            ck(not bad, f'{name}: the scripted guest\'s puppet is drawn at a finite place in every frame ({len(bad)} of {len(rows)} not)')
            ck(len(after) > 10 or t_end - t_bad < 8, f'{name}: the puppet is drawn after the bad states ({len(after)} rows)')
    a.bye()
    time.sleep(2)
    for name, c in (('host', host), ('guest', guest)):
        f1 = host_frame(c)
        time.sleep(1.5)
        ck(host_frame(c) != f1, f'the {name} game runs on (frames go on)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
