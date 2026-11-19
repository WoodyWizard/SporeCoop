#!/usr/bin/env python3
"""BUG-1294 (found by the Lineages fork, PR #196, its BUG-9166): a world share asked while another share's save is
being written went out with that older save (BUG-0112 joined any save in flight), so a share asked because the world
changed (the host's evolution, a new nest) sent the world from before the change.

Two games on the creature fixture. Host: `coop.worldfix queue 1` (a share that saves first); as soon as its save is
posted ("share for 1 saved, being written"), one second later a second `coop.worldfix queue 1`. Checked on the host:
the second share does not go out with the first share's save ("waits for its own save" in the log, not "goes out
with the save for player 1"), the game posts a second save for it, and the guest plays in the shared world after it.
SPORE_FIX_OFF=world.share_fresh_save must FAIL (one save for both: the second share gets the older world).

  SPORE_LANE=9 python3 tools/tests/coop_share_fresh.py"""
import os
import re
import time

import lane
from twoinst import Checks, start, wait_guest_in


def counters(con):
    m = re.search(r'world saves posted (\d+), for shares (\d+)', con.call('coop.status')['out'])
    return (int(m.group(1)), int(m.group(2))) if m else None


def log_size(inst):
    try:
        return os.path.getsize(lane.game_log(inst))
    except OSError:
        return 0


def log_since(inst, pos):
    try:
        with open(lane.game_log(inst), 'rb') as f:
            f.seek(pos)
            return f.read().decode('utf-8', 'replace')
    except OSError:
        return ''


def main():
    host, guest = start('creature')
    ck = Checks()
    inst = host.port - 47799
    c0 = counters(host)
    print('  host: ' + host.call('coop.worldfix queue 1')['out'].strip(), flush=True)
    t0, saved = time.time(), False
    while time.time() - t0 < 30 and not saved:
        saved = 'share for 1 saved, being written' in host.call('coop.status')['out']
        time.sleep(0.2)
    ck(saved, 'the first share\'s save is posted')
    time.sleep(1.0)
    pos = log_size(inst)
    print('  host (1 s later): ' + host.call('coop.worldfix queue 1')['out'].strip(), flush=True)
    t0 = time.time()
    while time.time() - t0 < 60 and 'share for 1' in host.call('coop.status')['out']:
        time.sleep(1)
    c1 = counters(host)
    log = log_since(inst, pos)
    joined = 'the share for player 1 goes out with the save for player 1' in log
    waited = 'the share for player 1 waits for its own save' in log
    print(f'  host share saves {c0} -> {c1}; joined the older save: {joined}; waited: {waited}', flush=True)
    st = [l for l in host.call('coop.status')['out'].splitlines() if 'waited for their own save' in l]
    print('  host: ' + (st[0] if st else '(no line)'), flush=True)
    ck(not joined, 'the second share does not go out with the save posted before it was asked for')
    ck(waited, 'the second share waits for its own save')
    ck(c0 and c1 and c1[1] - c0[1] == 2, f'the host\'s game made a save for each share ({c1[1] - c0[1] if c0 and c1 else "?"})')
    ck(wait_guest_in(host, guest), 'the guest plays in the shared world afterwards')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
