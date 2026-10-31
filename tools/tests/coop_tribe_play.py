#!/usr/bin/env python3
"""Tribe co-op mechanics through the game's damage path (not a real raid): pincer (+50% when
members of two players hit the same tribe), chiefs (one per player, the same on both machines),
the epic creature at the village (spawned on demand, killed by both players: 80 food)."""
import re
import time

from coop_tribe import members
from twoinst import Checks, shot, start


def coop_line(con):
    m = re.search(r'tribe co-op: .*', con.call('coop.status')['out'])
    return m.group(0) if m else ''


def food(con):
    m = re.search(r'food[= ]+(-?[\d.]+)', con.call('coop.res')['out'])
    return float(m.group(1)) if m else None


def main():
    host, guest = start('tribe')
    ck = Checks()
    t0 = time.time()
    while True:  # the split of the player tribe arrives with the first states
        hm = members(host)
        if any(v[0] == 1 for v in hm.values()) or time.time() - t0 > 90:
            break
        time.sleep(3)
    time.sleep(3)
    host_m = [i for i, v in hm.items() if v[0] == 0 and v[3] == 'player']
    guest_m = [i for i, v in hm.items() if v[0] == 1 and v[3] == 'player']
    enemy = [i for i, v in hm.items() if v[3] != 'player']
    print('  host members', host_m, 'guest members', guest_m, 'others', enemy[:5])
    # chiefs: one per player, the same on both machines
    hl, gl = coop_line(host), coop_line(guest)
    hc, gc = re.search(r'chiefs=(.*)', hl), re.search(r'chiefs=(.*)', gl)
    print('  chiefs host:', hc and hc.group(1), '| guest:', gc and gc.group(1))
    ck(hc and hc.group(1).count('#') >= 2, 'every player has a chief')
    # (compare the chief tokens only: spread= / placed= after them are per machine, BUG-0340)
    def chief_set(m):
        return set(re.findall(r'\d+:#\d+', m.group(1))) if m else None
    ck(hc and gc and chief_set(hc) == chief_set(gc), 'the same chiefs on both machines')
    # pincer: the host's member hits, then the guest's member hits the same enemy tribe
    if enemy and host_m and guest_m:
        v = enemy[0]
        r1 = host.call(f'coop.tribe pincer {v} {host_m[-1]} 2')['out']
        r2 = host.call(f'coop.tribe pincer {v} {guest_m[-1]} 2')['out']
        print('  host hit:', r1, '| guest hit:', r2)
        d = [float(a) - float(b) for a, b in re.findall(r'hp ([\d.]+) -> ([\d.]+)', r1 + ' ' + r2)]
        # base 2: the second player's hit is a pincer (x1.5); a hitter near its chief also gets x1.2
        ck(len(d) == 2 and d[1] >= 2 * 1.5 - 0.01 and d[0] <= 2 * 1.2 + 0.01,
           f'two players hitting one tribe deal more damage ({d})')
        ck('pincer attack' in host.call('coop.status')['out'], 'everybody is told about the pincer attack')
    # the epic: spawned, both players hit it, 80 food
    f0 = food(host)
    print(' ', host.call('coop.tribe epic')['out'])
    time.sleep(3)
    ck('epic creature comes' in guest.call('coop.status')['out'], 'the guest is told about the epic')
    if host_m and guest_m:
        for _ in range(40):
            host.call(f'coop.tribe epic hit {host_m[-1]} 40')
            r = host.call(f'coop.tribe epic hit {guest_m[-1]} 40')['out']
            if 'no epic' in r or 'hp 0.00' in r:
                break
            time.sleep(0.2)
        time.sleep(2)
        f1 = food(host)
        print(f'  food {f0} -> {f1};', coop_line(host))
        ck('down: +80 food' in host.call('coop.status')['out'], 'killed together: +80 food')
    shot(host, 'tribe_play_host.png')
    shot(guest, 'tribe_play_guest.png')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
