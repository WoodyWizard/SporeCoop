#!/usr/bin/env python3
"""Tribe: the guest's tribe roster shows the members' portraits of the host's own species (BUG-0643).

Player report 2026-11-06 (0.7.8): after the host's Creature -> Tribe editor the guest saw white squares
instead of the member portraits in the roster on the right of the screen. A roster portrait is the
species' picture, the png {0x2F7D0004, 0x40626200, species instance} (runtime: a broken png there gives
the failed-image square). The host's evolved species is a creation of the host's own My Creations; the
host sends its records to the guest (creations::host_species_out), which before the fix were the model
and the metadata only, no picture.

1. Two games on the tribe fixture; the guest's My Creations do not have the host's "TigerHorse"
   (290229B9, a creation of the host's).
2. The host's tribe members become that species (`coop.tribe setspecies`, cCreatureAnimal::SetSpecies as
   the game does after the editor).
3. The guest's game starts again and joins: the host's world (with that species) goes to it, its roster is built
   anew (a new process: the game keeps the pictures it drew).
PASS: the guest's game answers the picture of the species its members have (the resource manager finds the png the
roster portraits are drawn from). The roster column of a screenshot is printed (white pixels), not checked.
--nofix (`coop.fix creations.png off` on both, before the world goes again): must FAIL.

  SPORE_LANE=5 python3 tools/tests/coop_tribe_portraits.py [--nofix]
"""
import argparse
import os
import re
import time

import lane
import twoinst
from coop_creature_desync import kill_game
from twoinst import Checks, Console, shot, start, wait_up, watch

SPECIES = 0x290229B9  # "TigerHorse": the host's own creation, not in the guest's My Creations
HERE = os.path.dirname(os.path.abspath(__file__))


def white_in_roster(path):
    """the share of near-white pixels in the roster column of a 1440x1080 shot (the member portraits sit at
    x 1400..1432 below the Tribe button); a blank or failed portrait is a white square"""
    from PIL import Image
    im = Image.open(path).convert('RGB')
    w, h = im.size
    box = im.crop((int(w * 1400 / 1440), int(h * 560 / 1080), int(w * 1432 / 1440), int(h * 1000 / 1080)))
    px = list(box.getdata())
    return sum(1 for r, g, b in px if r > 240 and g > 240 and b > 240) / max(1, len(px))


def png_known(con, inst):
    m = re.search(r'known=(\d)', con.call(f'coop.creations png {inst:08X}')['out'])
    return m and m.group(1) == '1'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nofix', action='store_true')
    a = ap.parse_args()
    host, guest = start('tribe')
    ck = Checks()
    if a.nofix:
        for c in (host, guest):
            print(' ', c.call('coop.fix creations.png off')['out'].strip(), flush=True)
    ck(png_known(host, SPECIES), f'the host has the picture of its creation {SPECIES:08X}')
    ck(not png_known(guest, SPECIES), f'the guest\'s game does not have {SPECIES:08X} of its own')
    print('  host:', host.call(f'coop.tribe setspecies {SPECIES:08X}')['out'].strip(), flush=True)
    time.sleep(2)
    # a new guest process (the game keeps the pictures it drew for the whole process), joining the running host:
    # the host's world goes to it with the host's species
    for m in twoinst._monitors:
        m.stop = True
    kill_game(lane.GUEST)
    time.sleep(3)
    lane.run_guest()
    if not wait_up(lane.GUEST, 90):
        lane.run_guest()
        assert wait_up(lane.GUEST), 'the guest did not start'
    guest = Console(lane.GUEST)
    t0 = time.time()
    while time.time() - t0 < 120 and 'GalaxyGameEntry' not in guest.call('game.mode')['out']:
        time.sleep(1)
    guest.call('wait.frames 200 60000')
    if a.nofix:
        print(' ', guest.call('coop.fix creations.png off')['out'].strip(), flush=True)
    guest.call(lane.JOIN)
    ck(twoinst.wait_guest_in(host, guest, 180), 'the new guest is in the host\'s world')
    watch(lane.HOST, lane.GUEST)
    time.sleep(8)
    out = guest.call('coop.tribe')['out']
    sp = re.search(r'tribe \d+ \(player\) .* species=(\w+)', out)
    print('  guest player tribe species:', sp and sp.group(1), flush=True)
    print('  guest:', guest.call('coop.creations')['out'].strip().splitlines()[0][:200], flush=True)
    ck(sp is not None and int(sp.group(1), 16) == SPECIES,
       f'the guest\'s player tribe is of the host\'s species {SPECIES:08X} ({sp and sp.group(1)})')
    ck(png_known(guest, SPECIES), f'the guest\'s game answers the picture of the species {SPECIES:08X} (the portraits)')
    gpath = shot(guest, 'tribe_portraits_guest.png')
    hpath = shot(host, 'tribe_portraits_host.png')
    gw = white_in_roster(gpath) if gpath and os.path.exists(gpath) else -1
    hw = white_in_roster(hpath) if hpath and os.path.exists(hpath) else -1
    print(f'  white pixels in the roster column: guest {gw:.3f}, host {hw:.3f}', flush=True)
    # (printed only: in the lab the roster keeps the fixture species' portraits after the members' species change, and
    # the guest's sky is still white for a moment after the load: no reliable picture check, docs/re/tribe.md#tribe-portraits)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
