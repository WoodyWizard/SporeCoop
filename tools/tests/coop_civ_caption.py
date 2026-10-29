#!/usr/bin/env python3
"""Civilization: the host's capture caption does not stop everybody's game (BUG-0187).

A captured city plays a cinematic whose caption waits for OK; the host's whole world (and so every
guest's vehicles) stands still until then. With guests in the session the caption now closes by
itself after 8 s (the game's EndCinematic, as Enter does).

1. The host's civilization captures an NPC city (`coop.civ capture <city>`: the game's Capture,
   which plays the caption cinematic).
2. The game's cinematic manager plays it (state +0x2C set) on the host.
3. Nobody presses Enter: 14 s later no cinematic plays on the host any more, the city belongs to
   the player civilization on both machines, and a host vehicle drives again (its game position
   changes after a move order).

0. BUG-0224: a cinematic that no capture started (the camera cinematic "CVG_PRE_NewCityAppears",
   started through the game's PlayCinematic) still plays on the host 12 s later: only a capture's
   caption is closed. The test then ends it (EndCinematic, as the OK does).

--nofix: `coop.civ fix caption 0` (the old wait for the OK): must FAIL.
SPORE_FIX_OFF=civ.caption_only: every cinematic is closed after 8 s (0.7.3.0): must FAIL (step 0).

  SPORE_LANE=7 python3 tools/tests/coop_civ_caption.py [--nofix]
"""
import argparse
import re
import time

from coop_civ import civ, dist
from twoinst import Checks, start


def movie(con):
    out = con.call('coop.civ movie')['out']
    m = re.search(r'playing=(\d)', out)
    return (int(m.group(1)) if m else -1), out.strip()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nofix', action='store_true')
    a = ap.parse_args()
    host, guest = start('civ_npc')
    ck = Checks()
    if a.nofix:
        print(' ', host.call('coop.civ fix caption 0')['out'].strip())
    time.sleep(4)
    # 0. BUG-0224: another cinematic plays on
    m = re.search(r'eax=0x([0-9A-Fa-f]+)', host.call('call Simulator::cCinematicManager::Get cdecl')['out'])
    if ck(m, 'the cinematic manager'):
        cm = m.group(1)
        print(' ', host.call(f'call Simulator::cCinematicManager::PlayCinematic thiscall 0x{cm} s:CVG_PRE_NewCityAppears 1 1 0 0 0')['out'].strip())
        time.sleep(1)
        p0, out0 = movie(host)
        print('  ', out0)
        if ck(p0 == 1, 'a cinematic that no capture started plays on the host'):
            time.sleep(12)
            p1, out1 = movie(host)
            print('  13 s later:', out1)
            ck(p1 == 1, 'it still plays 13 s later (only a capture\'s caption is closed)')
        print(' ', host.call(f'call SporeCoop::cCinematicManager_EndCinematic thiscall 0x{cm}')['out'].strip())
        time.sleep(2)
        print('  ', movie(host)[1])
    hc, hv = civ(host)
    ppol = next((v['pol'] for v in hc.values() if v['player']), None)
    npc = [k for k, v in hc.items() if not v['player']]
    print('  player pol', ppol, 'npc cities', npc)
    if not ck(ppol and npc, 'an NPC city to capture'):
        return ck.result()
    city = npc[0]
    print(' ', host.call(f'coop.civ capture {city}')['out'].strip())
    t0 = time.time()
    seen = False
    while time.time() - t0 < 5 and not seen:
        seen = movie(host)[0] == 1
        time.sleep(0.3)
    print('  ', movie(host)[1])
    ck(seen, 'the capture caption plays on the host')
    time.sleep(14)
    p, out = movie(host)
    print('  14 s later:', out)
    ck(p == 0, 'no cinematic holds the host\'s world any more (nobody pressed OK)')
    hc, hv = civ(host)
    gc, _ = civ(guest)
    ck(hc.get(city, {}).get('pol') == ppol and gc.get(city, {}).get('pol') == ppol,
       'the city belongs to the player civilization on both machines')
    own = [i for i, v in hv.items() if v['owner'] == 0]
    if ck(own, 'the host has a vehicle'):
        vid = own[0]
        p0 = hv[vid]['pos']
        cpos = next(v['pos'] for v in hc.values() if v['player'])
        d = dist(p0, cpos) or 1
        tgt = tuple(c + (q - c) / d * (d + 40) for q, c in zip(p0, cpos))
        print(' ', host.call('coop.civ move %d %.1f %.1f %.1f' % ((vid,) + tgt))['out'].strip())
        time.sleep(8)
        _, hv = civ(host)
        moved = dist(hv[vid]['pos'], p0) if vid in hv else 0
        ck(moved > 5, f'the host\'s vehicle drives again ({moved:.1f} m)')
    if a.nofix:
        host.call('key enter')  # let the next test of the lane find a running world
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
