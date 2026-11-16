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

BUG-1173: the DNA reaches the guest while its own creature (chosen by the test, BUG-1176; BUG-0345) is not applied yet (the swap
comes 3 s after the avatar; the lab's start-up waits no fixed 3 s since A9): the guest's game plays its brain
upgrade CRG_BrainUpgrade1 and the swap came in the middle of it; that cinematic never showed its caption and the
input lock held the guest (its own editor after the vote never opened). Checked: the guest's caption closes with
Enter and leaves its input free. Twin: `coop.fix creations.swap_after_cinematic off` on the guest must FAIL here
(when the swap came after the DNA, the test prints it).

  coop_evolve_world.py [--without-fix]   (--without-fix: coop.npc sharecine off on the host, must FAIL)
"""
import re
import sys
import time

import creations_state
import lane
from coop_migration import CINE_MIGRATE, DNA, DONE, cinematic, close_boxes, confirm_captions, eax, migration, mode, total_dna, u32, wait
from coop_ui import Ui
from twoinst import Checks, shot, start


def age(con):
    nm = eax(con, 'Simulator::cGameNounManager::Get cdecl')
    av = nm and u32(con, nm + 0x54)
    return u32(con, av + 0xB34) if av else None


def guest_log():
    try:
        with open(lane.game_log(lane.GUEST), 'rb') as f:
            return f.read().decode('utf-8', 'replace')
    except OSError:
        return ''


def swapped_here():
    """the guest's own creature was applied to the avatar of its last world load (BUG-1173)"""
    t = guest_log()
    i = t.rfind('-> Loading ')  # (the stage line of the load's start)
    return i >= 0 and 'own avatar is now' in t[i:]


def own_record():
    """the guest plays its own creature in this world (worlds.ini record or choice, BUG-0345)"""
    t = guest_log()
    i = t.rfind('-> Loading ')
    return i >= 0 and bool(re.search(r"body: own avatar species .*from (this world's record|choices\.ini)", t[i:]))


def input_lock(con):
    m = re.search(r'input_lock=(-?\d+)', con.call('coop.uistate')['out'])
    return int(m.group(1)) if m else -1


def loads(con):
    return con.call('log 400')['out'].count('world: loading ')


def main():
    without = '--without-fix' in sys.argv
    host, guest = start()
    ck = Checks()
    if without:
        print('  ', host.call('coop.npc sharecine off')['out'].strip())
    # BUG-1176: the cold start puts the guest's worlds.ini back to the fixture state (no record: it plays the world's
    # species); BUG-1173 needs its own creature, so the test chooses it (the newest of creations-guest) and puts the
    # profile back at the end
    st0 = creations_state.save(guest)
    mine = creations_state.choose_own(guest)
    print('  guest profile before:', st0, '| chose', mine, flush=True)
    wait(lambda: swapped_here(), 30, 0.5)
    try:
        return run(host, guest, ck, bool(mine) or own_record())
    finally:
        creations_state.restore(guest, st0)


def run(host, guest, ck, own):
    if own and swapped_here():
        # BUG-1173 as players meet it: the DNA crosses a brain level within the 3 s after the guest's world load,
        # before its own creature is applied. The start took longer than that: the host's world once more, the DNA
        # as soon as the guest's avatar is in it
        n0 = guest_log().count('-> Loading ')
        print('   the guest\'s own creature came before the DNA: the world again,', host.call('coop.share')['out'].strip())
        wait(lambda: guest_log().count('-> Loading ') > n0 and
             re.search(r'stage: Loading \w+ -> Creature', guest_log()[guest_log().rfind('-> Loading '):]), 120, 0.2)
    d0, pre = total_dna(guest), swapped_here()
    host.call(f'coop.dna add {DNA}')
    ck(wait(lambda: total_dna(guest) >= d0 + DNA, 15), 'the guest has the host\'s DNA total')
    print(f'   guest plays its own creature: {own}, applied before the DNA was added: {pre} '
          f'(BUG-1173 needs True, False)')
    time.sleep(3)
    for con in (host, guest):
        c = cinematic(con)
        if c:
            confirm_captions(con, c)
    # BUG-1173: the guest's brain upgrade caption came and went with Enter, its input is free
    ck(wait(lambda: not cinematic(guest) and input_lock(guest) == 0, 15),
       f'the guest\'s brain upgrade caption closed, its input is free (cinematic {cinematic(guest)}, '
       f'input lock {input_lock(guest)})')
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
