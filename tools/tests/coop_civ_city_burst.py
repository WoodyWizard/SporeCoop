#!/usr/bin/env python3
"""Civilization: a guest that missed several NPC city foundings gets all of them, and their
nations' vehicles, at once (BUG-0380).

The campaign founds the cities still to come with SpawnNextCity(instant=1), which the host does not
broadcast; a guest that joins misses foundings the same way. The guest's catch-up (BUG-0120)
founded one city per 15 s, so six cities took ~90 s and every vehicle of their nations was missing
on the guest meanwhile (no civilization to make the mirror for): sync-check "v: 6 missing on the
guest" in every campaign_space.

1. Two games on civ_npc. The host founds 3 NPC cities with instant=1 (no broadcast), as the
   campaign's shortcut does.
2. Within 20 s the guest's own game has a city within 60 m of every host city and every vehicle the
   host has (by net id, bound to a game object of the guest: `coop.civ` vehicle lines).
3. 15 s later the guest has no more cities than the host (no city founded twice).

SPORE_FIX_OFF=civ.city_burst: FAILS (one city per 15 s, step 2 times out).

  SPORE_LANE=9 python3 tools/tests/coop_civ_city_burst.py [--cities 3]
"""
import argparse
import math
import re
import time

from play_civ import civ_state
from twoinst import Checks, start


def expansion(con):
    m = re.search(r'eax=(0x[0-9A-Fa-f]+)', con.call('call Simulator::cCivExpansion::Get cdecl')['out'])
    return int(m.group(1), 16) if m else 0


def compare(host, guest):
    hc, hv, _ = civ_state(host)
    gc, gv, _ = civ_state(guest)
    miss_c = [i for i, c in hc.items() if not any(math.dist(c['pos'], d['pos']) < 60 for d in gc.values())]
    miss_v = sorted(set(hv) - set(gv))
    return hc, gc, hv, gv, miss_c, miss_v


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cities', type=int, default=3)
    ap.add_argument('--within', type=float, default=20.0)
    a = ap.parse_args()
    host, guest = start('civ_npc')
    ck = Checks()
    time.sleep(3)
    hc, gc, hv, gv, miss_c, miss_v = compare(host, guest)
    print(f'  before: host {len(hc)} cities {len(hv)} vehicles, guest {len(gc)} cities {len(gv)} vehicles', flush=True)
    ck(hc and not miss_c, f'both games start with the same cities ({len(hc)}, missing on the guest {miss_c})')

    ex = expansion(host)
    ck(ex, 'the host has a city simulator')
    if not ex:
        return ck.result()
    made = 0
    for _ in range(a.cities):
        r = host.call(f'call Simulator::cCivExpansion::SpawnNextCity thiscall {ex:#x} 1 0')['out']
        m = re.search(r'eax=0x([0-9A-Fa-f]+)', r)
        if not m or int(m.group(1), 16) & 0xFF == 0:
            break
        made += 1
    t0 = time.time()
    print(f'  host founded {made} cities with instant=1 (not broadcast)', flush=True)
    ck(made == a.cities, f'the host founded {a.cities} cities ({made})')

    # the host's cities appear a few seconds after the founding
    while time.time() - t0 < 15 and len(civ_state(host)[0]) < len(hc) + made:
        time.sleep(1)
    n_host = len(civ_state(host)[0])
    ck(n_host == len(hc) + made, f'the host has its new cities ({len(hc)} -> {n_host})')

    done_at = None
    while time.time() - t0 < a.within + 10:
        hc2, gc2, hv2, gv2, miss_c, miss_v = compare(host, guest)
        if not miss_c and not miss_v and len(gc2) >= len(hc2):
            done_at = time.time() - t0
            break
        time.sleep(1)
    print(f'  after {time.time() - t0:.0f} s: host {len(hc2)} cities {len(hv2)} vehicles, guest {len(gc2)} cities '
          f'{len(gv2)} vehicles; host cities missing on the guest {miss_c}, vehicles {miss_v}', flush=True)
    ck(done_at is not None and done_at <= a.within,
       f'the guest has every new city and every vehicle of the host within {a.within:.0f} s '
       f'({"%.0f s" % done_at if done_at is not None else "not in %.0f s" % (a.within + 10)})')

    time.sleep(15)
    hc3, gc3, _, _, miss_c, _ = compare(host, guest)
    print(f'  15 s later: host {len(hc3)} cities, guest {len(gc3)}', flush=True)
    ck(len(gc3) == len(hc3) and not miss_c, f'no city founded twice on the guest (host {len(hc3)}, guest {len(gc3)})')
    st = civ_state(guest)[2]
    m = re.search(r'cities_caught_up=(\d+)', st)
    print(f'  guest cities_caught_up={m.group(1) if m else "?"}', flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
