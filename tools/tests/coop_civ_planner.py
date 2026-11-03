#!/usr/bin/env python3
"""Civilization: a guest's open city planner while the host's world changes the city (BUG-0491, BUG-0493).

0.7.3.6: one guest crashed three times in 15 minutes with its city planner open (the planner pauses
that game; the host's world goes on): at cLayoutSlot::RemoveObject on a garbage slot (the planner's
pick-up of a building that is in no slot of its own, slot index -1) and on a freed object of the
planner's moved-objects map (a mirror vehicle the mod removed). The planner keeps raw pointers into
the city it edits; the mod changed that city and removed vehicles under it.

1. Two games on civ_npc. The guest opens the city planner of its own city with the city menu
   (real clicks, like play_civ.py); the mod sees it open on that city (the planner's own field).
2. Host: a building in that city (`coop.civ build`), a vehicle near it dies (`coop.civ kill`).
3. 8 s with the planner open: the guest's game keeps the city's building list (the game's list, read
   on the guest) and every vehicle in the planner's moved-objects map.
4. The guest closes the planner (the green check): within 15 s it has the host's buildings and
   vehicles.
PASS: 3 and 4, no fault or crash on either game.
SPORE_FIX_OFF=civ.planner_hold: step 3 fails (the host's building is built into the city under the
open planner, the state both 0.7.3.6 crashes came from).
Not driven here: a building dropped from the palette (the civ_npc designs open the building editor,
docs/known-issues.md BUG-0189), so the moved-objects map is usually empty in this test and the
pick-up guard (civ.planner_pick) is not reached; both are in docs/re/civ.md#civ-planner-lifetime.

  SPORE_LANE=2 python3 tools/tests/coop_civ_planner.py
"""
import random
import re
import time

from play_civ import BTN_PLANNER, PLANNER_DONE, PLANNER_NAME, Player, civ_state, dist
from twoinst import Checks, start


def planner(con, extra=''):
    out = con.call('coop.civ planner ' + extra)['out']
    m = re.search(r'planner editor=(\S+) city=(\S+) host_index=(-?\d+)', out)
    moved = re.findall(r'moved (\S+) (vehicle|object)( \(mirror\))?', out)
    orphans = re.findall(r'orphan (\S+) noun=', out)
    return dict(open=bool(m and m.group(2) not in ('00000000', '(nil)', '0')), city=int(m.group(3)) if m else -1,
                moved=moved, orphans=orphans, out=out)


def buildings(con, ci):
    return [ln.split()[1] for ln in con.call(f'coop.civ buildings {ci}')['out'].splitlines() if ln.startswith('building ')]


def main():
    host, guest = start('civ_npc')
    ck = Checks()
    time.sleep(3)
    gc, gv, _ = civ_state(guest)
    mine = sorted(i for i, c in gc.items() if c['owner'] in (1, 253))
    ck(mine, f'the guest has a city of its own ({mine})')
    if not mine:
        return ck.result()
    ci = mine[0]
    for kind in ('house', 'factory'):  # buildings to pick in the planner (checked below)
        print(f'  host {kind}:', host.call(f'coop.civ build {ci} {kind}')['out'].strip(), flush=True)
    time.sleep(3)
    p = Player('guest', guest, 1, random.Random(5), [])
    opened = False
    for _ in range(3):
        if p.open_city(gc[ci]['pos']) and p.click_button(BTN_PLANNER) and p.wait_ui(PLANNER_NAME, True, 8):
            opened = True
            break
        guest.call('key esc')
        time.sleep(1)
    ck(opened, f'the guest opened the city planner of city {ci}')
    pl = planner(guest)
    print('  guest planner:', pl['out'].strip().replace('\n', ' | ')[:600], flush=True)
    ck(pl['open'] and pl['city'] == ci, f'the mod sees the planner open on city {ci} (city {pl["city"]})')
    if not opened:
        return ck.result()
    picks = re.findall(r'city building (\S+) noun=(\w+) pick_ok=(\d)', pl['out'])
    ok_picks = [b for b, n, k in picks if int(n, 16) in (0x18EB106, 0x18EA2CC, 0x1A56ABA) and k == '1']
    all_picks = [b for b, n, k in picks if int(n, 16) in (0x18EB106, 0x18EA2CC, 0x1A56ABA)]
    ck(all_picks and len(ok_picks) == len(all_picks), f'the pick-up guard lets every building in its slot be picked ({len(ok_picks)} of {len(all_picks)})')

    # 2. the host's world changes the city
    b0 = buildings(guest, ci)
    moved0 = [sp for sp, kind, mir in pl['moved'] if kind == 'vehicle']
    print('  host:', host.call(f'coop.civ build {ci} house')['out'].strip(), flush=True)
    _, hv, _ = civ_state(host)
    near = sorted((dist(v['pos'], gc[ci]['pos']), i) for i, v in hv.items())
    killed = [i for d, i in near if d < 120][:4]
    for i in killed:
        print(f'  host kills vehicle {i}:', host.call(f'coop.civ kill {i}')['out'].strip(), flush=True)
    # 3. 8 s with the planner open
    changed, lost, t0 = False, set(), time.time()
    while time.time() - t0 < 8:
        if buildings(guest, ci) != b0:
            changed = True
        now = planner(guest)
        have = {sp for sp, kind, _ in now['moved'] if kind == 'vehicle'}
        lost |= set(moved0) - have
        time.sleep(0.5)
    print(f'  planner open 8 s: buildings changed={changed}, vehicles the planner moved that went away: {len(lost)} of {len(moved0)}',
          flush=True)
    ck(not changed, 'the guest\'s game kept the planner city\'s buildings while the planner was open')
    ck(not lost, 'every vehicle the planner moves stayed in the guest\'s game while the planner was open')
    # 5. close
    for _ in range(3):
        guest.call(f'click {PLANNER_DONE[0]} {PLANNER_DONE[1]}')
        if p.wait_ui(PLANNER_NAME, False, 5):
            break
    t0, ok = time.time(), False
    while time.time() - t0 < 15 and not ok:
        hc, hv, _ = civ_state(host)
        gc2, gv2, _ = civ_state(guest)
        ok = hc.get(ci, {}).get('buildings') == gc2.get(ci, {}).get('buildings') and set(hv) == set(gv2)
        time.sleep(1)
    print(f'  after the planner ({time.time() - t0:.0f} s): host buildings {hc.get(ci, {}).get("buildings")} '
          f'guest {gc2.get(ci, {}).get("buildings")}, vehicles host {len(hv)} guest {len(gv2)}', flush=True)
    print('  guest:', [ln for ln in guest.call('coop.civ')['out'].splitlines() if ln.startswith('planner')], flush=True)
    ck(ok, 'after the planner the guest has the host\'s buildings and vehicles within 15 s')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
