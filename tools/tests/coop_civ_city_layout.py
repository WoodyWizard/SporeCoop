#!/usr/bin/env python3
"""Civilization: the buildings of NPC cities founded in play stand where the host has them (BUG-0602).

campaign_space with the detector: `Civilization:planet:building_industry` (one building of a city 8-9 m off on
the guest) and the guest's buildings_fixed counter climbing by hundreds after the campaign founded six cities
(SpawnNextCity) and they grew: the guest destroyed and rebuilt one building again and again.

1. Two games on civ_npc. The host founds NPC cities (SpawnNextCity instant=1, as the campaign); the guest's
   game founds its own copies (the catch-up). The host's player civ captures the first new city (cCity::Capture,
   the campaign's shortcut; --no-capture: not).
2. The layout slots of every city on both games (`coop.civ slots <i>`).
3. The host's game builds buildings in each new city (`coop.civ build <i> factory|house|fun`, the game's
   SnapBuildingToSlot as its growth). For 60 s: the game's own buildings of those cities on both games
   (`coop.civ blist <i>`: noun, position).
PASS: in the last 20 s every building of a host city has one of the same noun within 4 m on the guest and the
other way round, and the guest's game does not replace buildings any more.
SPORE_FIX_OFF=civ.city_layout: must FAIL.

  SPORE_LANE=4 python3 tools/tests/coop_civ_city_layout.py [--cities 3]
"""
import argparse
import math
import re
import time

from play_civ import civ_state
from twoinst import Checks, start


def blist(con, ci):
    out = []
    for ln in con.call(f'coop.civ blist {ci}')['out'].splitlines():
        m = re.match(r'b (\w+) pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\) state=(-?\d+) slot=(-?\d+)', ln)
        if m:
            out.append((m.group(1), tuple(float(m.group(i)) for i in (2, 3, 4)), int(m.group(6))))
    return out


def slots(con, ci):
    return [tuple(float(x) for x in m.groups()) for m in
            re.finditer(r'slot \d+ \w+ ([-\d.]+) ([-\d.]+) ([-\d.]+)', con.call(f'coop.civ slots {ci}')['out'])]


def unmatched(a, b, tol=4.0):
    """items of a with no item of the same noun within tol in b (one to one)"""
    used, miss = set(), []
    for noun, p, _ in a:
        best = None
        for k, (n2, q, _) in enumerate(b):
            if k in used or n2 != noun:
                continue
            d = math.dist(p, q)
            if d <= tol and (best is None or d < best[0]):
                best = (d, k)
        if best:
            used.add(best[1])
        else:
            miss.append((noun, tuple(round(x) for x in p)))
    return miss


def fixed(con):
    m = re.search(r'buildings_fixed=\+(\d+)', civ_state(con)[2])
    return int(m.group(1)) if m else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cities', type=int, default=3)
    ap.add_argument('--seconds', type=int, default=60)
    ap.add_argument('--no-capture', action='store_true')
    a = ap.parse_args()
    host, guest = start('civ_npc')
    ck = Checks()
    time.sleep(3)
    before = set(civ_state(host)[0])
    m = re.search(r'eax=(0x[0-9A-Fa-f]+)', host.call('call Simulator::cCivExpansion::Get cdecl')['out'])
    ck(m, 'the host has a city simulator')
    if not m:
        return ck.result()
    for _ in range(a.cities):
        r = host.call(f'call Simulator::cCivExpansion::SpawnNextCity thiscall {m.group(1)} 1 0')['out']
        e = re.search(r'eax=0x([0-9A-Fa-f]+)', r)
        if not e or int(e.group(1), 16) & 0xFF == 0:
            break
        host.call('wait.frames 30 10000')
    t0 = time.time()
    while time.time() - t0 < 40:
        hc, gc = civ_state(host)[0], civ_state(guest)[0]
        if len(hc) > len(before) and set(hc) <= set(gc):
            break
        time.sleep(2)
    hc, gc = civ_state(host)[0], civ_state(guest)[0]
    new = sorted(set(hc) - before)
    print(f'  new cities {new}; host {sorted(hc)}, guest {sorted(gc)}', flush=True)
    ck(new and set(new) <= set(gc), f'the host founded cities and the guest has them ({new})')
    if not new:
        return ck.result()
    time.sleep(5)
    if not a.no_capture:  # the campaign's shortcut: the player's civ takes a new city (cCity::Capture)
        print(f'  host: {host.call(f"coop.civ capture {new[0]}")["out"].strip()}', flush=True)
        time.sleep(15)
    for ci in new:
        hs, gs = slots(host, ci), slots(guest, ci)
        off = [p for p in hs if not any(math.dist(p, q) < 2 for q in gs)]
        print(f'  city {ci} slots: host {len(hs)}, guest {len(gs)}, host slots with no guest slot within 2 m: {len(off)}',
              flush=True)
        print(f'    host  {[tuple(round(x) for x in p) for p in hs]}\n    guest {[tuple(round(x) for x in p) for p in gs]}',
              flush=True)
        for kind in ('factory', 'house', 'fun', 'factory'):
            print(f'  host build {ci} {kind}:', host.call(f'coop.civ build {ci} {kind} nopay')['out'].strip(), flush=True)
    t0, last_bad, hist = time.time(), None, []
    while time.time() - t0 < a.seconds:
        t = time.time() - t0
        bad = []
        for ci in new:
            hb, gb = blist(host, ci), blist(guest, ci)
            mh, mg = unmatched(hb, gb), unmatched(gb, hb)
            if mh or mg:
                bad.append(f'city {ci}: host {len(hb)} guest {len(gb)}; host only {mh}; guest only {mg}')
        hist.append((t, fixed(guest)))
        if bad:
            last_bad = t
            print(f'    {t:4.1f} s: ' + ' | '.join(bad), flush=True)
        time.sleep(3)
    for ci in new:
        print(f'  city {ci} host {blist(host, ci)}\n  city {ci} guest {blist(guest, ci)}', flush=True)
    late = [n for t, n in hist if t >= a.seconds - 20]
    print('  guest buildings_fixed (added) over time:', [(round(t), n) for t, n in hist][::3], flush=True)
    ck(last_bad is None or last_bad < a.seconds - 20,
       f'every building of the new cities stands where the host has it in the last 20 s '
       f'(last difference at {"-" if last_bad is None else f"{last_bad:.0f} s"})')
    ck(not late or late[-1] == late[0], f'the guest does not replace buildings again and again ({late[:1]} -> {late[-1:]})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
