#!/usr/bin/env python3
"""BUG-0788 probe: does a Creature puppet keep a desired speed set on it (SetDesiredSpeed, spatial vt 0xC4)?

`coop.puppet speedset V` sets V on the host's puppet of the guest and reads cLocomotiveObject +0x1E0 right after it (the
same frame); `coop.puppet speed` reads it again on the next frames. Printed: what stays, for the standing puppet and
while it walks (the guest's creature walking with W, drive::walk_place).

  SPORE_LANE=7 python3 tools/tests/coop_puppet_speed_probe.py
"""
import re
import time

from twoinst import Checks, start


def main():
    host, guest = start('creature')
    ck = Checks()
    time.sleep(4)
    rows = []
    for v in (7.0, 2.5, 9.0):
        a = host.call(f'coop.puppet speedset {v}')['out'].strip()
        later = []
        for _ in range(6):
            time.sleep(0.04)
            m = re.search(r'desired=([-\d.]+) standard=([-\d.]+)', host.call('coop.puppet speed')['out'])
            if m:
                later.append(float(m.group(1)))
        print(f'  set {v}: {a} | then {later}', flush=True)
        m = re.search(r'-> ([-\d.]+)', a)
        rows.append((v, float(m.group(1)) if m else None, later))
    ck(rows and all(r[1] is not None and abs(r[1] - r[0]) < 0.01 for r in rows), 'SetDesiredSpeed writes +0x1E0 at once')
    kept = [r for r in rows if r[2] and abs(r[2][-1] - r[0]) < 0.01]
    print(f'  kept on later frames: {len(kept)} of {len(rows)}', flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
