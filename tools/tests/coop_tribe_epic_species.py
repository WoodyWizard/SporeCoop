#!/usr/bin/env python3
"""Tribe: the co-op epic is never the guest's creature after the live Creature -> Tribe change (BUG-0563).

Player report (0.7.5): right after the start of Tribe an "enemy monster" came, the guest's creature of the
Creature stage, with endless meat. Logs: the host's game had the guest's puppet (species of the guest's
creation) until the stage change, 8 minutes later the co-op epic came (tribe.cpp spawn_epic: the species of
the animal nearest the village).

1. The players' way from the `creature` fixture to Tribe (coop_tribe_snapshot.py: the evolution bar, History,
   the vote, the editors, the captions); the guest joined next to the host (its puppet near the host's avatar,
   where the village is made).
2. Together in Tribe, the host's game calls the co-op epic now (`coop.tribe epic`, what its timer does after
   8 minutes together). PASS: the epic is not of a player's species (`player_species=0`).

SPORE_FIX_OFF=tribe.epic_species: the nearest animal (0.7.6).

  SPORE_LANE=6 python3 tools/tests/coop_tribe_epic_species.py
"""
import re
import time

from coop_tribe_snapshot import HISTORY_BTN, HISTORY_OK, Player, click, out
from twoinst import Checks, start


def main():
    host, guest = start('creature')
    ck = Checks()
    players = [Player(host, 'host'), Player(guest, 'guest')]
    t0 = time.time()
    host.call('coop.dna add 3000')
    last_kick = time.time()
    tribe_at = None
    while time.time() - t0 < 600:
        h_mode = out(host, 'game.mode')
        if 'Tribe' in h_mode:
            tribe_at = time.time()
            break
        busy = False
        for p in players:
            did = p.service(host)
            if did:
                print(f'   {time.time() - t0:5.0f}s {p.name}: {did}', flush=True)
                busy = True
        busy |= 'movie=1' in out(host, 'coop.status') or out(host, 'coop.vote').startswith('vote active')
        if not busy and 'Creature' in h_mode and time.time() - last_kick > 25:
            last_kick = time.time()
            click(host, HISTORY_BTN)
            time.sleep(3)
            click(host, HISTORY_OK)
            print(f'   {time.time() - t0:5.0f}s host: History and its check', flush=True)
        time.sleep(2)
    if not ck(tribe_at, f'the host entered the Tribe stage ({time.time() - t0:.0f} s)'):
        return ck.result()
    t1, good = time.time(), None
    while time.time() - t1 < 300:
        for p in players:
            p.service(host)
        tog = re.search(r'^together 1 1$', out(host, 'coop.status'), re.M) is not None
        good = (good or time.time()) if tog and 'movie=1' not in out(host, 'coop.status') else None
        if good and time.time() - good > 10:
            break
        time.sleep(3)
    if not ck(good, 'the guest plays the Tribe stage together with the host'):
        return ck.result()
    r = out(host, 'coop.tribe epic').strip()
    print('   host: ' + r, flush=True)
    m = re.search(r'player_species=(\d+)', r)
    ck('spawned' in r and 'not spawned' not in r, 'the host\'s game made the co-op epic')
    ck(m and m.group(1) == '0', f'the co-op epic is not a player\'s creature ({r})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
