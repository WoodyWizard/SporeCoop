#!/usr/bin/env python3
"""World shares and queued world loads (wave 2, Creature fixture, one session):

  1. BUG-0112 two shares wait at the same moment (`coop.worldfix queue 1 0`: the guest and the host's
     own id 0 as a stand-in for a second guest, both in the session, one frame, each asking for a
     save): the host posts ONE save for both (world status "for shares"), and the guest still gets
     the world and plays in it.
  2. BUG-0166 a share for a player who is not in the session (`coop.worldfix queue 9`): dropped,
     no save posted for it.
  3. evolution vote: both open their editors. BUG-0157 while the guest is in its editor a load is
     queued (`coop.load` of the guest's received slot, the way via_menu_tick sees an editor that
     opened after the request): the game must not load from the editor (no "loading from Editor",
     the guest stays in the editor).
  4. the host leaves its editor: its evolution goes in place (BUG-0508: a guest in its own editor plays the
     same world), no world is sent to the guest. Then the host shares its world (`coop.share`, as at a load or
     a stage change of the host) while the guest is still in its editor: the received world waits for the
     editor (and replaces the coop.load of part 3). BUG-0074 the guest presses Leave, then "done": its game
     stays in the Creature stage (no quit to the galaxy menu, no load of the host's world) for 20 s.
     (Before BUG-0954 the share came from the host's evolution itself; since BUG-0508 it no longer does.)

  coop_world_queue.py [--without share_one_save,share_drop_peer,load_in_editor,world_reset | all]
  (the named fixes off: each part with its fix off must FAIL; SPORE_FIX_OFF=world.world_reset works as well)
"""
import glob
import os
import re
import subprocess
import sys
import time

import fixtures
import lane
from coop_ui import Ui
from twoinst import Checks, shot, start, wait_guest_in

DONE = (1416, 1062)  # the green "done" check of the creature editor (1440x1080 test window)
CREATURE = 'CreatureGame'


def mode(con):
    return con.call('game.mode')['out']


def counters(con):
    m = re.search(r'world saves posted (\d+), for shares (\d+), shares dropped (\d+), loads dropped (\d+), '
                  r'loads held (\d+)', con.call('coop.status')['out'])
    return tuple(int(x) for x in m.groups()) if m else None


def log_text(inst):
    """the instance's game log (tools/spore.sh: lane 1 = the main game, lane N = SPORE-lab/laneN next to
    it; found from the git common dir, so a worktree works too)"""
    common = subprocess.run(['git', 'rev-parse', '--path-format=absolute', '--git-common-dir'],
                            cwd=lane.ROOT, capture_output=True, text=True).stdout.strip()
    game = os.path.dirname(os.path.dirname(common))
    if lane.LANE > 1:
        game = os.path.join(os.path.dirname(game), 'SPORE-lab', f'lane{lane.LANE}')
    p = os.path.join(game, 'SporebinEP1', 'mods', 'logs', 'sporekit.log' if inst == 1 else f'sporekit.{inst}.log')
    try:
        return open(p, errors='replace').read()
    except OSError:
        return ''


def main():
    without = set()
    if '--without' in sys.argv:
        without = set(sys.argv[sys.argv.index('--without') + 1].split(','))
    # SPORE_FIX_OFF=world.<name> (the lab's twin switch) is the same as --without <name>: the switches are set below
    without |= {f[len('world.'):] for f in os.environ.get('SPORE_FIX_OFF', '').split(',') if f.startswith('world.')}
    host, guest = start()
    ck = Checks()
    names = ['share_one_save', 'share_drop_peer', 'load_in_editor', 'world_reset']
    for con in (host, guest):
        for n in names:
            con.call(f'coop.worldfix {n} {0 if (n in without or "all" in without) else 1}')
    print('  host switches:', host.call('coop.worldfix')['out'].strip().replace('\n', ', '))
    m0 = mode(host)
    print('  stage mode:', m0)

    # 1. BUG-0112: two shares in one frame, one save
    c0 = counters(host)
    print(' ', host.call('coop.worldfix queue 1 0')['out'].strip())
    time.sleep(15)
    c1 = counters(host)
    print(f'  host counters before {c0} after {c1}')
    ck(c0 and c1 and c1[1] - c0[1] == 1, f'one save for the two shares waiting at once ({c1[1] - c0[1]} posted)')
    ck(wait_guest_in(host, guest), 'the guest plays in the shared world again')
    time.sleep(10)

    # 2. BUG-0166: a share for a player who is not in the session
    c0 = counters(host)
    print(' ', host.call('coop.worldfix queue 9')['out'].strip())
    time.sleep(10)
    c1 = counters(host)
    print(f'  host counters before {c0} after {c1}')
    ck(c1[0] == c0[0], f'no save for a player who is gone ({c1[0] - c0[0]} posted)')
    ck('share for 9' not in host.call('coop.status')['out'], 'the share for the player who is gone is dropped')
    time.sleep(5)

    # 3. both in their editors (evolution vote)
    gu = Ui(guest, 'guest')
    print(' ', host.call('coop.vote editor')['out'].strip())
    time.sleep(2)
    gu.wait(lambda s: s['ready']['vis'], 10, 'ready button')
    gu.click('ready')
    t0 = time.time()
    while time.time() - t0 < 60 and (mode(host) == m0 or mode(guest) == m0):
        time.sleep(1)
    ck(mode(guest) != m0 and mode(host) != m0, f'both editors opened (guest {mode(guest)})')
    host.call('wait.frames 200 60000')
    guest.call('wait.frames 200 60000')
    # BUG-0157: a load queued while the guest is in its editor
    slots = [os.path.basename(os.path.dirname(p)) for p in glob.glob(os.path.join(lane.GUEST_DIR, 'Games', '*', 'sporecoop.txt'))]
    planet = fixtures.planet_of('creature').split('/')[-1]
    log0 = len(log_text(lane.GUEST))
    ed = mode(guest)
    out = guest.call(f'coop.load {slots[0] if slots else "Game0"} {planet}')['out'].strip()
    print(' ', out)
    ck(out.startswith('loading'), f'a load is queued in the editor ({slots})')
    time.sleep(6)
    from_editor = 'loading from Editor' in log_text(lane.GUEST)[log0:]
    ck(not from_editor and mode(guest) == ed, f'no load started inside the editor (guest {mode(guest)})')
    print('  guest counters:', counters(guest), guest.call('coop.status')['out'].count('load after the editor'))
    if from_editor or mode(guest) != ed:
        ck.faults(host=host, guest=guest)
        return ck.result()

    # 4. BUG-0074: the host's world waits on the guest's editor; the guest leaves the session
    host.call('click %d %d' % DONE)
    t0 = time.time()
    while time.time() - t0 < 60 and mode(host) != m0:
        time.sleep(1)
    ck(mode(host) == m0, 'the host is back in its stage')
    time.sleep(10)
    sent = 'world: received' in log_text(lane.GUEST)[log0:]
    ck(not sent and mode(guest) == ed,
       f'the host\'s evolution goes in place: no world for the guest in its editor (BUG-0508; guest {mode(guest).strip()})')
    print(' ', host.call('coop.share')['out'].strip())
    waits = False
    t0 = time.time()
    while time.time() - t0 < 90:
        # (the coop.load above waits there too: the received world replaces it)
        if 'load after the editor' in guest.call('coop.status')['out'] and \
                'world: received' in log_text(lane.GUEST)[log0:]:
            waits = True
            break
        time.sleep(1)
    ck(waits, "the host's world waits for the guest to leave its editor")
    print(' ', guest.call('coop.leave')['out'].strip())
    time.sleep(2)
    print('  guest after Leave:', guest.call('coop.status')['out'].splitlines()[-3:])
    guest.call('click %d %d' % DONE)
    t0 = time.time()
    while time.time() - t0 < 60 and mode(guest) == ed:
        time.sleep(1)
    left = mode(guest)
    modes = set()
    t0 = time.time()
    while time.time() - t0 < 20:
        modes.add(mode(guest).strip())
        time.sleep(1)
    shot(guest, 'guest_after_leave.png')
    print('  guest modes after its editor:', left.strip(), modes)
    ck(left == m0 and modes == {m0.strip()}, 'the guest stays in its Creature game after leaving the session')

    for con in (host, guest):
        con.call('coop.worldfix all 1')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
