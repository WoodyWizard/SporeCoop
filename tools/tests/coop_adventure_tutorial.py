#!/usr/bin/env python3
"""BUG-1254: two players who played the Creature stage together start the tutorial adventure from the galaxy menu.

The players' video: both played the Creature stage, went to the galaxy menu and started the tutorial adventure. The
host's game showed the guest's puppet at its captain's feet as the guest's Creature-stage creature at its stage size
(an [Adventure] plate), and the guest's game stopped about 3 s later.

1. start('creature'): both games in the Creature fixture, the guest joined: each game has the partner's creation
   (the puppet of the partner is built from it);
2. the host leaves for the galaxy menu (`coop.menu`, the game's own quit), then the guest;
3. both start the tutorial adventure at the same time (adventure_start.py). Each game plays its own copy of the
   adventure, and the copies' resource keys are the same in most lab runs (BF0CE0D0 on both games): the same place
   id on both games, the players' case. --own-places keeps the games' own place ids; by default both get one
   (`coop.adventure place`) so the case does not depend on the keys;
4. the intro card is closed (Enter: play state 3) on both games, then 25 s of play;
5. checks: both games are still in the adventure and running, with no fault and no crash report (the lab checks the
   reports too); a puppet of the partner that a game shows is the partner's captain: its species is the one the
   partner's own game reports for its captain, and its size is within 25 % of the captain's size on the partner's
   game (x 1.15, the puppets' factor).
SPORE_FIX_OFF=adventure.puppet_captain must FAIL.

  SPORE_LANE=N python3 tools/tests/coop_adventure_tutorial.py [--own-places]
"""
import argparse
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
import lane  # noqa: E402
import uifind  # noqa: E402
from twoinst import Checks, keyframe, start  # noqa: E402


def call(c, line):
    try:
        return c.call(line)['out'].strip()
    except Exception as e:  # noqa: BLE001 (a game that died answers nothing)
        return f'no answer ({e.__class__.__name__})'


def mode(c):
    m = call(c, 'game.mode').split()
    return m[1] if len(m) > 1 else 'gone'


def act(c):
    m = re.search(r'state=(-?\d+) act=(-?\d+)', call(c, 'coop.adventure'))
    return (int(m.group(1)), int(m.group(2))) if m else None


def captain(c):
    """(species key, scale) of this game's own captain"""
    out = call(c, 'coop.avatar')
    k = re.search(r'key=0x([0-9A-F]{8})', out)
    s = re.search(r'scale=([0-9.]+)', out)
    return (k.group(1) if k else None, float(s.group(1)) if s else None)


def puppet(c):
    """(species, scale) of the partner's puppet in this game, None when there is none"""
    m = re.search(r'puppet pos=\([^)]*\) err=\S+ rot_err=\S+ species=([0-9A-F]{8})(?: scale=([0-9.]+))?',
                  call(c, 'coop.status'))
    return (m.group(1), float(m.group(2)) if m.group(2) else None) if m else None


def start_adventure(inst):
    return subprocess.Popen([sys.executable, os.path.join(HERE, 'adventure_start.py'), '-i', str(inst)])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--own-places', action='store_true', help='keep each game\'s own place id')
    ap.add_argument('--play', type=float, default=25.0)
    a = ap.parse_args()
    host, guest = start('creature')
    ck = Checks()
    time.sleep(4)
    for who, c in (('host', host), ('guest', guest)):
        print(f'  {who} in Creature: ' + ' | '.join(l.strip() for l in call(c, 'coop.status').splitlines()
                                                  if 'puppet pos' in l)[:200], flush=True)
    # 2. the galaxy menu: the host first, then the guest
    for who, c in (('host', host), ('guest', guest)):
        print(f'  {who}: ' + call(c, 'coop.menu'), flush=True)
        for _ in range(120):
            if mode(c) == 'GalaxyGameEntry':
                break
            time.sleep(1)
        print(f'  {who}: mode {mode(c)}', flush=True)
    ck(mode(host) == 'GalaxyGameEntry' and mode(guest) == 'GalaxyGameEntry', 'both games are at the galaxy menu')
    # the lab's games left the menu before its start-up video ended (the fixture's quick load): back in the menu its
    # main menu panel stays hidden until the video's end message comes (BUG-1256); players watched the video
    time.sleep(3)
    for who, c in (('host', host), ('guest', guest)):
        print(f'  {who}: ' + call(c, 'coop.menuintro'), flush=True)
    for _ in range(40):
        if all(uifind.shown(c, 'Galactic Adventures') for c in (host, guest)):
            break
        time.sleep(1)
    ck(all(uifind.shown(c, 'Galactic Adventures') for c in (host, guest)), 'both games show the main menu panel')
    if not a.own_places:
        for who, c in (('host', host), ('guest', guest)):
            print(f'  {who}: ' + call(c, 'coop.adventure place 0ADF0E01'), flush=True)
    # 3. both start the tutorial adventure at the same time
    procs = [start_adventure(i) for i in (lane.HOST, lane.GUEST)]
    for p in procs:
        p.wait(timeout=900)
    for who, c, inst in (('host', host, lane.HOST), ('guest', guest, lane.GUEST)):
        if mode(c) == 'GalaxyGameEntry':  # a start whose clicks were lost: once more
            print(f'  {who}: still at the galaxy menu: once more', flush=True)
            start_adventure(inst).wait(timeout=900)
    hm, gm = mode(host), mode(guest)
    ck(hm == 'Scenario' and gm == 'Scenario', f'3. both games are in the adventure (host {hm}, guest {gm})')
    print(f'  host: {call(host, "coop.adventure").splitlines()[0] if hm == "Scenario" else hm}; '
          f'guest: {call(guest, "coop.adventure").splitlines()[0] if gm == "Scenario" else gm}', flush=True)
    keyframe('adventure', 'both games in the tutorial adventure (the intro card)', {'host': host, 'guest': guest}, settle=0)
    # 4. the intro card closed (Enter) and some play
    for who, c in (('host', host), ('guest', guest)):
        for i in range(6):
            sa = act(c)
            if sa and sa[0] == 3:
                break
            if i % 2 == 0:
                c.call('key enter')
            time.sleep(5)
        print(f'  {who}: play state and act {act(c)}', flush=True)
    # the adventure's start (Initialize -> SetCurrentAct) ran through: a fault in it leaves the play state at 0
    ck(all((act(c) or (0, 0))[0] == 3 for c in (host, guest)),
       f'4. both games play the adventure (play state 3: host {act(host)}, guest {act(guest)})')
    t0 = time.time()
    seen = {}
    while time.time() - t0 < a.play:
        for who, c in (('host', host), ('guest', guest)):
            p = puppet(c)
            if p and who not in seen:
                seen[who] = p
                print(f'  {who}: a puppet of the partner appeared: species {p[0]} scale {p[1]}', flush=True)
        time.sleep(1)
    keyframe('played', f'{a.play:.0f} s of the adventure: the partner\'s puppet at its captain\'s place',
             {'host': host, 'guest': guest}, settle=5)
    # 5. the checks
    hm, gm = mode(host), mode(guest)
    ck(hm == 'Scenario' and gm == 'Scenario', f'5. both games are still in the adventure ({hm}, {gm})')
    caps = {who: captain(c) for who, c in (('host', host), ('guest', guest)) if mode(c) == 'Scenario'}
    print(f'  captains (species, scale): {caps}', flush=True)
    for who, c, other in (('host', host, 'guest'), ('guest', guest, 'host')):
        p = puppet(c) if mode(c) == 'Scenario' else None
        print(f'  {who}\'s puppet of the {other}: {p}', flush=True)
        if p and other in caps:
            want_sp, want_sc = caps[other]
            ck(p[0] == want_sp, f'5. the {who}\'s puppet of the {other} is the {other}\'s captain (species {p[0]}, '
                                f'the captain {want_sp})')
            if want_sc and p[1]:
                k = p[1] / (want_sc * 1.15)
                ck(0.75 < k < 1.25, f'5. the {who}\'s puppet of the {other} has the captain\'s size ({p[1]:.2f}, the '
                                    f'captain {want_sc:.2f} x 1.15)')
    alive = {k: c for k, c in (('host', host), ('guest', guest)) if mode(c) != 'gone'}
    ck(len(alive) == 2, f'both games are still running ({", ".join(alive) or "none"})')
    ck.faults(**alive)
    lane.sh('kill')
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
