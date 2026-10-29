#!/usr/bin/env python3
"""Two NEW players eat in a NEW Cell game (BUG-0279, Discord 2026-10-29: "even at stage 1 only 1 of
us could eat").

Like two players who just bought the game: both profiles have no saves and no creations, the host
starts a new game from its galaxy menu through the stage wizard (diet card, difficulty, planet
name), hosts, and the guest joins from its menu (`--early`: as soon as the host's game is in the
Cell stage, during its intro). Then both play with the bots of play_cell.py (real input: swim, look
at the screen, go for food) and the test asserts the game's own state on both machines:
* each player's own game ran AvatarEat for food it swam into (AddFoodPoints from the avatar,
  `own_points`), and the food bar (cCellSerializableData +0x1C) went up on both;
* the food bar is the same on both machines;
* the cells a player ate are gone on the other machine too (`eats_in` on the host).

  SPORE_LANE=N python3 tools/tests/coop_cell_new_game.py [--diet herb|carn] [--early] [--minutes 3]
      [--probe 6] [--apart 70]
"""
import argparse
import os
import random
import subprocess
import sys
import time

from twoinst import Checks, HERE, Monitor, shot, wait_guest_in, wait_up, watch  # (sets the path)
import lane  # noqa: E402
from lane import sh  # noqa: E402
from sporectl import Console  # noqa: E402
from play_cell import Player, pool  # noqa: E402


def wipe_profile(d):
    """a player who just bought the game: no saves, no creations, no co-op choices"""
    subprocess.run(['rm', '-rf', d + '/Games', d + '/Games-sporecoop-backup', d + '/EditorSaves.package',
                    d + '/SporeCoop/choices.ini'])


def alive(con, key):
    """the cell of this sync key is still in this game (and not dying)"""
    return con.call(f'coop.pool cell {key}')['out'].startswith('cell ')


def probe(name, me, other, kind, tries):
    """`me` swims to the nearest cell of `kind` on its screen (`coop.pool swimto`, like a click on it)
    until it touches it: did its own game eat it (AvatarEat: own points up), the same cell left on both?"""
    ok = 0
    log = []
    for _ in range(tries):
        seen = [l.split() for l in me.call('coop.pool see 80')['out'].splitlines() if l.startswith('see ')]
        food = [f for f in seen if f[2] == kind]
        if not food:
            me.call('wait.frames 60 10000')
            log.append('none on screen')
            continue
        key = food[0][1]
        p0 = pool(me)['own_points'] or 0
        me.call(f'coop.pool swimto {key} tp')
        for _ in range(12):
            time.sleep(0.25)
            me.call(f'coop.pool swimto {key}')
            if not alive(me, key):
                break
        time.sleep(1.5)
        p1 = pool(me)['own_points'] or 0
        here, there = alive(me, key), alive(other, key)
        log.append(f'{key}: points {p0}->{p1} here {"alive" if here else "gone"} there {"alive" if there else "gone"}')
        ok += p1 > p0 and here == there  # eaten: gone, or shrunk one level (EatFood), the same on both
    print(f'  probe {name} {kind}: {ok}/{tries} eaten: ' + '; '.join(log), flush=True)
    return ok


def food_on_screen(con):
    out = con.call('coop.pool see 200')['out']
    return sum(1 for l in out.splitlines() if l.startswith('see ') and l.split()[2] in ('plant', 'meat'))


def apart(ck, host, guest, seconds):
    """BUG-0174 / BUG-0279: the guest swims straight away from the host (W held) and looks at its
    screen: is there food where it is? The host's game populates and keeps only the tiles around
    its own cell, the guest spawns none of its own."""
    far = []
    guest.call('key 0x57 down')
    t0 = time.time()
    try:
        while time.time() - t0 < seconds:
            d = pool(guest)['partner_dist'] or 0
            n, nh = food_on_screen(guest), food_on_screen(host)
            print(f'  apart {time.time() - t0:3.0f}s: {d:.0f} m from the host, food on the guest\'s screen {n} '
                  f'(the host\'s {nh})', flush=True)
            if d > 50:
                far.append(n)
            time.sleep(4)
    finally:
        guest.call('key 0x57 up')
    ck(far and min(far) > 0, f'the guest finds food more than 50 m from the host (food on screen there: {far})')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--diet', default='herb', choices=('herb', 'carn'))
    ap.add_argument('--early', action='store_true', help='join while the host plays its stage intro')
    ap.add_argument('--minutes', type=float, default=3)
    ap.add_argument('--seed', type=int, default=None)
    ap.add_argument('--apart', type=float, default=0, help='then the guest swims away from the host for N s '
                    '(BUG-0174: fails while the host populates only around itself)')
    ap.add_argument('--probe', type=int, default=6, help='swims to food of the diet per player before playing')
    a = ap.parse_args()
    seed = a.seed if a.seed is not None else random.randrange(1 << 30)
    print(f'  new cell game, diet {a.diet}, early join {a.early}, {a.minutes} min, seed {seed}', flush=True)
    ck = Checks()

    sh('kill')
    time.sleep(2)
    sh('deploy')
    for d in (lane.HOST_DIR, lane.GUEST_DIR):
        wipe_profile(d)
    lane.run_guest()
    time.sleep(2)
    lane.run_host()
    for inst, run in ((lane.GUEST, lane.run_guest), (lane.HOST, lane.run_host)):
        if not wait_up(inst, 90):
            run()
            assert wait_up(inst), f'instance {inst} did not start'
    host, guest = Console(lane.HOST), Console(lane.GUEST)

    t0 = time.time()
    r = subprocess.run([sys.executable, os.path.join(HERE, 'new_stage_game.py'), 'cell', '-i', str(lane.HOST),
                        '--slot', '0', '--diet', a.diet, '--shots'] + (['--no-settle'] if a.early else []),
                       capture_output=True, text=True, timeout=900)
    print('  new game:', ' / '.join(l for l in r.stdout.splitlines() if l.strip())[-300:], flush=True)
    if not ck(r.returncode == 0, f'the host started a new cell game ({time.time() - t0:.0f} s)'):
        shot(host, 'cell_new_game_wizard.png')
        return ck.result()
    watch(lane.HOST, lane.GUEST)
    host.call(lane.HOST_CMD)
    for _ in range(60):
        if 'GalaxyGameEntry' in guest.call('game.mode')['out']:
            break
        time.sleep(1)
    guest.call('wait.frames 200 60000')
    guest.call(lane.JOIN)
    t1 = time.time()
    joined = wait_guest_in(host, guest, 240)
    if not ck(joined, f'the guest is in the host\'s new game ({time.time() - t1:.0f} s)'):
        shot(host, 'cell_new_game_host.png')
        shot(guest, 'cell_new_game_guest.png')
        return ck.result()
    time.sleep(4)
    for name, con in (('host', host), ('guest', guest)):
        print(f'  {name} creations:', ' | '.join(con.call('coop.creations')['out'].strip().splitlines()[:8]), flush=True)
        print(f'  {name} diet:', con.call('coop.pool diet')['out'].strip(), flush=True)

    rng = random.Random(seed)
    P = [Player('host', host, 0, 1, random.Random(rng.random())), Player('guest', guest, 1, 0, random.Random(rng.random()))]
    st = {p.name: pool(p.c) for p in P}
    first = {p.name: dict(st[p.name]) for p in P}
    print(f'  start: food {st["host"]["food"]}/{st["guest"]["food"]}, own points '
          f'{st["host"]["own_points"]}/{st["guest"]["own_points"]}', flush=True)
    t2 = time.time()
    last = 0
    try:
        while time.time() - t2 < a.minutes * 60 and not Monitor.events:
            now = time.time()
            for p in P:
                st[p.name] = pool(p.c)
                p.step(now, st[p.name])
            if now - last > 20:
                last = now
                hs, gs = st['host'], st['guest']
                print(f'  {now - t2:4.0f}s food {hs["food"]}/{gs["food"]} own points {hs["own_points"]}/{gs["own_points"]} '
                      f'(eats {hs["own_eats"]}/{gs["own_eats"]}) guest eats sent {gs["eats_sent"]} host eats in {hs["eats_in"]} '
                      f'partner {hs["partner_dist"]}/{gs["partner_dist"]} m tries host {P[0].tries} guest {P[1].tries}', flush=True)
            time.sleep(0.15)
    except (OSError, ValueError) as e:
        print('  !! console error:', repr(e), flush=True)
    for p in P:
        try:
            p.release()
        except (OSError, ValueError):
            pass
    for p in P:
        print(f'  {p.name}: ' + ' '.join(f'{k}={v}' for k, v in sorted(p.count.items())), flush=True)
        print(f'  {p.name} diet tries {p.tries}', flush=True)
    hs, gs = pool(host), pool(guest)
    print('  host ', hs['raw'].strip().replace('\n', ' | '))
    print('  guest', gs['raw'].strip().replace('\n', ' | '))
    for name, con in (('host', host), ('guest', guest)):
        print(f'  {name} diet:', con.call('coop.pool diet')['out'].strip(), flush=True)
    shot(host, 'cell_new_game_host.png')
    shot(guest, 'cell_new_game_guest.png')
    hp0, gp0 = first['host']['own_points'] or 0, first['guest']['own_points'] or 0
    # then each player goes for food of its diet on purpose (swimto: like a click on it)
    kind = 'plant' if a.diet == 'herb' else 'meat'
    if a.probe:
        got = {n: probe(n, me, other, kind, a.probe) for n, me, other in (('host', host, guest), ('guest', guest, host))}
        for n in got:
            ck(got[n] >= a.probe // 2, f'{n}: its game ate the {kind} it swam into, gone on both ({got[n]}/{a.probe})')
        for n, me, other in (('host', host, guest), ('guest', guest, host)):
            probe(n + '(other diet)', me, other, 'meat' if kind == 'plant' else 'plant', 2)

    ck((hs['own_points'] or 0) > hp0, f'the host ate by itself ({hs["own_eats"]} items, {hs["own_points"]} points)')
    ck((gs['own_points'] or 0) > gp0, f'the guest ate by itself ({gs["own_eats"]} items, {gs["own_points"]} points)')
    ck((gs['eats_sent'] or 0) > 0 and (hs['eats_in'] or 0) > 0,
       f'what the guest ate is gone on the host (sent {gs["eats_sent"]}, applied {hs["eats_in"]})')
    ck((hs['food'] or 0) > (first['host']['food'] or 0), f'the food bar went up ({first["host"]["food"]} -> {hs["food"]})')
    for _ in range(3):
        if hs['food'] == gs['food']:
            break
        time.sleep(1.2)
        hs, gs = pool(host), pool(guest)
    ck(hs['food'] == gs['food'], f'the same food bar on both ({hs["food"]} / {gs["food"]})')
    if a.apart:
        apart(ck, host, guest, a.apart)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
