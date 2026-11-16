#!/usr/bin/env python3
"""A guest's editor right after it rejoined, on a slow PC, draws and can be left (BUG-0318).

After a rejoin the guest loads the host's world and the mod gives its avatar its own creature
(SetSpecies, "creations: own avatar is now ..."). An editor opened while that new body was still
being built stayed black for good and could not be left (seen at 2-17 fps in legless_guest_cancel,
2 of 3 runs; reproduced at 6.7 fps 0.3 s after the swap; 60 s later it was fine).

1. Two games on the creature fixture; the guest runs at ~6.7 fps (frame.limit 150).
2. The guest leaves and joins again (it loads the host's world, its own creature is put back).
3. 3 s after it is back it asks for its editor (the game's own entry, `coop.vote editor`).
4. Checked on the guest's game: the editor opens within 120 s, draws (the screen is not black) and
   one click on its green check brings the guest back to the Creature stage (an evolution editor has no red cross
   since BUG-0720; BUG-1199).

--no-swap: the guest plays the world's own creature (no swap after the load: only the new avatar of
the loaded world, lab 20261030-162204 showed the same black editor 1.7 s after such a load).

  coop_editor_after_swap.py [--no-swap]   (creations.editor_after_swap off failed (retired in 0.7.15, BUG-1138: FAIL run 20261030-161437:editor_after_swap))"""
import re
import sys
import time

import creations_state
import lane
from coop_legless_guest import DONE, QUIT, mode, wait
from twoinst import Checks, shot, start


def brightness(con, name):
    try:
        from PIL import Image, ImageStat
        return ImageStat.Stat(Image.open(shot(con, name)).convert('L')).mean[0]
    except (OSError, ImportError):
        return -1.0


def main():
    host, guest = start('creature')
    # BUG-1176: the cold start puts the guest's worlds.ini back to the fixture state (no record, no choice: the guest
    # plays the world's species); the swap case chooses its own creature (the newest of creations-guest), and the
    # profile is put back at the end
    st0 = creations_state.save(guest)
    try:
        return run(host, guest)
    finally:
        try:
            creations_state.restore(guest, st0)
        except OSError:
            pass


def world_own(guest, host):
    """the guest's creature is the world's species (the host's), not one of its own creations"""
    m = re.search(r"mine creature: ([0-9A-F]{8}) ''", guest.call('coop.creations')['out'])
    h = re.search(r'key=0x([0-9A-F]{8})', host.call('coop.avatar')['out'])
    return bool(m and h and m.group(1) == h.group(1))


def run(host, guest):
    ck = Checks()
    if '--no-swap' in sys.argv:  # the world's own creature: nothing to swap after the load
        for _ in range(4):
            if world_own(guest, host):
                break
            guest.call('coop.creations prev creature')
        time.sleep(8)
        print('  guest plays the world\'s own:', re.search(r'key=\S+', guest.call('coop.avatar')['out']).group(0), flush=True)
        ck(world_own(guest, host), 'the guest plays the world\'s species (no swap after the load)')
    else:
        print('  guest chose its own creature:', creations_state.choose_own(guest), flush=True)
        time.sleep(8)
    print('  guest:', guest.call('frame.limit 150')['out'].strip(), flush=True)
    guest.call('coop.leave')
    time.sleep(3)
    guest.call(lane.JOIN)
    left = wait(lambda: any(m in mode(guest) for m in ('Galaxy', 'Load')), 90, 0.3)
    back = wait(lambda: 'Creature' in mode(guest) and guest.call('coop.avatar')['ok'], 180, 0.3)
    ck(left and back, 'the guest left its old world and is back in the host\'s')
    time.sleep(3)
    print(' ', guest.call('coop.vote editor')['out'].splitlines()[0], flush=True)
    t0 = time.time()
    opened = wait(lambda: 'Editor' in mode(guest), 120, 1)
    ck(opened, f'its editor opens ({time.time() - t0:.0f} s after the request)')
    if not opened:
        guest.call('frame.limit auto')
        return ck.result()
    t1 = time.time()
    b = []
    drawn = False
    while time.time() - t1 < 60 and not drawn:
        time.sleep(5)
        b.append(round(brightness(guest, 'editor_after_swap.png'), 1))
        drawn = b[-1] > 25 or b[-1] < 0
    print(f'  editor screen brightness: {b}', flush=True)
    ck(drawn, 'the editor draws (not a black screen)')
    # the request is the game's own evolution entry (`coop.vote editor`, the evolution flags since BUG-0720, 0.7.11): its
    # editor has no red cross (only a creation editor has, BUG-0756), it is left with the green check (BUG-1199)
    guest.call('click %d %d' % DONE)
    out = wait(lambda: 'Creature' in mode(guest), 30, 1)
    if not out:
        guest.call('click %d %d' % QUIT)
        out = wait(lambda: 'Creature' in mode(guest), 20, 1)
    ck(out, 'the green check brings the guest back to the Creature stage')
    print(' ', [l for l in guest.call('log 200')['out'].splitlines() if 'body is built' in l or 'own avatar is now' in l][-3:],
          flush=True)
    guest.call('frame.limit auto')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
