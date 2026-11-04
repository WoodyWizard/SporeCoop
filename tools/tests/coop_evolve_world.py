#!/usr/bin/env python3
"""BUG-0019: the world a guest gets right after the host's evolution grows its creature up and
starts the migration as on the host.

An evolution that starts a migration (the pack's DNA reaches a bigger brain) ends in the host's
cinematic CRG_SpeciesMigrate with a caption waiting for OK. The world the host sent after its
editor was saved during that cinematic: the creature in it was still the hatchling (+0xB34 age 0),
the migration at state 1, and the guest's game that loaded it never grew up nor got the goal
"Migrate to a new nest" (state 2). The share now waits until the cinematic is over.

Checked on the guest's game after it loaded the host's world: the avatar's age (+0xB34) and the
player property of the migration (0x514A219) are the host's.

  coop_evolve_world.py [--without-fix]   (--without-fix: coop.npc sharecine off on the host, must FAIL)
"""
import re
import sys
import time

from coop_migration import CINE_MIGRATE, DNA, DONE, cinematic, close_boxes, confirm_captions, eax, migration, mode, total_dna, u32, wait
from coop_ui import Ui
from twoinst import Checks, shot, start


def age(con):
    nm = eax(con, 'Simulator::cGameNounManager::Get cdecl')
    av = nm and u32(con, nm + 0x54)
    return u32(con, av + 0xB34) if av else None


def loads(con):
    return con.call('log 400')['out'].count('world: loading ')


def main():
    without = '--without-fix' in sys.argv
    host, guest = start()
    ck = Checks()
    if without:
        print('  ', host.call('coop.npc sharecine off')['out'].strip())
    d0 = total_dna(guest)
    host.call(f'coop.dna add {DNA}')
    ck(wait(lambda: total_dna(guest) >= d0 + DNA, 15), 'the guest has the host\'s DNA total')
    time.sleep(3)
    for con in (host, guest):
        c = cinematic(con)
        if c:
            confirm_captions(con, c)
    m0 = mode(host)
    print('  ', host.call('coop.vote editor')['out'].strip().splitlines()[0])
    time.sleep(2)
    gu = Ui(guest, 'guest')
    gu.wait(lambda s: s['ready']['vis'], 10, 'ready button')
    gu.click('ready')
    ck(wait(lambda: mode(host) != m0 and mode(guest) != m0, 60), 'both players are in their editors')
    host.call('wait.frames 200 60000')
    guest.call('wait.frames 200 60000')
    n0 = loads(guest)
    host.call('click %d %d' % DONE)
    ck(wait(lambda: cinematic(host) == CINE_MIGRATE, 60), 'back from the editor the host\'s species migrates (CRG_SpeciesMigrate)')
    guest.call('click %d %d' % DONE)
    time.sleep(20)  # a player reads the caption; the old code sent the world meanwhile
    print(f'   host age {age(host)} migration {migration(host)} while the caption is up; guest loads {loads(guest) - n0}')
    ck(confirm_captions(host, CINE_MIGRATE), 'the host confirmed the migration caption')
    ck(wait(lambda: migration(host) == 2, 20), f'the host has the goal "Migrate to a new nest" (state {migration(host)})')
    h_age = age(host)
    def loaded():
        close_boxes(host, 2)  # the "game saved" box of the share stops the host's frames
        return loads(guest) > n0
    ck(wait(loaded, 120, 0), 'the guest loaded the host\'s world after the evolution')
    ck(wait(lambda: 'Creature' in mode(guest) and guest.call('coop.avatar')['ok'], 120, 2), 'the guest plays in it')
    time.sleep(8)
    ok = wait(lambda: migration(guest) == 2 and age(guest) == h_age, 30)
    print(f'   host: age {h_age} migration {migration(host)}; guest: age {age(guest)} migration {migration(guest)}')
    ck(ok, 'the guest\'s creature grew up and its game migrates as the host\'s')
    shot(guest, 'evolve_world_guest.png')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
