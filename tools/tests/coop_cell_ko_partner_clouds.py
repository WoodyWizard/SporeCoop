#!/usr/bin/env python3
"""Cell: a partner's poison clouds go on while the HOST is knocked out (BUG-1508, CELL-6 of the external audit).

The host's game makes a guest's clouds (puppet_poison lends the guest's puppet the avatar index for the game's poison
step, BUG-0716). The knocked-out check of BUG-0799 asked `maker == avatar()`, which inside that step is the puppet:
while the host was knocked out, every live partner's cloud was refused (ko_clouds_blocked).

The guest's puppet gets the poison part in the host's game (`coop.pool borrow poison graft p<id>`, as
coop_cell_ko_poison.py --who guest); the guest is invulnerable 30 m from the host. Control: the host's game makes
clouds of the guest's cell (the game's cloud maker 0xE76660 for the puppet: `puppet_clouds`, and new poison cells next
to the puppet). Then the host is knocked out (`coop.pool selfhit 50`): for the 8 s it is out the host's game still makes
the guest's clouds (puppet_clouds grows, new poison cells by the puppet).
SPORE_FIX_OFF=cell.ko_partner_clouds must FAIL (no cloud of the guest while the host is out).

  SPORE_LANE=3 python3 tools/tests/coop_cell_ko_partner_clouds.py
"""
import re
import time

from coop_cell_ko_food import wait_downed
from coop_cell_ko_poison import clouds, new_clouds
from coop_cell_puppet_scale import playing, wait
from twoinst import Checks, keyframe, start


def main():
    host, guest = start('cell')
    ck = Checks()
    ck(wait(lambda: playing(host) and playing(guest), 40), 'both games play (the hatch is over)')
    gid = (re.search(r'client id=(\d+)', guest.call('coop.status')['out']) or [None, '1'])[1]
    guest.call('coop.pool tp 30 0')
    guest.call('coop.pool invulnfor 120')
    got = host.call(f'coop.pool borrow poison graft p{gid}')['out'].strip()
    print('  host: ' + got, flush=True)
    if not ck(got.startswith('borrowed'), 'the guest\'s puppet has the poison part in the host\'s game'):
        return ck.result()

    def made():
        m = re.search(r'puppet_clouds=(\d+)', host.call(f'coop.pool borrow status x p{gid}')['out'])
        return int(m.group(1)) if m else None
    try:
        seen = clouds(host)
        m0 = made()
        ctl = new_clouds(host, seen, 12, gid)
        m1 = made()
        print(f'  control: {len(ctl)} new clouds by the guest\'s puppet, the game made {m0} -> {m1}', flush=True)
        ck(m0 is not None and m1 is not None and m1 > m0, f'control: the host\'s game makes the guest\'s clouds ({m0} -> {m1})')
        print('  host: ' + host.call('coop.pool selfhit 50')['out'].strip(), flush=True)
        down = wait_downed(host, 1)
        ck(down, 'the host is knocked out')
        m2 = made()
        during = new_clouds(host, seen, 8.0, gid)
        m3 = made()
        still = wait_downed(host, 1, 0.5)
        print(f'  while the host is out: the game made {m2} -> {m3} clouds of the guest, {len(during)} new by its puppet; '
              f'still out {still}', flush=True)
        print('  host: ' + ' '.join(x for x in host.call('coop.pool')['out'].splitlines()
                                    if x.startswith(('cell_c1', 'cell_audit'))), flush=True)
        keyframe('ko_clouds', 'host: its own cell knocked out; the guest\'s cell (30 m away) still puffs poison clouds')
        ck(down and still, 'the host stayed knocked out while it was watched')
        ck(m2 is not None and m3 is not None and m3 > m2,
           f'the guest\'s clouds go on while the host is knocked out ({m2} -> {m3} in 8 s)')
    finally:
        host.call(f'coop.pool borrow off x p{gid}')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
