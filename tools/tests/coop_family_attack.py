#!/usr/bin/env python3
"""BUG-0639 (Discord report 2026-11-06, 0.7.8: "for the host the nest is evolving, for the guest he can attack them";
the host's game had the goal "Migrate to a new nest", the guest played its own creature): can the guest attack the
members of the family nest, which the host cannot attack in its own game?

Creature fixture. The guest bites a family member of the world's player species (a host NPC of the host's avatar
species, mirrored on the guest) with real input (combat stance, a click on it, key 1) for ~15 bites, first with the
world's species, then (--own, default on) with its own creature from the co-op window.
PASS, in the host's game: the family member keeps its health (the game's health of the host's NPC), and the guest's
game answers the family member as friendly (vt 0xD4) like the host's.

  coop_family_attack.py [--no-own]"""
import re
import sys
import time

import creations_state
from coop_creature_flock import friendly
from coop_creature_kills import W, H, family, npcs, proxies
from twoinst import Checks, set_stance, start


def bite(guest, nid, n=15):
    guest.call(f'coop.social goto {nid}')
    time.sleep(1)
    set_stance(guest, 'combat', W, H)  # waits for the game time, read back from the game (BUG-0944)
    for _ in range(n):
        for line in guest.call('coop.targets all')['out'].splitlines():
            f = line.split()
            if len(f) == 5 and f[0] == 'obj' and f[1] == 'n' + nid and 20 < int(f[3]) < W - 20 and 90 < int(f[4]) < H - 170:
                guest.call(f'click {f[3]} {f[4]}')
        time.sleep(0.3)
        guest.call('key 1')
        time.sleep(0.9)
    guest.call(f'click {W // 2 + 43} {H - 82}')


def phase(host, guest, ck, fam, label):
    prox = proxies(guest)
    cand = sorted((n[3], i) for i, n in npcs(host).items() if i in prox and not n[4] and n[1] == fam and n[2] > 0)
    if not ck(cand, f'{label}: family members of the host ({fam}) mirrored on the guest ({len(cand)})'):
        return
    nid = cand[0][1]
    hp0 = npcs(host)[nid][2]
    fh, fg = friendly(host, nid), friendly(guest, nid)
    print(f'  {label}: family member #{nid} hp {hp0}; friendly (vt 0xD4, pack, hearts) host {fh} guest {fg}', flush=True)
    bite(guest, nid)
    time.sleep(2)
    n = npcs(host).get(nid)
    hp1 = n[2] if n else None
    print(f'  {label}: after the bites the host\'s #{nid} hp {hp1}, the guest\'s mirror hp '
          f'{proxies(guest).get(nid, (0, 0, None))[2]}; ' + guest.call('coop.npc')['out'].strip().splitlines()[-1][:200],
          flush=True)
    ck(hp1 is not None and hp1 >= hp0, f"{label}: the host's family member keeps its health ({hp0} -> {hp1})")
    ck(fh and fg and fg[0] == fh[0], f"{label}: the guest's game answers it as friendly like the host's ({fg} vs {fh})")


def main():
    host, guest = start('creature')
    ck = Checks()
    fam = family(host)
    phase(host, guest, ck, fam, 'world species')
    if '--no-own' not in sys.argv:
        st0 = creations_state.save(guest)
        own = creations_state.choose_own(guest)
        try:
            t0 = time.time()
            while time.time() - t0 < 40 and own not in guest.call('coop.avatar')['out']:
                time.sleep(1)
            ck(own and own in guest.call('coop.avatar')['out'], f'the guest plays its own creature {own}')
            time.sleep(3)
            phase(host, guest, ck, fam, 'own creature')
        finally:
            creations_state.restore(guest, st0)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
