#!/usr/bin/env python3
"""BUG-1288 (WP-S1 of 0.7.17): two players, the host leaves a system with NPC ships: no ghost or immortal ships stay in
the guest's game (the review of the outside branch fix/agy-open-bugs: its promotion of mirrors made the host's ships
the guest's own there, pinned, unverified, and kept them out of saves).

1. the host spawns three pirates next to both ships (`coop.fleet spawn 2`): the guest's game mirrors them;
2. the host jumps to a free star: within 5 s none of the mirror objects is in the guest's UFO list any more, the guest's
   fleet role is none (a guest alone leads nothing: 0.7.16.1), and no ship of that list is invincible except the guest's
   own ship and the puppets (a mirror is pinned and kept alive by the mod; a left-over one would be). Gone from the
   game's list, they cannot reach a save of the guest (BUG-0522 class; the save strip has its own test).

A regression guard: 0.7.16.1 passes it too (fleet.leader off changes nothing here), so it has no FAIL twin.

  SPORE_LANE=5 python3 tools/tests/coop_space_fleet_host_leaves.py
"""
import re
import time

import twoinst
from coop_space_fleet_leader import leader_line, ships
from coop_space_jump import place, settle, sp
from harness import waiter
from twoinst import Checks, start

wait = waiter(step=0.5)


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(3)
    home = place(host)[1]
    ck(home and place(guest)[1] == home, f'both games at the home star {home:08X}')
    for _ in range(3):
        print('  host:', host.call('coop.fleet spawn 2')['out'].strip(), flush=True)
    hid = lambda: {s['id'] for s in ships(host) if s['id'] >= 0}  # noqa: E731
    ok = wait(lambda: hid() and hid() <= {s['id'] for s in ships(guest) if s['id'] >= 0}, 10)
    ck(ok, f'the guest mirrors the host\'s NPC ships ({sorted(hid())})')
    mirrors = {s['ptr'] for s in ships(guest) if s['id'] >= 0}
    ck(leader_line(guest)[:2] == ('mirror', 0), f'the guest mirrors the host ({leader_line(guest)})')

    free = [int(x) for x in re.findall(r'free (\d+)', sp(host, 'allies free')) if int(x) != home]
    if not ck(free, f'a free star for the host ({free[:4]})'):
        return ck.result()
    settle(host, 'host', 30)
    print('  host jumpto', free[0], sp(host, 'jumpto', free[0]), flush=True)
    ck(wait(lambda: place(host)[1] == free[0], 60, 1), f'the host left for star {free[0]:08X}')
    t0 = time.time()
    ok = wait(lambda: not (mirrors & {s['ptr'] for s in ships(guest)}), 5)
    ck(ok, f'within 5 s none of the {len(mirrors)} mirror objects is in the guest\'s game ({time.time() - t0:.1f} s)')
    ck(leader_line(guest)[:2] == ('none', -1), f'the guest alone leads nothing ({leader_line(guest)})')
    ghosts = [s for s in ships(guest) if s['inv'] and not s['me'] and not s['puppet']]
    ck(not ghosts, f'no invincible ship of the mod left in the guest\'s game ({ghosts[:3]})')
    twoinst.keyframe('fleet_host_left', 'the guest at the home star after the host left: none of the host\'s three pirates '
                     'is on the guest\'s screen')
    time.sleep(3)
    ck(not (mirrors & {s['ptr'] for s in ships(guest)}), '3 s later still none is back')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
