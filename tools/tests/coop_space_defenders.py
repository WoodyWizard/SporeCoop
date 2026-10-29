#!/usr/bin/env python3
"""Space: the game's own Defenders are not taken for the partners' puppets (BUG-0111).

The mod shows a partner as a Defender UFO (UfoType 6); the game makes Defenders too (an NPC
empire's system, a colony's defence). Any type 6 counted as "our puppet":
1. the guest kept its own game's Defenders in the host's place while the host's came as mirrors:
   each defender twice. Here the guest's game spawns a Defender of its empire next to it
   (`coop.fleet spawn 6 own`, SpawnUFO as the game's colony defence does); the guest must remove
   it like its other NPC ships, and both games show as many Defenders in that place;
2. the puppet's identity is its object serial (BUG-0218):
   (a) `coop.fleet impostor 1` clears the invincible byte of the guest's puppet on the host (as a
       game change would): it stays the guest's puppet, invincible again, and the guest gets no
       mirror of it (before BUG-0218 it was forgotten and left as an armed NPC Defender);
   (c) `coop.fleet impostor_mission 1` makes it look held by a game mission (owner mission): a new
       puppet, the old ship is retired (not freed, not sent to the guest) and destroyed once the
       mission lets go;
   (b) `coop.fleet impostor_id 1`: another object at the puppet's address (BUG-0111): the host
       leaves that ship alone and shows the guest with a new puppet.

  SPORE_LANE=6 python3 tools/tests/coop_space_defenders.py [--without-fix]
  (--without-fix: `typefix 0`, part 1 fails; SPORE_FIX_OFF=fleet.puppet_id: (a) and (c) fail)
"""
import re
import sys
import time

from twoinst import Checks, start


def fleet(con):
    """{pointer: (id, type)} of the game's UFO list"""
    out = con.call('coop.fleet list')['out']
    return {m.group(2): (int(m.group(1)), int(m.group(3)))
            for m in re.finditer(r'ship #(-?\d+) (\S+) type=(\d+) pol=\w+ hp=', out)}


def main():
    without = '--without-fix' in sys.argv
    host, guest = start('space')
    ck = Checks()
    time.sleep(3)
    if without:
        for c in (host, guest):
            print(' ', c.call('coop.fleet typefix 0')['out'].strip())

    # ---- 1. a Defender of the guest's own game in the host's place ----
    r = guest.call('coop.fleet spawn 6 own')['out'].strip()
    print('  guest:', r)
    m = re.search(r'spawned (\S+) type=6', r)
    ck(m, 'the guest\'s game made a Defender')
    print('  host:', host.call('coop.fleet spawn 6 own')['out'].strip())
    time.sleep(5)
    hf, gf = fleet(host), fleet(guest)
    h6 = sorted(p for p, (i, t) in hf.items() if t == 6)
    g6 = sorted(p for p, (i, t) in gf.items() if t == 6)
    print(f'  type 6 ships: host {len(h6)} {h6}, guest {len(g6)} {g6}')
    if m:
        ck(m.group(1) not in gf, f'the guest removed its own game\'s Defender {m.group(1)} (the host\'s are mirrored)')
    ck(len(g6) == len(h6), f'both games show as many Defenders here (host {len(h6)}, guest {len(g6)})')

    # ---- 2. the puppet's identity (BUG-0111, BUG-0218) ----
    def puppet(con):
        r = con.call('coop.fleet puppet 1')['out'].strip()
        m = re.search(r'puppet (\S+) type=6 .* invincible=(\d)', r)
        return (m.group(1), int(m.group(2))) if m else (None, None)

    def guest_mirrors6():
        return sorted(p for p, (i, t) in fleet(guest).items() if t == 6 and i >= 0)

    # (a) our puppet's invincible byte cleared (as a game change would): it stays our puppet
    q, _ = puppet(host)
    ck(q, 'the host shows the guest as a Defender puppet')
    m0 = guest_mirrors6()
    print('  host:', host.call('coop.fleet impostor 1')['out'].strip())
    time.sleep(3)
    q1, inv = puppet(host)
    m1 = guest_mirrors6()
    print(f'  (a) puppet {q} -> {q1} invincible={inv}; guest type 6 mirrors {m0} -> {m1}; '
          + host.call('coop.fleet ident')['out'].strip())
    ck(q1 == q and inv == 1, f'(a) the same ship stays the guest\'s puppet and is invincible again ({q} -> {q1}, {inv})')
    ck(len(m1) == len(m0), f'(a) the guest gets no mirror of its own old ship ({len(m0)} -> {len(m1)} type 6 mirrors)')

    # (c) a game mission takes our ship: retired, not sent, destroyed once the mission lets go
    q, _ = puppet(host)
    m0 = guest_mirrors6()
    print('  host:', host.call('coop.fleet impostor_mission 1')['out'].strip())
    time.sleep(3)
    q1, _ = puppet(host)
    m1 = guest_mirrors6()
    print(f'  (c) puppet {q} -> {q1}; guest type 6 mirrors {m0} -> {m1}; ' + host.call('coop.fleet ident')['out'].strip())
    ck(q1 and q1 != q, f'(c) the host shows the guest with a new puppet ({q} -> {q1})')
    ck(q in fleet(host), '(c) the mission\'s ship is not freed while the mission holds it')
    ck(len(m1) == len(m0), f'(c) the retired ship is not sent to the guest ({len(m0)} -> {len(m1)} type 6 mirrors)')
    if q:
        print('  host:', host.call(f'coop.fleet impostor_mission 1 0 {q}')['out'].strip())
        t0 = time.time()
        while q in fleet(host) and time.time() - t0 < 3:
            time.sleep(0.2)
        ck(q not in fleet(host), f'(c) the mission let go: the old ship is destroyed ({time.time() - t0:.1f} s)')

    # (b) another object at the puppet's address (the recorded serial differs): left to the game
    q, _ = puppet(host)
    print('  host:', host.call('coop.fleet impostor_id 1')['out'].strip())
    time.sleep(3)
    q1, _ = puppet(host)
    hf = fleet(host)
    ck(q1 and q1 != q, f'(b) the host shows the guest with a new puppet ({q} -> {q1})')
    ck(q in hf, f'(b) the ship at the old address is left to the game, not destroyed ({q} listed: {q in hf})')
    ck.faults(host=host, guest=guest)
    if without:
        for c in (host, guest):
            c.call('coop.fleet typefix 1')
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
