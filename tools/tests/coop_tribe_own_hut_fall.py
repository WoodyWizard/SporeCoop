#!/usr/bin/env python3
"""Tribe: the players' own hut dead in the host's game does not fall in the guest's game (BUG-1076).

Campaign lab 20261113-082316 (4 players, the Tribe end): an NPC village raided the players' hut back, it fell in the
host's game, and the guest's realign (BUG-0777) gave the guest's own copy the killing blow too. The guest's game then
ran its own defeat (the hut-destroyed cinematic, a reload of its own save 20 s later, every conquest gone); after each
reload the realign conquered the villages and felled the hut again (11 reloads in 6 min), and a village caption
playing after such a reload read the conquered tribe the reload had freed: 9 faults at 0xCE69F9 / 0xCE69FB (the
Tribe token provider's tribe name, docs/re/tribe.md#tribe-caption-tribe-token).

1. Two games on the tribe fixture; the host's game forms an NPC tribe 45 m away (`coop.tribe npc_tribe`) and the
   guest's game gets its copy.
2. The host's game: the game's own killing blow on that village's hut and on the players' own hut at once
   (`coop.tribe hutfall`: the conquest caption and the players' defeat as in the campaign).
3. For 45 s the guest's game keeps the players' hut: the same object (no reload of its own save), its combatant
   state never 2 (`coop.realign planet hut` fl bit 2), and no fault in either game.
   SPORE_FIX_OFF=realign.own_hut: the guest's own hut falls (and its game reloads): FAIL.
"""
import re
import time

from coop_tribe_conquer_orders import HUT, huts
from coop_tribe_conquest_guest import near, planet
from coop_tribe_tools import dist
from twoinst import Checks, keyframe, start


def home_of(con):
    m = re.search(r'^tribe \d+ \(player\) at \(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', con.call('coop.tribe')['out'], re.M)
    return tuple(float(m.group(i)) for i in (1, 2, 3)) if m else None


def hut_near(con, p, tol=40):
    best = min(huts(con), key=lambda h: dist(h[1], p), default=None)
    return best if best and dist(best[1], p) < tol else None


def main():
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(5)
    home = home_of(host)
    own = hut_near(host, home) if home else None
    if not ck(bool(own), f"the host's game has the players' hut ({own}, village at {home})"):
        return ck.result()
    _, own_p = own
    gown = hut_near(guest, own_p, 10)
    if not ck(bool(gown), f"the guest's game has the players' hut ({gown})"):
        return ck.result()
    before = {h for h, _ in huts(host)}
    out = host.call('coop.tribe npc_tribe 3 45')['out'].strip()
    print('  host: ' + out, flush=True)
    m = re.search(r'at \(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', out)
    if not ck(bool(m), 'the host formed an NPC tribe'):
        return ck.result()
    tp = tuple(float(m.group(i)) for i in (1, 2, 3))
    time.sleep(3)
    new = sorted((dist(p, tp), h, p) for h, p in huts(host) if h not in before)
    if not ck(bool(new), f'the new tribe has a hut ({len(new)})'):
        return ck.result()
    _, _, npc_p = new[0]
    t0 = time.time()
    while time.time() - t0 < 30 and not near(planet(guest, 'hut'), npc_p, 10):
        time.sleep(2)
    ck(near(planet(guest, 'hut'), npc_p, 10), "the guest's game has the new village's hut")
    gown = hut_near(guest, own_p, 10)
    print(f"  the guest's players' hut before: {gown} {near(planet(guest, 'hut'), own_p, 10)}", flush=True)
    print('  host, the village: ' + host.call('coop.tribe hutfall %.2f %.2f %.2f' % npc_p)['out'].strip(), flush=True)
    print('  host, the players\' hut: ' + host.call('coop.tribe hutfall %.2f %.2f %.2f' % own_p)['out'].strip(), flush=True)
    hrow = near(planet(host, 'hut'), own_p, 10)
    if not ck(hrow and hrow[2] & 2, f"the players' hut is dead in the host's game ({hrow})"):
        ck.faults(host=host, guest=guest)
        return ck.result()
    t0, fell, moved, rows = time.time(), None, None, []
    while time.time() - t0 < 45:
        time.sleep(3)
        row = near(planet(guest, 'hut'), own_p, 10)
        cur = hut_near(guest, own_p, 10)
        busy = re.search(r'^busy: (\S+)', guest.call('coop.status')['out'], re.M)
        rows.append((round(time.time() - t0), row and row[1], row and row[2], cur and cur[0], busy and busy.group(1)))
        print(f'  {rows[-1][0]:3d} s: guest players\' hut {row} obj {cur and cur[0]} busy {rows[-1][4]}', flush=True)
        if row and row[2] & 2 and fell is None:
            fell = rows[-1][0]
        if cur and cur[0] != gown[0] and moved is None:
            moved = rows[-1][0]
    print('  guest: ' + guest.call('coop.realign')['out'].strip().splitlines()[0], flush=True)
    ck(fell is None, f"the guest's game never felled the players' own hut (fell at {fell} s)")
    ck(moved is None, f"the guest's players' hut is the same object: no reload of the guest's own save "
                      f"({gown[0]}; another at {moved} s)")
    keyframe('own-hut', "the players' hut standing on the guest's screen while the host's game has it down")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
