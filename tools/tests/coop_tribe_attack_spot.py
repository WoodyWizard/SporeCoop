#!/usr/bin/env python3
"""Tribe: the host's own creature attack (Drama Manager event kind 7) while its player tribe is knocked out does
not fault every frame (BUG-0582).

Lab (fix/w43-realign, tribe_bounce_world): about 15 s after the host's creature attack started, its game faulted at
0xD72745 every frame (258-1082 caught faults) in the attackers' behaviour (0xD73FD0 state 3): the spot record of
the attacked place (a global table keyed by a planet cell, 0xC945C0 returns null for an unknown cell) is read
without a check in 0xD72720 (its sibling 0xD72690 checks it). docs/re/tribe.md#tribe-attack-spot.

1. Tribe fixture. The host forms an NPC tribe (--no-form: not) (`coop.tribe form`), starts its creature attack event the game's own
   way (`coop.drama start 7`), and its player tribe is knocked out (`coop.tribe pincer`, as tribe_bounce_world).
2. --cut: the next spot lookup of an attacker returns no record (`coop.spotcut`: the state the host
   faulted in, made on purpose).
PASS: no caught fault on the host or the guest, the host's attackers that met the missing spot ended their attack
(the game's own "no free spot" branch), the host's game keeps drawing.

  SPORE_LANE=2 python3 tools/tests/coop_tribe_attack_spot.py [--cut]   (SPORE_FIX_OFF=tribe.attack_spot fails --cut)
"""
import re
import sys
import time

from coop_tribe_gameover import frames_go_on, player_members
from twoinst import Checks, start


def faults(con):
    m = re.search(r'total faults: (\d+)', con.call('exc')['out'])
    return int(m.group(1)) if m else -1


def spot_stats(con):
    m = re.search(r'attack spots missing (\d+) \(test cuts (\d+)', con.call("coop.status")['out'])
    return (int(m.group(1)), int(m.group(2))) if m else None


def main():
    cut = '--cut' in sys.argv
    pincer = '--pincer' in sys.argv  # the host's player tribe knocked out meanwhile (as tribe_bounce_world)
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(6)
    if '--no-form' not in sys.argv:
        print('  host: ' + host.call('coop.tribe form')['out'].strip(), flush=True)
    if cut:
        print('  host: ' + host.call('coop.spotcut')['out'].strip(), flush=True)
    print('  host: ' + host.call('coop.drama start 7')['out'].strip().replace('\n', ' | '), flush=True)
    own, enemy = player_members(host)
    ck(own and enemy, 'the player tribe and another tribe have members')
    t0, f = time.time(), 0
    while time.time() - t0 < 150:
        own, enemy = player_members(host)
        alive = [i for i, hp in own.items() if hp > 0]
        if pincer and alive and enemy:
            for i in alive:
                host.call(f'coop.tribe pincer {i} {enemy[0]} 300')
        f = faults(host)
        st = spot_stats(host)
        if int(time.time() - t0) % 10 < 2:
            print(f'  {time.time() - t0:.0f} s: host faults {f}, spots {st}, alive {len(alive)}', flush=True)
        if f > 0 or (st and st[0] > 0 and (not cut or st[1] > 0)):
            break
        time.sleep(1.5)
    st = spot_stats(host)
    print('  host: ' + host.call('coop.drama')['out'].strip().replace('\n', ' | '), flush=True)
    print(f'  host spot guard: {st}', flush=True)
    if cut:
        ck(st is not None and st[1] > 0, f'the test cut was used by an attacker ({st})')
    ck(st is not None and st[0] > 0, f'an attacker met the missing spot and ended its attack (the scenario happened: {st})')
    ck(frames_go_on(host, 5), 'the host keeps drawing')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
