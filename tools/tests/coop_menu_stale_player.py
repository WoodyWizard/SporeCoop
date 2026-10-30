#!/usr/bin/env python3
"""BUG-0284: a load from the galaxy menu after a stage exit that left the old world's player empire
in the menu (the 4-player campaign's jump from Tribe into the civ_npc fixture crashed the host in
cStarRecord::GetPlanetRecord+0x12, read of 0).

The game's own reset for the galaxy menu (0xB32C30) clears the player's record (SpacePlayerData:
empire id, cached cEmpire*, colonies) at 0xB32CF0 before it reads the slot's stars. In the campaign a
game fault while the tribe was left (handled by the game's own exception handler) cut that reset
short. The mod's load then read another slot's stars, the star manager wrote stars.db back and asked
for the player's home planet: the old planet id indexed a star of the new galaxy without planets.

That game fault cannot be made on purpose, so the test makes its effect: the call of the player
reset at 0xB32CF0 is patched out (5 NOPs, `mem.write`, GOG 2024 bytes checked first) while the host
leaves its stage for the menu, and put back once the menu is up. Then the mod's load runs as in the
campaign.

  rounds (--rounds, default 3): the host plays the tribe fixture (Zorispe), loads the civ_npc fixture
  (Tavael, a fresh slot) with the reset patched out, must reach Civ with an avatar and no fault; then
  loads the tribe fixture again (patched out too: the other direction) and must reach Tribe.
  Checks: no fault and no crash on either game, the host reached each stage, the guest followed,
  the mod cleared the stale player each time (`coop.status`: stale menu player cleared N).

  coop_menu_stale_player.py [--nofix] [--rounds N]   (--nofix: coop.fix world.menu_player off: must
  FAIL, the host crashes in the first load)
"""
import argparse
import re
import time

import fixtures
import lane
from lane import sh
from twoinst import Checks, Monitor, start, wait_guest_in

CALL_AT = 0xB32CF0                 # galaxy-menu reset 0xB32C30: call SpacePlayerData_Reset 0x1022350
CALL_BYTES = 'E8 5B F6 4E 00'      # GOG 2024 (dc04aee5)
MODES = {'Tribe': 0x1654C02, 'Civ': 0x1654C04}


def mode(con):
    try:
        return int(con.call('game.mode')['out'].split()[0], 16)
    except (OSError, ValueError, IndexError):
        return -1


def call_bytes(con):
    out = con.call(f'mem.read 0x{CALL_AT:X} 5')['out']
    return ' '.join(re.findall(r'\b[0-9A-Fa-f]{2}\b', out.split('  ', 1)[1] if '  ' in out else '')).upper()


def cleared(con):
    m = re.search(r'stale menu player cleared (\d+)', con.call('coop.status')['out'])
    return int(m.group(1)) if m else -1


def jump(host, guest, fx, stage, slot):
    """the host loads fixture fx from a fresh slot with the menu's player reset patched out;
    True when the host plays `stage` with an avatar and the guest followed"""
    planet = fixtures.planet_of(fx).split('/')[1]
    sh('fixture', 'slot', fx, lane.HOST_PROFILE, str(slot))
    host.call(f'mem.write 0x{CALL_AT:X} 9090909090')
    print('  ', host.call(f'coop.load Game{slot} {planet}')['out'].strip()[:60], flush=True)
    t0, left, restored = time.time(), False, False
    try:
        while time.time() - t0 < 150 and not Monitor.events:
            m = mode(host)
            if m == 0x2CCD1D2 and not restored:  # the menu is up: its reset ran (without the call)
                time.sleep(0.3)
                host.call(f'mem.write 0x{CALL_AT:X} {CALL_BYTES.replace(" ", "")}')
                restored = True
                print(f'   host in the galaxy menu after {time.time() - t0:.1f} s (player reset skipped)', flush=True)
            left |= m != MODES[stage] or not host.call('coop.avatar')['ok']
            if left and m == MODES[stage] and host.call('coop.avatar')['ok']:
                break
            time.sleep(0.4)
        else:
            return False
    finally:
        if not restored:
            try:
                host.call(f'mem.write 0x{CALL_AT:X} {CALL_BYTES.replace(" ", "")}')
            except OSError:
                pass
    print(f'   host in {stage} after {time.time() - t0:.0f} s', flush=True)
    return wait_guest_in(host, guest, 150)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nofix', action='store_true')
    ap.add_argument('--rounds', type=int, default=3)
    a = ap.parse_args()
    host, guest = start('tribe')
    ck = Checks()
    have = call_bytes(host)
    if have != CALL_BYTES:
        print(f'RESULT: SKIP (not the GOG 2024 bytes at 0x{CALL_AT:X}: {have})')
        return 0
    if a.nofix:  # (SPORE_FIX_OFF=world.menu_player does the same through start())
        for c in (host, guest):
            print(' ', c.call('coop.fix world.menu_player off')['out'].strip())
    fix_on = 'world.menu_player on' in host.call('coop.fix list')['out']
    time.sleep(3)
    n = 0
    for r in range(a.rounds):
        for fx, stage in (('civ_npc', 'Civ'), ('tribe', 'Tribe')):
            print(f'  round {r + 1}: tribe/civ -> {fx}', flush=True)
            ok = jump(host, guest, fx, stage, 10 + n % 2)
            n += 1
            if not ck(ok, f'round {r + 1}: the host loaded {fx} after a stage exit without the player reset and the guest followed'):
                break
            ck(cleared(host) == (n if fix_on else 0), f'round {r + 1}: the mod cleared the stale menu player ({cleared(host)} of {n})')
            time.sleep(3)
        else:
            continue
        break
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
