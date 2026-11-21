#!/usr/bin/env python3
"""BUG-0546: an NPC empire's ship without a model on the host's own game (UfoGfx_SetVisible 0xE98DD0).

The host's Sporepedia cache (`Pollination.package` of its profile) decides which ships an NPC empire flies. On lab
lane 5 the cache holds no complete UFO (docs/re/space.md#space-lane5-pollination): arriving at the home star of the
empire 8000216E (star 004A0014 of the space_empire fixture) faulted the host about 2 s later. This test loads the
fixture, flies to that star and waits: no fault, and the game still answers.

  coop_space_npc_model.py [--solo] [--cache <Pollination.package>]
  --solo   one game, no co-op session (the game's own behaviour, the mod's session code idle)
  --cache  the host's Sporepedia cache for the test: a file, or `bug0546` (default): lane 5's cache of 2026-11-04
           without a complete UFO, looked up as out/fixtures/bug0546-Pollination.package of the main checkout,
           then out/bug0546/lane5-Pollination.package of this worktree; `lane`: the lane's own cache. The test
           puts the lane's own file back at the end. No such file: RESULT: SKIP.
  SPORE_FIX_OFF=<switch> as every test (twoinst.start); with --solo the switch is set on the one game.

BUG-1363: which ship the empire flies is the game's random pick; 3 lab runs in a row saw no ship without its model
(INCONCLUSIVE in a row fails, A15). After NATURAL arrivals without the case, the next arrival's ship models get a key no
game has (`coop.ufoplant`; the guard and the game's SetModel work on it as on a cache ship without its model): the case
happens every run; which way it came is printed ("natural" / "planted").
"""
import os
import re
import shutil
import subprocess
import sys
import time

from harness import sp
import twoinst  # noqa: F401 (first: tools/ on sys.path)
import fixtures
import lane
from twoinst import Console  # noqa: F401 (twoinst puts tools/ on the path)
from twoinst import Checks, alive, shot, start, wait_up

FIXTURE = 'space_empire'
STAR = 0x004A0014  # home star of the NPC empire 8000216E in the fixture
VISITS = 3
NATURAL = 2  # BUG-1363: arrivals with the game's own pick of ships; the last one gets planted unknown model keys


def faults(con):
    out = con.call('exc')['out'].splitlines()
    m = re.search(r'total faults: (\d+)', out[0] if out else '')
    return (int(m.group(1)) if m else None), out[:4]


def start_solo():
    lane.sh('kill')
    time.sleep(2)
    lane.sh('deploy')
    lane.sh('fixture', 'restore', FIXTURE, lane.HOST_PROFILE)
    lane.run_host()
    assert wait_up(lane.HOST, 120), 'the game did not start'
    con = Console(lane.HOST)
    for _ in range(180):
        if 'GalaxyGameEntry' in con.call('game.mode')['out']:
            break
        time.sleep(1)
    con.call('wait.frames 240 90000')
    slot, name = fixtures.planet_of(FIXTURE).split('/')
    con.call(f'coop.load {slot} {name}')
    t0 = time.time()
    while time.time() - t0 < 240 and not con.call('coop.avatar')['ok']:
        time.sleep(2)
    time.sleep(4)
    for name in [n.strip() for n in os.environ.get('SPORE_FIX_OFF', '').split(',') if n.strip()]:
        print('  SPORE_FIX_OFF:', con.call(f'coop.fix {name} off')['out'].strip(), flush=True)
    return con


def bad_cache():
    here = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    main_root = os.path.join(os.path.dirname(subprocess.run(
        ['git', 'rev-parse', '--path-format=absolute', '--git-common-dir'], cwd=here, capture_output=True,
        text=True).stdout.strip()))
    for p in (os.path.join(main_root, 'out', 'fixtures', 'bug0546-Pollination.package'),
              os.path.join(here, 'out', 'bug0546', 'lane5-Pollination.package')):
        if os.path.isfile(p):
            return p
    return None


GFX_CACHES = ('GraphicsCache.package', 'GraphicsCache.001.package')


def gfx_aside(tag):
    """BUG-1213: the host's GraphicsCache (the game's baked models) goes aside for the run, the game starts with an empty
    one. A lane that ever had those ships' models keeps them baked there: the resource manager knows their keys, so a
    ship of the cache's summaries got its model and the case never came (lanes 2-4, 6, 7; a fresh one on lane 6: lab
    20261116 exp, 2 ships defaulted at the first visit). Call before the games start; gfx_back(tag) after they ended"""
    for n in GFX_CACHES:
        f = os.path.join(lane.HOST_DIR, n)
        if os.path.isfile(f) and not os.path.isfile(f + tag):
            os.replace(f, f + tag)


def gfx_back(tag):
    """the lane's own GraphicsCache files again (the game's new ones go)"""
    for n in GFX_CACHES:
        f = os.path.join(lane.HOST_DIR, n)
        if os.path.isfile(f + tag):
            if os.path.isfile(f):
                os.remove(f)
            os.replace(f + tag, f)


def main():
    arg = sys.argv[sys.argv.index('--cache') + 1] if '--cache' in sys.argv else 'bug0546'
    cache = bad_cache() if arg == 'bug0546' else None if arg == 'lane' else arg
    if arg == 'bug0546' and not cache:
        print('RESULT: SKIP (no copy of the BUG-0546 cache: out/fixtures/bug0546-Pollination.package)', flush=True)
        return 0
    own = os.path.join(lane.HOST_DIR, 'Pollination.package')
    keep = own + '.bug0546-own'
    if cache:  # before the games start (the game reads it at its start); the lane's own file comes back
        lane.sh('kill')
        if os.path.isfile(own) and not os.path.isfile(keep):
            shutil.copy2(own, keep)
        shutil.copy(cache, own)
        gfx_aside('.bug0546-own')
        print(f'  host cache: {cache} (and an empty GraphicsCache)', flush=True)
    try:
        return run()
    finally:
        if cache and os.path.isfile(keep):
            lane.sh('kill')
            shutil.move(keep, own)
        if cache:
            gfx_back('.bug0546-own')


def run():
    solo = '--solo' in sys.argv
    ck = Checks()
    if solo:
        host = start_solo()
        guest = None
        print('  status (no session):', ' | '.join(host.call('coop.status')['out'].splitlines()[:2])[:200], flush=True)
    else:
        host, guest = start(FIXTURE)
    time.sleep(3)
    f0, _ = faults(host)
    home = re.search(r'star (\d+)', sp(host, 'star'))
    home = int(home.group(1)) if home else 0

    def defaulted():
        m = re.search(r'ship models defaulted (\d+)', host.call('coop.status')['out'])
        return int(m.group(1)) if m else 0

    def fly(star, world):
        for _ in range(5):  # a badge or caption cinematic blocks the jump: confirm it
            r = sp(host, 'jumpto', star)
            print(f'  host flies to star {star:08X}: {r}', flush=True)
            if 'cinematic' not in r:
                break
            host.call('key enter')
            time.sleep(2)
        t0 = time.time()
        arrived = False
        while time.time() - t0 < 60:
            st = host.call('coop.status')['out']
            if re.search(rf'world={world}|Space {world}', st) or world in sp(host, 'hp'):
                arrived = True
            hp = sp(host, 'hp')
            if 'cine=1' in hp or 'movie=1' in hp:
                host.call('key enter')
            time.sleep(2)
            if arrived and time.time() - t0 > 25:
                break
        return arrived

    # the NPC empire's ships at its star take their models from the cache's UFO list, one picked per ship: the case
    # (a ship without its model) comes with some of them only. Up to VISITS arrivals there (out to the home star and
    # back in between) until one ship needed the guard (BUG-1213; with the empty GraphicsCache the first one had it)
    for visit in range(VISITS):
        if visit and home:
            fly(home, f'80{home:06X}')
        planted = visit >= NATURAL
        if planted:  # BUG-1363: the ships of this arrival ask for a model no game has
            print('  visit %d: planted: %s' % (visit + 1, host.call('coop.ufoplant 4')['out'].strip()), flush=True)
        fly(STAR, '804A0014')
        n = defaulted()
        print(f'  visit {visit + 1}: ship models defaulted {n}' + (f' ({"planted" if planted else "natural"})' if n else ''),
              flush=True)
        if n or not alive(lane.HOST):
            break
    host.call('coop.ufoplant 0')  # nothing planted stays armed
    fleet = host.call('coop.fleet list')['out']
    print('  fleet:', ' | '.join(fleet.splitlines()[:3])[:300], flush=True)
    m = re.search(r'ship models defaulted \d+ \(SetVisible without a model skipped \d+\)', host.call('coop.status')['out'])
    print('  game fixes:', m.group(0) if m else '(no ship model guard in this build)', flush=True)
    shot(host, 'space_npc_model_host.png')
    f1, lines = faults(host)
    print('  host faults:', lines[0] if lines else '?', flush=True)
    for line in lines[1:]:
        print('    ' + line.strip(), flush=True)
    ck(alive(lane.HOST), 'the host game still answers after the jump')
    ck(f1 is not None and f1 == (f0 or 0), f'no fault on the host at the NPC empire\'s star ({f0} -> {f1})')
    if guest is not None:
        ck.faults(host=host, guest=guest)
    else:
        ck.faults(host=host)
    # the case must have happened: with the guard on, a ship model it had to replace (lane 4's run with lane 5's
    # Pollination.package had none: inferred, its own GraphicsCache.package holds those ships' baked models)
    n = int(re.search(r'defaulted (\d+)', m.group(0)).group(1)) if m else None
    if ck.ok and n == 0:
        print('RESULT: SKIP (no NPC ship without its model in this run: the case did not happen on this lane)', flush=True)
        return 0
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
