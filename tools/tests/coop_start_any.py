#!/usr/bin/env python3
"""BUG-9293: in a new game started at a later stage every guest chooses its own creature and the cards of the earlier
stages (the owner's rule of 2026-11-20).

Both games fresh (no saves, the guest's My Creations from its profile); the host hosts in the galaxy menu, the guest
joins, the host starts a NEW game at --stage (tribe by default) through the stage wizard (new_stage_game.py).
Checked on the games' own state:
  1. the guest's game is asked (coop.startany prompt=<stage>) once the stage's intro is over;
  2. the guest answers: one of its My Creations creatures, cards Cell carnivore, Creature social, Tribe mixed (as many
     as the stage has earlier stages): the host's party seat of the guest has exactly those cards (own history), the
     guest's own cPlayer (its History) has them, the host's cPlayer does not;
  3. the guest's game plays that creature and the guest's party seat carries it (the host's key of it), so its nest,
     village or nation are of it;
  4. no faults.
--old (`coop.fix party.start_any off` on the host, twin start_any_off): step 1 fails (nobody is asked).

  SPORE_LANE=N python3 tools/tests/coop_start_any.py [--stage creature|tribe|civ] [--old]
"""
import argparse
import os
import re
import subprocess
import sys
import time

import lane
from lane import sh
from sporectl import Console
from twoinst import HERE, Checks, shot, wait_up

CARDS = ['CFB01B93', 'FD159D91', 'C2CA9495', '0A35D0F5']  # Cell carnivore, Creature social, Tribe mixed, Civ military
STAGE = {'creature': 1, 'tribe': 2, 'civ': 3}


def out(con, line):
    r = con.call(line)
    return (r.get('out') or r.get('error') or '').strip()


def wait(f, secs, step=2.0):
    t = time.time() + secs
    while time.time() < t:
        try:
            if f():
                return True
        except (OSError, ValueError, AttributeError):
            pass
        time.sleep(step)
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--stage', default='tribe', choices=sorted(STAGE))
    ap.add_argument('--old', action='store_true')
    a = ap.parse_args()
    ck = Checks()
    sh('kill')
    time.sleep(2)
    sh('deploy')
    lane.run_host()
    time.sleep(2)
    lane.run_guest()
    for inst, run in ((lane.HOST, lane.run_host), (lane.GUEST, lane.run_guest)):
        if not wait_up(inst, 90):
            run()
            assert wait_up(inst), f'instance {inst} did not start'
    host, guest = Console(lane.HOST), Console(lane.GUEST)
    wait(lambda: 'GalaxyGameEntry' in out(host, 'game.mode') and 'GalaxyGameEntry' in out(guest, 'game.mode'), 180)
    if a.old:
        host.call('coop.fix party.start_any off')
    host.call(lane.HOST_CMD)
    time.sleep(2)
    guest.call(lane.JOIN)
    time.sleep(5)
    creature = (re.findall(r'^creature .* (\w{8})$', out(guest, 'coop.startany creatures'), re.M) or [None])[0]
    if not ck(creature, 'the guest has a My Creations creature'):
        return ck.result()
    r = subprocess.run([sys.executable, os.path.join(HERE, 'new_stage_game.py'), a.stage, '-i', str(lane.HOST), '--slot', '0'],
                       capture_output=True, text=True, timeout=900)
    if not ck(r.returncode == 0, f'the host started a new {a.stage} game'):
        return ck.result()
    want = CARDS[:STAGE[a.stage]]
    asked = wait(lambda: re.search(r'prompt=[1-4]', out(guest, 'coop.startany')) is not None, 180)
    ck(asked, '1: the guest is asked for its own start')
    if asked:
        print('  guest:', out(guest, 'coop.startany pick %s %s' % (creature, ' '.join(str(i) for i in range(len(want))))), flush=True)
    time.sleep(8)
    party = out(host, 'coop.party')
    seat = re.search(r'seat 1: .*?\n\s+cards own=1 ((?:\w{8} ){5})', party + ' ')
    got = seat.group(1).split() if seat else []
    ck(got[:len(want)] == want, f"2: the guest's seat has its chosen cards {want} ({got})")
    gp = re.search(r'cplayer ((?:\w{8} ){5})', out(guest, 'coop.cards') + ' ')
    ck(gp and gp.group(1).split()[:len(want)] == want, "2: the guest's own History (cPlayer) has them")
    hp = re.search(r'cplayer ((?:\w{8} ){5})', out(host, 'coop.cards') + ' ')
    ck(hp and hp.group(1).split()[:len(want)] != want, "2: the host's own cards are not the guest's")
    mine = re.search(r'mine creature: (\w{8})', out(guest, 'coop.creations'))
    ck(mine and mine.group(1) == creature, f'3: the guest plays its chosen creature {creature}')
    pk = re.search(r'player 1 creature: (\w{8})', out(host, 'coop.creations'))
    ck(pk and wait(lambda: pk.group(1) in out(host, 'coop.party'), 40), "3: the guest's party seat carries that creature")
    shot(guest, f'start_any_{a.stage}_guest.png')
    host.call('coop.fix party.start_any on')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
