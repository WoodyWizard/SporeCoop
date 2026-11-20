#!/usr/bin/env python3
"""BUG-0128, the DNA half: the host sent its DNA total only when it changed, and a guest without
an avatar (loading, in an editor) dropped it. A guest whose total differs from what the host sends it (a total
it missed: `coop.dna local <n>` changes the guest's own only) gets it again within 4 s although nothing changed.
The food half is in coop_cell_sync.py.

What the host sends the guest: with the shared pool (--shared: all.shared_dna on, suite dna_resend_shared) the host's
own total; with own DNA (the 0.7.16 default) the guest's own wallet, which may differ from the host's DNA (a warm lane's
guest keeps its wallet over the host's reload, lab 20261120-041304: guest 14.9, host 0.0, BUG-1340). Own DNA: the guest's
game settles first (its total stays the same for 3 s: the host's resends every 2 s included) and must come back to
that total; the host's record of the wallet (`coop.dna bar` wallet line) is printed for the log only.

SPORE_CELL_FIX=0: `coop.pool fix resend 0` on the host (the old behaviour, the run that must FAIL).
"""
import os
import re
import time

import dna_mode
from harness import dna
from twoinst import Checks, start


def settled(con, secs=12.0, hold=3.0):
    """the game's DNA once it stayed the same for `hold` s (None: it kept moving)"""
    t0, since, last = time.time(), None, None
    while time.time() - t0 < secs:
        now = dna(con)
        if now is not None and now == last:
            since = since or time.time()
            if time.time() - since >= hold:
                return now
        else:
            since, last = None, now
        time.sleep(0.25)
    return None


def main():
    fix = os.environ.get('SPORE_CELL_FIX', '1')
    host, guest = start('creature')
    ck = Checks()
    try:
        shared = dna_mode.setup(host, guest, ck)
        out = host.call(f'coop.pool fix resend {fix}')['out']
        ck(f'resend={fix}' in out, f'resend fix set to {fix}: {out.strip()}')
        time.sleep(3)
        h0, g0 = dna(host), settled(guest)
        wallet = re.search(r"wallet 1 '[^']*': reads_own=\d creature=([^ ]+)", host.call('coop.dna bar')['out'] or '')
        print(f'  host {h0}, guest {g0}, the host\'s record of the guest\'s wallet: {wallet.group(1) if wallet else "-"}',
              flush=True)
        want = h0 if shared else g0
        if shared:
            ck(h0 is not None and h0 == g0, f'the guest has the host DNA total ({g0}, host {h0})')
        else:
            ck(g0 is not None, f'the guest\'s own DNA total is steady ({g0}; host {h0})')
        guest.call('coop.dna local 7')
        time.sleep(0.3)
        print(f'  guest after a missed total: {dna(guest)}')
        same = None
        for _ in range(16):
            time.sleep(0.25)
            if want is not None and dna(guest) == want:
                same = True
                break
        ck(dna(host) == h0, f'the host total did not change meanwhile ({h0} -> {dna(host)})')
        ck(same, f'BUG-0128: the guest gets its DNA total again without a change (guest {dna(guest)}, want {want}, '
                 f'host {dna(host)}, {"one pool" if shared else "own DNA"})')
    finally:
        host.call('coop.pool fix resend 1')
        dna_mode.teardown(host, guest)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
