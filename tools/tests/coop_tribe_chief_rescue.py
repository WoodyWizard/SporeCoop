#!/usr/bin/env python3
"""Tribe: a chief saved by a partner's member keeps 30 % of its real maximum health (BUG-1052).

With the session setting tribe.chief_aura on (the default), a blow that would fell a player's chief next to a member of
another player is cut so the chief keeps 30 % of its maximum ("<A> saved <B>'s chief"). The maximum was read from the
citizen's +0x5D4, which holds 1.0 (BUG-0745): the saved chief kept 0.3 hit points and fell at the next blow.

1. Two games on the tribe fixture; every player has a chief (`coop.tribe`: chiefs=).
2. The host puts the guest's chief next to its own (`coop.tribe near 1.5`: well inside the 15 m of the rescue).
3. A member of another tribe deals the host's chief a blow 5 over its health through the game's TakeDamage path
   (`coop.tribe pincer <chief> <attacker> <dmg>`, the same path as a real blow, with our hooks).
4. The host's game: the chief's health (+0x5E0) is 30 % of its maximum (game::max_health, the `maxhp=` of its row);
   the guest's game shows its mirror of that chief with that health within 5 s (the health of the guest's own copy).
SPORE_FIX_OFF=tribe.rescue_max: the chief keeps 0.3 (FAIL).

  SPORE_LANE=6 python3 tools/tests/coop_tribe_chief_rescue.py
"""
import re
import time

from coop_tribe import members
from twoinst import Checks, start


def chiefs(con):
    """owner -> member id on this game"""
    m = re.search(r'chiefs=([^\n]*?)(?: spread=\d+.*)?\n', con.call('coop.tribe')['out'])
    return dict((int(a), int(b)) for a, b in re.findall(r'(\d+):#(\d+)', m.group(1))) if m else {}


def row(con, mid):
    """(hp, maxhp) of member mid on this game, (None, None) when not listed"""
    m = re.search(rf'\n\s+#{mid} \S+ owner=-?\d+ .*? hp=([-\d.]+) .*? maxhp=([-\d.]+)', con.call('coop.tribe')['out'])
    return (float(m.group(1)), float(m.group(2))) if m else (None, None)


def rescues(con):
    m = re.search(r'chief_rescues=(\d+)', con.call('coop.status')['out'])
    return int(m.group(1)) if m else -1


def main():
    host, guest = start('tribe')
    ck = Checks()
    t0 = time.time()
    hc = {}
    while time.time() - t0 < 90:  # the split of the player tribe arrives with the first states
        hc = chiefs(host)
        if len(hc) >= 2:
            break
        time.sleep(3)
    print(f'  chiefs on the host: {hc}', flush=True)
    if not ck(0 in hc and len(hc) >= 2, 'the host and the guest have a chief'):
        return ck.result()
    enemy = [i for i, v in members(host).items() if v[3] != 'player']
    if not ck(enemy, f'a member of another tribe ({enemy[:4]})'):
        return ck.result()
    print('  host:', host.call('coop.tribe near 1.5')['out'].strip(), flush=True)
    time.sleep(2)
    chief = hc[0]
    hp, mx = row(host, chief)
    r0 = rescues(host)
    print(f'  host chief #{chief}: hp {hp} of {mx}, rescues {r0}', flush=True)
    if not ck(hp and mx and hp > 1 and mx > 1, f'the host\'s chief has its health ({hp} of {mx})'):
        return ck.result()
    out = host.call(f'coop.tribe pincer {chief} {enemy[0]} {hp + 5:.1f}')['out'].strip()
    print(f'  blow of {hp + 5:.1f} on #{chief}: {out}', flush=True)
    m = re.search(r'hp ([\d.]+) -> ([\d.]+)', out)
    after = float(m.group(2)) if m else None
    r1 = rescues(host)
    print(f'  rescues {r0} -> {r1}', flush=True)
    ck(r1 == r0 + 1, f'the blow was a rescue by the partner\'s member ({r0} -> {r1})')
    want = 0.3 * mx
    ck(after is not None and abs(after - want) <= max(0.5, 0.05 * mx),
       f'the saved chief keeps 30 % of its maximum in the host\'s game ({after} of {mx}, want {want:.1f})')
    g = None
    t1 = time.time()
    while time.time() - t1 < 5:
        g, _ = row(guest, chief)
        if g is not None and after is not None and abs(g - after) <= max(0.5, 0.05 * mx):
            break
        time.sleep(0.5)
    ck(g is not None and after is not None and abs(g - after) <= max(0.5, 0.05 * mx),
       f'the guest\'s game shows the saved chief with that health ({g})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
