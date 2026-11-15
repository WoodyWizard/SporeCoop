#!/usr/bin/env python3
"""Tribe: the host's chief plays every animation track at the host's moment on the guest too (BUG-1072).

same_view_tribe's stills (lab 20261113-070558 #3 view 4, 20261113-063317 #3 view 1) showed the host's chief in the same
main animation at the same moment on both games, yet bent on one and upright on the other, the staff at another angle.
The chief plays more than its main track: a second looping track (0x05EF4EE1, a ~2 s loop; also 0x0354559B) and its
locomotion idle 0x02481DE5, which BUG-1069's relay (kind 15) does not cover; each game ran them from its own start,
seconds apart (docs/re/tribe.md#tribe-chief-tracks).

Two games on the tribe fixture. Every --every s for --secs s both games' animation state of the host's chief is read
(AnimatedCreature = creature +0xB54, its +0x184: the playing tracks +0xF0C, the main index +0xFA4, tracks of 0xF0 bytes
from +8: id +0xD8, time +0xA8 (double), +0xA0 -1 loops, +0xB0 passes, +0xC0 fade state) with mem.read, the host's then
the guest's. Checked (the game's own animation state on both machines): of the samples where both games play the same
non-main track or the idle, at least --share have the two tracks' times within --tol s. Key frames `chief1` / `chief2`:
the same camera at the chief on both games (the posture must match).
Twin: SPORE_FIX_OFF=tribe.mirror_phase_tracks: the second track and the idle run seconds apart (FAIL).

  SPORE_LANE=6 python3 tools/tests/coop_tribe_chief_tracks.py [--secs 90] [--every 0.4]
"""
import argparse
import os
import re
import struct
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from twoinst import Checks, game_runs, keyframe, start  # noqa: E402
from coop_tribe_looks import view_both  # noqa: E402

RAW = os.environ.get('PROBE_RAW') == '1'  # the tracks' raw dwords too (exploration)
IDLE = 0x02481DE5


def read(con, addr, n):
    out = con.call(f'mem.read 0x{addr:08X} {n}').get('out') or ''
    b = bytearray()
    for line in out.splitlines():
        parts = line.split()
        if len(parts) > 1 and re.fullmatch(r'[0-9A-F]{8}', parts[0]):
            b += bytes(int(x, 16) for x in parts[1:])
    return bytes(b) if len(b) >= n else None


def u32(b, o):
    return struct.unpack_from('<I', b, o)[0]


def f32(b, o):
    return struct.unpack_from('<f', b, o)[0]


def chief_ptr(con, cid=None):
    out = con.call('coop.tribe').get('out') or ''
    for line in out.splitlines():
        m = re.match(r'\s+#(\d+) ([0-9A-Fa-f]{8}) owner=\S+ .*leader=1', line)
        if m and (cid is None or int(m.group(1)) == cid):
            return int(m.group(1)), int(m.group(2), 16)
    return None, None


def tracks(con, c):
    hdr = read(con, c + 0xB54, 4)
    if not hdr:
        return None
    ac = u32(hdr, 0)
    p = read(con, ac + 0x184, 4) if ac else None
    if not p:
        return None
    st = u32(p, 0)
    b = read(con, st, 0xFB0) if st else None
    if not b:
        return None
    main = u32(b, 0xFA4)
    playing = [u32(b, 0xF0C + 4 * i) for i in range(16)]
    pending = [u32(b, 0xF4C + 4 * i) for i in range(16)]
    rows = []
    for slot, k in enumerate(playing):
        if k > 0xF:
            continue
        tr = b[8 + k * 0xF0: 8 + (k + 1) * 0xF0]
        rows.append(dict(slot=slot, k=k, main=slot == main, id=u32(tr, 0xD8),
                         t=struct.unpack_from('<d', tr, 0xA8)[0], a0=f32(tr, 0xA0), b0=u32(tr, 0xB0),
                         st=u32(tr, 0xC0), c4=f32(tr, 0xC4), fin=f32(tr, 0x88), fout=f32(tr, 0x78),
                         x68=u32(tr, 0x68), x6c=u32(tr, 0x6C), raw=tr))
    return dict(rows=rows, pending=[k for k in pending if k <= 0xF], main=main, fa0=u32(b, 0xFA0), fa8=u32(b, 0xFA8))


def fmt(s):
    if not s:
        return '-'
    o = [f'main={s["main"]} fa8={s["fa8"]} pend={s["pending"]}']
    for r in s['rows']:
        o.append(f'  [{r["slot"]}{"*" if r["main"] else ""} k{r["k"]}] id={r["id"]:08X} t={r["t"]:.3f} a0={r["a0"]:.2f} '
                 f'b0={r["b0"]} st={r["st"]} c4={r["c4"]:.3f} fin={r["fin"]:.3f} fout={r["fout"]:.3f} '
                 f'68={r["x68"]:08X} 6c={r["x6c"]:08X}')
        if RAW:
            o.append('     ' + ' '.join(f'{u32(r["raw"], i):08X}' for i in range(0, 0xF0, 4)))
    return '\n'.join(o)


def pos_of(con, cid):
    m = re.search(r'#' + str(cid) + r' \S+ owner=\S+ .*?pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', con.call('coop.tribe').get('out') or '')
    return tuple(float(m.group(i)) for i in (1, 2, 3)) if m else None


def shared(hs, gs):
    """(id, host t, guest t) of every track both games play that is not the main one (or is the idle)"""
    out = []
    for h in hs['rows']:
        if h['main'] and h['id'] != IDLE:
            continue
        g = next((x for x in gs['rows'] if x['id'] == h['id'] and x['st'] != 3), None)
        if g and h['st'] != 3:
            out.append((h['id'], h['t'], g['t']))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--secs', type=float, default=90)
    ap.add_argument('--every', type=float, default=0.4)
    ap.add_argument('--tol', type=float, default=0.3)
    ap.add_argument('--share', type=float, default=0.85)
    a = ap.parse_args()
    host, guest = start('tribe')
    ck = Checks()
    game_runs(guest, 90)
    time.sleep(5)
    cid, hc = chief_ptr(host)
    _, gc = chief_ptr(guest, cid)
    print(f'  chief #{cid}: host {hc and hex(hc)}, guest {gc and hex(gc)}', flush=True)
    if not ck(hc and gc, 'the chief on both games'):
        ck.faults(host=host, guest=guest)
        return ck.result()
    t0, n, good, per = time.time(), 0, 0, {}
    shots = 0
    while time.time() - t0 < a.secs:
        hs = tracks(host, hc)
        gs = tracks(guest, gc)
        if hs and gs:
            sh = shared(hs, gs)
            for tid, ht, gt in sh:
                n += 1
                ok = abs(ht - gt) <= a.tol
                good += ok
                p = per.setdefault(tid, [0, 0])
                p[0] += 1
                p[1] += ok
            if RAW or int(time.time() - t0) % 5 == 0:
                print(f'--- {time.time() - t0:6.1f} s ' + ' '.join(f'{tid:08X} {ht:.2f}/{gt:.2f}' for tid, ht, gt in sh),
                      flush=True)
                if RAW:
                    print('host  ' + fmt(hs) + '\nguest ' + fmt(gs), flush=True)
        if shots < 2 and time.time() - t0 > (25 if shots == 0 else 60):
            p = pos_of(host, cid)
            if p and view_both(host, guest, p, 10.0, 3.5, k=shots):
                keyframe(f'chief{shots + 1}', 'the host\'s chief from the same camera on both games: the same posture (bent '
                         'or upright alike) and the staff at the same angle', kind='sameview', settle=0)
            for c in (host, guest):
                c.call('coop.view off')
            shots += 1
        time.sleep(a.every)
        if int(time.time() - t0) % 15 == 0:  # (a mirror made again has another object)
            _, gc2 = chief_ptr(guest, cid)
            gc = gc2 or gc
    for tid, (k, okk) in sorted(per.items()):
        print(f'  track {tid:08X}: {okk} of {k} samples within {a.tol} s', flush=True)
    print('  ' + ' | '.join(ln for ln in guest.call('coop.tribe anims')['out'].splitlines() if ln.startswith(('tracks', 'phase'))),
          flush=True)
    share = good / n if n else 0
    ck(n >= 30, f'both games play the chief\'s other tracks / idle together in enough samples ({n})')
    ck(share >= a.share, f'the chief\'s other tracks and idle run at the host\'s moment on the guest: {good} of {n} samples '
                         f'within {a.tol} s ({share:.0%}, need {a.share:.0%})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
