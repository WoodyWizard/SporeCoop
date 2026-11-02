#!/usr/bin/env python3
"""BUG-0433: a session hosted before the game reaches its main menu (game mode 0) must not crash.

The host sends its time of day once a second (on_frame -> game::time_of_day). The game's
TimeOfDay_Unavailable (= Simulator::GetCurrentContext) reads [[SpacePlayerData]+0x10], and SpacePlayerData
does not exist before the main menu: the first send after `coop host` crashed the game (0xC0000005 at
0x01020F35, read of 0x10; players suite creature_fps15burst, lab 20261101-160125).

1. Start the lane's host game alone and send `cheat coop host` as soon as its console accepts a connection
   (the line runs on the game's first frame, inside cSporeApp::Init: mode 0). SPORE_FIX_OFF=core.tod_guard
   switches the guard off first on that console.
2. For 10 s the game must keep answering and write no crash report, then it must reach its galaxy menu with
   the session still hosting (`coop.status` says host).

  SPORE_LANE=9 python3 tools/tests/coop_tod_early.py [--ui]
  --ui: the player's way instead of the console line: F9 and a real click on the window's Host, as early as the
  window shows (reachability of BUG-0433 for players)
  SPORE_FIX_OFF=core.tod_guard: the game crashes (must FAIL)
"""
import glob
import os
import sys
import time

import lane
from twoinst import Checks, alive, lane_state  # (sets the import path of tools/)
from sporectl import Console  # noqa: E402

LOGS = os.path.join(lane.game_dir(), 'SporebinEP1', 'mods', 'logs')


def reports(since):
    return [os.path.basename(f) for f in glob.glob(os.path.join(LOGS, 'crash-*.txt'))
            if os.path.getmtime(f) >= since - 2 and f'-i{lane.HOST}.' in f]


def host_by_window(c, t0):
    """the player's way: F9 (the co-op window opens "anywhere in the game") and a real click on Host, as early
    as the window can be shown; {'ok': True} once the session hosts, with where the game was"""
    from coop_ui import Ui
    ui = Ui(c, 'host')
    while time.time() - t0 < 90:
        st = ui.state()
        if st.get('open') != '1':
            c.call('key 0x78 down')
            time.sleep(0.15)
            c.call('key 0x78 up')
            time.sleep(0.3)
            continue
        mode = c.call('game.mode')['out'].strip()
        if ui.click('host'):
            time.sleep(0.5)
            hosting = c.call('coop.status')['out'].startswith('host ')
            print(f'  the co-op window was up {time.time() - t0:.1f} s after the start in game mode "{mode}"; Host '
                  f'clicked: hosting {hosting}', flush=True)
            return {'ok': hosting, 'mode': mode}
        time.sleep(0.3)
    return {'ok': False}


def main():
    ck = Checks()
    lane.sh('kill')
    time.sleep(2)
    lane.sh('deploy')
    lane_state(build='')  # these games are no warm games
    t0 = time.time()
    lane.run_host()
    # the console listens from the boot on, long before the game's first frame; its commands run on the next
    # frame, the first one in the middle of cSporeApp::Init (the crash's stack): send the host line at once,
    # as the creature test did when its load had failed (BUG-0433), not after a ping
    con, out = None, {}
    fixes = [n for n in (n.strip() for n in os.environ.get('SPORE_FIX_OFF', '').split(',')) if n]
    while time.time() - t0 < 120 and con is None:
        try:
            c = Console(lane.HOST, timeout=60)
            for name in fixes:  # coop's console commands exist from its first frame on: ask until it knows them
                for _ in range(200):
                    ans = c.call(f'coop.fix {name} off')['out'].strip()
                    if ans.startswith(f'{name} off'):
                        break
                print('  SPORE_FIX_OFF: ' + (ans.splitlines()[0] if ans else '-'), flush=True)
                if not ans.startswith(f'{name} off'):
                    print('RESULT: FAIL (SPORE_FIX_OFF: the switch could not be set)')
                    return 1
            if '--ui' in sys.argv:
                out = host_by_window(c, t0)
            else:
                out = c.call(lane.HOST_CMD)
            con = c
        except OSError:
            time.sleep(0.05)
    t_host = time.time()
    if not ck(con is not None, 'the host game started (its console answers)'):
        return ck.result()
    try:
        mode = con.call('game.mode')['out'].strip()
        frame = con.call('frame')['out'].strip()
    except OSError as e:
        mode, frame = f'(no answer: {e})', '-'
    print(f'  {t_host - t0:.1f} s after the start: {lane.HOST_CMD} -> {out.get("ok")}; then game mode "{mode}", '
          f'{frame}', flush=True)
    early = 'GalaxyGameEntry' not in mode
    if '--ui' in sys.argv:  # where the player's way gets: printed (the window first shows in the galaxy menu)
        print(f'  the player\'s way hosted in game mode "{mode}" (before the galaxy menu: {early})', flush=True)
    else:
        ck(early, f'hosted before the galaxy menu (game mode at the host: "{mode}")')
    up = True
    while time.time() - t_host < 10:
        if not alive(lane.HOST):
            up = alive(lane.HOST)  # one more try: a busy console
            if not up:
                break
        time.sleep(0.5)
    rep = reports(t0)
    ck(up and not rep, f'the hosting game keeps running for 10 s (alive {up}, crash reports {rep})')
    if up:
        t1 = time.time()
        while time.time() - t1 < 120 and 'GalaxyGameEntry' not in Console(lane.HOST).call('game.mode')['out']:
            time.sleep(1)
        st = Console(lane.HOST).call('coop.status')['out']
        ck('GalaxyGameEntry' in Console(lane.HOST).call('game.mode')['out'] and st.startswith('host '),
           f'it reaches its galaxy menu, still hosting ({st.splitlines()[0] if st else "-"})')
        ck.faults(host=Console(lane.HOST))
    lane.sh('kill', check=False)
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
