#!/usr/bin/env python3
"""Creations: a dropped creation package is closed, its file can be written again (BUG-0996).

Every creation a game applies for another player is written to a .package file in its co-op folder
(p<owner>_<kind>_<bundle id>.package, the id a hash of the creation) and registered with the game's resource manager.
A newer creation of that player drops the older package. A dropped package was only unregistered: the game's
DatabasePackedFile stayed allocated with the file open for the rest of the process, so when the player came back to
that creation its package could not be written under its name and went to "<name>_2.package", "_3", ... (after 48:
"cannot write the package", the creation not shown).

1. Two games on the creature fixture; the guest's profile has two creatures of its own (A, B).
2. The guest plays A, B, A, B, A (`coop.creations choose creature <key>`), 10 s each: the host's game applies each
   one and drops the one before.
3. The host's game: its `coop.creations` packages line: no package written under another name (`renamed=0`: the
   operating system refused the old file's name while the game kept it open), the dropped ones closed; and the host's
   game draws the guest with the creation it plays (the puppet's species is the guest's bundle key there).
SPORE_FIX_OFF=creations.package_close: renamed > 0 (FAIL). `RESULT: SKIP` without two own creatures.

  SPORE_LANE=6 python3 tools/tests/coop_creations_package_close.py
"""
import re
import time

import creations_state
from twoinst import Checks, start


def packages(con):
    m = re.search(r'packages: open=(\d+) closing=(\d+) closed=(\d+) kept=(\d+) renamed=(\d+)', con.call('coop.creations')['out'])
    return tuple(int(x) for x in m.groups()) if m else None


def guest_key(host):
    m = re.search(r"player 1 creature: ([0-9A-F]{8})", host.call('coop.creations')['out'])
    return m.group(1) if m else None


def puppet_key(host):
    m = re.search(r"remote 1 '[^']*'.*\n.*species=([0-9A-F]{8})", host.call('coop.status')['out'])
    return m.group(1) if m else None


def main():
    host, guest = start('creature')
    ck = Checks()
    st0 = creations_state.save(guest)
    try:
        return run(host, guest, ck)
    finally:
        creations_state.restore(guest, st0)  # the profile as it was (BUG-0345)


def run(host, guest, ck):
    own = creations_state.own_keys(guest)
    print(f'  guest own creatures: {own[:4]}', flush=True)
    if len(own) < 2:
        print('RESULT: SKIP (the guest profile has fewer than two own creatures)', flush=True)
        return 0
    a, b = own[0], own[1]
    keys = []
    for k, key in enumerate((a, b, a, b, a)):
        print(' ', guest.call(f'coop.creations choose creature {key}')['out'].strip(), flush=True)
        t0, gk = time.time(), None
        while time.time() - t0 < 10:
            time.sleep(1)
            gk = guest_key(host)
            if gk and (not keys or gk != keys[-1]):
                break
        time.sleep(max(0.0, 10 - (time.time() - t0)))  # the dropped package's close comes 2 s after the drop
        keys.append(gk)
        print(f'  {k + 1}: host applies {gk}, packages (open, closing, closed, kept, renamed) {packages(host)}', flush=True)
    p1 = packages(host)
    pk = puppet_key(host)
    print(' ', [ln.strip() for ln in host.call('coop.creations')['out'].splitlines() if 'error=' in ln][:1], flush=True)
    ck(len(set(k for k in keys if k)) >= 2, f'the host applied both creations of the guest in turn ({keys})')
    ck(p1 and p1[4] == 0, f'no package went to another name: the dropped ones were closed (renamed={p1 and p1[4]})')
    ck(p1 and p1[2] >= 1, f'the dropped packages were closed and released (closed={p1 and p1[2]}, kept={p1 and p1[3]})')
    ck(pk and pk == keys[-1], f"the host's game draws the guest with the creation it plays ({pk} / {keys[-1]})")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
