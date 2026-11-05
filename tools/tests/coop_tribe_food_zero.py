#!/usr/bin/env python3
"""Tribe: the guest's food follows the host's after the guest's game spent its last food (BUG-0572).

Player report (0.7.5): the guest's food counter stayed 0 for good while the host's tribe had food. Its log:
the two games showed the same food until the guest's game spent its last 10 (a purchase, 19:15:48) and
again after the rejoin when its members ate the last of it (-1, -1, -1, -0.6 at 19:56); from then on the
guest had 0 and the host 15 .. 91. A drop to 0 that came from the host (19:53) did not stick.

The game's own spending (cTribe::AddFood with a negative delta -> cBundleManager::RemoveBundles) removes
the emptied food bundles from the tribe's container (tribe+0x20C), and shared.cpp s_food only edits the
amounts of existing bundles: with none left it returns without a change (docs/re/tribe.md#tribe-food-zero).

1. Two games on the tribe fixture; the guest's game spends all its food the game's way
   (`call SporeCoop::cTribe_AddFood thiscall <player tribe> -<food>`). Both games show 0.
2. The host's tribe gets 30 food (`coop.res add food 30`). PASS: within 10 s the guest's game holds the
   host's total (the food bundles of its own tribe, what its HUD shows), and still 20 s later.

  SPORE_LANE=4 python3 tools/tests/coop_tribe_food_zero.py
"""
import re
import time

from twoinst import Checks, start


def food(con):
    m = re.search(r'shared food=(-?[\d.]+)', con.call('coop.res')['out'])
    return float(m.group(1)) if m else None


def bundles(con, pt):
    """the number of food bundles in the player tribe's container (vector begin/end at tribe+0x210)"""
    out = con.call(f'mem.read 0x{pt + 0x210:X} 8')['out'].split()
    if len(out) < 9:
        return None
    b = int(''.join(reversed(out[1:5])), 16)
    e = int(''.join(reversed(out[5:9])), 16)
    return (e - b) // 4


def main():
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(5)
    m = re.search(r'player_tribe=([0-9A-F]+)', guest.call('coop.tribe')['out'])
    ck(m is not None, 'the guest\'s game has a player tribe')
    if not m:
        return ck.result()
    pt = int(m.group(1), 16)
    f0 = food(guest)
    print(f'  food host {food(host)} guest {f0}, guest bundles {bundles(guest, pt)}', flush=True)
    ck(f0 is not None and f0 >= 1, f'the guest has food to spend ({f0})')
    if not f0:
        return ck.result()
    print('  guest spends: ' + guest.call(f'call SporeCoop::cTribe_AddFood thiscall 0x{pt:X} -{int(f0)}')['out'].strip()
          .splitlines()[0], flush=True)
    time.sleep(4)
    hf, gf, nb = food(host), food(guest), bundles(guest, pt)
    print(f'  after the spend: host {hf} guest {gf}, guest bundles {nb}', flush=True)
    ck(hf == 0 and gf == 0, f'both games show 0 food after the guest spent it (host {hf}, guest {gf})')
    print('  host: ' + host.call('coop.res add food 30')['out'].strip(), flush=True)
    seen = []
    t0 = time.time()
    while time.time() - t0 < 10:
        time.sleep(1)
        seen.append((food(host), food(guest)))
        if seen[-1][1] == seen[-1][0] and seen[-1][0]:
            break
    print(f'  host / guest food: {seen}', flush=True)
    ck(seen[-1][0] >= 25 and abs(seen[-1][1] - seen[-1][0]) < 1,
       f'the guest\'s game holds the host\'s food within 10 s (host {seen[-1][0]}, guest {seen[-1][1]})')
    time.sleep(20)
    hf, gf = food(host), food(guest)
    print(f'  20 s later: host {hf} guest {gf}, guest bundles {bundles(guest, pt)}', flush=True)
    ck(hf and abs(gf - hf) < 1, f'and still 20 s later (host {hf}, guest {gf})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
