#!/usr/bin/env python3
"""Creations: a dropped creation package is closed, its file can be written again (BUG-0996).

Every creation a guest applies is written to a .package file in its co-op folder and registered with the game's
resource manager. The host's creation is also registered under its world key (world_<kind>_<id>.package); the
session end drops it (BUG-0055). A dropped package was only unregistered: the game's DatabasePackedFile stayed
allocated with the file open for the rest of the process, so the next session's package of the same creation could
not be written under its name and went to "<name>_2.package", "_3", ... (after 48: "cannot write the package").

1. Two games on the creature fixture; the guest has the host's creature (its world package registered).
2. The guest leaves and joins again, twice (the host's world comes again each time).
3. The guest's game: its `coop.creations` packages line: no package written under another name (`renamed=0`; the
   operating system refused the old file's name while the game kept it open), the dropped ones closed; the guest's
   game draws the host with the host's creature after the last join (the puppet's species is the host's bundle key).
SPORE_FIX_OFF=creations.package_close: renamed > 0 (FAIL).

  SPORE_LANE=6 python3 tools/tests/coop_creations_package_close.py
"""
import re
import time

import lane
from twoinst import Checks, start, wait_guest_in


def packages(con):
    m = re.search(r'packages: open=(\d+) closing=(\d+) closed=(\d+) kept=(\d+) renamed=(\d+)', con.call('coop.creations')['out'])
    return tuple(int(x) for x in m.groups()) if m else None


def host_key(guest):
    m = re.search(r"player 0 creature: ([0-9A-F]{8})", guest.call('coop.creations')['out'])
    return m.group(1) if m else None


def puppet_key(guest):
    m = re.search(r"remote 0 '[^']*' puppet=\S+ state=\S+ [^\n]*\n\s+puppet pos=[^\n]*species=([0-9A-F]{8})",
                  guest.call('coop.status')['out'])
    return m.group(1) if m else None


def main():
    host, guest = start('creature')
    ck = Checks()
    time.sleep(6)
    p0 = packages(guest)
    print(f'  guest packages (open, closing, closed, kept, renamed): {p0}', flush=True)
    if not ck(p0 and p0[0] >= 1, f"the guest registered the host's creation packages ({p0})"):
        return ck.result()
    for rnd in range(2):
        print(' ', guest.call('coop.leave')['out'].strip(), flush=True)
        time.sleep(4)
        print(f'  after leave {rnd + 1}: {packages(guest)}', flush=True)
        guest.call(lane.JOIN)
        ck(wait_guest_in(host, guest), f'the guest is back in the host\'s world (round {rnd + 1})')
        time.sleep(20)
        wait_guest_in(host, guest)
        time.sleep(4)
        print(f'  after join {rnd + 1}: {packages(guest)}', flush=True)
    p1 = packages(guest)
    hk = pk = None
    t0 = time.time()
    while time.time() - t0 < 40:
        hk, pk = host_key(guest), puppet_key(guest)
        if hk and pk == hk:
            break
        time.sleep(2)
    print(f'  guest: packages {p1}, host bundle {hk}, puppet {pk}', flush=True)
    print(' ', [ln for ln in guest.call('coop.creations')['out'].splitlines() if 'error' in ln or 'creations:' in ln][:2], flush=True)
    ck(p1 and p1[4] == 0, f'no package of the guest went to another name: the dropped ones were closed (renamed={p1 and p1[4]})')
    ck(p1 and p1[2] >= 1, f'the dropped packages were closed and released (closed={p1 and p1[2]}, kept={p1 and p1[3]})')
    ck(hk and pk == hk, f"the guest's game draws the host with the host's creature after the joins ({pk} / {hk})")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
