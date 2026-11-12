#!/usr/bin/env python3
"""BUG-0779: a frame limit that another program sets is kept, SporeKit does not write its own value back.

A player's report (Discord 2026-11-09, "Screen tearing"): with the mod the game ran uncapped (82-138 fps) and tore;
he found it goes away without Spore ModAPI. The game's frame limit is its property kFrameLimitMS (AppProperties id
44, `[[App::FrameLimit_ptr]+0x3C]+0xB0`; 16 ms in the GOG 2024 lab games, the 60 fps cap). SporeKit read it once at its first frame and wrote
that value back every 10 frames (sporekit/focus.cpp apply_frame_limit), so a frame limit another program set later
(a ModAPI mod, a frame limiter, the game itself) was undone within 10 frames.

1. Creature fixture, two games in a session (the host hosts). The host's frames: the Present interval statistic
   (`frame.interval`) is about the game's own limit (16 ms).
2. "Another program" sets kFrameLimitMS to 50 ms on the host (mem.write into the game's property, as a mod would).
   PASS: 10 s later the property still holds 50 and the measured Present interval is 50 ms +- 5 (20 fps) on the
   host; the guest is untouched. `SPORE_FIX_OFF=kit.framelimit` (the old write-back): the property is 16 again
   and the interval 16 ms: FAIL.
3. The property set back to the game's own value: that frame rate again.

  SPORE_LANE=4 python3 tools/tests/kit_frame_limit.py
"""
import re
import struct
import time

from twoinst import Checks, start


def field(con):
    out = con.call('mem.ptr App::FrameLimit_ptr 0x3C 0xB0')['out']
    m = re.findall(r'-> 0x([0-9A-F]+)', out)
    return int(m[-1], 16) if len(m) == 2 else None


def limit(con, addr):
    out = con.call(f'mem.read 0x{addr:X} 4')['out']
    m = re.match(r'\s*[0-9A-Fa-f]+[: ]+((?:[0-9A-Fa-f]{2}\s*){4})', out.strip())
    if not m:
        return None
    return struct.unpack('<i', bytes.fromhex(m.group(1).replace(' ', '')))[0]


def set_limit(con, addr, ms):
    return con.call(f'mem.write 0x{addr:X} {struct.pack("<i", ms).hex()}')['out'].strip()


def interval(con, secs):
    """the mean Present interval (ms) over `secs` seconds of play"""
    con.call('frame.interval reset')
    time.sleep(secs)
    out = con.call('frame.interval')['out'].strip()
    m = re.search(r'mean ([\d.]+) ms', out)
    return (float(m.group(1)) if m else None), out


def main():
    host, guest = start('creature')
    ck = Checks()
    print('  host: ' + host.call('frame')['out'].strip(), flush=True)
    ha, ga = field(host), field(guest)
    ck(ha and ga, f'the frame limit property is found (host 0x{ha or 0:X}, guest 0x{ga or 0:X})')
    if not (ha and ga):
        return ck.result()
    own = limit(host, ha)
    ck(own is not None and 10 <= own <= 40, f'the host\'s game has its own frame limit ({own} ms; GOG 2024: 16)')
    if own is None:
        return ck.result()

    def near_own(m):
        return m is not None and own - 2 <= m <= own + 4

    mean, txt = interval(host, 6)
    print('  host before: ' + txt, flush=True)
    ck(near_own(mean), f'host: at its own limit before ({mean} ms, own {own})')

    # 2. another program sets a 20 fps limit
    print('  host: ' + set_limit(host, ha, 50), flush=True)
    time.sleep(3)
    mean, txt = interval(host, 10)
    now = limit(host, ha)
    print('  host after: ' + txt + f'; property {now} ms; ' + host.call('frame')['out'].strip(), flush=True)
    ck(now == 50, f'host: the frame limit another program set stays (property {now} ms, wanted 50)')
    ck(mean is not None and 45 <= mean <= 56, f'host: the game runs at that limit (Present interval {mean} ms, '
                                               'wanted 50 +- 5)')
    gmean, gtxt = interval(guest, 4)
    print('  guest: ' + gtxt, flush=True)
    ck(near_own(gmean), f'guest: at its own limit ({gmean} ms)')

    # 3. back to the game's value
    set_limit(host, ha, own)
    time.sleep(2)
    mean, txt = interval(host, 5)
    print('  host back: ' + txt, flush=True)
    ck(near_own(mean) and limit(host, ha) == own, f'host: its own limit again ({mean} ms)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
