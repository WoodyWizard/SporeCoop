#!/usr/bin/env python3
"""Cell: a guest's kill the host did not make does not block that creature's next kill (BUG-0821).

A guest's lethal bite on a host creature (MSG_CELL_EAT ACT_BREAK / ACT_KILL) went into the host's 10 s ledger
(BUG-0632, kill_once) BEFORE the host decided on it: a kill the host refused (the guest knocked out: an attack in
flight, BUG-0509 / BUG-0714) or one the game spared still made every later kill of that creature by any guest
"refused (killed here already)" for 10 s.

1. The guest finds a host creature; the host gives it hp 1 (any bite kills it); the guest swims 30 m away.
2. The host knocks the guest out (`coop.pool hurt`); a lethal bite of the guest reaches the host while it is
   knocked out (`coop.pool sendact c<key> break`: one in flight): refused, the creature lives in the host's game.
3. The host swims next to the guest (`coop.pool tp 1 0 <id>`): the guest's own game revives it.
4. At once the guest bites the creature with the game's Bite (`coop.pool kill c<key>`: lethal on its mirror):
   within 3 s the creature is dying or gone in the HOST's game (well inside the 10 s of the refused one).
SPORE_FIX_OFF=cell.kill_refused must FAIL at 4 ("refused (killed here already)": alive).

  SPORE_LANE=3 python3 tools/tests/coop_cell_kill_refused.py
"""
import re
import time

from coop_cell_guest_hit import host_cell, num
from coop_cell_ko_food import wait_downed, where
from twoinst import Checks, start


def dead(st):
    return 'no such cell' in st or 'dying=1' in st or (num(st, 'hp') or 0) <= 0


def main():
    host, guest = start('cell')
    ck = Checks()
    time.sleep(3)
    key, r = None, ''
    for _ in range(15):
        r = guest.call('coop.pool kill - hit')['out']
        m = re.match(r'bit (c\d+) ', r)
        if m and 'dying' not in r:
            key = m.group(1)
            break
        time.sleep(1)
    ck(key is not None, f'the guest found a host creature ({r.strip()[:100]})')
    if not key:
        return ck.result()
    gid = (re.search(r'client id=(\d+)', guest.call('coop.status')['out']) or [None, '1'])[1]
    for _ in range(4):  # 30 m from the host's cell (and the creature): no rescue
        guest.call('coop.pool tp 30 0')
        time.sleep(1.0)
        w = where(guest)
        if isinstance(w, tuple) and float(w[2]) > 15:
            break
    # 2. the refused kill
    print('  ' + host.call(f'coop.pool hurt {gid} 50')['out'].strip(), flush=True)
    down = wait_downed(guest, 1)
    ck(down, '2. the guest is knocked out')
    print('  guest (knocked out): ' + guest.call(f'coop.pool sendact {key} break')['out'].strip(), flush=True)
    time.sleep(1.0)
    st = host_cell(host, key)
    print(f'  host: {st.strip()[:200]}', flush=True)
    ck(not dead(st), f'2. the knocked-out guest\'s kill is refused: the creature lives in the host\'s game (hp {num(st, "hp")})')
    if dead(st) or not down:
        return ck.result()
    t_ref = time.time()
    # 3. the revive
    print('  host: ' + host.call(f'coop.pool tp 1 0 {gid}')['out'].strip(), flush=True)
    up = wait_downed(guest, 0, 5)
    ck(up, f'3. the host revives the guest ({time.time() - t_ref:.1f} s after the refused kill)')
    host.call('coop.pool tp 30 0')  # (away again: the creature is the guest's)
    guest.call(f'coop.pool swimto {key} tp')
    print('  ' + host.call(f'coop.pool npcflag {key} hp 1')['out'].strip(), flush=True)  # (now: no NPC eats it first)
    time.sleep(0.5)  # the mirror has hp 1
    # 4. the next lethal bite kills it
    killed, t0 = False, time.time()
    while time.time() - t0 < 6 and not killed:
        r = guest.call(f'coop.pool kill {key}')['out'].strip()
        print(f'  guest bite (t+{time.time() - t0:.1f}): {r[:120]}', flush=True)
        for _ in range(6):
            time.sleep(0.25)
            if dead(host_cell(host, key)):
                killed = True
                break
        if 'gone' in r:
            break
    gone_first = 'gone' in r and not killed  # (something else ate it before the bite: the round proves nothing)
    st = host_cell(host, key)
    print(f'  host after: {st.strip()[:200]}', flush=True)
    print('  host: ' + ' '.join(x for x in host.call('coop.pool')['out'].splitlines() if x.startswith('cell_once')), flush=True)
    print('  guest: ' + ' '.join(x for x in guest.call('coop.pool')['out'].splitlines()
                                 if x.startswith('cell_once') or x.startswith('cell_c1')), flush=True)
    hl = host.call('log 200')['out']
    applied = re.search(rf'kill {key} \([a-z]+\): applied', hl) is not None
    print(f'  host log: ' + ' | '.join(x.strip()[-110:] for x in hl.splitlines() if f'kill {key}' in x)[:500], flush=True)
    ck(not gone_first, 'the creature was there for the guest\'s bite (nothing else ate it first)')
    ck(killed, f'4. the guest\'s next lethal bite kills the creature in the host\'s game ({time.time() - t_ref:.1f} s after the refused one)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
