#!/usr/bin/env python3
"""BUG-1295 (found by the Lineages fork, PR #196, its BUG-9215): a player who joins while the host sits on its death
screen arrives dead at the host's spot ("You were killed in battle"): the join's share saved the world with the host's
dead avatar at once.

Two games on the creature fixture. Host: a lethal hit (`coop.team die`, the game's damage path), its death caption
stays up (nobody presses OK). Guest: leaves and joins again. Checked: while the host is dead its game posts no share
save (the share waits, "waits until the host's creature lives again" in the host's log); then the host presses OK and
respawns; the guest gets the world and its own creature lives there (health above 0, no death caption on the guest's
screen for 10 s). A key frame shows both games after the join.
SPORE_FIX_OFF=world.join_after_respawn must FAIL (the save goes out while the host is dead; the guest arrives dead).

  SPORE_LANE=9 python3 tools/tests/coop_join_host_dead.py"""
import os
import re
import time

import lane
import uifind
from twoinst import Checks, keyframe, start, wait_guest_in

DEAD = 'You were killed'


def av(con):
    out = con.call('coop.avatar')['out']
    m = re.search(r'hp=([-\d.]+)/([-\d.]+)', out)
    return float(m.group(1)) if m else -1.0


def saves(con):
    m = re.search(r'world saves posted (\d+), for shares (\d+)', con.call('coop.status')['out'])
    return int(m.group(2)) if m else -1


def log_since(inst, pos):
    try:
        with open(lane.game_log(inst), 'rb') as f:
            f.seek(pos)
            return f.read().decode('utf-8', 'replace')
    except OSError:
        return ''


def main():
    host, guest = start('creature')
    ck = Checks()
    inst = host.port - 47799
    time.sleep(3)
    print('  host: ' + host.call('coop.team die')['out'].strip(), flush=True)
    caption = False
    for _ in range(40):
        if uifind.shown(host, DEAD):
            caption = True
            break
        time.sleep(0.5)
    ck(caption, 'the host is killed (its death caption is up)')
    if not caption:
        ck.faults(host=host, guest=guest)
        return ck.result()
    try:
        pos = os.path.getsize(lane.game_log(inst))
    except OSError:
        pos = 0
    s0 = saves(host)
    print('  guest: ' + guest.call('coop.leave')['out'].strip(), flush=True)
    time.sleep(5)
    print('  guest: ' + guest.call(lane.JOIN)['out'].strip(), flush=True)
    t0 = time.time()
    while time.time() - t0 < 30 and 'Bob' not in host.call('coop.status')['out']:
        time.sleep(1)
    time.sleep(15)  # the host stays dead: its caption is up
    s1 = saves(host)
    log = log_since(inst, pos)
    held = 'waits until the host\'s creature lives again' in log
    print(f'  host share saves while dead {s0} -> {s1}, share held: {held}, caption still up: {uifind.shown(host, DEAD)}',
          flush=True)
    ck(s1 == s0, 'the host\'s game posts no share save while the host is dead')
    ck(held, 'the joining guest\'s share waits for the host\'s respawn')
    for _ in range(30):  # OK on the caption until the host's creature lives again
        if uifind.shown(host, DEAD):
            host.call('key enter')
        time.sleep(1)
        if not uifind.shown(host, DEAD) and av(host) > 0:
            break
    ck(av(host) > 0, f'the host respawned (hp {av(host)})')
    ck(wait_guest_in(host, guest), 'the guest plays in the host\'s world after the host respawned')
    time.sleep(5)
    keyframe('join_host_dead', 'after the join: the guest\'s own creature stands alive in the host\'s world (no "You were '
             'killed" caption on the guest); the host\'s creature alive at its nest')
    dead_seen = False
    t0 = time.time()
    while time.time() - t0 < 10:
        dead_seen |= bool(uifind.shown(guest, DEAD))
        time.sleep(1)
    hp = av(guest)
    print(f'  guest: hp {hp}, death caption seen: {dead_seen}', flush=True)
    ck(hp > 0 and not dead_seen, 'the guest arrives alive (health above 0, no death caption)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
