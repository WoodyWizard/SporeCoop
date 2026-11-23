#!/usr/bin/env python3
"""A My Creations file (EditorSaves.package) above the 256 MB the mod reads does not end the game (BUG-1423, CORE-11
of the 0.7.15.5 audit).

creations.cpp read_file allocated the whole file size (std::vector::resize) before it looked at the 256 MB limit, and
ignored GetFileSizeEx's result. Every 10 s the co-op window's creation list reads the file again. A file of several
hundred MB was zero-filled for nothing in the 32-bit game, or threw std::bad_alloc where no such block was free; a
size above 2 GB always throws (std::length_error: above the vector's max_size): nothing catches it, the game ends.

creature fixture, the guest:
  1. its own creations are listed (`coop.creations list`, which reads the file);
  2. its EditorSaves.package is moved aside and a sparse file of 2.2 GB put in its place (no disk space used); after
     12 s and two more lists the guest's game still answers, is in the Creature stage, wrote no fault, and its log
     says the file was refused (BUG-1423);
  3. the real file comes back: within 25 s its creations are listed again.
The real file is put back in every case (also after a failed or killed run: the next start restores a left .bak).
SPORE_FIX_OFF=creations.size_first (suite creations_big_file_off, the twin): the allocation first: the guest's game
ends in step 2 (FAIL).

  SPORE_LANE=4 python3 tools/tests/coop_creations_big_file.py
"""
import os
import time

import lane
from coop_late_species import own_list
from twoinst import Checks, start

BIG = 2200 << 20  # above 2 GB: std::length_error on the old side, whatever the address space holds
PKG = os.path.join(lane.GUEST_DIR, 'EditorSaves.package')
BAK = PKG + '.bug9308.bak'


def restore():
    if os.path.exists(BAK):
        os.replace(BAK, PKG)


def log_has(inst, pat):
    try:
        with open(lane.game_log(inst), 'rb') as f:
            return pat in f.read().decode('utf-8', 'replace')
    except OSError:
        return False


def alive(con):
    try:
        return 'Creature' in (con.call('game.mode').get('out') or '')
    except OSError:
        return False


def main():
    restore()  # a run killed in step 2 left the guest's real file aside
    host, guest = start('creature')
    ck = Checks()
    own = own_list(guest)
    if not ck(len(own) >= 1, f'1. the guest lists its own creations ({len(own)})'):
        return ck.result()
    os.replace(PKG, BAK)
    try:
        with open(PKG, 'wb') as f:
            f.truncate(BIG)
        print(f'  the guest\'s EditorSaves.package is a sparse file of {BIG >> 20} MB now', flush=True)
        time.sleep(12)  # (the list reads the file at most every 10 s)
        listed = None
        for _ in range(2):
            try:
                listed = own_list(guest)
            except OSError as e:  # the game ended in the read (the bug)
                print(f'  coop.creations list: no answer ({e})', flush=True)
                listed = None
                break
            time.sleep(11)
        ck(listed is not None and alive(guest), '2. the guest\'s game still runs after reading the 2.2 GB file')
        ck(log_has(lane.GUEST, 'not read, own creations are off (BUG-1423)'), '2. the guest\'s log says the file was refused before it was read (BUG-1423)')
    finally:
        try:
            os.remove(PKG)
        except OSError:
            pass
        restore()
    if alive(guest):
        t0, back = time.time(), []
        while time.time() - t0 < 25 and len(back) < len(own):
            back = own_list(guest)
            if len(back) < len(own):
                time.sleep(3)
        ck(len(back) >= len(own), f'3. the real file back: the guest lists its creations again ({len(back)} of {len(own)})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
