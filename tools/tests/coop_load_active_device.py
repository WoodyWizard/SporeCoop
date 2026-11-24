#!/usr/bin/env python3
"""Creature: queued world load waits while the D3D9 graphics device is lost/minimized, and sync-check flags
terrain height divergence (BUG-1537).

A player report (0.7.16.1, Creature stage) had the guest load a shared Creature world while minimized across
D3D9 device resets; the guest's nest terrain ended up 1.3-3.0 m below the host's.

1. Clean cold Creature load: sync-check reports terrain_warns=0 and max terrain height diff < 0.50 m.
2. Guest leaves for the galaxy menu (`coop.menu`) and sets `coop.worldfix devbusy 1` (simulating a minimized
   window / lost D3D9 device). Host saves and shares its Creature world (`coop.save`, `coop.share`).
   - While `devbusy=1`, the guest holds the queued world load in the galaxy menu (no `world: loading ` fired;
     `loads delayed for graphics device >= 1`).
   - SPORE_FIX_OFF=world.load_active_device (load_active_device_off): guest loads immediately while `devbusy=1`:
     must FAIL.
3. Guest releases `coop.worldfix devbusy 0`: guest loads cleanly into the host's Creature world with
   terrain_warns=0 and terrain_max_m < 0.50 m.
4. Terrain height diagnostic (`sync.terrain_check`): guest runs `coop.diag sync force terrain` (shifts guest NPC
   ground radius by -2.5 m keeping AGL unchanged) and host runs `coop.diag sync now`.
   - PASS: host logs `sync-check: terrain height differs` and `terrain_warns >= 1`, `terrain_max_m >= 1.5`.
   - SPORE_FIX_OFF=sync.terrain_check (sync_terrain_check_off): terrain_warns stays 0 and no warning is logged:
     must FAIL.

  SPORE_LANE=2 python3 tools/tests/coop_load_active_device.py
"""
import re
import time

import lane
from harness import field, mode, waiter
from twoinst import Checks, start, wait_guest_in

wait = waiter(step=0.5)


def read_log(inst):
    try:
        with open(lane.game_log(inst), 'rb') as f:
            return f.read().decode('utf-8', 'replace')
    except OSError:
        return ''


def guest_loads():
    return read_log(lane.GUEST).count('world: loading ')


def dev_delayed(con):
    v = field(con.call('coop.status')['out'], r'loads delayed for graphics device (\d+)')
    return int(v) if v is not None else -1


def sync_info(con):
    out = con.call('coop.diag sync')['out'].strip()
    d = {'raw': out}
    for k in ('terrain_warns',):
        m = re.search(rf'\b{k}=(\d+)', out)
        if m:
            d[k] = int(m.group(1))
    m = re.search(r'\bterrain_max_m=([0-9.]+)', out)
    if m:
        d['terrain_max_m'] = float(m.group(1))
    return d


def main():
    host, guest = start()
    ck = Checks()

    # 1. Clean initial Creature world: verify zero terrain divergence
    host.call('coop.diag sync now')
    time.sleep(3)
    s0 = sync_info(host)
    print(f'  initial host sync: {s0["raw"]}', flush=True)
    ck(s0.get('terrain_warns', -1) == 0 and s0.get('terrain_max_m', 99.0) < 0.50,
       f'clean Creature load has matching terrain height (warns={s0.get("terrain_warns")}, max_m={s0.get("terrain_max_m")})')

    # 2. Guest goes to galaxy menu and marks graphics device busy/minimized
    print('  guest menu:', guest.call('coop.menu')['out'].strip(), flush=True)
    ck(wait(lambda: 'GalaxyGameEntry' in mode(guest), 60), f'guest reached galaxy menu ({mode(guest).strip()})')
    print('  guest devbusy 1:', guest.call('coop.worldfix devbusy 1')['out'].strip(), flush=True)
    loads0 = guest_loads()
    delayed0 = dev_delayed(guest)
    recv0 = read_log(lane.GUEST).count('world: received')

    # Host saves and shares world to guest
    save_out = host.call('coop.save')['out'].strip()
    print('  host save:', save_out, flush=True)
    ck('saved' in save_out, f'host saved world ({save_out})')
    host.call('coop.share')

    # Wait until the guest receives the shared world (or starts loading prematurely when fix is OFF)
    got_share = wait(lambda: read_log(lane.GUEST).count('world: received') > recv0 or guest_loads() > loads0, 90)
    ck(got_share, 'guest received the host world share')
    # Hold devbusy=1 for 6 s after share arrival: with fix ON, guest stays in galaxy menu and does NOT load
    time.sleep(6)
    loads_held = guest_loads()
    delayed_held = dev_delayed(guest)
    m_held = mode(guest).strip()
    print(f'  while devbusy=1: mode={m_held} loads={loads0}->{loads_held} delayed={delayed0}->{delayed_held}', flush=True)
    ck('GalaxyGameEntry' in m_held and loads_held == loads0,
       f'guest held the world load in the galaxy menu while graphics device was busy (mode={m_held}, loads {loads0}->{loads_held})')
    ck(delayed_held > delayed0,  # counter: confirms the graphics-device gate held via_menu_tick; mode and loads checked above
       f'graphics-device gate delayed the queued load ({delayed0} -> {delayed_held})')

    # 3. Release devbusy=0: guest loads cleanly into Creature
    print('  guest devbusy 0:', guest.call('coop.worldfix devbusy 0')['out'].strip(), flush=True)
    loaded = wait(lambda: guest_loads() > loads0, 60)
    in_world = loaded and wait_guest_in(host, guest)
    ck(in_world, 'guest loaded into the Creature world once graphics device became active')
    if not in_world:
        return ck.result()

    time.sleep(4)
    host.call('coop.diag sync now')
    time.sleep(3)
    s1 = sync_info(host)
    print(f'  after reload host sync: {s1["raw"]}', flush=True)
    ck(s1.get('terrain_warns', -1) == 0 and s1.get('terrain_max_m', 99.0) < 0.50,
       f'reloaded Creature world has matching terrain height (warns={s1.get("terrain_warns")}, max_m={s1.get("terrain_max_m")})')

    # 4. Planted terrain height divergence (-2.5 m ground shift on guest NPCs)
    warns_before = s1.get('terrain_warns', 0)
    log_warns_before = read_log(lane.HOST).count('sync-check: terrain height differs')
    print('  guest force terrain:', guest.call('coop.diag sync force terrain')['out'].strip(), flush=True)
    try:
        host.call('coop.diag sync now')
        flagged = wait(lambda: read_log(lane.HOST).count('sync-check: terrain height differs') > log_warns_before, 15)
        s2 = sync_info(host)
        print(f'  with forced terrain diff host sync: {s2["raw"]}', flush=True)
        ck(flagged and s2.get('terrain_warns', 0) > warns_before and s2.get('terrain_max_m', 0.0) >= 1.5,
           f'sync-check flagged the 2.5 m terrain height divergence (flagged={flagged}, warns={warns_before}->{s2.get("terrain_warns")}, max_m={s2.get("terrain_max_m")})')
    finally:
        guest.call('coop.diag sync force off')

    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
