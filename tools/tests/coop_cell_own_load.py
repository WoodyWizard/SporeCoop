#!/usr/bin/env python3
"""BUG-0342 / BUG-0349: a guest whose own cell is applied at the world load (the level rebuild on the
first Cell frames) was a "ghost" for minutes in the players' games (it swam through everything and
could not eat, bite or be hit); after a reload of the world it kept the world's cell while the host
drew its own one (BUG-0349).

The guest's choices.ini names a premade cell before it starts (the test profiles have no own cells),
so its own cell comes with the load of the host's world, as for the players. Then, in the guest's
own game: it plays that cell, and swimming into food of its diet makes its game eat it (AvatarEat:
own points up, the cell gone on both machines, coop_cell_new_game.py's probe); its bite through the
game's Bite takes hp from a host NPC in the host's game.

Then the host shares its world again (as an evolution or a manual share does): after that load the
guest's game plays its own cell again (its avatar's model), the host draws the same key for it, and
the guest still eats.

SPORE_FIX_OFF=creations.cell_reapply: the guest keeps the world's cell after the reload (must FAIL).
SPORE_FIX_OFF=cell.puppet_index: the rebuilt avatar can sit at the old puppet's address and is taken
for the puppet: its collision pairs are dropped (fails in about a third of the runs: the address).
SPORE_FIX_OFF=cell.own_rebuild: the rebuild comes on the first frames of the reload and the guest's
game does not eat what it swims into (must FAIL at "after the reload ... still eats"; the switch goes
off after the first load, so only the reload checks the old timing)."""
import random
import re
import time

import twoinst
from coop_cell_new_game import alive
from play_cell import pool
from twoinst import Checks, start

OTHER_CELL = 'BAF1AD40'  # a premade player cell in Spore_Content (coop_creations_cell.py)


def field(out, pat):
    m = re.search(pat, out)
    return m.group(1) if m else None


def eat_plants(guest, host, want=3, timeout=90):
    """the guest swims into plants on its screen (`coop.pool swimto`, like a click on one), moving on
    when none is there: (eaten by its own game, plants tried). Eaten = AvatarEat ran (own points up)
    and the plant is gone on both machines"""
    eaten, tried, log, done = 0, 0, [], set()
    t0 = time.time()
    while tried < want and time.time() - t0 < timeout:
        seen = [l.split() for l in guest.call('coop.pool see 80')['out'].splitlines() if l.startswith('see ')]
        plants = [f[1] for f in seen if f[2] == 'plant' and f[1] not in done]  # a new plant each time
        if not plants:
            guest.call(f'coop.pool goto {random.choice((-30, 30))} {random.choice((-30, 30))}')
            time.sleep(3)
            continue
        key = plants[0]
        done.add(key)
        tried += 1
        p0 = pool(guest)['own_points'] or 0
        op0 = int(field(guest.call('coop.pool')['out'], r'own_pairs=(\d+)') or 0)
        guest.call(f'coop.pool swimto {key} tp')
        col = ''
        for i in range(12):
            time.sleep(0.25)
            guest.call(f'coop.pool swimto {key}')
            if i == 2:
                col = guest.call('coop.pool collide')['out'].strip()
            if not alive(guest, key):
                break
        op1 = int(field(guest.call('coop.pool')['out'], r'own_pairs=(\d+)') or 0)
        time.sleep(1.5)
        p1 = pool(guest)['own_points'] or 0
        ok = p1 > p0 and not alive(guest, key) and not alive(host, key)
        eaten += ok
        log.append(f'{key}: points {p0}->{p1} {"eaten" if ok else "not eaten"} own_pairs +{op1 - op0}')
        if not ok or tried == 1:
            print('    collide:', col.replace('\n', ' | ')[:900], flush=True)
        if not ok and tried == 1:  # what the guest's game has for its cell and the plant (BUG-0342)
            me = [l for l in guest.call('coop.pool prey 40')['out'].splitlines() if l.startswith('player ')]
            print('    diag:', ' | '.join(me)[:300], flush=True)
            print('    diag:', guest.call(f'coop.pool cell {key}')['out'].strip()[:300], flush=True)
            print('    diag:', guest.call('coop.pool diet')['out'].strip().replace('\n', ' | ')[:300], flush=True)
            print('    diag:', guest.call('coop.pool sapme')['out'].strip()[:600], flush=True)
            print('    diag plant:', guest.call(f'coop.pool sapme {key}')['out'].strip()[:600], flush=True)
            print('    diag sap:', guest.call('coop.pool sap')['out'].strip()[:700], flush=True)
            print('    diag: own_pairs', field(guest.call('coop.pool')['out'], r'own_pairs=(\d+)'), 'then',
                  (guest.call('wait.frames 30 5000'), field(guest.call('coop.pool')['out'], r'own_pairs=(\d+)'))[1], flush=True)
    print(f'  plants: {eaten}/{tried} eaten: ' + '; '.join(log), flush=True)
    return eaten, tried


def main():
    twoinst.PRESET_CHOICES = {'guest': {'cell': OTHER_CELL}}
    host, guest = start('cell')
    ck = Checks()
    t0 = time.time()
    while time.time() - t0 < 40:
        gc = guest.call('coop.creations')['out']
        if field(gc, r'avatar model ([0-9A-F]{8})') == OTHER_CELL:
            break
        time.sleep(2)
    print('  guest:', guest.call('coop.creations')['out'].replace('\n', ' | ')[-160:])
    ck(field(guest.call('coop.creations')['out'], r'avatar model ([0-9A-F]{8})') == OTHER_CELL,
       'the guest plays its own cell after the load')
    time.sleep(8)
    # BAF1AD40 eats plants (meat stays where it is: runs of 2026-10-31)
    # BUG-0342: after the first load the own cell did not eat in 12 of 34 runs before cell.puppet_index
    eaten, tried = eat_plants(guest, host)
    ck(tried >= 2 and eaten >= 2, f"after the first load the guest's own game eats plants it swims into ({eaten} of {tried})")
    # a bite through the game's Bite on the nearest host NPC: the host's game takes its hp
    out = guest.call('coop.pool realbite 40')['out'].strip()
    print('  realbite:', out[:160])
    key = field(out, r'\bc(\d+)')
    time.sleep(2)
    p = pool(guest)
    print(f"  guest hits_sent={p['hits_sent']} own_points={p['own_points']}")
    ck(key is not None and (p['hits_sent'] or 0) > 0, "the guest's bite reaches a host cell")
    # BUG-0349: the world again (a share / an evolution): a new load of the same world
    print(' ', host.call('coop.worldfix queue 1 0')['out'].strip())
    time.sleep(3)
    ck(twoinst.wait_guest_in(host, guest), 'the guest is in the world again after the second load')
    t0 = time.time()
    model = None
    while time.time() - t0 < 40:
        model = field(guest.call('coop.creations')['out'], r'avatar model ([0-9A-F]{8})')
        if model == OTHER_CELL:
            break
        time.sleep(2)
    time.sleep(6)
    puppet = field(host.call('coop.status')['out'], r"remote 1 '[^']*'.*\n.*species=([0-9A-F]{8})")
    print(f'  after the reload: guest avatar model {model}, the host draws the guest as {puppet}')
    ck(model == OTHER_CELL, f"after the reload the guest's game plays its own cell again ({model})")
    ck(puppet == model, f'the host draws the guest with the cell the guest plays ({puppet} / {model})')
    eaten, tried = eat_plants(guest, host)
    ck(tried >= 2 and eaten >= 2, f"after the reload the guest's game still eats plants ({eaten} of {tried})")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
