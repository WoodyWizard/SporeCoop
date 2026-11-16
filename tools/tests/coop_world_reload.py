#!/usr/bin/env python3
"""BUG-0119: after a reload the guest plays its own creature again, and only once the new world has
played for 3 s.

The mod told "a new world" from "the same world" by the avatar's address. The game pools creature
objects, so the avatar of a reloaded world can have the old address: the own-creature swap was then
taken as done (the guest kept the species the save gives it, the host's) or ran at once on a world
still loading. Whether the allocator gives the old address back cannot be forced, so the guest's
`coop.creations sameaddr` makes the next new avatar look like the old one (the state a reused
address leaves). The fix counts creature worlds (game::world_epoch).

Checked on the guest's game: the avatar's species key (coop.avatar) is the guest's own creation
after the host's world was loaded again, and the swap came at least 3 s after the new avatar.

  coop_world_reload.py [--without-fix]   (--without-fix: coop.npc worldfix off, must FAIL)
"""
import sys
import time

import creations_state
from harness import field, waiter
import lane
from twoinst import Checks, shot, start
import twoinst


def guest_loads():
    """the guest game's world loads so far (its whole log file: the console's `log 400` ring dropped the round before
    once the log was busier, BUG-1171)"""
    try:
        with open(lane.game_log(lane.GUEST), 'rb') as f:
            return f.read().decode('utf-8', 'replace').count('world: loading ')
    except OSError:
        return -1


def swaps(con):
    return int(field(con.call('coop.status')['out'], r'avatar_swaps=(\d+)') or -1)


wait = waiter(step=1)


def main():
    host, guest = start()
    # BUG-1176: the cold start puts the guest's worlds.ini back to the fixture state (no record: the guest plays the
    # world's species, the host's); this test needs the guest's own creature, so it chooses it (the newest of
    # creations-guest) and puts the profile back at the end
    st0 = creations_state.save(guest)
    print('  guest profile before:', st0, '| chose', creations_state.choose_own(guest), flush=True)
    try:
        return run(host, guest)
    finally:
        creations_state.restore(guest, st0)


def run(host, guest):
    without = '--without-fix' in sys.argv
    ck = Checks()
    if without:
        print(' ', guest.call('coop.npc worldfix off')['out'].strip())
    mine = field(guest.call('coop.creations')['out'], r"mine creature: ([0-9A-F]{8})")
    # (a warm game counts the swaps of the test before it: wait for the own creature itself, BUG-1171)
    ok = wait(lambda: swaps(guest) >= 1 and field(guest.call('coop.avatar')['out'], r'key=0x([0-9A-F]{8})') == mine, 40)
    av = field(guest.call('coop.avatar')['out'], r'key=0x([0-9A-F]{8})')
    h_av = field(host.call('coop.avatar')['out'], r'key=0x([0-9A-F]{8})')
    print(f'  guest own {mine}, guest avatar {av}, host avatar {h_av}')
    ck(ok and av == mine and mine != h_av, 'before: the guest plays its own creature')

    for rnd in (1, 2):
        n0 = swaps(guest)
        print(' ', guest.call('coop.creations sameaddr')['out'].strip())
        loads0 = guest_loads()
        out = host.call('coop.save')['out']
        ck('saved' in out, f'round {rnd}: the host saved ({out.strip()})')
        host.call('coop.share')
        loaded = wait(lambda: guest_loads() > loads0 >= 0, 120)
        ok = loaded and twoinst.wait_guest_in(host, guest)
        ck(ok, f'round {rnd}: the guest loaded the host\'s world again')
        t_in = time.time()
        # the avatar of the loaded world has the host's species until the own creature is applied
        got = wait(lambda: field(guest.call('coop.avatar')['out'], r'key=0x([0-9A-F]{8})') == mine and swaps(guest) > n0,
                   30)
        av = field(guest.call('coop.avatar')['out'], r'key=0x([0-9A-F]{8})')
        n1 = swaps(guest)
        print(f'  round {rnd}: guest avatar {av} (own {mine}), swaps {n0} -> {n1}, {time.time() - t_in:.1f} s after it was in')
        ck(got and av == mine, f'round {rnd}: the guest plays its own creature again after the load')
        shot(guest, f'world_reload_guest_{rnd}.png')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
