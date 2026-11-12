#!/usr/bin/env python3
"""BUG-0854: in an adventure started from the galaxy menu (Galactic Adventures, no campaign home planet) the mod's
campaign save (`coop.save`, the game's SaveGame message 0x1CD20F0) and its quit message (`coop.menu`, 0x4715068, the
path of coop.load and received worlds) must be refused; the game goes on in the adventure, no fault, no crash
report. The player leaves through the adventure's own menu. SPORE_FIX_OFF=world.adventure_saves fails.

One game (the lane's host instance), cold: the tutorial adventure from the galaxy menu (adventure_start.py).

  SPORE_LANE=N python3 tools/tests/coop_adventure_save.py
"""
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
import lane  # noqa: E402
from sporectl import Console  # noqa: E402
from twoinst import Checks, wait_up  # noqa: E402


def mode(con):
    try:
        return con.call('game.mode')['out'].split()[1]
    except Exception:  # noqa: BLE001 (a game that died answers nothing)
        return 'gone'


def main():
    ck = Checks()
    lane.sh('kill')
    time.sleep(2)
    lane.sh('deploy')
    lane.run_host()
    ck(wait_up(lane.HOST, 180), 'the game starts')
    h = Console(lane.HOST)
    for name in [n.strip() for n in os.environ.get('SPORE_FIX_OFF', '').split(',') if n.strip()]:
        print('  SPORE_FIX_OFF: ' + h.call(f'coop.fix {name} off')['out'].strip(), flush=True)
    r = subprocess.run([sys.executable, os.path.join(HERE, 'adventure_start.py'), '-i', str(lane.HOST)], timeout=600)
    ck(r.returncode == 0 and mode(h) == 'Scenario', f'the tutorial adventure runs ({mode(h)})')
    if mode(h) != 'Scenario':
        return ck.result()
    time.sleep(3)
    out = h.call('coop.save')['out'].strip()
    print(f'  coop.save: {out}', flush=True)
    ck('saved' not in out and 'adventure' in out, f'the campaign save is refused in an adventure ({out})')
    time.sleep(12)
    ck(mode(h) == 'Scenario', f'the game goes on in the adventure after the save ({mode(h)})')
    out = h.call('coop.menu')['out'].strip()
    print(f'  coop.menu: {out}', flush=True)
    ck('leaving' not in out and 'adventure' in out, f'the quit message is refused in an adventure ({out})')
    time.sleep(15)
    m = mode(h)
    ck(m == 'Scenario', f'the game goes on in the adventure after the quit message ({m})')
    if m != 'gone':
        ck.faults(host=h)
    else:
        ck(False, 'the game is still running')
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
