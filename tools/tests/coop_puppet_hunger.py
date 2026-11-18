#!/usr/bin/env python3
"""BUG-1252 (players' video): a thought bubble with a gnawed bone hung over the guest's creature in the host's game
for a long time, while the guest's own creature was not hungry.

A partner's puppet is a creature of the game that shows it: the game's hunger update runs its food meter (+0xBBC)
down (it never eats there), and the per-creature UI update 0xE8BF00 shows the hunger bubble (pool 2, key 062EDBA6)
while its mood is hungry (docs/re/creature.md#creature-puppet-hunger). Creature fixture, the guest 4 m in front of
the host's camera.
  1. the puppet's own meter runs down in the host's game: set to 60 (`coop.puppet hunger 60`, as two minutes after
     its spawn leave it) it is read again 20 s later (the game's update ran it down, or the fix put it back to full);
  2. the meter as minutes of the game's update leave it (`coop.puppet hunger 10`): 6 s later the host's puppet must
     have no active hunger bubble (`coop.effects map`, key 062EDBA6) unless the guest's own avatar has one, and the
     host's puppet's meter must be full again; key frame of the host's screen.
SPORE_FIX_OFF=creature.puppet_full must FAIL (the bubble hangs over the puppet)."""
import re
import time

from coop_puppet_after_social import cur, pools
from harness import avatar_pos as pos
from twoinst import Checks, keyframe, shot, start

BUBBLE = '062EDBA6'  # the hunger bubble's pool key (0xE8BF00)


def hunger(con, who):
    m = re.search(rf'^cur {who} .*hunger=([-\d.]+)', con.call('coop.npc cur')['out'], re.M)
    return float(m.group(1)) if m else None


def main():
    host, guest = start('creature')
    ck = Checks()
    for c in (host, guest):
        c.call('coop.team hp 1000')
    gid = re.search(r'client id=(\d+)', guest.call('coop.status')['out'])
    gid = gid.group(1) if gid else '1'
    time.sleep(3)
    hp = pos(host)
    guest.call('coop.npc gopos %.2f %.2f %.2f' % (hp[0] + 4, hp[1], hp[2]))
    time.sleep(4)
    print(f'  puppet {cur(host, "p" + gid)}, hunger host puppet {hunger(host, "p" + gid)}, guest own {hunger(guest, "avatar")}', flush=True)

    # 1. the puppet's own meter in the host's game
    print('  host: ' + host.call('coop.puppet hunger 60')['out'].strip(), flush=True)
    time.sleep(20)
    h1 = hunger(host, 'p' + gid)
    print(f'  20 s later: host puppet hunger {h1}, guest own {hunger(guest, "avatar")}', flush=True)

    # 2. hungry as minutes of the game's update leave it
    print('  host: ' + host.call('coop.puppet hunger 10')['out'].strip(), flush=True)
    rows = []
    t0 = time.time()
    while time.time() - t0 < 8:
        hp_, gp = pools(host, gid), pools(guest, 'avatar')
        rows.append((round(time.time() - t0, 1), hunger(host, 'p' + gid), hp_.get(BUBBLE), gp.get(BUBBLE)))
        time.sleep(0.5)
    keyframe('bubble', 'the host\'s screen, the guest\'s creature 4 m ahead: NO thought bubble (gnawed bone) over it '
             '(the guest\'s own creature is not hungry)', settle=0)
    print(f'  (s, host puppet hunger, its bubble active, the guest avatar\'s bubble active) {rows}', flush=True)
    m = re.search(r'remote animations.*', host.call('coop.status')['out'])
    print('  host: ' + (m.group(0) if m else '?'), flush=True)
    late = rows[len(rows) // 2:]
    shown = [r for r in late if r[2] and not r[3]]
    ck(not shown, f'the host\'s puppet shows no hunger bubble the guest\'s own creature does not have ({shown[:3]})')
    ck(late and all(r[1] is not None and r[1] >= 99 for r in late),
       f'the host\'s puppet of the guest is full (its own meter does not run down there): {[r[1] for r in late]}')
    shot(host, 'puppet_hunger_host.png')
    shot(guest, 'puppet_hunger_guest.png')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
