#!/usr/bin/env python3
"""BUG-0782 / BUG-0779: the player is told about programs that change the game.

1. RivaTuner / MSI Afterburner (BUG-0782: crashes at start reported with it running). A do-nothing DLL named
   rtsshooks.dll (build/test/rtsshooks.dll, tests/dll/stand_in.c) is loaded into the host's game
   (`kit.test.loadlib`), as RTSS injects its RTSSHooks.dll. PASS: SporeKit's `other hooks` names RivaTuner, the
   host's `env` (the text of every crash report) has the advice line, and within 15 s the host's game shows the
   notice about RivaTuner (a toast, the game's own panel); the guest shows none. `SPORE_FIX_OFF=core.rtss_note`:
   no notice: FAIL.
2. The game without its frame limit (BUG-0779: a player's game ran at 82-138 fps and tore). kFrameLimitMS is set to
   0 on the host (as a mod that lifts the cap would). PASS: within two minutes (the minute line of the frame
   statistic) the host shows the notice "Spore runs at N fps without its frame cap"; the guest (capped) none.
   `SPORE_FIX_OFF=core.uncapped_note`: no notice: FAIL.
The DLL is removed from the game folder and the frame limit put back at the end.

  SPORE_LANE=4 python3 tools/tests/coop_env_notes.py
"""
import os
import re
import shutil
import struct
import time

import lane
from twoinst import Checks, start

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
STAND_IN = os.path.join(ROOT, 'build', 'test', 'rtsshooks.dll')
DLL = os.path.join(lane.game_dir(), 'SporebinEP1', 'rtsshooks.dll')


def toasts(con):
    return re.findall(r"^\s*toast \S* \([^)]*\) '(.*?)'(?: \||$)", con.call('coop.status')['out'], re.M)


def wait_toast(con, pattern, secs):
    t0 = time.time()
    while time.time() - t0 < secs:
        for t in toasts(con):
            if re.search(pattern, t):
                return t
        time.sleep(0.5)
    return None


def field(con):
    m = re.findall(r'-> 0x([0-9A-F]+)', con.call('mem.ptr App::FrameLimit_ptr 0x3C 0xB0')['out'])
    return int(m[-1], 16) if len(m) == 2 else None


def read_i32(con, a):
    m = re.match(r'\s*[0-9A-Fa-f]+\s+((?:[0-9A-Fa-f]{2}\s*){4})', con.call(f'mem.read 0x{a:X} 4')['out'].strip())
    return struct.unpack('<i', bytes.fromhex(m.group(1).replace(' ', '')))[0] if m else None


RTSS = r'RivaTuner'
UNCAPPED = r"fps without its frame cap"


def main():
    host, guest = start('creature')
    ck = Checks()
    if not os.path.exists(STAND_IN):
        print(f'RESULT: FAIL ({STAND_IN} missing: make)')
        return 1
    addr, own = None, None
    try:
        # 1. RivaTuner's DLL
        shutil.copyfile(STAND_IN, DLL)
        out = host.call('kit.test.loadlib rtsshooks.dll')['out'].strip()
        print('  host: ' + out, flush=True)
        ck('RivaTuner' in out, 'SporeKit names RivaTuner among the other hooks')
        env = host.call('env')['out']
        ck('advice: RivaTuner' in env, 'the host\'s environment text (crash reports) has the RivaTuner advice')
        t = wait_toast(host, RTSS, 15)
        print(f'  host toast: {t!r}', flush=True)
        ck(t is not None, 'the host\'s game tells the player about RivaTuner')
        ck(wait_toast(guest, RTSS, 2) is None, 'the guest (no RivaTuner) shows no such notice')

        # 2. the game without its frame limit
        addr = field(host)
        own = read_i32(host, addr) if addr else None
        ck(own is not None and own > 0, f'the host\'s game has a frame limit ({own} ms)')
        if addr:
            host.call(f'mem.write 0x{addr:X} 00000000')
            t = wait_toast(host, UNCAPPED, 140)
            print(f'  host toast: {t!r}; {host.call("frame")["out"].strip()}', flush=True)
            ck(t is not None, 'the host\'s game tells the player it runs without its frame cap')
            ck(wait_toast(guest, UNCAPPED, 2) is None, 'the guest (capped) shows no such notice')
        ck.faults(host=host, guest=guest)
    finally:
        if addr and own:
            host.call(f'mem.write 0x{addr:X} {struct.pack("<i", own).hex()}')
        try:
            os.remove(DLL)  # (loaded: Wine keeps the mapping, the file can go)
        except OSError as e:
            print(f'  could not remove {DLL}: {e}', flush=True)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
