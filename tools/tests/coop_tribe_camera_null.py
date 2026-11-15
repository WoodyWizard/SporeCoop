#!/usr/bin/env python3
"""Tribe: the tribe setup with no simulator camera does not fault (BUG-0167).

A guest of a 4-player tribe churn (2026-10-23, GOG 2016) faulted at 0xB12D32 (data 0x2B4): the tribe setup
TribeGame_Setup (0xCD78E0, one step after a load) points the simulator camera [0x167FAAC] at the tribe
(SimCamera_LookAt, then SimCamera_Update) without a check, and the camera object was null. The lab's 7 later
cases of the same null camera (BUG-0656, GOG 2024) all had the galaxy menu's intro GGE_intro playing into the stage:
it owns the camera while the setup runs (BUG-0607 / BUG-0626). The guard of d605e96 (tribe.camera_guard) skips
both calls while the camera is null.

The race the players' way (a guest of the churn joining soon after its galaxy menu came up): before its join
world.intro_at_once off (the intro may start outside the menu, as before 0.7.9), then the join join_at s after the
guest's menu came up (twoinst.start(join_at=), the BUG-0607 timing), so that the menu's own timer (~15.85 s) starts
the intro in the first frames of the guest's Tribe, right before its tribe setup. Cold starts with the join times of
JOIN_AT until the guest's log shows the setup meeting the null camera (with the guard: "skipped the tribe camera
set-up", without it: "tribe camera guard off"); the guest's game must then be in a playable Tribe with no fault.
SPORE_FIX_OFF=tribe.camera_guard fails: the setup calls LookAt on the null camera, an access violation at
SimCamera_LookAt+0x52 reading 0x2B4 (the report's fault; lab 20261107-231118, at the first join, 12.5 s).
Not reproduced this way (lab 20261107-225810 .. 230854): the intro started by hand (PlayCinematic) in the menu right
after the load request, in the Loading mode, or on a host reloading its Tribe: the intro played into the stage, the
camera was there.
The join time depends on the guest's console: on a slow start (lanes 2 / 3 of the merged rel/0.7.11, lab
20261108-012203) it answered only ~30 s after the menu came up, the menu's intro had played in the menu already
(ended at the load, BUG-0654) and the race was never made. So the guest is also armed with `coop.introstage`: the
intro starts in the first tick of its next stage, as the menu's timer starts it there in the natural race; this makes
the race on every start, whatever the join time.

  SPORE_LANE=9 python3 tools/tests/coop_tribe_camera_null.py
"""
import os
import re
import time

import lane
import twoinst
from twoinst import Checks, start

TRIBE = 0x1654C02
JOIN_AT = (12.5, 12.2, 12.8, 12.4, 12.6, 12.0)
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
    ck = Checks()
    hit = False

    def prep(guest):
        # the intro may start outside the menu (0.7.8); SPORE_FIX_OFF goes to the guest before the join (twoinst)
        for name in ['world.intro_at_once']:
            print('  guest before the join: ' + guest.call(f'coop.fix {name} off')['out'].strip()[:100], flush=True)
        print('  guest: ' + guest.call('coop.introstage')['out'].strip(), flush=True)

    host = guest = None
    for i, join_at in enumerate(JOIN_AT):
        off = 0  # a cold start: the guest's log is the new process's
        # the BUG-0607 race: the guest joins join_at s after its menu came up, its load request comes ~3.0-3.5 s later,
        # just before the menu's own timer starts the intro (~15.85 s): the intro starts in the Loading mode
        print(f'  #{i + 1}: the guest joins {join_at:.2f} s after its galaxy menu came up', flush=True)
        host, guest = start('tribe', join_at=join_at, before_join=prep)
        time.sleep(4)
        new = log_since(lane.GUEST, off)
        for ln in new:
            if 'intro' in ln or any(h in ln for h in HIT):
                print('    ' + ln.strip()[:200], flush=True)
        lines = [ln for ln in new if any(h in ln for h in HIT)]
        playable = twoinst.alive(lane.GUEST) and mode(guest) == TRIBE and guest.call('coop.avatar')['ok']
        print(f'  #{i + 1}: guest in a playable Tribe: {playable}, faults {faults(guest) if playable else "?"}', flush=True)
        if lines:
            hit = True
            ck(playable, f'#{i + 1}: the guest\'s tribe setup met the null camera and its game is in a playable Tribe')
            break
        if not playable:
            ck(False, f'#{i + 1}: the guest\'s game is in a playable Tribe')
            break
    ck(hit, 'the race was made: the tribe setup ran without a simulator camera')
    ck(twoinst.alive(lane.GUEST) and faults(guest) == 0, 'guest: no fault')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
