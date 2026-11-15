#!/usr/bin/env python3
"""Joining after the start with a name of the session (BUG-1001, NET-1 of the 0.7.14 audit).

With all.late_join off the host takes no new players once it plays its world. A player who was in the session comes
back by its rejoin token (the owner's rule, plan 0.7.14 decision 7); a name alone never passes. Before the fix any
game with a name seen in the session came in: two friends who both kept a default name were enough.

Creature fixture, three games (the host Alice, the guest Bob, an extra guest in the lane's bottle, coop_multi):
  1. the host switches all.late_join off;
  2. the extra guest (another game, its own token) joins with the name Bob: refused ("does not take new players"),
     the host still has 2 players (before: admitted as "Bob 2");
  3. Bob leaves (coop.leave) and joins again as Bob: admitted (its token kept for this host and name);
  4. all.late_join on: the extra guest joins (as "Bob 2").
SPORE_FIX_OFF=net.join_seen must FAIL (step 2).

  SPORE_LANE=2 python3 tools/tests/coop_join_late_names.py --players 3
"""
import argparse
import re
import subprocess
import time

import lane
import settings_lib as sl
import twoinst
from coop_multi import join_cmd, start_extra
from twoinst import Checks, lane_state, start, wait_guest_in

KEYS = ('all.late_join',)


def players(host):
    m = re.search(r'players=(\d+)/', host.call('coop.status')['out'])
    return int(m.group(1)) if m else None


def status(con):
    return con.call('coop.status')['out']


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--players', type=int, default=3)  # (the lab's memory gate counts the games by it)
    ap.parse_args()
    twoinst.forget_settings()
    host, guest = start('creature')
    lane_state(**{**lane_state(), 'build': ''})  # the extra shares the bottle: no warm reuse of these games
    ck = Checks()
    try:
        extra, _inst = start_extra(1)['Carol']
        time.sleep(2)
        # 1. no new players after the start
        ck(sl.force(host, [guest], 'all.late_join', 'off'), 'the guest got all.late_join off')
        n0 = players(host)
        print(f'  host players before: {n0}', flush=True)
        # 2. another game named Bob
        extra.call(join_cmd('Bob'))
        told = sl.wait(lambda: 'does not take new players' in status(extra), 20)
        time.sleep(3)
        n2 = players(host)
        roster = [ln.strip() for ln in status(host).splitlines() if ln.strip().startswith('[')]
        print(f'  another game named Bob after the start: refused={told}, host players={n2}', flush=True)
        print('  host roster: ' + ' | '.join(r[:28] for r in roster), flush=True)
        ck(told and n2 == n0 == 2, 'late join off: another game with the name of a player in the session is refused')
        # 3. Bob himself comes back
        guest.call('coop.leave')
        time.sleep(3)
        guest.call(lane.JOIN)
        back = wait_guest_in(host, guest, 200)
        print(f'  Bob left and joined again: back={back}, host players={players(host)}', flush=True)
        ck(back and players(host) == 2, 'late join off: a player who left comes back (its token)')
        # 4. late join on: the extra game joins
        ck(sl.force(host, [guest], 'all.late_join', 'on'), 'all.late_join on again')
        extra.call(join_cmd('Bob'))
        joined = sl.wait(lambda: players(host) == 3, 60)
        names = re.findall(r'^\s*\[\d+\] (.{1,16})', status(host), re.M)
        print(f'  late join on: joined={joined}, host roster {[n.strip() for n in names]}', flush=True)
        ck(joined and any(n.strip() == 'Bob 2' for n in names), 'late join on: the other Bob joins as "Bob 2"')
    finally:
        sl.reset([host], KEYS)
        twoinst.forget_settings()
    ck.faults(host=host, guest=guest)
    res = ck.result()
    subprocess.run([lane.SPORE, 'kill'], capture_output=True)  # the extra shares the bottle (as party.Party.stop)
    return res


if __name__ == '__main__':
    raise SystemExit(main())
