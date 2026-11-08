#!/usr/bin/env python3
"""BUG-0754: the sync-check does not compare a guest while the host shares its world with it.

campaign_ct (lab 20261108-085348-2 #2 of fix/w46-k4-ground): the host saved for a world share to its three guests (a
792 ms hitch), the transfer ran, and a check of that moment reported `MISMATCH ... n: 4 apart (21-28 m)` on the host and
the guest; the guest replaced its world 1.3 s later. A guest that is about to load another world is not in sync with
anything, as a guest in another place (diag.stage_end, BUG-0588).

Two games on the creature fixture. The host's digest gets a planted difference (`coop.diag sync force shift`: its own
position 100 m off, inside the lab's `sync-force` mark), the host shares its world with the guest (`coop.share 1`) and
asks for a check at once (`coop.diag sync now`, once: it drops a pending recheck), checks every 2 s, for DURING s. Checks: no MISMATCH in that time (the
share is going on); after the share's quiet time the same planted difference is a MISMATCH again (the check still works).
The planted difference stands in for the campaign's moved NPCs: the test checks the rule (no compare while a share
goes on), it can not make the host's save move NPCs.

SPORE_FIX_OFF=diag.share_quiet: the check of the share's moment reports the planted difference, it FAILS.

  SPORE_LANE=6 python3 tools/tests/coop_share_sync.py
"""
import os
import time

import lane
from twoinst import Checks, lab_mark, start

DURING = 12  # s after the share is asked for (the save ~2 s, the transfer ~1 s, the guest's load ~8 s)


def log_size(inst):
    try:
        return os.path.getsize(lane.game_log(inst))
    except OSError:
        return 0


def log_since(inst, off):
    try:
        with open(lane.game_log(inst), 'rb') as f:
            f.seek(off)
            return f.read().decode('utf-8', 'replace').splitlines()
    except OSError:
        return []


def mismatches(lines):
    return [ln for ln in lines if 'sync-check: MISMATCH' in ln]


def main():
    host, guest = start('creature')
    ck = Checks()
    host.call('coop.diag sync 2')
    time.sleep(3)
    h0 = log_size(lane.HOST)
    lab_mark('sync-force', 'on')  # the lab's sync-check gate leaves these MISMATCH lines out
    host.call('coop.diag sync force shift')
    # as after an evolution (the save first, then the transfer and the guest's load: the campaign's path)
    print('  share: ' + (host.call('coop.share due')['out'].strip() or '-')[:160], flush=True)
    host.call('coop.diag sync now')  # once: `now` also drops a pending recheck (a MISMATCH needs 2 checks 3 s apart)
    time.sleep(DURING)
    during = mismatches(log_since(lane.HOST, h0))
    h1 = log_size(lane.HOST)
    ck(not during, f'no MISMATCH while the host shares its world with the guest ({len(during)}: '
                   f'{during[0].split("coop: ", 1)[-1][:160] if during else "none"})')
    # after the quiet time the planted difference is found again
    t1 = time.time()
    while time.time() - t1 < 40 and not mismatches(log_since(lane.HOST, h1)):
        time.sleep(1)
    host.call('coop.diag sync force off')
    time.sleep(4)
    lab_mark('sync-force', 'off')
    after = mismatches(log_since(lane.HOST, h1))
    ck(after, f'after the share the planted difference is a MISMATCH again ({after[0].split("coop: ", 1)[-1][:160] if after else "none"})')
    host.call('coop.diag sync 30')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
