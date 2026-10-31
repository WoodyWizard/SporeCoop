#!/usr/bin/env python3
"""BUG-0344: the host's choices.ini holds an own creature chosen in another world (an earlier
session). Loading this world and hosting must leave the host's avatar on the world's species, in the
host's game and on the guest; a choice made now, in this world, still swaps it (the feature).

SPORE_FIX_OFF=creations.host_world_species: the host plays the old creature (the run that must FAIL)."""
import re
import time

import twoinst
from twoinst import Checks, start

OLD = '290229B9'  # Tiger Horse of the host fixture's My Creations
WORLD = '065CBE70'  # the creature fixture's species


def key(con):
    m = re.search(r'key=0x([0-9A-F]{8})', con.call('coop.avatar')['out'])
    return m and m.group(1)


def puppet(con):
    m = re.search(r"remote 0 '[^']*'.*\n.*species=([0-9A-F]{8})", con.call('coop.status')['out'])
    return m and m.group(1)


def main():
    twoinst.PRESET_CHOICES = {'host': {'creature': OLD}}
    host, guest = start('creature')
    ck = Checks()
    time.sleep(12)  # the swap comes 3 s after the avatar is seen
    hk, gp = key(host), puppet(guest)
    print(f'  host avatar {hk}, the guest shows the host as {gp}')
    print('  host:', host.call('coop.creations')['out'].splitlines()[1].strip())
    ck(hk == WORLD, f'the host keeps the world species {WORLD} ({hk})')
    ck(hk != OLD and gp != OLD and gp is not None, f'the guest does not see the old creature ({gp})')
    # the feature: a choice made in this world swaps the host's avatar
    print(' ', host.call(f'coop.creations choose creature {OLD}')['out'].strip())
    t0 = time.time()
    while time.time() - t0 < 30 and key(host) != OLD:
        time.sleep(1)
    ck(key(host) == OLD, f'a choice made in this world still swaps the host ({key(host)})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
