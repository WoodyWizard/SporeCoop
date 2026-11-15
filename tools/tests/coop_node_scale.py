#!/usr/bin/env python3
"""A spice (commodity) node has the host's size on the guest (BUG-0485).

The detector after the live Cell -> Creature start (campaign_short, campaign_ct): `Creature:scale:node` /
`Tribe:scale:node`, one node at 10.00 on the host and 1.00 on the guest. The game gives a node it makes
live the placement's scale (0xBC05F3 .. SetScale from 0xBC0641: 10), a node read from a planet save
(every guest's shared world, any loaded game) has 1.0: the scale is not saved.

1. Two games on the creature fixture (loaded: every node 1.0 on both games).
2. The host's node nearest the guest gets the scale 10 (`coop.fields scale <node> 10`: the state a
   live-made node has on the host).
3. For 15 s: the guest's node at that place, its scale (the game's GetScale, `coop.wobj list node`).
PASS: within 8 s the guest's node has the host's scale (10) and keeps it.
--nofix (`coop.fix wobj.node_scale off`): must FAIL (1.0 stays).

  SPORE_LANE=4 python3 tools/tests/coop_node_scale.py [--nofix]
"""
import argparse
import math
import re
import struct
import time

from twoinst import Checks, start


def nodes(con, where=''):
    out = []
    for ln in con.call(f'coop.wobj list node 3000 {where}'.strip())['out'].splitlines():
        m = re.match(r'node (\S+) id=\d+ pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\) st=([0-9A-Fa-f]+)', ln)
        if m:
            s = struct.unpack('<f', struct.pack('<I', int(m.group(5), 16)))[0]
            out.append((m.group(1), tuple(float(m.group(i)) for i in (2, 3, 4)), s))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nofix', action='store_true')
    ap.add_argument('--fixture', default='creature')
    a = ap.parse_args()
    host, guest = start(a.fixture)
    ck = Checks()
    if a.nofix:
        for c in (host, guest):
            print(' ', c.call('coop.fix wobj.node_scale off')['out'].strip(), flush=True)
    time.sleep(3)
    gn = nodes(guest)
    hn = nodes(host, ' '.join(f'{v:.1f}' for v in gn[0][1])) if gn else []
    print(f'  guest nodes {len(gn)}, host nodes {len(hn)}', flush=True)
    ck(gn and hn, 'nodes on both games')
    if not (gn and hn):
        return ck.result()
    hp, hpos, hs0 = hn[0]
    ck(abs(hs0 - 1.0) < 0.05, f'the host\'s node is at the loaded scale 1.0 before ({hs0:.2f})')
    print('  host:', host.call(f'coop.fields scale {hp} 10')['out'].strip(), flush=True)
    where = ' '.join(f'{v:.2f}' for v in hpos)
    t0, first_ok, last_bad = time.time(), None, None
    while time.time() - t0 < 15:
        t = time.time() - t0
        g = [n for n in nodes(guest, where) if math.dist(n[1], hpos) < 3]
        hs = [n for n in nodes(host, where) if n[0] == hp]
        gs, h = (g[0][2] if g else None), (hs[0][2] if hs else None)
        same = gs is not None and h is not None and abs(gs - h) < 0.02 * h
        if same and first_ok is None:
            first_ok = t
        if not same:
            last_bad = t
            print(f'    {t:4.1f} s: host {h} guest {gs}', flush=True)
        time.sleep(1)
    ck(first_ok is not None and (last_bad is None or last_bad < 8),
       f'the guest\'s node has the host\'s scale within 8 s and keeps it (first {first_ok}, last difference '
       f'{"-" if last_bad is None else f"{last_bad:.0f} s"})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
