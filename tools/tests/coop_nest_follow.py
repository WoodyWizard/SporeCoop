#!/usr/bin/env python3
"""BUG-0640 (+ BUG-0496): the host's species migrates to a new nest, the guest's game follows (Discord report
2026-11-06, 0.7.8: "after going to the new nest I didn't get DNA or the cutscene, and because of that my spawn point
wasn't there either"; the guest's respawn stayed at the first nest after two migrations of the host).

The report's way (the host's log s 1140-1321), creature fixture, no forced world share:
  1. the pack earns DNA for a bigger brain (the host's AddEvolutionPoints, the shared total), the brain upgrade
     captions, no editor;
  2. the host's creature dies and respawns at the nest: its game's stage return (CreatureGame_AfterEditor) starts
     the migration (CRG_SpeciesMigrate, the goal "Migrate to a new nest"); the guest's game has no such return;
     (--editor: instead the evolution vote and both editors: each game migrates by itself, the fix is not needed);
  3. the guest plays on (in place, or after the world the host sends for the migration);
  4. the host goes to its new nest and claims it (CRG_ClaimNest); then the guest goes there.
PASS, on the guest's game: its own game claims that nest (CRG_ClaimNest), its avatar lives in the herd of that nest (the
respawn place and the home of its family), and that nest heals it (BUG-0496, the game's own health).

  coop_nest_follow.py [--own]  (SPORE_FIX_OFF=wsync.creature_nest: the guest's game never claims: FAILS)
  --own (BUG-0642): the guest plays its own creature (chosen in the co-op window), another species than the world's:
        the nest rules (claim, home, heal) are the player species' and hold for it too."""
import re
import sys
import time

import creations_state
import uifind

from coop_migration import (CINE_CLAIM, CINE_MIGRATE, DONE, avatar_herd, cinematic, confirm_captions, eax, in_world,
                            migration, mode, new_nest, total_dna, vec3, wait)
from coop_nest_heal import heal
from coop_ui import Ui
from twoinst import Checks, start


def herd_nest_pos(con):
    h = avatar_herd(con)
    p = h and eax(con, f'SporeCoop::Herd_NestPosition thiscall {h:#x}')
    return vec3(con, p) if p else None


def near(a, b, d=30):
    return bool(a and b) and sum((x - y) ** 2 for x, y in zip(a, b)) < d * d


def editors(host, guest, ck):
    """the evolution vote, both editors (each game's AfterEditor runs: both migrate by themselves)"""
    m0 = mode(host)
    print('  ', host.call('coop.vote editor')['out'].strip().splitlines()[0], flush=True)
    time.sleep(2)
    gu = Ui(guest, 'guest')
    gu.wait(lambda s: s['ready']['vis'], 10, 'ready button')
    gu.click('ready')
    ck(wait(lambda: mode(host) != m0 and mode(guest) != m0, 60), 'both players are in their editors')
    host.call('wait.frames 200 60000')
    guest.call('wait.frames 200 60000')
    host.call('click %d %d' % DONE)
    ck(wait(lambda: cinematic(host) == CINE_MIGRATE, 60), "back from the editor the host's species migrates (CRG_SpeciesMigrate)")
    guest.call('click %d %d' % DONE)
    time.sleep(10)
    ck(confirm_captions(host, CINE_MIGRATE), 'the host confirmed the migration caption')


def host_respawn(host, ck):
    """the report's way (s 1246 of the host's log): the brain grew in play, no editor; the host's creature dies and
    respawns at the nest: the stage's return runs CreatureGame_AfterEditor, whose threshold check starts the
    migration in the host's game only"""
    print('  host:', host.call('coop.team die')['out'].strip()[:100], flush=True)
    back = False
    for _ in range(60):
        if uifind.shown(host, 'You were killed'):
            host.call('key enter')
        c = cinematic(host)
        if c:
            host.call('key enter')
        if migration(host) in (1, 2):
            back = True
            break
        time.sleep(1)
    ck(back, f"the host's creature died and respawned: its game starts the migration (state {migration(host)})")
    for _ in range(10):
        c = cinematic(host)
        if c:
            confirm_captions(host, c)
        time.sleep(2)


def main():
    host, guest = start()
    ck = Checks()
    if '--own' not in sys.argv:
        return run(host, guest, ck)
    st0 = creations_state.save(guest)
    own = creations_state.choose_own(guest)
    ck(own, f'the guest chose its own creature {own}')
    ok = wait(lambda: re.search(r'key=0x([0-9A-F]{8})', guest.call('coop.avatar')['out']) and
              re.search(r'key=0x([0-9A-F]{8})', guest.call('coop.avatar')['out']).group(1) == own, 40)
    ck(ok, f"the guest plays its own creature, not the world's species ({guest.call('coop.avatar')['out'].strip()[:80]})")
    try:
        return run(host, guest, ck)
    finally:
        creations_state.restore(guest, st0)


def run(host, guest, ck):
    home0 = herd_nest_pos(host), herd_nest_pos(guest)
    print(f'  home nests before: host {home0[0]}, guest {home0[1]}', flush=True)

    # 1. DNA, brain upgrade, editors
    d0 = total_dna(guest)
    host.call('coop.dna add 250')
    ck(wait(lambda: total_dna(guest) >= d0 + 250, 15), f'the guest has the DNA total too ({d0:.0f} -> {total_dna(guest):.0f})')
    time.sleep(3)
    for con in (host, guest):
        c = cinematic(con)
        if c:
            confirm_captions(con, c)
    if '--editor' in sys.argv:
        editors(host, guest, ck)
    else:
        host_respawn(host, ck)
    ck(wait(lambda: migration(host) == 2, 40), f'the host has the goal "Migrate to a new nest" (state {migration(host)})')

    # 3. the guest plays on (no world forced)
    ck(wait(lambda: in_world(guest), 150, 2), "the guest plays in the host's world")
    for _ in range(3):
        c = cinematic(guest)
        if c:
            confirm_captions(guest, c)
        time.sleep(5)
    print(f'  guest migration state {migration(guest)} | ' + guest.call('coop.wsync')['out'].strip().splitlines()[-1], flush=True)

    # 4. the host claims its new nest, then the guest goes there
    hh, hpos = new_nest(host)
    ck(hpos, f"the host's game has its new nest ({hpos})")
    if not hpos:
        return ck.result()
    host.call('coop.npc gopos %.2f %.2f %.2f' % (hpos[0] + 2, hpos[1], hpos[2] + 2))
    ck(wait(lambda: cinematic(host) == CINE_CLAIM, 15, 0.5), 'the host claims the new nest (CRG_ClaimNest)')
    confirm_captions(host, CINE_CLAIM)
    ck(wait(lambda: near(herd_nest_pos(host), hpos), 10), "the host's creature lives in the new nest's herd")
    time.sleep(25)  # the host's state reaches the guest (the wsync row, 1 Hz)
    print(f'  guest migration state {migration(guest)}, its next nest {new_nest(guest)[1]}', flush=True)
    guest.call('coop.npc gopos %.2f %.2f %.2f' % (hpos[0] - 2, hpos[1], hpos[2] + 2))
    claimed = wait(lambda: cinematic(guest) == CINE_CLAIM, 20, 0.25)
    gn = herd_nest_pos(guest)
    print(f"  guest: claim cinematic {bool(claimed)}, its home nest {gn}, the host's {hpos}; "
          + guest.call('coop.wsync')['out'].strip().splitlines()[-1], flush=True)
    ck(claimed, "the guest's own game claims the host's new nest (CRG_ClaimNest)")
    confirm_captions(guest, CINE_CLAIM)
    ck(wait(lambda: near(herd_nest_pos(guest), hpos), 10),
       f"the guest's creature lives in the host's new nest's herd: its respawn and home (guest {herd_nest_pos(guest)})")

    # BUG-0496: the guest's home nest heals it there (the game's own health)
    for con in (host, guest):
        con.call('coop.team off')
    guest.call('coop.npc gopos %.2f %.2f %.2f' % (hpos[0] + 2, hpos[1], hpos[2] + 2))
    time.sleep(2)
    a, b = heal(guest)
    print(f'  guest at the new nest: hp {a} -> {b}; ' + guest.call('coop.rules heal')['out'].strip(), flush=True)
    ck(a is not None and b is not None and b >= a + 2, f"the guest's new home nest heals its creature (hp {a} -> {b} in 12 s)")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
