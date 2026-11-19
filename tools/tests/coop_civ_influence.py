#!/usr/bin/env python3
"""Civilization: the cities' religious and economic standing is the host's on the guests (WP-F2 of 0.7.16; found by the
Lineages fork (PR #196), its BUG-9078, 9084, 9086, 9081). Two games on civ_npc; every check reads the game's own state
(`coop.civ influence <city>`: the city's follower map +0x5A0, converter +0x754, meter +0x750, buy meter +0x758 / buyer
+0x75C, price map +0x570, preaching slot capacities +0x618; `coop.civ canbuy`: the game's CanBuyCity / GetBuyPrice;
`coop.civ roads`: the cities' route lists).

  --case attribution  BUG-1284: the host governs religious (`coop.role 3`); an economic vehicle of the player next to an
                      NPC city gives no conversion bonus (ConvertCitizens count 1 converts 1), a religious one does (2).
                      SPORE_FIX_OFF=civ.religious_attribution: the trader gives the bonus: FAIL.
  --case religion     BUG-1285: the host converts followers of an NPC city (the game's ConvertCitizens, 2 at a time until
                      the player has some; not all: at a meter of 100 the game captures the city, then it is 0): the
                      guest's game has the same followers of the player civilization and the same meter within 5 s.
                      SPORE_FIX_OFF=civ.religious_state: the guest's stay 0: FAIL.
  --case preach       BUG-1286: three religious vehicles put 80 m from the NPC city farthest from the host's camera and
                      ordered to convert it (religious attack, type 3): its preaching slots get places (not 0/0) and
                      followers of the player civilization appear within 90 s, on the host and on the guest.
                      SPORE_FIX_OFF=civ.remote_preaching: 0/0, nobody converted: FAIL.
  --case economy      BUG-1287: the host's route NPC city - player city (the game's AddTradeRoute) and its buy meter at
                      100 for the player: the guest's city has the buyer, the meter and the price entry, and the guest's
                      own CanBuyCity says yes with a price; the host's game removes the route (RemoveTradeRoute at both
                      ends): the guest's route goes within 15 s.
                      SPORE_FIX_OFF=civ.economic_state: no meter, no price, the route stays: FAIL.
"""
import argparse
import math
import re
import struct
import time

from play_civ import civ_state, frame_city
from twoinst import Checks, keyframe, start

CITY = re.compile(r'^city (\d+) ([0-9A-Fa-fx]+) owner=(\w+) convert=([-\d.]+) convert_id=(\w+) buy=([-\d.]+) buyer=(\w+) '
                  r'followers:(.*?) prices:(.*?) slots:(.*)$', re.M)


def influence(con, ci):
    out = con.call(f'coop.civ influence {ci}')['out']
    pol = re.search(r'player_pol=(\w+)', out)
    m = CITY.search(out)
    if not m:
        return None, pol.group(1) if pol else None
    kv = lambda s: {k: v for k, v in (x.split('=') for x in s.split())}
    d = dict(ptr=int(m.group(2), 16), owner=m.group(3), convert=float(m.group(4)), convert_id=m.group(5),
             buy=float(m.group(6)), buyer=m.group(7), followers={k: int(v) for k, v in kv(m.group(8)).items()},
             prices={k: float(v) for k, v in kv(m.group(9)).items()},
             slots=[tuple(int(x) for x in s.split('/')) for s in m.group(10).split()])
    return d, pol.group(1) if pol else None


def wait_for(fn, secs, step=0.5):
    t0 = time.time()
    v = fn()
    while not v and time.time() - t0 < secs:
        time.sleep(step)
        v = fn()
    return v


def new_vehicle(host, home, purpose):
    # `force`: the civ_npc fixture's civilization (one military city) has no religious or economic vehicle allowance
    # (VehicleSlotsLeft 0 even with all its vehicles removed: the allowance 0xBF2110 counts cities of that purpose;
    # lab runs 20261119-104136, 20261119-114425), so these are spawned past it
    _, before, _ = civ_state(host)
    print('  host spawnveh:', host.call(f'coop.civ spawnveh {home} {purpose} 0 force')['out'].strip(), flush=True)
    time.sleep(2)
    _, after, _ = civ_state(host)
    new = [i for i, v in after.items() if i not in before and v['purpose'] == purpose and v['owner'] >= 0]
    return new[0] if new else None


def put_near(host, vid, pos, d):
    # d m along x from the city, then back onto the sphere of the city's distance from the centre: a point d m off along x
    # alone is up in the air (lab run 20261119-114030: 90 m off, 34 m over the ground; the host's preacher stayed there,
    # the guest's game put its copy on the ground, a sync-check MISMATCH v6 34.0)
    p = (pos[0] + d, pos[1], pos[2])
    k = math.hypot(*pos) / math.hypot(*p)
    return host.call(f'coop.civ vput {vid} {p[0] * k:.1f} {p[1] * k:.1f} {p[2] * k:.1f}')['out'].strip()


def followers_of(d, pol):
    return d['followers'].get(pol, 0) if d else None


def case_attribution(host, guest, ck, cities, home, tgt):
    print(' ', host.call('coop.role 3')['out'].strip().replace('\n', ' | '), flush=True)
    eco = new_vehicle(host, home, 2)
    ck(eco is not None, f'the host has an economic vehicle ({eco})')
    if eco is None:
        return
    print('  economic:', put_near(host, eco, cities[tgt]['pos'], 40), flush=True)
    for _ in range(2):  # (the first conversion of a city may only switch its converter: docs/re/civ.md section B)
        host.call(f'coop.civ convert {tgt} 1')
    d0, pol = influence(host, tgt)
    host.call(f'coop.civ convert {tgt} 1')
    d1, _ = influence(host, tgt)
    de = followers_of(d1, pol) - followers_of(d0, pol) if d0 and d1 else None
    print(f'  economic vehicle near city {tgt}: one convert moved {de} followers ({followers_of(d0, pol)} -> {followers_of(d1, pol)})',
          flush=True)
    ck(de == 1, f'a trader next to the city gives no religious bonus (1 convert moved {de})')
    rel = new_vehicle(host, home, 1)
    ck(rel is not None, f'the host has a religious vehicle ({rel})')
    if rel is None:
        return
    print('  religious:', put_near(host, rel, cities[tgt]['pos'], -40), flush=True)
    d0, _ = influence(host, tgt)
    host.call(f'coop.civ convert {tgt} 1')
    d1, _ = influence(host, tgt)
    dr = followers_of(d1, pol) - followers_of(d0, pol) if d0 and d1 else None
    print(f'  religious vehicle near city {tgt}: one convert moved {dr} followers', flush=True)
    ck(dr == 2, f'the religious governor\'s preacher next to the city gives the bonus (1 convert moved {dr}, x1.5 rounded up)')
    gd = wait_for(lambda: (lambda g: g if g and followers_of(g, pol) == followers_of(d1, pol) else None)(influence(guest, tgt)[0]), 5)
    ck(gd is not None, f'the guest\'s city has the host\'s followers of the player ({followers_of(d1, pol)})')


def case_religion(host, guest, ck, cities, home, tgt):
    # a part of the city only: the meter is 200 * converted / (converted + the owner's), and at 100 the game captures
    # the city (CheckReligiousConversion; lab run 20261119-104136: 4 x 5 converted all 15 citizens, "Alice captured a
    # city", the host's meter 0 again). Converts of 2 until the player has followers (2 of 15: the meter ~27;
    # the first convert of a city may only switch its converter: docs/re/civ.md section B)
    _, pol = influence(host, tgt)
    for _ in range(4):
        print('  host:', host.call(f'coop.civ convert {tgt} 2')['out'].strip().splitlines()[0], flush=True)
        time.sleep(0.5)
        hd, _ = influence(host, tgt)
        if followers_of(hd, pol):
            break
    time.sleep(1)
    hd, pol = influence(host, tgt)
    print(f'  host city {tgt}: {hd}', flush=True)
    hf = followers_of(hd, pol)
    ck(hd and hf and hf > 0 and hd['convert'] > 0, f'host (control): the city has followers of the player ({hf}), meter {hd and hd["convert"]}')
    if not (hd and hf):
        return
    gd = wait_for(lambda: (lambda g: g if g and followers_of(g, pol) == followers_of(influence(host, tgt)[0], pol) else None)(
        influence(guest, tgt)[0]), 5)
    gd = gd or influence(guest, tgt)[0]
    hd, _ = influence(host, tgt)
    print(f'  guest city {tgt}: {gd}', flush=True)
    ck(gd and followers_of(gd, pol) == followers_of(hd, pol),
       f'the guest\'s game has the host\'s followers of the player ({gd and followers_of(gd, pol)} / {followers_of(hd, pol)})')
    ck(gd and gd['convert_id'] == hd['convert_id'], f'the guest\'s city has the host\'s converter ({gd and gd["convert_id"]} / {hd["convert_id"]})')
    ck(gd and abs(gd['convert'] - hd['convert']) <= 2, f'the guest\'s religious meter is the host\'s ({gd and gd["convert"]} / {hd["convert"]})')
    frame_city({'host': host, 'guest': guest}, cities[tgt]['pos'])
    keyframe('religion_meter', f'Civ: NPC city {tgt} with the religious conversion meter over it at the same level on both screens '
             f'({hd["convert"]:.0f} %)')


def case_preach(host, guest, ck, cities, home, far):
    hd, pol = influence(host, far)
    print(f'  host city {far} before: slots {hd and hd["slots"]} followers {hd and hd["followers"]}', flush=True)
    f0 = followers_of(hd, pol) or 0
    vids = [new_vehicle(host, home, 1) for _ in range(3)]
    vids = [v for v in vids if v is not None]
    ck(len(vids) >= 2, f'the host has religious vehicles ({vids})')
    for k, v in enumerate(vids):
        print(f'  #{v}:', put_near(host, v, cities[far]['pos'], 80 + 10 * k), '|',
              host.call(f'coop.civ attack {v} {far} 3')['out'].strip(), flush=True)
    t0 = time.time()
    hd = gd = None
    while time.time() - t0 < 90:
        hd, _ = influence(host, far)
        if hd and followers_of(hd, pol) > f0:
            break
        time.sleep(3)
    gd = wait_for(lambda: (lambda g: g if g and followers_of(g, pol) > f0 else None)(influence(guest, far)[0]), 5) or \
        influence(guest, far)[0]
    print(f'  host city {far} after {time.time() - t0:.0f} s: slots {hd and hd["slots"]} followers {hd and hd["followers"]}',
          flush=True)
    print(f'  guest city {far}: followers {gd and gd["followers"]} convert {gd and gd["convert"]}', flush=True)
    print('  host', host.call('coop.civ influence 0')['out'].splitlines()[0], flush=True)
    _, hv, _ = civ_state(host)
    _, gv, _ = civ_state(guest)
    for v in vids:  # (support: where each preacher stands on both games, the sync-check compares them)
        a, b = hv.get(v), gv.get(v)
        print(f'  #{v}: host {a and a["pos"]} order {a and a["order"]} | guest {b and b["pos"]} '
              f'({math.dist(a["pos"], b["pos"]) if a and b else -1:.1f} m apart)', flush=True)
    ck(hd and any(s[0] or s[1] for s in hd['slots']), f'the far city\'s preaching slots have places ({hd and hd["slots"]})')
    ck(hd and followers_of(hd, pol) > f0, f'the preachers converted citizens of the far city on the host ({f0} -> {followers_of(hd, pol)})')
    ck(gd and followers_of(gd, pol) > f0, f'the guest\'s game shows them ({gd and followers_of(gd, pol)})')
    frame_city({'host': host, 'guest': guest}, cities[far]['pos'])
    keyframe('preach_far', f'Civ: the religious vehicles preach at NPC city {far} far from the host\'s capital; its conversion '
             'meter shows the same progress on both screens')


def routes(con):
    return {tuple(sorted((int(m.group(1)), int(m.group(2)))))
            for m in re.finditer(r'^route (\d+) (-?\d+) ', con.call('coop.civ roads')['out'], re.M) if int(m.group(2)) >= 0}


def case_economy(host, guest, ck, cities, home, tgt):
    print('  host:', host.call(f'coop.civ roads {tgt} {home}')['out'].strip().splitlines()[0], flush=True)
    pair = tuple(sorted((tgt, home)))
    ck(wait_for(lambda: pair in routes(guest), 15), f'the guest has the host\'s route {pair} (BUG-0494)')
    hd, pol = influence(host, tgt)
    if not ck(hd is not None and pol, f'host city {tgt} read ({pol})'):
        return
    host.call(f'mem.write 0x{hd["ptr"] + 0x758:08X} {struct.pack("<f", 100.0).hex()}')
    host.call(f'mem.write 0x{hd["ptr"] + 0x75C:08X} {struct.pack("<I", int(pol, 16)).hex()}')
    time.sleep(0.5)
    hd, _ = influence(host, tgt)
    print(f'  host city {tgt}: buy {hd["buy"]} buyer {hd["buyer"]} prices {hd["prices"]}', flush=True)
    print('  host', host.call(f'coop.civ canbuy {tgt}')['out'].strip(), flush=True)
    gd = wait_for(lambda: (lambda g: g if g and g['buyer'] == hd['buyer'] and g['buy'] >= 99.9 else None)(influence(guest, tgt)[0]), 5) \
        or influence(guest, tgt)[0]
    print(f'  guest city {tgt}: buy {gd and gd["buy"]} buyer {gd and gd["buyer"]} prices {gd and gd["prices"]}', flush=True)
    ck(gd and gd['buyer'] == hd['buyer'] and gd['buy'] >= 99.9, f'the guest\'s city has the host\'s buy meter and buyer '
       f'({gd and gd["buy"]} / {gd and gd["buyer"]})')
    ck(gd and all(abs(gd['prices'].get(k, -1e9) - v) < 0.01 for k, v in hd['prices'].items()),
       f'the guest\'s city has the host\'s price entries ({gd and gd["prices"]} / {hd["prices"]})')
    cb = guest.call(f'coop.civ canbuy {tgt}')['out'].strip()
    print('  guest', cb, flush=True)
    m = re.search(r'can=(\d) price=([-\d.]+)', cb)
    ck(m and m.group(1) == '1' and float(m.group(2)) >= 0, f'the guest\'s game would buy the city (CanBuyCity, GetBuyPrice: {cb})')
    frame_city({'host': host, 'guest': guest}, cities[tgt]['pos'])
    keyframe('econ_route', f'Civ: NPC city {tgt} with the road (trade route) to the player\'s city on both screens')
    print('  host:', host.call(f'coop.civ unroad {tgt} {home}')['out'].strip().splitlines()[0], flush=True)
    gone = wait_for(lambda: pair not in routes(guest), 15)
    ck(pair not in routes(host), f'host (control): its route {pair} is gone')
    ck(gone, f'the guest\'s route {pair} is gone too ({sorted(routes(guest))})')
    keyframe('econ_route_gone', f'Civ: NPC city {tgt}: the road to the player\'s city is gone on both screens', settle=0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--case', choices=('attribution', 'religion', 'preach', 'economy'), required=True)
    args = ap.parse_args()
    host, guest = start('civ_npc')
    ck = Checks()
    time.sleep(4)
    cities, _, _ = civ_state(host)
    mine = [c for c, d in cities.items() if d['player']]
    npc = [c for c, d in cities.items() if not d['player'] and d['owner'] < 0]
    if not ck(mine and npc, f'the host has a city ({mine}) and there are NPC cities ({npc})'):
        return ck.result()
    home = mine[0]
    near = min(npc, key=lambda c: math.dist(cities[c]['pos'], cities[home]['pos']))
    far = max(npc, key=lambda c: math.dist(cities[c]['pos'], cities[home]['pos']))
    print(f'  home {home}, nearest NPC city {near}, farthest {far} '
          f'({math.dist(cities[far]["pos"], cities[home]["pos"]):.0f} m)', flush=True)
    {'attribution': lambda: case_attribution(host, guest, ck, cities, home, near),
     'religion': lambda: case_religion(host, guest, ck, cities, home, near),
     'preach': lambda: case_preach(host, guest, ck, cities, home, far),
     'economy': lambda: case_economy(host, guest, ck, cities, home, near)}[args.case]()
    print('  host', host.call('coop.civ influence 0')['out'].splitlines()[0], flush=True)
    print('  guest', guest.call('coop.civ influence 0')['out'].splitlines()[0], flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
