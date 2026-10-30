#!/usr/bin/env python3
"""BUG-0310 / BUG-0285: the host leaves the Tribe stage for the galaxy menu (the campaign's jump to
the civ_npc fixture) and the stage change sends a citizen the stage had already cleaned up on a new
walk: CreatureBase_SetMoveScale (0xC0DB10) read +0x3C of the citizen's null +0xB54 (0xC0DB23), the
game's handler caught it and unwound past the rest of the mode change, and the next load from the
menu faulted in the space graphics init and never finished (lab 20261030-104149 campaign#3).

Which citizen the stage had cleaned up cannot be made on purpose, so the test makes one: `coop.leavewalk`
arms the mod's off-stage guard to clear +0xB54 (what CreatureBase_Cleanup leaves) of the first
citizen the game checks outside the Tribe mode, and, with the fix off, to send it on the walk the
check's own end gives a citizen with no place in range (Citizen_IdleWalk 0xD57680). With the fix on
the guard skips that citizen (and puts +0xB54 back).

  rounds (--rounds, default 2): the host plays the tribe fixture, arms the stand-in, loads the civ_npc
  fixture (Tavael, a fresh slot) through the galaxy menu, must reach Civ with an avatar; then loads
  the tribe fixture again. Checks: the stand-in fired, no fault and no crash on either game, the host
  reached each stage, the guest followed.

  coop_tribe_leave_walk.py [--nofix] [--rounds N]   (--nofix: coop.fix tribe.leave_walk off: must
  FAIL with the campaign's fault 0xC0DB23)
"""
import argparse
import re
import time

import fixtures
import lane
from lane import sh
from twoinst import Checks, Monitor, start, wait_guest_in

MODES = {'Tribe': 0x1654C02, 'Civ': 0x1654C04}


def mode(con):
    try:
        return int(con.call('game.mode')['out'].split()[0], 16)
    except (OSError, ValueError, IndexError):
        return -1


def gf(con, key):
    m = re.search(key + r' (\d+)', con.call('coop.status')['out'])
    return int(m.group(1)) if m else -1


def play(host, guest, seconds, seed):
    """the play_tribe bots (real clicks) for a while: citizens that gather and carry food are the
    ones the game's food behaviour hands to Citizen_OffStageCheck when the stage ends"""
    if seconds <= 0:
        return
    import play_tribe
    g = play_tribe.Game(host, guest, seed, 1.0)
    for p in g.p:
        p.calibrate()
    t0, n = time.time(), 0
    while time.time() - t0 < seconds and not Monitor.events:
        g.step(g.p[n % 2])
        n += 1
    print(f'   played {time.time() - t0:.0f} s, {n} actions', flush=True)


def jump(host, guest, fx, stage, slot, arm):
    """the host loads fixture fx from a fresh slot (through the galaxy menu); True when the host plays
    `stage` with an avatar and the guest followed"""
    planet = fixtures.planet_of(fx).split('/')[1]
    sh('fixture', 'slot', fx, lane.HOST_PROFILE, str(slot))
    if arm:
        print('  ', host.call('coop.leavewalk')['out'].strip(), flush=True)
    print('  ', host.call(f'coop.load Game{slot} {planet}')['out'].strip()[:60], flush=True)
    t0, left = time.time(), False
    while time.time() - t0 < 150 and not Monitor.events:
        try:
            m = mode(host)
            av = host.call('coop.avatar')['ok']
        except OSError:
            return False
        left |= m != MODES[stage] or not av
        if left and m == MODES[stage] and av:
            break
        time.sleep(0.4)
    else:
        return False
    print(f'   host in {stage} after {time.time() - t0:.0f} s', flush=True)
    return wait_guest_in(host, guest, 150)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nofix', action='store_true')
    ap.add_argument('--rounds', type=int, default=2)
    ap.add_argument('--play', type=float, default=90, help='seconds of play_tribe bots before each leave of Tribe')
    a = ap.parse_args()
    host, guest = start('tribe')
    ck = Checks()
    if 'off-stage checks' not in host.call('coop.status')['out']:
        print('RESULT: SKIP (no citizen off-stage guard on this build)')
        return 0
    if a.nofix:  # (SPORE_FIX_OFF=tribe.leave_walk does the same through start())
        print(' ', host.call('coop.fix tribe.leave_walk off')['out'].strip())
    print('  setup:', host.call('coop.tribe spawn 3')['out'].strip(), flush=True)
    time.sleep(10)
    n = 0
    for r in range(a.rounds):
        for fx, stage in (('civ_npc', 'Civ'), ('tribe', 'Tribe')):
            arm = stage == 'Civ'  # only a leave of the Tribe stage
            before = gf(host, 'test citizens')
            print(f'  round {r + 1}: -> {fx}', flush=True)
            if arm:
                play(host, guest, a.play, 1000 + r)
            ok = jump(host, guest, fx, stage, 10 + n % 2, arm)
            n += 1
            try:
                if arm:
                    print('  ', re.search(r'off-stage checks.*', host.call('coop.status')['out']).group(0), flush=True)
                    ck(gf(host, 'test citizens') == before + 1, f'round {r + 1}: the stand-in made a cleaned-up citizen as Tribe was left')
            except (OSError, AttributeError):
                pass
            if not ck(ok, f'round {r + 1}: the host loaded {fx} after leaving the stage and the guest followed'):
                break
            time.sleep(3)
        else:
            continue
        break
    try:
        ck.faults(host=host, guest=guest)
    except OSError as e:  # a game that crashed does not answer its console
        ck(False, f'both games answer after the loads ({e})')
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
