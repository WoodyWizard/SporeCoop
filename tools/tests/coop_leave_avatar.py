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

--cell: the campaign's way in first: Cell fixture, the host finishes the Cell stage, both follow
into the Creature stage, the host's first creature editor (the vote, the guest ready), and the world
the host shares after it (the window the campaign's guests faulted in); then the rounds.

  coop_leave_avatar.py [--rounds N] [--cell]   (SPORE_FIX_OFF=world.leave_avatar must FAIL)
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


READY = (478, 747)     # "I'm ready" in the co-op window
DONE = (1416, 1058)    # the editors' green check (1440x1080 test window)
CANCEL = (1383, 1062)  # the editors' red cross
STAGE_BTN = (1396, 1063)  # the cell HUD button under the portrait: History, then the next stage
HISTORY_OK = (1415, 1055)
CAPTION_OK = (1410, 1048)


def mode(con):
    return con.call('game.mode')['out']


def window_check(ck, guest, s0, what):
    s1 = stats(guest)
    d = {k: s1.get(k, 0) - s0.get(k, 0) for k in ('windows', 'frames', 'raw_gone', 'used_gone', 'refused', 'bad_vtable', 'stage_gone')}
    print(f'  {what}: {d["windows"]} leaving window(s), {d["frames"]} frames, the avatar field held a freed avatar '
          f'in {d["raw_gone"]}, the mod used one in {d["used_gone"]} (refused {d["refused"]}, bad vtable '
          f'{d["bad_vtable"]}, unlisted outside {d["stage_gone"]})', flush=True)
    ck(d['used_gone'] == 0, f'{what}: the guest never used an avatar the game had already torn down')


def cell_to_creature(ck, host, guest):
    """coop_editor_desync.py step 5: the end of the Cell stage, the first creature editor"""
    host.call('coop.pool food 1000')
    t0 = time.time()
    while time.time() - t0 < 120 and 'Creature' not in mode(host):
        if 'cine=1' in host.call('coop.pool')['out']:
            host.call('click %d %d' % CAPTION_OK)
            time.sleep(3)
            continue
        if time.time() - t0 > 25:  # the cinematic is over: History, then its check = next stage
            host.call('click %d %d' % STAGE_BTN)
            time.sleep(3)
            host.call('click %d %d' % HISTORY_OK)
        time.sleep(4)
    ck('Creature' in mode(host), f'the host entered the Creature stage ({mode(host).strip()})')
    s0 = stats(guest)
    ck(wait(lambda: 'vote active' in host.call('coop.vote')['out'], 30), 'its first editor waits for the vote')
    ck(wait(lambda: 'Creature' in mode(guest) and guest.call('coop.avatar')['ok'], 120), 'the guest follows')
    window_check(ck, guest, s0, 'Cell -> Creature')
    time.sleep(3)
    s0 = stats(guest)
    guest.call('click %d %d' % READY)
    ck(wait(lambda: 'Editor' in mode(host), 40), 'the host is in the creature editor')
    time.sleep(5)
    host.call('click %d %d' % DONE)
    if wait(lambda: 'Editor' in mode(guest), 20):  # the guest's own editor (the vote opened it)
        time.sleep(4)
        guest.call('click %d %d' % CANCEL)
    ck(wait(lambda: 'Creature' in mode(host), 40), 'the host is back in the Creature stage')
    # the host shares its evolved world a few seconds later: the guest leaves its stage for it
    ck(wait(lambda: stats(guest).get('windows', 0) > s0.get('windows', 0), 90), 'the guest leaves its stage for '
       'the world the host shares after its editor')
    ck(twoinst.wait_guest_in(host, guest), 'the guest plays in the host\'s evolved world')
    window_check(ck, guest, s0, 'after the host\'s editor')


def main():
    rounds = int(sys.argv[sys.argv.index('--rounds') + 1]) if '--rounds' in sys.argv else 6
    cell = '--cell' in sys.argv
    host, guest = start('cell' if cell else 'creature')
    ck = Checks()
    if cell:
        cell_to_creature(ck, host, guest)
    else:
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
        window_check(ck, guest, s0, f'round {rnd}')
        if rnd < rounds:  # the next share comes soon after the guest is back (the campaign: ~5 s)
            time.sleep(4 if rnd % 2 else 12)
    shot(guest, 'leave_avatar_guest.png')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
