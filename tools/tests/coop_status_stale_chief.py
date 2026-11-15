#!/usr/bin/env python3
"""BUG-0919 (tests): `coop.status` in Tribe does not call the game on a chief of the last tick that is gone now.

`coop.status` printed each chief's size in the game (`chief_scale=`, a virtual call GetScale on the chief) from the
mod's table of chiefs, filled by the tick. A console command runs between frames: a chief taken out of its tribe
after the tick (here `coop.tribe drop_chief <owner>`: RemoveMember(c, destroy), as guest_state does for a mirror the
host no longer sends) was still in that table, and the status called into the destroyed citizen.

The host's console gets `coop.tribe drop_chief <owner>` and, 3 ms later on a second connection, `coop.status`: both are
run in the same pump of the main thread when they arrive within one frame (no tick between). A round counts when the
status still names the dropped chief in `chiefs=` (the table of the last tick); then its `chief_scale=` entry must
say `gone` (the game's object list does not have it), not a size read from the destroyed citizen. Up to 2 rounds (the
host's and the guest's chief). Before the fix the entry was a size (FAIL).

  SPORE_LANE=5 python3 tools/tests/coop_status_stale_chief.py
"""
import re
import threading
import time

from twoinst import Checks, game_runs, start  # (first: it puts tools/ on the path)
import lane
from sporectl import Console


def main():
    host, guest = start('tribe')
    ck = Checks()
    game_runs(guest, 90)
    time.sleep(4)
    counted, bad = 0, []
    for owner in (0, 1):
        st0 = host.call('coop.status')['out']
        m = re.search(r'chiefs=([^\n]*?) spread', st0)
        print(f'  before: chiefs={m and m.group(1)}', flush=True)
        a, b = Console(lane.HOST), Console(lane.HOST)
        out = {}

        def drop():
            out['drop'] = a.call(f'coop.tribe drop_chief {owner}')['out'].strip()

        def status():
            time.sleep(0.003)
            out['st'] = b.call('coop.status')['out']

        ths = [threading.Thread(target=drop), threading.Thread(target=status)]
        for t in ths:
            t.start()
        for t in ths:
            t.join()
        md = re.search(r'dropped chief #(\d+)', out.get('drop', ''))
        chiefs = re.search(r'chiefs=([^\n]*?) spread', out.get('st', ''))
        scales = re.search(r'chief_scale=([^\n]*)', out.get('st', ''))
        named = bool(md and chiefs and f'{owner}:#{md.group(1)}' in chiefs.group(1))
        entry = re.search(rf'\b{owner}:(\S+)', scales.group(1)) if scales else None
        print(f'  owner {owner}: {out.get("drop")} | chiefs={chiefs and chiefs.group(1)} | chief_scale='
              f'{scales and scales.group(1).strip()} | the status in the same pump after the drop: {named}', flush=True)
        if named:
            counted += 1
            if not entry or entry.group(1) != 'gone':
                bad.append(f'owner {owner}: {entry and entry.group(1)}')
        time.sleep(6)
    ck(counted >= 1, f'the status ran right after a drop at least once ({counted} of 2 rounds)')
    ck(not bad, f'the status asked no dropped chief for its size ({bad})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
