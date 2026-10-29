#!/usr/bin/env python3
"""BUG-0264: a guest in the Creature stage leaves it for the galaxy menu to load a world the host
shared; the mod must not use the avatar the game tears down meanwhile.

Final 4-player campaign: the host shared its world again right after its creature editor; all three
guests faulted ~1 s after "world: leaving the stage to the galaxy menu before loading ...", in
game::max_health (the avatar's combatant vtable read 0x01) from creature_fill. The game frees the
stage while mode_id() still reads Creature, and game::avatar() handed the freed avatar out.

The check is the game's own state on the guest, every frame of the window between the quit message
and the mode change (`coop.leaving`): `used_gone` = frames where the entity the mod used as the local
player was no longer in the game's object list (or its vtables were no longer the game's). It must
stay 0, and no game may fault or die. Rounds: the host saves and shares its world while the guest
plays (several times, also right after the guest came back in, the campaign's timing).

  coop_leave_avatar.py [--rounds N]   (SPORE_FIX_OFF=world.leave_avatar must FAIL)
"""
import re
import sys
import time

import twoinst
from twoinst import Checks, shot, start


def stats(con):
    out = con.call('coop.leaving')['out']
    return {k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', out)}


def loads(con):
    return con.call('log 600')['out'].count('world: loading ')


def wait(fn, t, step=0.5):
    t0 = time.time()
    while time.time() - t0 < t:
        v = fn()
        if v:
            return v
        time.sleep(step)
    return None


def main():
    rounds = int(sys.argv[sys.argv.index('--rounds') + 1]) if '--rounds' in sys.argv else 6
    host, guest = start('creature')
    ck = Checks()
    ck(twoinst.wait_guest_in(host, guest), 'the guest plays in the host\'s world')
    for rnd in range(1, rounds + 1):
        s0 = stats(guest)
        n0 = loads(guest)
        out = host.call('coop.save')['out']
        ck('saved' in out, f'round {rnd}: the host saved ({out.strip()})')
        host.call('coop.share')
        left = wait(lambda: stats(guest).get('windows', 0) > s0.get('windows', 0), 60)
        ck(left, f'round {rnd}: the guest left its stage for the galaxy menu')
        loaded = wait(lambda: loads(guest) > n0, 120, 1)
        back = loaded and twoinst.wait_guest_in(host, guest)
        ck(back, f'round {rnd}: the guest loaded the host\'s world and plays in it again')
        s1 = stats(guest)
        d = {k: s1.get(k, 0) - s0.get(k, 0) for k in ('frames', 'raw_gone', 'used_gone', 'refused', 'bad_vtable')}
        print(f'  round {rnd}: leaving window {d["frames"]} frames, the avatar field held a freed avatar in '
              f'{d["raw_gone"]}, the mod used one in {d["used_gone"]} (refused {d["refused"]}, bad vtable '
              f'{d["bad_vtable"]})', flush=True)
        ck(d['used_gone'] == 0, f'round {rnd}: the guest never used an avatar the game had already torn down')
        if rnd < rounds:  # the next share comes soon after the guest is back (the campaign: ~5 s)
            time.sleep(4 if rnd % 2 else 12)
    shot(guest, 'leave_avatar_guest.png')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
