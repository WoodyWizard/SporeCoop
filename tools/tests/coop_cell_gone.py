#!/usr/bin/env python3
"""Cell: a guest's mirror dies when the host's cell is gone (BUG-0480, divergence detector
`Cell:guest_only:cell`).

A mirror is stale when the guest still shows a cell of the host's pool (a proxy with the host's cell
index, `coop.census all` on the guest) that the host's game had in neither of two dumps around it:
one taken --gap seconds before the guest's dump and one right after it. A cell that died between the
host's first dump and the guest's dump is not counted (its mirror may still be on its way out).

  --case play   both players swim around while the host's cells live and die (no forced loss); also
                counts the detector's `Cell:guest_only:cell` / `host_only:cell` with one host dump
                (SPORE_DETECTOR_BRACKET=0, the old compare) and with the host dumped around the guest
  --case lost   the guest's game misses the host's state for --hold seconds (`coop.pool statehold <s>`:
                MSG_CELL_STATE dropped, a dead link) while the host destroys 8 cells near it
                (`coop.pool cull`); then the state flows again: every mirror of them must be gone
  --case held   the probe's own check: the host's state stays lost while the sample is taken, the
                host destroys cells near the guest: the probe must see stale mirrors (FAILS when it
                sees none, i.e. when the probe could not see the bug)
"""
import argparse
import math
import re
import time

import worlddiff
from twoinst import Checks, start


def ids(w, kind='l', own=None):
    return {x['id']: x for x in w.rows if x['kind'] == kind and (own is None or x['own'] in own)}


def stale_sample(host, guest, gap):
    """(stale proxies, guest proxies near it, host cells): stale = on the guest now, in neither host dump"""
    gw0 = worlddiff.dump(guest, 250)
    centre = gw0.me
    h1 = worlddiff.dump(host, 250, centre) if centre else worlddiff.dump(host, 250)
    time.sleep(gap)
    gw = worlddiff.dump(guest, 250)
    h2 = worlddiff.dump(host, 250, gw.me) if gw.me else worlddiff.dump(host, 250)
    hid = set(ids(h1, own=('npc',))) | set(ids(h2, own=('npc',)))
    near = {i: x for i, x in ids(gw, own=('proxy',)).items()
            if gw.me and all(map(math.isfinite, x['pos'])) and math.dist(x['pos'], gw.me) <= 60.0}
    stale = {i: x for i, x in near.items() if i not in hid}
    if stale:
        print(f'    dumps: host {len(h1.rows)} rows complete={h1.complete}, {len(h2.rows)} complete={h2.complete}; guest {len(gw.rows)} complete={gw.complete}; '
              f'guest at {gw.me}, host centre {h2.me}')
    return stale, near, hid


def pool(con):
    return con.call('coop.pool')['out']


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--case', default='lost', choices=('play', 'lost', 'held'))
    ap.add_argument('--gap', type=float, default=2.5)
    ap.add_argument('--hold', type=float, default=4.0)
    ap.add_argument('--rounds', type=int, default=10)
    a = ap.parse_args()
    host, guest = start('cell')
    ck = Checks()
    try:
        t0 = time.time()
        while time.time() - t0 < 60 and int((re.search(r'mirrors=(\d+)', pool(guest)) or [0, 0])[1]) < 15:
            time.sleep(1)
        print('  guest:', pool(guest).strip().splitlines()[-1][:300])
        total_stale, total_near, worst = 0, 0, []
        det = {False: {}, True: {}}  # detector findings per round: one host dump / host dumped around the guest
        moves = [(20, 0), (0, 20), (-20, 0), (0, -20), (15, 15), (-15, -15)]
        for i in range(a.rounds):
            if a.case == 'play':
                dx, dy = moves[i % len(moves)]
                host.call(f'coop.pool goto {dx} {dy}')
                guest.call(f'coop.pool goto {-dy} {dx}')
                for _ in range(3):
                    host.call('coop.pool kill - hit')
            else:
                # the guest's link goes quiet, the host destroys the cells around the guest meanwhile
                hold = a.hold if a.case == 'lost' else a.gap + 8.0
                out = guest.call(f'coop.pool statehold {hold}')['out'].strip()
                if i == 0:
                    print('  statehold:', out)
                    if not out.startswith('statehold'):
                        ck(False, f'coop.pool statehold works ({out})')
                        break
                t1 = time.time()
                out = host.call('coop.pool cull 30 8')['out'].strip()
                if i == 0:
                    print('  host cull:', out[:200])
                if a.case == 'lost':
                    time.sleep(max(0.0, hold - (time.time() - t1)) + 0.3)
            stale, near, hid = stale_sample(host, guest, a.gap)
            total_stale += len(stale)
            total_near += len(near)
            ex = ', '.join(f'{k} at ({x["pos"][0]:.0f} {x["pos"][1]:.0f}) alive={x["alive"]}' for k, x in list(stale.items())[:3])
            print(f'  round {i + 1}: {len(near)} mirrors within 60 m of the guest, {len(stale)} stale '
                  f'(gone from the host >= {a.gap:.1f} s){": " + ex if ex else ""}', flush=True)
            worst.append(len(stale))
            for k in list(stale)[:3]:  # what each game knows of it (the guest: its mirror entry; the host: the cell)
                print(f'    guest: {guest.call(f"coop.pool cell {k}")["out"].strip()} | host: {host.call(f"coop.pool cell {k}")["out"].strip()}')
            if stale:
                print('    host:', host.call('coop.pool statehold 0')['out'].strip())
                print('    guest pool:', ' '.join(w for w in pool(guest).split() if w.startswith(('late_chunks=', 'rounds_partial=', 'host_of=', 'mirrors=', 'kept=', 'created=', 'destroyed='))))
            if a.case == 'held':
                guest.call('coop.pool statehold 0')
                time.sleep(1.0)
            if a.case == 'play':
                for br in (False, True):
                    worlddiff.CELL_BRACKET = br
                    hw, gw = worlddiff.pair_dump(host, guest)
                    for f in worlddiff.diff(hw, gw):
                        if f['sig'] in ('Cell:guest_only:cell', 'Cell:host_only:cell'):
                            det[br][f['sig']] = det[br].get(f['sig'], 0) + 1
                worlddiff.CELL_BRACKET = True
        print('  guest:', pool(guest).strip().splitlines()[-1][:400])
        ck(total_near > 0, f'the guest mirrored host cells near it ({total_near} over {len(worst)} rounds)')
        if a.case == 'held':
            ck(total_stale > 0, f'the probe sees the mirrors of destroyed cells while the host\'s state is lost '
                                f'({total_stale} stale in {len(worst)} rounds: {worst})')
        else:
            ck(total_stale == 0, f'no mirror of a cell the host no longer has ({total_stale} stale in {len(worst)} rounds: {worst})')
        if a.case == 'play':
            print(f'  detector over {len(worst)} rounds: one host dump {det[False]}, host dumped around the guest {det[True]}')
            ck(det[True].get('Cell:guest_only:cell', 0) <= det[False].get('Cell:guest_only:cell', 0),
               'the bracketed compare reports no more stale-looking mirrors than the old one')
    finally:
        ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
