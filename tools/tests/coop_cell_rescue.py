#!/usr/bin/env python3
"""Cell stage: a knocked-out partner rescues nobody (BUG-0131).

A lethal hit knocks a player out instead of killing it only when a partner is there to revive it.
A knocked-out cell is held at hp 1 and never flagged dead, so it counted as that partner: two
players knocked out side by side revived each other, and a lethal hit became a knockout although
nobody could help.

1. The guest is knocked out (a lethal bite from the host's side).
2. The host takes a lethal bite itself (`coop.pool selfhit`): its only partner is knocked out, so
   the game's own death runs (the avatar dying, +0x113), not a knockout (hp held at 1).
3. On the guest's machine the host is not shown knocked out (no `ko[0]=1`).

pool.rescue off failed (retired in 0.7.15, BUG-1138: FAIL run 20261030-010849:party_cell_ko); the test runs
PASS-only.
"""
import re
import time

from harness import num, pool, waiter
from twoinst import Checks, start


wait = waiter(step=0.25)


def main():
    host, guest = start('cell')
    ck = Checks()
    ck(wait(lambda: (num(pool(guest), 'mirrors') or 0) > 5, 30), 'the guest mirrors the host pool')
    guest.call('coop.pool tp 30 0')
    time.sleep(1.5)
    host.call('coop.pool hurt 1 50')
    ck(wait(lambda: num(pool(guest), 'downed') == 1, 5), 'the guest is knocked out')
    ck(wait(lambda: re.search(r'^ko\[1\]=1', pool(host), re.MULTILINE), 3), 'the host sees the guest knocked out')
    r = host.call('coop.pool selfhit 250')['out']
    print('  host:', r)
    time.sleep(1.0)
    h, g = pool(host), pool(guest)
    print('  host after:', [l for l in h.splitlines() if 'downed=' in l or l.startswith('ko[')][:2])
    ck(num(r, 'downed') == 0 and (num(r, 'dying') == 1 or num(r, 'dead') == 1),
       f'the host\'s lethal hit runs the game\'s death, its only partner is knocked out ({r})')
    ck(not re.search(r'^ko\[0\]=1', g, re.MULTILINE), 'the guest does not see the host knocked out')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
