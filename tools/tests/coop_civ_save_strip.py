#!/usr/bin/env python3
"""Civilization: a guest's save holds none of the session's mirror vehicles (BUG-0470).

The guest does not simulate the host's civilizations, it shows mirrors of the host's vehicles (real
cVehicles of its game, `CreateVehicle`). They went into the guest's saves like its own vehicles and
came back after a load as vehicles of that world, with nothing to drive them.

1. The host plays the civ world (Bob joins on the main lane); Carol joins on a second lane.
2. The host buys vehicles: Carol mirrors them (the game's vehicle list on Carol).
3. Carol saves with the game's own save (`coop.save`), leaves the session; her game is restarted
   WITHOUT a co-op session and loads that save.
4. The loaded world: no mirror (Carol had only mirrors: a guest's own vehicles are removed; a world
   without vehicles gets the game's free first vehicle on load, so at most 1 vehicle),
   no null in any civilization's vehicle lists (`coop.civ civlists`: what a stripped reference becomes
   on load), no fault for 10 s.
PASS: 2 to 4. SPORE_FIX_OFF=civ.save_strip: the loaded world has the mirror vehicles (FAIL).

  SPORE_LANE=2 SPORE_FREE_LANE=3 python3 tools/tests/coop_civ_save_strip.py
"""
import math
import os
import re
import subprocess
import time

import lane
import twoinst
from coop_content_civ import player_city, restart_without_session, save_slot, wait_saved
from coop_creature_e84 import close_boxes
from coop_multi import join_cmd, start_extra
from coop_unknown_parts import clean_caches, lane_b
from twoinst import Checks, start


def vehicles(con):
    return [ln for ln in con.call('coop.civ')['out'].splitlines() if ' veh #' in ln]


def positions(lines):
    out = []
    for ln in lines:
        m = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', ln)
        if m:
            out.append(tuple(float(m.group(i)) for i in (1, 2, 3)))
    return out


def civlists(con):
    out = con.call('coop.civ civlists')['out']
    n = [tuple(map(int, m)) for m in re.findall(r'vehicles=(\d+) nulls=(\d+) selectable=(\d+) nulls=(\d+)', out)]
    return sum(x[0] for x in n), sum(x[1] + x[3] for x in n), out


def main():
    if os.environ.get('SPORE_FREE_LANE') == '' and not os.environ.get('SPORE_LANE_B'):
        print('RESULT: SKIP (no second lane free for the guest that reloads)')
        return 0
    b = lane_b()
    os.environ['SPORE_LANE_B'] = str(b)
    assert b > 1 and b != lane.LANE, 'Carol needs a lane of its own'
    host, bob = start('civ')
    ck = Checks()
    time.sleep(3)
    appdata = lane.APPDATA.replace(f'/{lane.BOTTLE}/', f'/SporeLab{b}/' if b > 1 else '/Games/')
    carol_dir = os.path.join(appdata, 'SporeKit-mp1', 'Spore')
    clean_caches(carol_dir)
    city = player_city(host)
    ck(city is not None, f'the host plays a civilization with its own city ({city})')
    print(f'LAB-WATCH lane={b} since={time.time():.1f}', flush=True)
    try:
        carol, carol_inst = start_extra(1)['Carol']
        for name in filter(None, os.environ.get('SPORE_FIX_OFF', '').split(',')):
            print('  carol:', carol.call(f'coop.fix {name.strip()} off')['out'].strip().splitlines()[0])
        carol.call(join_cmd('Carol'))
        t0, in_world = time.time(), False
        while time.time() - t0 < 180:
            f1 = twoinst.host_frame(host)
            time.sleep(1)
            if twoinst.host_frame(host) == f1:
                host.call('key enter')  # the "game saved" box of the share
            try:
                if 'Civ' in carol.call('game.mode')['out'] and carol.call('coop.avatar').get('ok'):
                    in_world = True
                    break
            except (OSError, ValueError):
                break
        ck(in_world, "Carol plays the host's civilization")
        for _ in range(3):
            if city is not None:
                print('  host:', host.call(f'coop.civ buy {city} 0')['out'].strip())
            time.sleep(2)
        time.sleep(4)
        hv, cv = vehicles(host), vehicles(carol)
        print(f'  vehicles: host {len(hv)}, Carol {len(cv)}', flush=True)
        ck(len(cv) > 0 and len(cv) == len(hv), f'Carol mirrors the host\'s vehicles ({len(cv)} of {len(hv)})')
        slot, folder, planet = save_slot(carol, carol_dir)
        ck(slot != '' and planet != '', f'Carol plays a save of her own ({slot}/{planet})')
        t_save = time.time()
        print(' ', carol.call('coop.save')['out'].strip()[:80], flush=True)
        close_boxes(carol, 12)
        ck(wait_saved(folder, t_save), f'Carol wrote her save ({folder})')
        print(' ', [ln for ln in carol.call('coop.civ')['out'].splitlines() if ln.startswith('planner')], flush=True)
        print(' ', carol.call('coop.savestrip')['out'].strip()[:140], flush=True)
        print(' ', carol.call('coop.leave')['out'].strip()[:80], flush=True)
        time.sleep(5)
        re_ = restart_without_session(b, carol_inst)
        print(' ', re_.call(f'coop.load {slot} {planet}')['out'].strip()[:120], flush=True)
        loaded, t0 = False, time.time()
        while time.time() - t0 < 240:
            try:
                if re_.call('coop.avatar')['ok']:
                    loaded = True
                    break
            except (OSError, ValueError):
                break
            time.sleep(2)
        ck(loaded, 'Carol loads the save she wrote, alone (no session)')
        time.sleep(10)
        out2 = re_.call('exc')['out']
        lv = vehicles(re_)
        nveh, nulls, lists = civlists(re_)
        print(f'  after the reload: {(out2.strip().splitlines() or ["-"])[0][:100]} | vehicles {len(lv)} | civ lists '
              f'{nveh} entries, {nulls} nulls', flush=True)
        print('   ', ' | '.join(lists.strip().splitlines())[:400], flush=True)
        ck('total faults: 0' in out2, f'the reloaded world runs without a fault ({(out2.strip().splitlines() or ["-"])[0][:90]})')
        # a world without a vehicle gets the game's free first vehicle at its city on load: a vehicle
        # where none of the session's mirrors stood is the game's own
        saved_at = positions(cv)
        left = [p for p in positions(lv) if any(math.dist(p, q) < 2.0 for q in saved_at)]
        print(f'  mirrors when saved at {[tuple(round(x) for x in p) for p in saved_at]}, loaded vehicles at '
              f'{[tuple(round(x) for x in p) for p in positions(lv)]}', flush=True)
        ck(loaded and len(lv) <= 1 < len(cv),
           f'no mirror vehicle of the session is in the reloaded world ({len(lv)} vehicles, at most the game\'s free '
           f'one; {len(left)} within 2 m of a mirror\'s place)')
        ck(loaded and nulls == 0, f'no null in the civilizations\' vehicle lists ({nulls})')
    finally:
        subprocess.run([lane.SPORE, 'kill'], env=dict(os.environ, SPORE_LANE=str(b)), capture_output=True)
        time.sleep(2)
    ck(bob.call('coop.avatar')['ok'], 'Bob still plays in the world')
    ck.faults(host=host, guest=bob)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
