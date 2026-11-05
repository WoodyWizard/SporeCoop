#!/usr/bin/env python3
"""BUG-0570: the guest sees the red ring under the enemy its members attack (player report 0.7.5: "I don't see the
red circle under an enemy when I target it", Tribe).

The ring is the Tribe HUD's (TribeTargetRings_Update 0xCCC7C0): every frame a ring under the combat target (+0xE7C)
of each selected player citizen, a map of live rings keyed by the target (docs/re/tribe.md#tribe-target-ring).
+0xE7C is set by the citizen's AI; a guest's members are mirrors without AI and its order goes to the host.

1. Two games on the tribe fixture, 12 more members; the guest selects the members it owns.
2. The guest orders an attack on a member of another tribe (`coop.tribe act 7 <id>`: the game's
   DoActionForObjects, which the mod sends to the host).
3. Within 3 s the guest's own HUD holds a live ring under that enemy (`coop.tribe ring <id>`: the ring map of the
   game's HUD, an effect in it) while its members stay selected; deselected, the ring goes within 2 s.

  coop_tribe_target_ring.py   (SPORE_FIX_OFF=tribe.target_ring: no ring on the guest, FAILS)"""
import re
import time

from twoinst import Checks, start


def members(con):
    """[(id, owner)] of the player tribe, [enemy ids]"""
    own, enemy, player = [], [], False
    for line in con.call('coop.tribe')['out'].splitlines():
        m = re.match(r'tribe (\d+)', line)
        if m:
            player = 'player' in line
            continue
        m = re.match(r'\s+#(\d+) \S+ owner=(-?\d+) age=\d+ sel=\d+ hp=([\d.]+)', line)
        if m and m.group(1) != '0' and float(m.group(3)) > 0:
            (own.append((int(m.group(1)), int(m.group(2)))) if player else enemy.append(int(m.group(1))))
    return own, enemy


def ring(con, i):
    m = re.search(r'ring=(\d)', con.call(f'coop.tribe ring {i}')['out'])
    return m and m.group(1) == '1'


def main():
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(5)
    print(' ', host.call('coop.tribe spawn 12')['out'].strip())
    time.sleep(5)
    gid = re.search(r'client id=(\d+)', guest.call('coop.status')['out'])
    gid = int(gid.group(1)) if gid else 1
    own, enemy = members(guest)
    mine = [i for i, o in own if o == gid]
    ck(mine and enemy, f'the guest owns members ({mine}) and another tribe has members ({enemy[:5]})')
    if not (mine and enemy):
        return ck.result()
    for i in mine:
        guest.call(f'coop.tribe select {i} 1')
    target = enemy[0]
    print('  ' + guest.call(f'coop.tribe act 7 {target}')['out'].strip(), flush=True)
    shown = 0
    t0 = time.time()
    while time.time() - t0 < 3:
        shown += bool(ring(guest, target))
        time.sleep(0.25)
    print('  guest ' + guest.call(f'coop.tribe ring {target}')['out'].strip(), flush=True)
    ck(shown >= 4, f'the guest\'s HUD shows the ring under the enemy #{target} it attacks ({shown} of 12 samples)')
    for i in mine:
        guest.call(f'coop.tribe select {i} 0')
    time.sleep(2)
    ck(not ring(guest, target), 'deselected: the ring is gone')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
