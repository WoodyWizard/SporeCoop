#!/usr/bin/env python3
"""Stage switches in running games (docs/known-issues.md history: loading a Cell save while the
Space stage ran crashed in cStar::GetSolarSystem). The host loads a fixture of another stage with
`coop.load` while the guest stays connected; the guest is then "behind" (another stage than the
host), receives the host's world and loads it in game (world.cpp). After every switch: no faults
in either game, both in the target stage and together (`together 1 1` on the host).

usage: coop_stage_switch.py [--steps space,rejoin,space,cell,...]
  default route: space -> rejoin -> space -> cell -> space -> creature -> space -> tribe -> civ -> cell
  (guest rejoin in Space = space->space for the guest's received world, then host space->space,
  space->cell, cell->space, space->creature, creature->space, space->tribe, tribe->civ, civ->cell)
  "rejoin": the guest leaves and joins again in the current stage, receives the host world and
  loads it in game (seen crashing in the nightly chaos run: Space -> Space)
"""
import argparse
import time

from harness import mode_id as mode
import fixtures
import lane
from lane import sh
from twoinst import Checks, lane_state, start, wait_guest_in, build_id, Monitor

ROUTE = 'space,rejoin,space,cell,space,creature,space,tribe,civ,cell'
MODE = {'cell': 0x1654C00, 'creature': 0x1654C01, 'tribe': 0x1654C02, 'civ': 0x1654C04, 'space': 0x1654C05}


def faults(con):
    try:
        return con.call('exc')['out'].splitlines()[0]
    except (OSError, ValueError, IndexError):
        return 'no answer'


def switch(host, guest, fixture, slot):
    """the host loads `fixture` from a fresh slot in game; True when both play it together"""
    t0 = time.time()
    planet = fixtures.planet_of(fixture).split('/')[1]
    sh('fixture', 'slot', fixture, lane.HOST_PROFILE, str(slot))  # never the loaded save's files
    r = host.call(f'coop.load Game{slot} {planet}')['out'].strip()
    want = MODE[fixture]
    th = None
    host_left = host_left_at_start = mode(host) != want  # same stage (space -> space): leave it first
    while time.time() - t0 < 120:
        m = mode(host)
        host_left |= m != want
        if host_left and m == want and host.call('coop.avatar')['ok']:
            th = time.time() - t0
            break
        if Monitor.events:
            break
        time.sleep(0.3)
    # the guest is behind now: the host sends its world and the guest loads it in game; "together"
    # counts only after the guest has left the old stage. A reload of the same stage by the host
    # sends nothing (coop.cpp on_mode_change: not a new stage), the guest keeps its world: the
    # guest's own in-game reload is the "rejoin" step.
    guest_left = mode(guest) != want or not host_left_at_start
    while th is not None and not guest_left and time.time() - t0 < 120 and not Monitor.events:
        guest_left = mode(guest) != want
        f = host.call('frame')['out']
        time.sleep(0.7)
        if host.call('frame')['out'] == f:
            host.call('key enter')  # the host's "game saved" box (wait_guest_in does the same)
    together = th is not None and guest_left and wait_guest_in(host, guest, 150)
    hm, gm = mode(host), mode(guest)
    print(f'    {r[:50]}: host in {th and round(th, 1)} s, together={together} after {time.time() - t0:.1f} s; '
          f'host 0x{hm:08X} guest 0x{gm:08X}', flush=True)
    return together and hm == want and gm == want


def rejoin(host, guest, stage):
    """the guest leaves and joins again: it receives the host world and loads it in game"""
    t0 = time.time()
    guest.call('coop.leave')
    time.sleep(2)
    guest.call(lane.JOIN)
    # it is "together" again only after it has loaded the received world (leaves the stage first)
    left = False
    while time.time() - t0 < 90 and not left:
        left = mode(guest) != MODE[stage]
        if not left:
            time.sleep(0.3)
    together = left and wait_guest_in(host, guest, 150)
    hm, gm = mode(host), mode(guest)
    print(f'    rejoin: together={together} after {time.time() - t0:.1f} s; host 0x{hm:08X} guest 0x{gm:08X}',
          flush=True)
    return together and hm == gm == MODE[stage]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--steps', default=ROUTE)
    a = ap.parse_args()
    steps = a.steps.split(',')
    host, guest = start(steps[0])
    c = Checks()
    slot = 10
    stage = steps[0]
    for nxt in steps[1:]:
        if nxt == 'rejoin':
            ok = rejoin(host, guest, stage)
            label = f'guest rejoin in {stage} ({stage} -> {stage} for the received world)'
        else:
            label = f'{stage} -> {nxt} (host coop.load, guest received world)'
            ok = switch(host, guest, nxt, slot)
            lane_state(build=build_id(), fixture=nxt, planet=fixtures.planet_of(nxt), slot=slot, warm_runs=1)
            stage = nxt
            slot = slot + 1 if slot < 19 else 10
        fh, fg = faults(host), faults(guest)
        c(ok and 'total faults: 0' in fh and 'total faults: 0' in fg,
          f'{label}: both in {stage} together, faults host "{fh}" guest "{fg}"')
        if Monitor.events or 'total faults: 0' not in fh or 'total faults: 0' not in fg:
            break  # the next switch would only add noise
        time.sleep(3)
    c.faults(host=host, guest=guest)
    return c.result()


if __name__ == '__main__':
    raise SystemExit(main())
