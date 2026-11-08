#!/usr/bin/env python3
"""Tribe: the tribe setup with no simulator camera does not fault (BUG-0167).

A guest of a 4-player tribe churn (2026-10-23, GOG 2016) faulted at 0xB12D32 (data 0x2B4): the tribe setup
TribeGame_Setup (0xCD78E0, one step after a load) points the simulator camera [0x167FAAC] at the tribe
(SimCamera_LookAt, then SimCamera_Update) without a check, and the camera object was null. The lab's 7 later
cases of the same null camera (BUG-0656, GOG 2024) all had the galaxy menu's intro GGE_intro playing into the stage:
it owns the camera while the setup runs (BUG-0607 / BUG-0626). The guard of d605e96 (tribe.camera_guard) skips
both calls while the camera is null.

The race made by hand, on one game alone in Tribe: world.intro_at_once off (so the intro may start outside the
menu, as it did before 0.7.9), then a load of the tribe planet (coop.load) and the intro started as the menu's
handler starts it as soon as the game is in the Loading mode, so that it plays while the setup runs. Several
delays are tried until the game's log shows the setup meeting the null camera (with the guard: "skipped the tribe
camera set-up", without it: "tribe camera guard off"). The game must come back to a playable Tribe with no fault.
SPORE_FIX_OFF=tribe.camera_guard fails (the setup calls LookAt on the null camera).

  SPORE_LANE=9 python3 tools/tests/coop_tribe_camera_null.py
"""
import os
import re
import time

import lane
from twoinst import Checks, start

TRIBE = 0x1654C02
HIT = ('skipped the tribe camera set-up', 'tribe camera guard off')


def mode(con):
    try:
        return int(con.call('game.mode')['out'].split()[0], 16)
    except (ValueError, IndexError, OSError):
        return None


def log_size(inst):
    try:
        return os.path.getsize(lane.game_log(inst))
    except OSError:
        return 0


def log_since(inst, off):
    try:
        with open(lane.game_log(inst), 'rb') as f:
            f.seek(off)
            return f.read().decode('utf-8', 'replace').splitlines()
    except OSError:
        return []


def faults(con):
    m = re.search(r'total faults: (\d+)', con.call('exc')['out'])
    return int(m.group(1)) if m else -1


def main():
    host, guest = start('tribe')
    ck = Checks()
    guest.call('coop.leave')
    time.sleep(1)
    host.call('coop.leave')  # alone: the load is the host's own, nobody follows it
    time.sleep(2)
    print('  ' + host.call('coop.fix world.intro_at_once off')['out'].strip(), flush=True)
    m = re.search(r'eax=(0x[0-9A-Fa-f]+)', host.call('call Simulator::cCinematicManager::Get cdecl')['out'])
    ck(m, 'host: the cinematic manager')
    if not m:
        return ck.result()
    mgr = m.group(1)
    hit = False
    for i, delay in enumerate((0.0, 0.5, 1.5, 3.0, 0.2)):
        off = log_size(lane.HOST)
        host.call('coop.load Game0 Zorispe')
        t0 = time.time()
        while time.time() - t0 < 30 and mode(host) == TRIBE:
            time.sleep(0.05)
        t_load = time.time() - t0
        time.sleep(delay)
        out = host.call(f'call Simulator::cCinematicManager::PlayCinematic thiscall {mgr} s:GGE_intro 1 0 0 0 0')['out']
        print(f'  #{i + 1}: Loading after {t_load:.2f} s, intro started {delay:.1f} s later (mode '
              f'{mode(host) or 0:08X}): {out.strip()[:60]}', flush=True)
        t0 = time.time()
        while time.time() - t0 < 120 and not (mode(host) == TRIBE and host.call('coop.avatar')['ok']):
            time.sleep(0.5)
        time.sleep(4)
        lines = [ln for ln in log_since(lane.HOST, off) if any(h in ln for h in HIT)]
        for ln in lines[:2]:
            print('    ' + ln.strip()[:200], flush=True)
        playable = mode(host) == TRIBE and host.call('coop.avatar')['ok']
        print(f'  #{i + 1}: back in Tribe: {playable}, faults {faults(host)}', flush=True)
        if lines:
            hit = True
            ck(playable, f'#{i + 1}: the tribe setup met the null camera and the game is in a playable Tribe')
            break
        if not playable:
            ck(False, f'#{i + 1}: the game is back in a playable Tribe')
            break
    ck(hit, 'the race was made: the tribe setup ran without a simulator camera')
    ck(faults(host) == 0, 'host: no fault')
    ck.faults(host=host)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
