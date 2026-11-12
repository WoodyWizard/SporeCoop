#!/usr/bin/env python3
"""Two players in one adventure (there is no adventure co-op yet: this is about safety), BUG-0845 and BUG-0789.

Each game plays its own copy of an adventure (the scenario resource's key differs, so the place ids differ and no
puppet is made); for the fork's case both games get one place id (`coop.adventure place`, --own-places: not), so each
has the partner's captain as a creature puppet in its scenario's cast. Both games start at the galaxy menu, the host
hosts and the guest joins THERE (a fork's host crashed in
cScenarioPlayMode::SetCurrentAct, called from Initialize, after this order); then both start the tutorial adventure
(adventure_start.py, the same adventure: the guest gets the host's profile packages), the one named by --first
first, the other when the first is in it.
1. both games are in the adventure (Scenario) with their captain, the session is up;
2. BUG-0789, the sky: the guest stands still, the host's captain is put --walk m away (200); 12 s later the two
   games' skies (the time-of-day object's orientation, `coop.wsync sky`) are compared. An adventure's sky depends on
   the captain's place: the host's must have moved by 1 degree or more, and the guest's must stay its own (more than
   half of that away from the host's new sky; with the host's MSG_WORLD_TIME applied it is the host's, 0 degrees);
3. BUG-0845, the act change: the host's game jumps to the next act (`coop.adventure act 1`: the game's JumpToAct,
   what the act's end does: SetCurrentAct rebuilds the cast; play state 3 needed: the intro card is closed with Enter
   first), then the guest's; 15 s later both are still in the adventure, in act 1, with no fault and no crash report (the lab checks the reports too).
SPORE_FIX_OFF=wsync.tod,wsync.adventure_sky must FAIL at 2 (the legacy MSG_WORLD_TIME applied in an adventure).

  SPORE_LANE=N python3 tools/tests/coop_adventure_duo.py [--first host|guest|both] [--walk 200] [--no-act]
"""
import argparse
import math
import os
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
import lane  # noqa: E402
from sporectl import Console  # noqa: E402
from twoinst import Checks, wait_up  # noqa: E402


def call(c, line):
    try:
        return c.call(line)['out'].strip()
    except Exception as e:  # noqa: BLE001 (a game that died answers nothing)
        return f'no answer ({e.__class__.__name__})'


def mode(c):
    m = call(c, 'game.mode').split()
    return m[1] if len(m) > 1 else 'gone'


def sky(c):
    m = re.search(r'sky (\S+) (\S+) (\S+) (\S+)', call(c, 'coop.wsync sky'))
    return tuple(float(v) for v in m.groups()) if m else None


def angle(a, b):
    """degrees between two orientations (normalized: the console prints 4 decimals)"""
    if not a or not b:
        return None
    na, nb = math.sqrt(sum(x * x for x in a)), math.sqrt(sum(x * x for x in b))
    d = abs(sum(x * y for x, y in zip(a, b))) / (na * nb)
    return math.degrees(2 * math.acos(min(1.0, d)))


def pos(c):
    m = re.search(r'pos=\((\S+) (\S+) (\S+)\)', call(c, 'coop.avatar'))
    return [float(v) for v in m.groups()] if m else None


def act(c):
    m = re.search(r'state=(-?\d+) act=(-?\d+)', call(c, 'coop.adventure'))
    return (int(m.group(1)), int(m.group(2))) if m else None


def puppet(c):
    m = re.search(r'puppet pos=\(([^)]*)\)', call(c, 'coop.status'))
    return m.group(1) if m else None


def start_adventure(inst):
    return subprocess.Popen([sys.executable, os.path.join(HERE, 'adventure_start.py'), '-i', str(inst)])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--first', choices=('host', 'guest', 'both'), default='guest')
    ap.add_argument('--walk', type=float, default=200.0)
    ap.add_argument('--no-act', action='store_true')
    ap.add_argument('--own-places', action='store_true', help='keep each game\'s own place id (no puppets)')
    a = ap.parse_args()
    ck = Checks()
    lane.sh('kill')
    time.sleep(2)
    lane.sh('deploy')
    # the Galactic Adventures play button offers the next adventure of the profile's campaign: the same for both
    for f in os.listdir(lane.HOST_DIR):
        if f.endswith('.package'):
            shutil.copy2(os.path.join(lane.HOST_DIR, f), os.path.join(lane.GUEST_DIR, f))
    lane.run_guest()
    ck(wait_up(lane.GUEST, 180), 'the guest\'s game starts')
    time.sleep(5)
    lane.run_host()
    ck(wait_up(lane.HOST, 180), 'the host\'s game starts')
    h, g = Console(lane.HOST), Console(lane.GUEST)
    for c in (h, g):
        for _ in range(180):
            if 'GalaxyGameEntry' in call(c, 'game.mode'):
                break
            time.sleep(1)
        c.call('wait.frames 700 90000')
    for name in [n.strip() for n in os.environ.get('SPORE_FIX_OFF', '').split(',') if n.strip()]:
        for who, c in (('host', h), ('guest', g)):
            print(f'  SPORE_FIX_OFF ({who}): ' + call(c, f'coop.fix {name} off'), flush=True)
    if not a.own_places:
        # each game plays its own copy of the adventure (its resource key differs: two place ids, no puppets); one
        # place id for both makes the partner's captain a puppet in the other game's scenario, the case of the fork's
        # crash (BUG-0845)
        for who, c in (('host', h), ('guest', g)):
            print(f'  {who}: ' + call(c, 'coop.adventure place 0ADF0E01'), flush=True)
    print('  host: ' + call(h, lane.HOST_CMD), flush=True)
    print('  guest: ' + call(g, f'cheat coop join 127.0.0.1:{lane.PORT} Bob'), flush=True)
    joined = False
    for _ in range(60):
        if re.search(r'remote 1\b', call(h, 'coop.status')):
            joined = True
            break
        time.sleep(1)
    ck(joined, 'the guest joined the host at the galaxy menu')

    def enter(order):
        procs = [start_adventure(lane.HOST if w == 'host' else lane.GUEST) for w in order]
        for p in procs:
            p.wait(timeout=900)
    if a.first == 'both':
        enter(['host', 'guest'])
    else:
        enter([a.first])
        enter(['guest' if a.first == 'host' else 'host'])
    hm, gm = mode(h), mode(g)
    ck(hm == 'Scenario' and gm == 'Scenario', f'1. both games are in the adventure (host {hm}, guest {gm})')
    if hm != 'Scenario' or gm != 'Scenario':
        ck.faults(**{k: c for k, c, m in (('host', h, hm), ('guest', g, gm)) if m != 'gone'})
        return ck.result()
    st = call(h, 'coop.status')
    print('  host status: ' + ' | '.join(l.strip() for l in st.splitlines() if 'remote' in l or 'puppet' in l)[:400], flush=True)
    ck(re.search(r'remote 1\b', st) is not None, '1. the session is up in the adventure')
    # the adventure's intro card waits for the player (play state 1); Enter starts the play (state 3), as a click would
    for who, c in (('host', h), ('guest', g)):
        for i in range(6):
            sa = act(c)
            if sa and sa[0] == 3:
                break
            if i % 2 == 0:
                c.call('key enter')
            time.sleep(5)
        print(f'  {who}: play state and act {act(c)}', flush=True)
    ck(all((act(c) or (0, 0))[0] == 3 for c in (h, g)), '1. both games play the adventure (play state 3)')
    print(f'  host: {call(h, "coop.adventure").splitlines()[0]}; guest: {call(g, "coop.adventure").splitlines()[0]}', flush=True)
    if not a.own_places:
        hp_, gp_ = puppet(h), puppet(g)
        print(f'  host\'s puppet of the guest: {hp_}; guest\'s puppet of the host: {gp_}', flush=True)
        ck(hp_ is not None and gp_ is not None, '1. each game has the partner\'s captain as a puppet in its scenario')
    time.sleep(5)

    # 2. the sky
    g0, h0 = sky(g), sky(h)
    hp = pos(h)
    print(f'  skies at the start: host {h0} guest {g0} (apart {angle(h0, g0)})', flush=True)
    if hp:
        r = math.sqrt(sum(v * v for v in hp))
        up = [v / r for v in hp]
        side = [0, -up[2], up[1]] if abs(up[0]) < 0.9 else [-up[2], 0, up[0]]
        sl = math.sqrt(sum(v * v for v in side))
        tgt = [hp[i] + side[i] / sl * a.walk + up[i] * 2 for i in range(3)]
        print('  host: ' + call(h, 'coop.npc gopos %.2f %.2f %.2f' % tuple(tgt)), flush=True)
    for _ in range(12):
        time.sleep(1)
    h1, g1 = sky(h), sky(g)
    moved, own, apart = angle(h0, h1), angle(g0, g1), angle(h1, g1)
    print(f'  sky 12 s after the move: the host\'s moved {moved} deg, the guest\'s {own} deg; the two {apart} deg apart',
          flush=True)
    ck(moved is not None and moved >= 1.0, f'2. the host\'s sky moved with its captain ({moved} deg: else nothing to tell)')
    if moved is not None and moved >= 1.0:
        ck(apart is not None and apart > moved / 2,
           f'2. the guest keeps its own sky, not the host\'s ({apart:.2f} deg from the host\'s, which moved {moved:.2f})')

    # 3. the act change
    if not a.no_act:
        print(f'  acts before: host {act(h)} guest {act(g)}', flush=True)
        for who, c in (('host', h), ('guest', g)):
            print(f'  {who}: ' + call(c, 'coop.adventure act 1').replace('\n', ' '), flush=True)
            time.sleep(15)
        ah, ag = act(h), act(g)
        print(f'  acts after: host {ah} guest {ag}', flush=True)
        hm, gm = mode(h), mode(g)
        ck(hm == 'Scenario' and gm == 'Scenario', f'3. both games are still in the adventure ({hm}, {gm})')
        ck(ah is not None and ah[1] == 1, f'3. the host\'s game is in act 1 ({ah})')
        ck(ag is not None and ag[1] == 1, f'3. the guest\'s game is in act 1 ({ag})')
        if not a.own_places:
            print(f'  after the act change: host\'s puppet {puppet(h)}, guest\'s puppet {puppet(g)}', flush=True)
        st = call(h, 'coop.status')
        print('  host status after: ' + ' | '.join(l.strip() for l in st.splitlines() if 'remote' in l or 'puppet' in l)[:400],
              flush=True)
    alive = {k: c for k, c in (('host', h), ('guest', g)) if mode(c) != 'gone'}
    ck(len(alive) == 2, f'both games are still running ({", ".join(alive) or "none"})')
    ck.faults(**alive)
    lane.sh('kill')
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
