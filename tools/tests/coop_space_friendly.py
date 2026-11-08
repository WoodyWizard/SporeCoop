#!/usr/bin/env python3
"""BUG-0420 (class: hits without a relation filter): in Space, shots between the players' empire and its own ships
(an escort of the host's) are not relayed as hits.

The host forwarded every NPC ship's hit on a guest's puppet to the guest (MSG_UFO_DAMAGE), the host's own escorts
too, and applied every guest shot on a mirror (MSG_UFO_HIT), on the host's escort too. Same star system (fixture
space), the host spawns an escort of its own empire (coop.fleet spawn 2 own) and a ship without an empire:

  1. the escort hits the guest's puppet (coop.fleet hurt): the guest's own ship takes nothing (its game hp, and no
     MSG_UFO_DAMAGE); control: the empire-less ship's hit reaches the guest
  2. the guest shoots the escort's mirror: the host's escort keeps its hp in the host's game; control: the guest's
     shot on the other ship is applied

  SPORE_FIX_OFF=space.friendly_hits coop_space_friendly.py   (relayed, and the game ignores them: PASSES, BUG-0420
  evidence: the game applies no damage between ships of one empire, lab 20261107-222158)
"""
import os
import re
import time

from twoinst import Checks, start


FIX = 'space.friendly_hits' not in os.environ.get('SPORE_FIX_OFF', '')


def ships(con):
    out = con.call('coop.fleet list')['out']
    d = {}
    for m in re.finditer(r'ship #(-?\d+) \S+ type=(\d+) pol=(\w+) hp=([\d.]+)/([\d.]+)( \(me\))?', out):
        d['me' if m.group(6) else int(m.group(1))] = (int(m.group(2)), m.group(3), float(m.group(4)))
    return d


def num(con, key):
    m = re.search(r'space\(\w+\):.*?\b' + key + r'=(\d+)', con.call('coop.status')['out'])
    return int(m.group(1)) if m else -1


def spawned_id(host, before):
    for _ in range(10):
        now = ships(host)
        new = [k for k in now if k != 'me' and k >= 0 and k not in before]
        if new:
            return new[0], now
        time.sleep(0.5)
    return None, ships(host)


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(3)
    b0 = ships(host)
    print(' ', host.call('coop.fleet spawn 2 own')['out'].strip())
    esc, b1 = spawned_id(host, b0)
    print(' ', host.call('coop.fleet spawn 2')['out'].strip())
    pir, b2 = spawned_id(host, b1)
    time.sleep(3)
    gs = ships(guest)
    print(f'  escort #{esc} {b2.get(esc)}, other #{pir} {b2.get(pir)}; host ship {b2.get("me")}')
    ck(esc is not None and pir is not None, 'the host has its escort and a ship without an empire')
    ck(esc in gs and pir in gs, f'the guest mirrors both ({esc in gs}, {pir in gs})')
    if esc is None or pir is None:
        ck.faults(host=host, guest=guest)
        return ck.result()
    # 1. hits on the guest's puppet
    d0, hp0 = num(guest, 'damage_in'), ships(guest)['me'][2]
    print(' ', host.call(f'coop.fleet hurt {esc} 1 20')['out'].strip())
    time.sleep(2)
    d1, hp1 = num(guest, 'damage_in'), ships(guest)['me'][2]
    print(f'  escort hit the guest: damage_in {d0} -> {d1}, guest ship hp {hp0} -> {hp1}')
    ck(hp1 >= hp0, f"the host's escort does not hurt the guest's ship in the guest's game (hp {hp0} -> {hp1})")
    ck(d1 == d0 or not FIX, f"with the fix the escort's hit is not even relayed (damage_in {d0} -> {d1})")
    print(' ', host.call(f'coop.fleet hurt {pir} 1 5')['out'].strip())
    time.sleep(2)
    d2 = num(guest, 'damage_in')
    ck(d2 > d1, f"control: the other ship's hit reaches the guest (damage_in {d1} -> {d2})")
    # 2. the guest's shots on mirrors
    h0, e0 = num(host, 'hits_in'), ships(host)[esc][2]
    print(' ', guest.call(f'coop.fleet shoot {esc} 20')['out'].strip())
    time.sleep(2)
    h1, e1 = num(host, 'hits_in'), ships(host).get(esc, (0, '', -1))[2]
    print(f"  guest shot the escort: host hits_in {h0} -> {h1}, escort hp {e0} -> {e1}")
    ck(e1 >= e0, f"the guest's shot does not hurt the host's escort in the host's game (hp {e0} -> {e1})")
    ck(h1 == h0 or not FIX, f"with the fix the guest's shot is not even relayed (hits_in {h0} -> {h1})")
    p0 = ships(host)[pir][2]
    print(' ', guest.call(f'coop.fleet shoot {pir} 5')['out'].strip())
    time.sleep(2)
    h2, p1 = num(host, 'hits_in'), ships(host).get(pir, (0, '', -1))[2]
    ck(h2 > h1 and p1 < p0, f"control: the guest's shot on the other ship is applied (hits_in {h1} -> {h2}, hp {p0} -> {p1})")
    print('  host:', ' '.join(x for x in host.call('coop.status')['out'].split() if x.startswith('friendly_dropped=')))
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
