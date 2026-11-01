#!/usr/bin/env python3
"""Creature stage desync after editors and a reload (player report 2026-10-25: "he is my species
from my view", "I don't even see his changes", "he doesn't even see my companion", "he can't eat
the ones I killed since he still sees them alive").

The players' order, from the creature fixture: the guest starts without own creations (it plays
the host world's species); the host's pack has a companion of a species only the host's profile
has (a creation of its My Creations: the host's creations and Sporepedia downloads populate its
planet); both evolve through the vote (real clicks: "I'm ready", a body drag, "done"); both games
crash; the host starts again and loads its save; the guest joins again. Checked after the editors
and after the reload:

  companion  the guest builds the host's companion (before: "unknown species", never shown)
  ghosts     every NPC the guest mirrors is one the host has (before: after the host's editor the
             guest kept the old NPCs, a living copy of each next to the new one)
  kill       an NPC the host kills dies in the guest's game (the game's death flag +0xB5E of the
             mirror, `coop.npc creatures`; not the " DEAD" of the guest's `coop.npc list`, which is
             only the mod's bookkeeping, BUG-0148), and no living copy of it stays there
  species    the host shows the guest with the guest's edited creature

  coop_creature_desync.py [--no-crash] [--seed-nodeath]

  --seed-nodeath  the guest's mirrors do not play the game's death (`coop.npc deathfix off`, the
                  bug BUG-0015 fixed): the kill check must FAIL
"""
import re
import subprocess
import sys
import time

import lane
from coop_ui import Ui
from twoinst import Checks, Console, shot, start, wait_guest_in, wait_up, watch
import twoinst

DONE = (1416, 1062)          # the creature editor's green check (1440x1080 test window)
WORLD_SPECIES = '065CBE70'   # the fixture's species (the host's creature, the guest's too at first)
HOST_ONLY = '29020CC4'       # "Judge", a creature of the host's My Creations (fixture creations-host)


def mode(con):
    return con.call('game.mode')['out']


def wait(cond, secs, step=1.0):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return None


def npcs(con):
    """{net id: (species, state)}: state 'dead', 'alive' (host) / 'spawned' / 'pending' (guest)"""
    d = {}
    for line in con.call('coop.npc list')['out'].splitlines():
        m = re.match(r'\s*#(\d+) (?:\S+ )?species=([0-9A-F]{8}) (spawned |pending )?hp=\S+ dist=\S+( DEAD)?', line)
        if not m:
            continue
        st = 'dead' if m.group(4) or ' DEAD ' in line else (m.group(3) or 'alive ').strip()
        d[int(m.group(1))] = (m.group(2), st, ' pack' in line)
    return d


def mirror_ptr(con, nid):
    """guest: the game object of the mirror of NPC nid"""
    m = re.search(r'#%d species=\S+ spawned .* ptr=([0-9A-Fa-fx]+)' % nid, con.call('coop.npc list')['out'])
    return int(m.group(1), 16) if m else None


def game_dead(con, ptr, species):
    """the game's own death flag of a creature (as coop_creature_kills.py): None = no such object"""
    for line in con.call(f'coop.npc creatures {species}')['out'].splitlines():
        if f'creature {ptr:08X} ' in line:
            return ' DEAD' in line
    return None


def drag(con, x1, y1, x2, y2):
    """a slow drag the editor takes as moving a part"""
    con.call(f'move {x1} {y1}')
    time.sleep(0.3)
    con.call(f'button {x1} {y1} down')
    time.sleep(0.3)
    for i in range(1, 6):
        con.call(f'move {x1 + (x2 - x1) * i // 5} {y1 + (y2 - y1) * i // 5}')
        time.sleep(0.2)
    time.sleep(0.3)
    con.call(f'button {x2} {y2} up')
    time.sleep(1)


def own(con, who='mine'):
    m = re.search(who + r' creature: ([0-9A-F]{8})', con.call('coop.creations')['out'])
    return m.group(1) if m else None


def checks(ck, host, guest, tag, edited=True):
    # companion: the host's pack member of a species only the host had
    hn = npcs(host)
    pack = [i for i, (sp, st, p) in hn.items() if p and sp == HOST_ONLY]
    ck(pack, f'{tag}: the host has its companion ({pack})')
    seen = wait(lambda: [i for i in pack if npcs(guest).get(i, ('', ''))[1] == 'spawned'], 30)
    ck(seen, f'{tag}: the guest sees the host\'s companion ({pack} -> '
              f'{[npcs(guest).get(i) for i in pack]}; {guest.call("coop.creations")["out"].splitlines()[0]})')

    # ghosts: two samples 3 s apart (a despawn on its way is not a ghost)
    def ghosts():
        return set(npcs(guest)) - set(npcs(host))
    g1 = ghosts()
    time.sleep(3)
    g = g1 & ghosts()
    print(f'  {tag}: host mirrors {len(npcs(host))} NPCs, the guest has {len(npcs(guest))}; '
          + guest.call('coop.npc')['out'].splitlines()[-1].strip())
    ck(not g, f'{tag}: every NPC on the guest is one of the host\'s (living copies of old ones: {sorted(g)})')

    # kill: a living NPC the guest builds, not the companion and not the family
    hn, gn = npcs(host), npcs(guest)
    cand = [i for i, (sp, st, p) in hn.items() if st == 'alive' and not p and sp != WORLD_SPECIES
            and gn.get(i, ('', ''))[1] == 'spawned']
    ck(cand, f'{tag}: an NPC to kill ({len(cand)} candidates)')
    if cand:
        nid = cand[0]
        sp = hn[nid][0]
        ptr = mirror_ptr(guest, nid)
        host.call(f'coop.npc hosthit {nid} 100000')
        booked = wait(lambda: npcs(guest).get(nid, ('', ''))[1] == 'dead', 10, 0.5)
        # the game's death of the guest's mirror (BUG-0148): the mod's " DEAD" is set before and
        # apart from the death the game plays
        dead = ptr and wait(lambda: game_dead(guest, ptr, sp), 10, 0.5)
        ck(dead, f'{tag}: n{nid} the host killed is dead in the guest\'s game (mirror {ptr and hex(ptr)}: '
                 f'game dead {ptr and game_dead(guest, ptr, sp)}, mod flag {bool(booked)})')

    # species: the host shows the guest with its own (edited) creature
    def shown():
        want = own(host, 'player 1')
        m = re.search(r"remote 1 'Bob'.*\n.*species=([0-9A-F]{8})", host.call('coop.status')['out'])
        return (want, m and m.group(1)) if want and m and m.group(1) == want else None
    r = wait(shown, 30, 2)
    g_own = own(guest)
    print(f'  {tag}: guest own creature {g_own}, the host shows it as {r}')
    if edited:
        ck(r and g_own != WORLD_SPECIES, f'{tag}: the host shows the guest with its edited creature')
    else:
        ck(r, f'{tag}: the host shows the guest with the guest\'s creature')


def kill_game(inst):
    """SIGKILL one game (a crash): this lane's SporeApp.exe of that instance. spore.sh pids knows the
    lane's game folder and, on lane 1 (the real install), takes only games started for tests: the
    owner's own Spore there has no test argument and is never touched (BUG-0103)"""
    pids = subprocess.run([lane.SPORE, 'pids', str(inst)], capture_output=True, text=True).stdout.split()
    for pid in pids:
        subprocess.run(['kill', '-9', pid])
    if not pids:
        print(f'  kill_game: no game of instance {inst} found on lane {lane.LANE}')


def crash_and_reload(host, guest, planet):
    for m in twoinst._monitors:
        m.stop = True
    kill_game(lane.GUEST)  # the guest's crash first, the host's right after (report: 14:12 / 14:13)
    time.sleep(3)
    lane.sh('kill')
    time.sleep(3)
    lane.run_guest()
    time.sleep(2)
    lane.run_host()
    for inst, run in ((lane.GUEST, lane.run_guest), (lane.HOST, lane.run_host)):
        if not wait_up(inst, 90):
            run()
            assert wait_up(inst), f'instance {inst} did not start'
    host, guest = Console(lane.HOST), Console(lane.GUEST)
    wait(lambda: 'GalaxyGameEntry' in mode(host), 180)
    host.call('wait.frames 240 90000')
    slot, name = planet.split('/')
    host.call('coop.npc strays off')  # keep the test's companion (see main)
    host.call(f'coop.load {slot} {name}')  # its own save: written by the share after the editor
    wait(lambda: host.call('coop.avatar')['ok'], 240, 2)
    time.sleep(4)
    host.call(lane.HOST_CMD)
    wait(lambda: 'GalaxyGameEntry' in mode(guest), 120)
    guest.call('wait.frames 200 60000')
    guest.call(lane.JOIN)
    ok = wait_guest_in(host, guest)
    watch(lane.HOST, lane.GUEST)
    return host, guest, ok


def main():
    host, guest = start('creature', guest_creations=False)
    ck = Checks()
    for c in (host, guest):
        c.call('coop.team hp 1000')
    if '--seed-nodeath' in sys.argv:
        print(' ', guest.call('coop.npc deathfix off')['out'].strip())

    # the host's companion: an NPC of a species only the host's profile has, in its pack. It is
    # spawned by console in a herd without an archetype, so it has no tuning (+0xE84) and looks
    # like a creature a save brought back from a partner's puppet: the stray removal
    # (creature_e84) would take it. A real companion comes from a game herd and has its tuning.
    host.call('coop.npc strays off')
    out = host.call(f'coop.npc spawn {HOST_ONLY}')['out']
    ck('spawned' in out, f'an NPC of the host-only species {HOST_ONLY} ({out.strip()})')
    nid = wait(lambda: [i for i, (sp, st, p) in npcs(host).items() if sp == HOST_ONLY], 10)
    if nid:
        print(' ', host.call(f'coop.npc recruit {nid[0]}')['out'].strip())
    time.sleep(5)
    checks(ck, host, guest, 'start', edited=False)

    # 1. both evolve: the vote, the guest's "I'm ready", a body drag in each editor, "done"
    print(' ', host.call('coop.vote editor')['out'].splitlines()[0])
    gu = Ui(guest, 'guest')
    for _ in range(10):
        time.sleep(2)
        if 'vote none' in host.call('coop.vote')['out'] or 'Editor' in mode(host):
            break
        if gu.wait(lambda s: s['ready']['vis'], 10, 'ready button'):
            gu.click('ready')
    ck(wait(lambda: 'Editor' in mode(host), 60), 'the host is in its editor')
    ck(wait(lambda: 'Editor' in mode(guest), 60), 'the guest is in its own editor')
    host.call('wait.frames 300 60000')
    guest.call('wait.frames 300 60000')
    time.sleep(4)
    drag(host, 858, 536, 940, 500)
    drag(guest, 884, 536, 960, 500)
    shot(guest, 'creature_desync_guest_editor.png')
    host.call('click %d %d' % DONE)
    ck(wait(lambda: 'Creature' in mode(host), 60), 'the host is back in the Creature stage')
    guest.call('click %d %d' % DONE)
    ck(wait(lambda: 'Creature' in mode(guest) and guest.call('coop.avatar')['ok'], 120),
       'the guest is back in the Creature stage')
    g_new = wait(lambda: (lambda k: k if k and k != WORLD_SPECIES else None)(own(guest)), 60, 2)
    ck(g_new, f'the guest\'s edited creature is its own ({g_new})')
    time.sleep(15)  # the host's world after its editor reaches the guest
    wait(lambda: 'together 1 1' in host.call('coop.status')['out'] and guest.call('coop.avatar')['ok'], 90, 2)
    time.sleep(5)
    checks(ck, host, guest, 'after the editors')
    shot(guest, 'creature_desync_guest_after_editors.png')

    # 2. both crash; the host loads its save again, the guest joins again
    if '--no-crash' not in sys.argv:
        ck.faults(host=host, guest=guest)
        host, guest, ok = crash_and_reload(host, guest, twoinst.lane_state().get('planet') or 'Game0/Scalinus')
        ck(ok, 'after both crashed the host loaded its save and the guest joined again')
        for c in (host, guest):
            c.call('coop.team hp 1000')
        time.sleep(10)
        checks(ck, host, guest, 'after the reload')
        shot(guest, 'creature_desync_guest_after_reload.png')
        open(twoinst.STATE, 'w').write('{}')  # these games are no warm fixture games any more
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
