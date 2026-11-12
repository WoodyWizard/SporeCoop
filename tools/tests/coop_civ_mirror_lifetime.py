#!/usr/bin/env python3
"""BUG-0951 (Civ vehicles, class BUG-0426, as BUG-0844 for the Tribe mirrors): a Civ guest never drives a vehicle of
its own civilization that its game built where a removed copy of a host's vehicle was.

A mirror's vehicle was kept across frames and checked by its own memory (object_alive). The game frees a removed
vehicle later and gives the block to the next vehicle it makes. While the host's states paused (its editors) the
mirror stayed bound to that address; when they came again the guest drove the new vehicle as the host's.

Two games on the civ fixture. On the guest, up to --tries times:
  1. `coop.civ vehhold 1`: the host's vehicle states are not taken (as while the host is in an editor);
  2. `coop.civ reusehunt npc 150`: the game removes the vehicle of a mirror of another civilization than the guest's
     (the disband path, no bookkeeping of ours), then once a frame a vehicle of the same kind is made for the guest's
     own civilization 40 m away until the game gives one the freed block;
  3. `coop.civ vehhold 0`, 3 s of the host's states: where is the guest's new vehicle? Taken for the mirror, it
     stands where the host's vehicle is; left alone, it stays where it was made (or the guest's rules remove it: a
     vehicle the host has not).
PASS: the guest's new vehicle at a freed mirror's address is never moved onto the host's vehicle, and no game
faulted. SPORE_FIX_OFF=civ.mirror_lifetime must FAIL.

  SPORE_LANE=6 python3 tools/tests/coop_civ_mirror_lifetime.py [--tries 1]"""
import argparse
import math
import re
import time

from twoinst import Checks, start


def hunt(con):
    out = con.call('coop.civ reusehunt')['out']
    m = re.search(r'mirror=(\d+) left=(-?\d+) tries=(\d+) hit=([0-9A-Fa-f]+|\(nil\)|0+) alive=(\d) dead=(-?\d) own_civ=(\d) '
                  r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\) host_vehicle=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\) bound_as=(-?\d+)', out)
    if not m:
        return None
    hit = m.group(4)
    return {'id': int(m.group(1)), 'left': int(m.group(2)), 'tries': int(m.group(3)),
            'hit': int(hit, 16) if re.fullmatch(r'[0-9A-Fa-f]+', hit) else 0, 'alive': m.group(5) == '1',
            'dead': m.group(6) == '1', 'own': m.group(7) == '1', 'pos': tuple(float(x) for x in m.group(8, 9, 10)),
            'want': tuple(float(x) for x in m.group(11, 12, 13)), 'bound': int(m.group(14)), 'line': out.strip()}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--tries', type=int, default=1)
    ap.add_argument('--rounds', type=int, default=12)
    ap.add_argument('--minutes', type=float, default=8)
    a = ap.parse_args()
    host, guest = start('civ')
    ck = Checks()
    time.sleep(10)
    t_end = time.time() + a.minutes * 60
    reused, wrong = 0, []
    for _ in range(a.rounds):
        if reused >= a.tries or time.time() > t_end:
            break
        guest.call('coop.civ vehhold 1')
        out = guest.call('coop.civ reusehunt npc 150')['out'].strip()
        print('  ' + out, flush=True)
        if 'removed by the game' not in out:
            guest.call('coop.civ vehhold 0')
            time.sleep(3)
            continue
        t1 = time.time()
        h = hunt(guest)
        while time.time() - t1 < 8 and h and not h['hit'] and h['left'] > 0:
            time.sleep(0.25)
            h = hunt(guest)
        if not h or not h['hit']:
            print(f'  no vehicle got the freed block ({h and h["tries"]} tries): another round', flush=True)
            guest.call('coop.civ vehhold 0')
            time.sleep(4)
            continue
        reused += 1
        made = h['pos']
        print(f'  a new vehicle of the guest\'s civilization at the freed block of mirror #{h["id"]}: {h["line"]}', flush=True)
        guest.call('coop.civ vehhold 0')
        time.sleep(3)
        h = hunt(guest)
        d_want = math.dist(h['pos'], h['want']) if h['alive'] else -1
        moved = math.dist(h['pos'], made) if h['alive'] else -1
        print(f'  3 s later: {h["line"]} (moved {moved:.1f} m, {d_want:.1f} m from the host\'s vehicle)', flush=True)
        if h['alive'] and not h['dead'] and 0 <= d_want < 8 and moved > 5:
            wrong.append(h['id'])
    ck(reused >= 1, f'the game gave a removed mirror\'s block to a new vehicle of the guest\'s civilization ({reused} times)')
    ck(not wrong, f'that vehicle was not driven as the host\'s vehicle (taken for it: {wrong})')
    guest.call('coop.civ vehhold 0')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
