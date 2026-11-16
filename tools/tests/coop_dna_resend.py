#!/usr/bin/env python3
"""BUG-0128, the DNA half: the host sent its DNA total only when it changed, and a guest without
an avatar (loading, in an editor) dropped it. A guest whose total differs from the host's (a total
it missed: `coop.dna local <n>` changes the guest's own only) gets the host's within 4 s although the
host's total does not change. The food half is in coop_cell_sync.py.

SPORE_CELL_FIX=0: `coop.pool fix resend 0` on the host (the old behaviour, the run that must FAIL).
"""
import os
import time

from harness import dna
from twoinst import Checks, start


def main():
    fix = os.environ.get('SPORE_CELL_FIX', '1')
    host, guest = start('creature')
    ck = Checks()
    try:
        out = host.call(f'coop.pool fix resend {fix}')['out']
        ck(f'resend={fix}' in out, f'resend fix set to {fix}: {out.strip()}')
        time.sleep(3)
        h0, g0 = dna(host), dna(guest)
        ck(h0 is not None and h0 == g0, f'the guest has the host DNA total ({g0}, host {h0})')
        guest.call('coop.dna local 7')
        time.sleep(0.3)
        print(f'  guest after a missed total: {dna(guest)}')
        same = None
        for _ in range(16):
            time.sleep(0.25)
            if dna(guest) == dna(host):
                same = True
                break
        ck(dna(host) == h0, f'the host total did not change meanwhile ({h0} -> {dna(host)})')
        ck(same, f'BUG-0128: the guest gets the host DNA total again without a change (guest {dna(guest)}, host {dna(host)})')
    finally:
        host.call('coop.pool fix resend 1')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
