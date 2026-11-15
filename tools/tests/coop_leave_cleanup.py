#!/usr/bin/env python3
"""Every way a session ends cleans up every stage module (BUG-0033): in Space the guest presses
Leave and the host's ships it showed (mirrors) must be gone from its game, not fly on with their
own AI; then the guest rejoins and starts hosting while still connected, and the old remote must
not carry over into the new session.

  coop_leave_cleanup.py   (its switch session.end was retired in 0.7.15, BUG-1138: FAIL run
                           20261115-030257 on record)"""
import re

REMOTE = re.compile(r"^(\s+\[\d+\] |remote \d+ ')", re.M)  # a player or a remote line of coop.status
import time

import lane
from twoinst import Checks, start


def ships(con):
    """{net id: pointer} of the ships the game has (coop.fleet list walks the game's UFO list)"""
    out = con.call('coop.fleet list')['out']
    return {int(m.group(1)): m.group(2) for m in re.finditer(r'ship #(-?\d+) (\S+) type=', out)}, out


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(3)
    print(' ', host.call('coop.fleet spawn 2')['out'].strip())
    mirrors = {}
    for _ in range(15):
        hs, _ = ships(host)
        gs, gout = ships(guest)
        mirrors = {i: p for i, p in gs.items() if i >= 0 and i in hs}
        if mirrors:
            break
        time.sleep(1)
    ck(mirrors, f'the guest shows the host\'s ships {sorted(mirrors)}')

    # 1. the guest's own Leave
    print(' ', guest.call('coop.leave')['out'].strip())
    time.sleep(2)
    after, aout = ships(guest)
    left = [p for p in mirrors.values() if p in aout]
    ck(mirrors and not left, f'after Leave the host\'s ships are gone from the guest\'s game (left: {left})')
    st = guest.call('coop.status')['out']
    ck(st.startswith('offline') and not REMOTE.search(st), 'the guest has no session and no remote players')

    # 2. hosting while still connected: the old session's remotes must not carry over
    guest.call(lane.JOIN)
    for _ in range(40):
        if 'connected' in guest.call('coop.status')['out'].splitlines()[0]:
            break
        time.sleep(1)
    time.sleep(3)
    print(' ', guest.call(f'coop.host {lane.PORT + 150} Bob')['out'].strip())
    time.sleep(2)
    st = guest.call('coop.status')['out']
    ck(st.startswith('host') and not REMOTE.search(st),
       'a new session starts clean')
    for line in st.splitlines():
        if REMOTE.search(line):
            print('    left over:', line.strip()[:160])
    gs2, _ = ships(guest)
    ck(not [p for p in gs2.values() if p in mirrors.values()], 'no ship of the old session is left')
    guest.call('coop.leave')

    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
