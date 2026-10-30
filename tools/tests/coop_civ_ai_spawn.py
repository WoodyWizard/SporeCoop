#!/usr/bin/env python3
"""Civilization: the other nations' AI spawns no vehicles on a guest (BUG-0188).

The host simulates the planet; a guest only mirrors the host's vehicles. Before the fix the
guest's own copy of the NPC AI bought vehicles too (a new NPC city buys its first one at once),
and the next full snapshot killed them again: vehicles appeared and died on the guest only.

1. Two games on civ_npc. The host founds two NPC cities (the hooked SpawnNextCity, as its city
   manager does); the guest replays them.
2. The guest's game must create no vehicle of its own (SpawnVehicle calls by the game on the
   guest, each refused one counted apart; with the fix the AI's calls get null back).
3. For 60 s the guest's game vehicle list is sampled every 0.3 s: every vehicle in it must be a
   mirror of a host vehicle (a net id from the host, `veh #0` = a vehicle of the guest's own),
   and the game's vehicles of the guest that the host does not know (`removed_local=`) must not grow.
   (`removed=` also counts mirrors of host vehicles that died or went away meanwhile: a host vehicle
   that vanished made the test fail once, lab 20261030-163302-fix-w14-tribe-civ, "removed 24 -> 25"
   with the host's vehicles 7 -> 6; that is not the guest's own AI, BUG-0327.)

--nofix: `coop.civ fix aispawn 0` (the code before): must FAIL.

  SPORE_LANE=7 python3 tools/tests/coop_civ_ai_spawn.py [--nofix]
"""
import argparse
import re
import time

from twoinst import Checks, start


def guest_stat(con, key):
    m = re.search(rf'\b{key}=(\d+)', con.call('coop.civ')['out'])
    return int(m.group(1)) if m else -1


def own_vehicles(con):
    """the guest's game vehicles that are not mirrors of the host's (net id 0)"""
    return [m.group(0).strip()[:90] for m in re.finditer(r'^\s+veh #0 .*$', con.call('coop.civ')['out'], re.M)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nofix', action='store_true')
    a = ap.parse_args()
    host, guest = start('civ_npc')
    ck = Checks()
    for c in (host, guest):
        print(' ', c.call(f'coop.civ fix aispawn {0 if a.nofix else 1}')['out'].strip())
    time.sleep(3)
    removed0 = guest_stat(guest, 'removed_local')
    m = re.search(r'eax=(0x[0-9A-Fa-f]+)', host.call('call Simulator::cCivExpansion::Get cdecl')['out'])
    ck(m and int(m.group(1), 16), 'the host has a city manager')
    if not (m and int(m.group(1), 16)):
        return ck.result()
    for _ in range(2):
        print('  host founds a city:', host.call(f'call Simulator::cCivExpansion::SpawnNextCity thiscall {m.group(1)} 0 0')['out'].strip())
        time.sleep(4)
    seen = []
    t0 = time.time()
    while time.time() - t0 < 60:
        for v in own_vehicles(guest):
            seen.append(v)
        host.call('key enter')  # a caption that waits for OK
        time.sleep(0.3)
    removed = guest_stat(guest, 'removed_local')
    sp = guest.call('coop.civ spawns')['out']
    print(' ', sp.strip().replace('\n', ' | '), flush=True)
    made = sum(int(n) for n in re.findall(r'^spawn from \S+ x(\d+)', sp, re.M))
    blocked = int((re.search(r'ai spawns blocked (\d+)', sp) or [0, 0])[1])
    print(f'  guest vehicles of its own seen: {len(seen)} {seen[:3]}; removed {removed0} -> {removed}', flush=True)
    ck(guest_stat(guest, 'cities_in') >= 2, 'the guest founded the host\'s two new cities')
    ck(not seen, f'no vehicle of the guest\'s own AI appeared on the guest ({len(seen)} samples)')
    ck(made - blocked == 0, f'the guest\'s game created no vehicle by itself ({made} SpawnVehicle calls, {blocked} refused)')
    ck(removed == removed0, f'the snapshot had no local vehicle to kill (removed {removed0} -> {removed})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
