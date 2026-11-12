#!/usr/bin/env python3
"""Two players in one adventure (there is no adventure co-op yet: this is about safety), BUG-0845 and BUG-0789.

Both games start at the galaxy menu, the host hosts and the guest joins THERE (a fork's host crashed in
cScenarioPlayMode::SetCurrentAct, called from Initialize, after this order); then both start the tutorial adventure
(adventure_start.py, the same adventure: the guest gets the host's profile packages), the one named by --first
first, the other when the first is in it.
1. both games are in the adventure (Scenario) with their captain, the session is up;
2. BUG-0789, the sky: the guest stands still, the host's captain is put --walk m away; for 12 s the two games'
   skies (the time-of-day object's orientation, `coop.wsync sky`) are read once a second. An adventure's sky
   depends on the captain's place: when the host's sky moved by more than 0.5 degrees, the guest's sky must stay
   its own (not come within 0.2 degrees of the host's while it was over 0.5 degrees off at the start);
3. BUG-0845, the act change: the host's game jumps to the next act (`coop.adventure act 1`: the game's JumpToAct,
   what the act's end does: SetCurrentAct rebuilds the cast), then the guest's; 15 s later both are still in the
   adventure, in act 1, with no fault and no crash report (the lab checks the reports too).
SPORE_FIX_OFF=wsync.tod,wsync.adventure_sky must FAIL at 2 (the legacy MSG_WORLD_TIME applied in an adventure).

  SPORE_LANE=N python3 tools/tests/coop_adventure_duo.py [--first host|guest|both] [--walk 60] [--no-act]
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
    """degrees between two orientations"""
    if not a or not b:
        return None
    d = abs(sum(x * y for x, y in zip(a, b)))
    return math.degrees(2 * math.acos(min(1.0, d)))


def pos(c):
    m = re.search(r'pos=\((\S+) (\S+) (\S+)\)', call(c, 'coop.avatar'))
    return [float(v) for v in m.groups()] if m else None


def act(c):
    m = re.search(r'state=(-?\d+) act=(-?\d+)', call(c, 'coop.adventure'))
    return (int(m.group(1)), int(m.group(2))) if m else None


def start_adventure(inst):
    return subprocess.Popen([sys.executable, os.path.join(HERE, 'adventure_start.py'), '-i', str(inst)])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--first', choices=('host', 'guest', 'both'), default='guest')
    ap.add_argument('--walk', type=float, default=60.0)
    ap.add_argument('--no-act', action='store_true')
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
    time.sleep(10)

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
    samples = []
    for _ in range(12):
        time.sleep(1)
        samples.append((sky(h), sky(g)))
    h1, g1 = samples[-1]
    moved = angle(h0, h1)
    own = angle(g0, g1)
    near = min((angle(x, y) for x, y in samples if x and y), default=None)
    start_apart = angle(h0, g0)
    print(f'  sky: the host\'s moved {moved} deg; the guest\'s {own} deg from its start; nearest to the host\'s {near} deg '
          f'(apart at the start {start_apart})', flush=True)
    if moved is not None and near is not None and (moved > 0.5 or start_apart > 0.5):
        ck(near > 0.2 or start_apart <= 0.5,
           f'2. the guest keeps its own sky (nearest to the host\'s: {near:.3f} deg, apart at the start {start_apart:.3f})')
    else:
        print('  2. the host\'s sky did not move and the skies were the same: nothing to tell apart', flush=True)
        ck(moved is not None, '2. both games have a sky in the adventure')

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
