#!/usr/bin/env python3
"""BUG-1533: lifecycle's kPlayerJoin / kPlayerLeave reach the modules registered for them.

The events were declared and listed by `coop.lifecycle hooks` but never fired (a LIFECYCLE_HOOK on them was never
called); own progress and revive were called by hand from session_glue.cpp's player callbacks. Now the callbacks fire
both events (lifecycle::fire_player) and own_progress.player / revive.player are hooks.

  1. the usual session (fixture creature): the guest has joined: the host's registry fired a join, and its hook
     own_progress.player ran (calls >= 1)
  2. the guest leaves (coop.leave): the host's registry fired a leave, own_progress.player and revive.player ran for it,
     and own progress forgot the leaver (`coop.progress` lists no player 1 any more: the module's own state)
  3. the guest joins again: one more join on the host, and own progress lists the guest again

  SPORE_FIX_OFF=core.player_events coop_lifecycle_player.py   FAILS (join=0 leave=0, the hooks' calls 0: the hand
  calls do the modules' work, so step 2's `coop.progress` check passes in both arms)
"""
import re
import time

import lane
from twoinst import Checks, keyframe, start, wait_guest_in


def hooks(con):
    """{'join': n, 'leave': n, '<hook>': calls} of `coop.lifecycle hooks`"""
    out = con.call('coop.lifecycle hooks')['out']
    r = {}
    m = re.search(r'fired session=\d+ world=\d+ join=(\d+) leave=(\d+)', out)
    if m:
        r['join'], r['leave'] = int(m.group(1)), int(m.group(2))
    for h in re.finditer(r'^-?\d+ (\S+) \S* ?(?:game )?calls=(\d+)', out, re.M):
        r[h.group(1)] = int(h.group(2))
    return r


def progress_lists(con, pid):
    return re.search(rf'^\s+player {pid} ', con.call('coop.progress')['out'], re.M) is not None


def wait(pred, secs=20):
    t0 = time.time()
    while time.time() - t0 < secs:
        if pred():
            return True
        time.sleep(0.5)
    return pred()


def main():
    host, guest = start('creature')
    ck = Checks()
    time.sleep(3)
    h0 = hooks(host)
    print(f'  host after the join: {h0}', flush=True)
    ck(h0.get('join', 0) >= 1, f'the host\'s registry fired the guest\'s join (join={h0.get("join")})')
    ck(h0.get('own_progress.player', 0) >= 1, f'own_progress.player ran for the join ({h0.get("own_progress.player")})')
    had = progress_lists(host, 1)
    print(f'  host coop.progress lists player 1: {had}', flush=True)
    # 2. the guest leaves
    print('  ' + guest.call('coop.leave')['out'].strip(), flush=True)
    ok = wait(lambda: hooks(host).get('leave', 0) > h0.get('leave', 0))
    h1 = hooks(host)
    print(f'  host after the leave: {h1}', flush=True)
    ck(ok, f'the host\'s registry fired the guest\'s leave (leave {h0.get("leave")} -> {h1.get("leave")})')
    ck(h1.get('own_progress.player', 0) > h0.get('own_progress.player', 0),
       f'own_progress.player ran for the leave ({h0.get("own_progress.player")} -> {h1.get("own_progress.player")})')
    ck(h1.get('revive.player', 0) > h0.get('revive.player', 0),
       f'revive.player ran for the leave ({h0.get("revive.player")} -> {h1.get("revive.player")})')
    ck(not progress_lists(host, 1), 'own progress on the host forgot the leaver (no player 1 in coop.progress)')
    # 3. the guest comes back
    guest.call(lane.JOIN)
    back = wait_guest_in(host, guest, 150)
    ck(back, 'the guest joined again')
    h2 = hooks(host)
    print(f'  host after the second join: {h2}', flush=True)
    ck(h2.get('join', 0) > h1.get('join', 0), f'one more join fired ({h1.get("join")} -> {h2.get("join")})')
    if had:
        ck(wait(lambda: progress_lists(host, 1)), 'own progress on the host lists the guest again')
    keyframe('rejoined', 'Creature: both games in the session again after the guest\'s leave and join (the partner '
             'shown on each screen)', {'host': host, 'guest': guest})
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
