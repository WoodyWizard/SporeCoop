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
   one click on the red cross brings the guest back to the Creature stage.

  coop_editor_after_swap.py   (SPORE_FIX_OFF=creations.editor_after_swap must FAIL)"""
import time

import lane
from coop_legless_guest import CANCEL, QUIT, mode, wait
from twoinst import Checks, shot, start


def brightness(con, name):
    try:
        from PIL import Image, ImageStat
        return ImageStat.Stat(Image.open(shot(con, name)).convert('L')).mean[0]
    except (OSError, ImportError):
        return -1.0


def main():
    host, guest = start('creature')
    ck = Checks()
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
    guest.call('click %d %d' % CANCEL)
    out = wait(lambda: 'Creature' in mode(guest), 30, 1)
    if not out:
        guest.call('click %d %d' % QUIT)
        out = wait(lambda: 'Creature' in mode(guest), 20, 1)
    ck(out, 'the red cross brings the guest back to the Creature stage')
    print(' ', [l for l in guest.call('log 200')['out'].splitlines() if 'body is built' in l or 'own avatar is now' in l][-3:],
          flush=True)
    guest.call('frame.limit auto')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
