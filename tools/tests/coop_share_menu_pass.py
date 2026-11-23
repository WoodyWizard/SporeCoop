#!/usr/bin/env python3
"""A guest's slow pass through the galaxy menu to load the host's world gets that world once (BUG-1435).

A player report (2026-11-22, coop 0.7.15.5): the host sent its world 8 times, about every 53 s, and the guest left
the stage and reloaded each time ("it constantly reloads the world") until the host left. The guest plays on in the
stage while the share is saved and sent, so the host took those frames for "the guest joined the phase"; then the
guest leaves the stage for the galaxy menu to load the world (its save on quit, the menu's 3 s settle: 5.2-5.4 s on
the players' PCs) and the host's 5 s "left to the main menu" window (session_glue.cpp follow_phase) sent the world
again. The next load went the same way.

1. Creature fixture. The guest's galaxy menu settles 6 s before a queued load (`coop.worldfix menusettle 6`: the
   slow PC's menu pass, longer than the host's 5 s window).
2. The guest leaves the session and joins again while it plays the host's world: the host sends the world at the join.
3. For 150 s from the join: the guest's game mode every second (its passes through the galaxy menu), the worlds the
   guest's game received and started loading (its log), the shares the host started (its log).
4. PASS: the guest's game received the world once, passed the galaxy menu once and plays the host's world with the
   host at the end; the host sent the world once.
   SPORE_FIX_OFF=core.share_load_wait (the twin, share_menu_pass_off): the host sends the world again while the guest
   is in the menu, the guest loads it again and again: FAIL.

  SPORE_LANE=4 python3 tools/tests/coop_share_menu_pass.py
"""
import re
import time

import lane
from harness import log_since, log_size, mode
from twoinst import Checks, keyframe, start, wait_guest_in

WATCH = 150  # s from the join; in the players' loop a share came every ~53 s, in the lab's every ~15 s


def main():
    host, guest = start('creature')
    ck = Checks()
    print('  guest:', guest.call('coop.worldfix menusettle 6')['out'].strip(), flush=True)
    guest.call('coop.leave')
    time.sleep(3)
    h0, g0 = log_size(lane.HOST), log_size(lane.GUEST)
    print('  guest joins again:', guest.call(lane.JOIN)['out'].strip()[:120], flush=True)
    t0 = time.time()
    passes, in_menu, modes = 0, False, []
    while time.time() - t0 < WATCH:
        m = mode(guest)
        menu = 'GalaxyGameEntry' in m
        if menu and not in_menu:
            passes += 1
            print(f'  {time.time() - t0:5.1f} s: the guest is in the galaxy menu (pass {passes})', flush=True)
        in_menu = menu
        modes.append(m.split()[0] if m.split() else '?')
        f1 = host.call('frame')['out']
        time.sleep(1)
        if host.call('frame')['out'] == f1:
            host.call('key enter')  # the host's "game saved" box of the share's save
    ck(wait_guest_in(host, guest, 60), "the guest plays the host's world with the host at the end")
    hl, gl = '\n'.join(log_since(lane.HOST, h0)), '\n'.join(log_since(lane.GUEST, g0))
    received = len(re.findall(r'world: received \d+ bytes into', gl))
    loads = len(re.findall(r'world: loading .+?\.spo from ', gl))
    sends = len(re.findall(r'(sending the world to|back to the world: sending it to|next phase: sending the world to) Bob', hl))
    print(f'  guest: received {received}, loads {loads}, menu passes {passes}; host: shares {sends}', flush=True)
    print('  host:', ' '.join(ln for ln in host.call('coop.status')['out'].splitlines() if ln.startswith('load_wait')),
          flush=True)
    ck(received == 1, f"the guest's game received the host's world once ({received})")
    ck(loads == 1 and passes == 1, f"the guest's game went through the galaxy menu and loaded the world once "
       f"(loads {loads}, menu passes {passes})")
    ck(sends == 1, f'the host sent its world once ({sends})')
    keyframe('share_menu_pass', "Creature after the guest's rejoin with a slow galaxy menu pass: both play the host's "
             "world together, the guest loaded it once")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
