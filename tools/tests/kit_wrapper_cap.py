#!/usr/bin/env python3
"""BUG-0779: a d3d9 wrapper's frame cap holds under SporeKit's hooks (alone, with the mod, hosting).

The player of "Screen tearing" had a 7672 KB d3d9.dll next to SporeApp.exe: the size of the 32-bit DXVK 3.1.1 d3d9.dll
of the lab's bottles (7 856 142 bytes). SporeKit hooks Direct3DCreate9, CreateDevice and Present of the d3d9.dll the
game loads (the wrapper's then). This test puts that DXVK d3d9.dll into the lane's game folder with a dxvk.conf
`d3d9.maxFrameRate = 30`, lifts the game's own frame limit ([sporekit] frame_ms = 0: kFrameLimitMS 0) and measures the
Present interval (`frame.interval`) of the game:

1. SporeKit without mods ([mods] load = none), in the menu;
2. SporeKit + coop, in the menu;
3. hosting: two games in the Creature fixture (both with the wrapper; the guest too);
4. a control: the same without dxvk.conf (no cap from anywhere): the game runs faster than 30 fps, so the 30 fps of
   1-3 came from the wrapper's limiter;
5. [sporekit] vsync = 1 (BUG-0779's remedy): CreateDevice and Reset ask for presentation interval one (the log line).
PASS: 1-3 at 30 fps (mean 33.3 ms +- 3), the log names Present in "d3d9.dll (game folder)", 4 faster, 5 logged.
The lane's game folder and its sporekit.ini are put back exactly as they were at the end (also after a failure).

  SPORE_LANE=4 python3 tools/tests/kit_wrapper_cap.py
"""
import os
import re
import shutil
import time

import lane
import twoinst
from sporectl import Console
from twoinst import Checks

BIN = os.path.join(lane.game_dir(), 'SporebinEP1')
INI = os.path.join(BIN, 'mods', 'sporekit.ini')
DLL = os.path.join(BIN, 'd3d9.dll')
CONF = os.path.join(BIN, 'dxvk.conf')
SRC = os.path.expanduser(f'~/.var/app/com.usebottles.bottles/data/bottles/bottles/{lane.BOTTLE}/drive_c/windows/'
                         'syswow64/d3d9.dll')


def set_ini(text, frame_ms=None, load=None, vsync=None):
    out, sect = [], ''
    for ln in text.splitlines():
        s = ln.strip()
        if s.startswith('['):
            sect = s
        if sect == '[sporekit]' and re.match(r'(frame_ms|vsync)=', s):
            continue
        if sect == '[mods]' and s.startswith('load=') and load is not None:
            ln = 'load=' + load
        out.append(ln)
        if s == '[sporekit]':
            if frame_ms is not None:
                out.append(f'frame_ms={frame_ms}')
            if vsync is not None:
                out.append(f'vsync={vsync}')
    with open(INI, 'w') as f:
        f.write('\n'.join(out) + '\n')


def up(inst, t=120):
    t0 = time.time()
    while time.time() - t0 < t:
        try:
            c = Console(inst)
            if c.call('ping')['ok']:
                return c
        except OSError:
            pass
        time.sleep(1)
    return None


def interval(con, secs=8):
    con.call('frame.interval reset')
    time.sleep(secs)
    out = con.call('frame.interval')['out'].strip()
    m = re.search(r'mean ([\d.]+) ms', out)
    return (float(m.group(1)) if m else None), out


def log_text(inst):
    try:
        with open(lane.game_log(inst), 'rb') as f:
            return f.read().decode('utf-8', 'replace')
    except OSError:
        return ''


def menu_run(ck, what):
    """the host game alone up to the menu, then its frame interval"""
    lane.sh('kill')
    time.sleep(2)
    lane.run_host()
    con = up(lane.HOST)
    if not con:
        ck(False, f'{what}: the game started')
        return None
    for _ in range(120):
        if 'GalaxyGameEntry' in con.call('game.mode')['out']:
            break
        time.sleep(1)
    con.call('wait.frames 200 30000')
    mean, txt = interval(con)
    print(f'  {what}: {txt}; {con.call("frame")["out"].strip()}', flush=True)
    return mean, con


def at30(m):
    return m is not None and 30.3 <= m <= 36.5


def main():
    ck = Checks()
    old_ini = open(INI).read()
    had = [p for p in (DLL, CONF) if os.path.exists(p)]
    if had:
        print(f'RESULT: FAIL (the lane already has {had}: not this test\'s files, left alone)')
        return 1
    try:
        shutil.copyfile(SRC, DLL)
        with open(CONF, 'w') as f:
            f.write('d3d9.maxFrameRate = 30\n')
        print(f'  wrapper: {SRC} ({os.path.getsize(DLL)} bytes) -> {DLL}, dxvk.conf d3d9.maxFrameRate = 30', flush=True)

        # 1. SporeKit alone
        set_ini(old_ini, frame_ms=0, load='none')
        r = menu_run(ck, 'SporeKit alone')
        mods = r[1].call('info')['out'] if r else ''
        ck(r and at30(r[0]), f'SporeKit alone: the wrapper\'s 30 fps hold ({r and r[0]} ms)')
        ck('coop' not in mods.split('mods:')[-1], 'SporeKit alone: no mod loaded')
        log = log_text(lane.HOST)
        ck('Present at' in log and 'd3d9.dll (game folder)' in log.split('Present at')[-1].splitlines()[0],
           'the log names the hooked Present in the game folder\'s d3d9.dll')

        # 2. SporeKit + coop
        set_ini(old_ini, frame_ms=0)
        r = menu_run(ck, 'SporeKit + coop')
        ck(r and at30(r[0]), f'SporeKit + coop: the wrapper\'s 30 fps hold ({r and r[0]} ms)')

        # 3. hosting (cold start of both games with the same files)
        lane.sh('kill')
        time.sleep(2)
        os.environ.pop('SPORE_WARM', None)
        host, guest = twoinst.start('creature')
        for who, con in (('host', host), ('guest', guest)):
            m, txt = interval(con)
            print(f'  hosting, {who}: {txt}', flush=True)
            ck(at30(m), f'hosting, {who}: the wrapper\'s 30 fps hold ({m} ms)')
        ck.faults(host=host, guest=guest)

        # 4. control: no cap anywhere
        os.remove(CONF)
        r = menu_run(ck, 'no cap (control)')
        ck(r and r[0] is not None and r[0] < 28, f'control: without the wrapper\'s cap the game runs faster ({r and r[0]} ms)')

        # 5. vsync = 1
        set_ini(old_ini, vsync=1)
        r = menu_run(ck, 'vsync=1')
        log = log_text(lane.HOST)
        ck('vsync: [sporekit] vsync=1: presentation interval 0x1' in log, '[sporekit] vsync=1 asks for interval one')
    finally:
        lane.sh('kill', check=False)
        time.sleep(2)
        for p in (DLL, CONF):
            if os.path.exists(p):
                os.remove(p)
        with open(INI, 'w') as f:
            f.write(old_ini)
        print('  lane restored: d3d9.dll and dxvk.conf removed, sporekit.ini as before', flush=True)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
