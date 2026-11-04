#!/usr/bin/env python3
"""Creature stage: the species migrates to a new nest, both players claim it (a player report,
2026-10-25, 0.7.2.3: "My friend crashed after I completed migration to a new nest"; the guest's
game faulted at SporeApp 0xC6AE60 reading 0x160, docs/re/creature.md 16).

1. The pack earns enough DNA for a bigger brain: the host gets it (AddEvolutionPoints), the mod
   gives the guest the same total. The evolution vote, both players leave their editors: the host's
   game plays CRG_SpeciesMigrate ("Your species is migrating to a new nest") and sets the goal
   "Migrate to a new nest"; the host confirms the caption.
2. The host saves and sends its world again (coop.save, coop.share: as after a rejoin): the guest's game has the same
   pending migration (player property 0x514A219 = 2).
3. The host reaches the new nest and claims it (CRG_ClaimNest), then the guest arrives there too
   (both teleported, `coop.npc gopos`): the guest's own game claims the nest and plays CRG_ClaimNest
   with the nest herd's members as actors. On the guest those are its own game's creatures, which
   the update filter removes (the host owns the NPCs); the cinematic placed them "relative to their
   herd's nest" after that and read the null herd. Before the fix: faults at 0xC6AE60 on the guest
   (a crash on Windows). Now creatures removed while a cinematic plays go when it is over.

  SPORE_LANE=2 python3 tools/tests/coop_migration.py [--without-fix]
  (--without-fix: `coop.npc cinedefer off` on the guest, the old behaviour; the test must fail)
"""
import re
import struct
import sys
import time

from coop_ui import Ui
import twoinst
from twoinst import Checks, start

DONE = (1416, 1062)  # the green "done" check of the creature editor (1440x1080 test window)
MIGRATION = 0x514A219  # player property: 1 migration pending, 2 goal "Migrate" set, gone when claimed
CINE_MIGRATE, CINE_CLAIM = 'FBB3661D', '63B7060A'  # FNV ids of CRG_SpeciesMigrate, CRG_ClaimNest
DNA = float(next((a.split('=')[1] for a in sys.argv if a.startswith('--dna=')), 250))


def eax(con, what):
    m = re.search(r'eax=0x([0-9A-Fa-f]+)', con.call('call ' + what)['out'])
    return int(m.group(1), 16) if m else None


def mem(con, a, n):
    """bytes at a (mem.read prints lines of an address and up to 16 hex bytes)"""
    b = b''
    for line in con.call(f'mem.read {a:#x} {n}')['out'].splitlines():
        p = line.split()
        if len(p) > 1 and re.fullmatch(r'[0-9A-F]{8}', p[0]):
            b += bytes(int(x, 16) for x in p[1:17] if re.fullmatch(r'[0-9A-F]{2}', x))
    return b[:n] if len(b) >= n else None


def u32(con, a):
    b = mem(con, a, 4)
    return struct.unpack('<I', b)[0] if b else None


def vec3(con, a):
    b = mem(con, a, 12)
    return struct.unpack('<3f', b) if b else None


def player(con):
    nm = eax(con, 'Simulator::cGameNounManager::Get cdecl')
    return u32(con, nm + 0x74) if nm else None  # cPlayer


def total_dna(con):
    """the DNA earned in the stage (cPlayer +0x10F0, AddEvolutionPoints adds to it; the migration
    thresholds are on it)"""
    p = player(con)
    b = p and mem(con, p + 0x10F0, 4)
    return struct.unpack('<f', b)[0] if b else -1


def migration(con):
    p = player(con)
    return eax(con, f'SporeCoop::Player_GetProperty thiscall {p:#x} {MIGRATION:#x}') if p else None


def new_nest(con):
    """the herd of the nest the species moves to and the nest's position"""
    h = eax(con, 'SporeCoop::CreatureGame_NestHerdFor cdecl 1')
    p = h and eax(con, f'SporeCoop::Herd_NestPosition thiscall {h:#x}')
    return h, (vec3(con, p) if p else None)


def avatar_herd(con):
    nm = eax(con, 'Simulator::cGameNounManager::Get cdecl')
    av = nm and u32(con, nm + 0x54)
    return u32(con, av + 0x1674) if av else None


def cinematic(con):
    m = re.search(r'cinematic ([0-9A-F]{8}) playing', con.call('coop.status')['out'])
    return m.group(1) if m else None


def mode(con):
    return con.call('game.mode')['out']


def wait(cond, secs, step=1.0):
    t0 = time.time()
    while time.time() - t0 < secs:
        try:
            v = cond()
        except OSError:
            v = None
        if v:
            return v
        time.sleep(step)
    return None


def in_world(con):
    return 'Creature' in mode(con) and con.call('coop.avatar')['ok'] and 'receiving world' not in con.call('coop.status')['out']


def close_boxes(host, secs):
    """the "game saved" box stops the host's frames: Enter closes it (coop_creature_e84.py)"""
    t0 = time.time()
    while time.time() - t0 < secs:
        f1 = twoinst.host_frame(host)
        time.sleep(0.7)
        if twoinst.host_frame(host) == f1:
            host.call('key enter')


def confirm_captions(con, cine, secs=30):
    """Enter on the caption of the cinematic until it is over"""
    t0 = time.time()
    while time.time() - t0 < secs and cinematic(con) == cine:
        con.call('key enter')
        time.sleep(2)
    return cinematic(con) != cine


def main():
    without_fix = '--without-fix' in sys.argv
    host, guest = start()
    ck = Checks()
    if without_fix:
        print('  ', guest.call('coop.npc cinedefer off')['out'].strip())

    # 1. DNA for a bigger brain, the shared total, the evolution
    d0 = total_dna(guest)
    host.call(f'coop.dna add {DNA}')
    ok = wait(lambda: total_dna(guest) >= d0 + DNA, 15)
    ck(ok, f'the guest has the host\'s DNA total too ({d0:.0f} -> {total_dna(guest):.0f}, host {total_dna(host):.0f})')
    # a bigger brain: CRG_BrainUpgradeN with a caption on both
    time.sleep(3)
    for con in (host, guest):
        c = cinematic(con)
        print('   brain upgrade cinematic:', c)
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
    host.call('click %d %d' % DONE)
    ck(wait(lambda: cinematic(host) == CINE_MIGRATE, 60), 'back from the editor the host\'s species migrates (CRG_SpeciesMigrate)')
    guest.call('click %d %d' % DONE)
    time.sleep(10)  # the guest is in its stage, then loads the host's world (docs/06-testing.md 6.8)
    ck(wait(lambda: in_world(guest), 120, 2), 'the guest is back in the host\'s world')
    ck(confirm_captions(host, CINE_MIGRATE), 'the host confirmed the migration caption')
    ck(wait(lambda: migration(host) == 2, 20), f'the host has the goal "Migrate to a new nest" (state {migration(host)})')

    # 2. the world once more (as after a rejoin): the guest's game migrates too
    # (the world sent after the host's editor was saved during CRG_SpeciesMigrate: the guest's
    # creature is still the hatchling, age 0, and its game never sets the goal; a save made now has
    # the grown-up creature and the goal, like a rejoin after the caption)
    print(f'   guest before the second world: migration state {migration(guest)}')
    print('  ', host.call('coop.save')['out'].strip())
    close_boxes(host, 10)
    print('  ', host.call('coop.share')['out'].strip())
    time.sleep(8)
    ck(wait(lambda: in_world(guest), 120, 2), 'the guest loaded the world again')
    time.sleep(5)
    ck(wait(lambda: migration(guest) == 2, 30), f'the guest\'s game has the same migration (state {migration(guest)})')

    # 3. the host claims the new nest, then the guest arrives there
    hh, hpos = new_nest(host)
    gh, gpos = new_nest(guest)
    ck(hpos and gpos and max(abs(a - b) for a, b in zip(hpos, gpos)) < 1, f'the same new nest on both {hpos} {gpos}')
    if not (hpos and gpos):
        return ck.result()
    print('  ', host.call('coop.npc gopos %.2f %.2f %.2f' % (hpos[0] + 2, hpos[1], hpos[2] + 2))['out'])
    ck(wait(lambda: cinematic(host) == CINE_CLAIM, 15, 0.5), 'the host claims the new nest (CRG_ClaimNest)')
    ck(confirm_captions(host, CINE_CLAIM), 'the host confirmed "You have claimed this nest"')
    ck(wait(lambda: avatar_herd(host) == hh, 10), 'the host\'s creature lives in the new nest\'s herd')
    time.sleep(3)
    print('  ', guest.call('coop.npc gopos %.2f %.2f %.2f' % (gpos[0] - 2, gpos[1], gpos[2] + 2))['out'])
    claimed = wait(lambda: cinematic(guest) == CINE_CLAIM, 15, 0.25)
    ck(claimed, 'the guest\'s game claims the nest too (CRG_ClaimNest)')
    time.sleep(4)  # its actors are placed right after the start
    exc = guest.call('exc')['out'].splitlines()
    ck('total faults: 0' in exc[0], f'no fault on the guest while its cinematic places the family ({exc[0]}: '
       + (exc[1].strip() if len(exc) > 1 else '') + ')')
    st = re.search(r'deferred=(\d+) done_after=(\d+) pending=(\d+)', guest.call('coop.npc')['out'])
    print('   guest', st.group(0) if st else '?')
    if claimed and not without_fix:
        ck(st and int(st.group(1)) >= 1, 'creatures the guest removed during the cinematic waited for its end')
    confirm_captions(guest, CINE_CLAIM)
    ck(wait(lambda: avatar_herd(guest) == gh, 10), 'the guest\'s creature lives in the new nest\'s herd')
    if not without_fix:
        st = wait(lambda: re.search(r'pending=0\b', guest.call('coop.npc')['out']), 10)
        ck(st, 'after the cinematic they are gone (' + guest.call('coop.npc')['out'].splitlines()[-1].strip() + ')')
    time.sleep(3)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
