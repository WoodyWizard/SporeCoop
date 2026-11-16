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
import os
import re
import struct
import sys
import time

import creations_state
import twoinst
import uifind

from coop_migration import (CINE_CLAIM, CINE_MIGRATE, DONE, avatar_herd, cinematic, confirm_captions, eax, in_world, mem,
                            migration, mode, new_nest, player, total_dna, u32, vec3, wait)
from coop_nest_heal import heal
from coop_ui import Ui
from twoinst import Checks, start

GUEST_PLAY_S = 8  # BUG-1178: s the guest's game plays before the DNA (the old start: about 6 s more than A9's)


def herd_nest_pos(con):
    h = avatar_herd(con)
    p = h and eax(con, f'SporeCoop::Herd_NestPosition thiscall {h:#x}')
    return vec3(con, p) if p else None


def nest_state(con):
    """BUG-1178 diagnostics (prints only): the brain level [0x169E370], the point NestHerdFor ranks nests by
    [0x167EA30], the brain upgrade state (player property 0x3A8D77C), the migration state, the avatar's herd (its
    species, brain range +0x120..+0x124, nest), NestHerdFor(1) (its species, range, nest) and the species manager's
    player species"""
    try:
        lv = u32(con, 0x169E370)
        pt = vec3(con, 0x167EA30)
        p = player(con)
        bu = p and eax(con, f'SporeCoop::Player_GetProperty thiscall {p:#x} {0x3A8D77C:#x}')

        def herd(h):
            if not h:
                return '-'
            sp = u32(con, h + 0xA4)
            key = sp and u32(con, sp + 0x504)
            lo, hi = (struct.unpack('<ii', mem(con, h + 0x120, 8) or bytes(8)))
            n = eax(con, f'SporeCoop::Herd_NestPosition thiscall {h:#x}') if u32(con, h + 0x160) else None
            q = vec3(con, n) if n else None
            return (f'{h:#x} species {key or 0:08X} range {lo}..{hi} nest '
                    + (f'({q[0]:.0f} {q[1]:.0f} {q[2]:.0f})' if q else '-'))
        mgr = eax(con, 'Editors::cSpeciesManager::Get cdecl')
        ps = mgr and eax(con, f'SporeCoop::cSpeciesManager_PlayerSpecies thiscall {mgr:#x}')
        return (f'level {lv} point ({pt[0]:.0f} {pt[1]:.0f} {pt[2]:.0f}) brain_upgrade {bu} migration {migration(con)} '
                f'player species {(ps and u32(con, ps + 0x504)) or 0:08X} | avatar herd {herd(avatar_herd(con))} | '
                f'NestHerdFor(1) {herd(eax(con, "SporeCoop::CreatureGame_NestHerdFor cdecl 1"))}')
    except (TypeError, OSError) as e:
        return f'(not read: {e!r})'


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
    if '--own-at-join' in sys.argv:
        # BUG-1178: the guest's own creature is recorded for this world (worlds.ini, as after an evening it played it
        # here) before it joins: its game applies it 3 s after it entered the world, so the DNA given at once comes
        # before that swap (the lane state of the failed runs, BUG-1176)
        st0 = creations_state.save(guest)
        own = creations_state.choose_own(guest)
        ck(own, f'the guest chose its own creature {own}')
        ok = wait(lambda: re.search(r'key=0x([0-9A-F]{8})', guest.call('coop.avatar')['out']) and
                  re.search(r'key=0x([0-9A-F]{8})', guest.call('coop.avatar')['out']).group(1) == own, 40)
        ck(ok, 'the guest played its own creature in this world')
        st = twoinst.lane_state()
        ck(twoinst.warm_reset(host, guest, st.get('fixture', 'creature'), st.get('planet')),
           'the guest joined the world again (its own creature recorded for it)')
        t_in = time.time()
        print('  guest after its join: ' + guest.call('coop.avatar')['out'].strip()[:100], flush=True)
        try:
            return run(host, guest, ck, t_in)
        finally:
            creations_state.restore(guest, st0)
    if '--swap-after-dna' in sys.argv:
        st0 = creations_state.save(guest)
        try:
            return run(host, guest, ck)
        finally:
            creations_state.restore(guest, st0)
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


def run(host, guest, ck, t_in=None):
    home0 = herd_nest_pos(host), herd_nest_pos(guest)
    print(f'  home nests before: host {home0[0]}, guest {home0[1]}', flush=True)
    print('  guest before the DNA:', nest_state(guest), flush=True)

    # BUG-1178: the guest's game plays its world for a while before the brain upgrade's DNA: given 6 s after it entered
    # (the lab's condition start-up waits of A9, BUG-1112, ended the old fixed 4 s / 3 s sleeps) its game planned no
    # migration (its next nest stayed its home nest: the guest never followed); the old start gave it 12 s.
    # NEST_FOLLOW_NO_PLAY=1: no wait (the twin of this test fix)
    # --early: the DNA at once (the brain threshold crossed in the guest's first seconds in the world: the test of
    # the player path behind BUG-1178)
    if os.environ.get('NEST_FOLLOW_NO_PLAY') != '1' and '--early' not in sys.argv:
        time.sleep(GUEST_PLAY_S)
    if t_in:
        print(f'  the DNA {time.time() - t_in:.1f} s after the guest entered the world', flush=True)
    # 1. DNA, brain upgrade, editors
    d0 = total_dna(guest)
    own = creations_state.own_keys(guest)[0] if '--swap-after-dna' in sys.argv and creations_state.own_keys(guest) else None
    host.call('coop.dna add 250')
    if '--swap-after-dna' in sys.argv:
        # BUG-1178: the guest's own creature is applied right after the brain upgrade's DNA reached it, before its
        # game starts the upgrade's cinematic (0.6-1.4 s later): the order of the failed runs, planted
        t1 = time.time()
        while time.time() - t1 < 15 and total_dna(guest) < d0 + 250:
            time.sleep(0.05)
        guest.call(f'coop.creations choose creature {own}')
        print(f'  the guest chose its own creature {own} {time.time() - t1:.2f} s after the DNA was given; '
              f'cinematic {cinematic(guest)}', flush=True)
        p = player(guest)
        seen = []
        while time.time() - t1 < 4:  # (diagnostics: the brain upgrade state 0x3A8D77C, the cinematic, the avatar key)
            bu = eax(guest, f'SporeCoop::Player_GetProperty thiscall {p:#x} {0x3A8D77C:#x}')
            k = re.search(r'key=0x([0-9A-F]{8})', guest.call('coop.avatar')['out'])
            row = (bu, cinematic(guest), k and k.group(1))
            if not seen or seen[-1][1:] != row:
                seen.append((round(time.time() - t1, 2),) + row)
            time.sleep(0.1)
        print(f'  guest after the DNA (s, brain upgrade state, cinematic, avatar key): {seen}', flush=True)
    ck(wait(lambda: total_dna(guest) >= d0 + 250, 15), f'the guest has the DNA total too ({d0:.0f} -> {total_dna(guest):.0f})')
    print('  guest after the DNA:', nest_state(guest), flush=True)
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
    print('  host before its claim:', nest_state(host), flush=True)
    print('  guest before the host\'s claim:', nest_state(guest), flush=True)

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
    print('  guest after the host\'s claim:', nest_state(guest), flush=True)
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
