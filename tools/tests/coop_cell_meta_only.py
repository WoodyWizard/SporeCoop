#!/usr/bin/env python3
"""Cell: a creation bundle without its model is not built; our own is never sent without it (BUG-0802).

Found by a fork (two PCs, Steam): a guest renamed a cell whose model is not in its EditorSaves (a partner's or an
imported cell played as its own: the model stays in the package it came from); the editor saved only the new
metadata record, the guest's bundle went out with that record alone and the host's game faulted at 0xE83EC8
building the guest's cell from a key without a model.

1. Both games in the Cell fixture; the host draws the guest's own cell (its puppet, a creation of the guest).
2. The guest sends its cell's bundle with the metadata record only (`coop.creations sendmeta cell <n>`: what the
   0.7.12 guest sent after such a rename), three times with another name byte.
3. The host's game does not fault (the lab's fault check), still draws the guest's cell within 10 s (its
   puppet, `coop.pool scalepos`), and keeps the guest's creation it had (`coop.creations`: the player's cell key
   is not a new key of a bundle without a model).
4. The guest's own share still sends its cell with its model (`creations: sending our cell ...: N record(s),
   1 model(s)` in its log, if it sent one in the run).
SPORE_FIX_OFF=creations.meta_only must FAIL at 3 (the host builds the guest's cell from a key without a model:
a fault at 0xE83EC8, or the puppet gone).

  SPORE_LANE=2 python3 tools/tests/coop_cell_meta_only.py
"""
import re
import time

from coop_cell_puppet_scale import playing, wait
from twoinst import Checks, start


def applied(con, pid):
    m = re.search(rf'^\s*player {pid} cell: ([0-9A-F]{{8}})', con.call('coop.creations')['out'], re.M)
    return m.group(1) if m else None


def drawn(con, pid):
    return re.search(rf'\bp{pid}=\(', con.call('coop.pool scalepos')['out']) is not None


def faults(con):
    m = re.search(r'total faults: (\d+)', con.call('exc')['out'])
    return int(m.group(1)) if m else -1


def main():
    host, guest = start('cell')
    ck = Checks()
    ck(wait(lambda: playing(host) and playing(guest), 40), 'both games play (the hatch is over)')
    gid = (re.search(r'client id=(\d+)', guest.call('coop.status')['out']) or [None, '1'])[1]
    guest.call('coop.pool tp 4 0')
    ok = wait(lambda: drawn(host, gid) and applied(host, gid), 30)
    k0, f0 = applied(host, gid), faults(host)
    print(f'  host: the guest\'s cell {k0}, drawn {drawn(host, gid)}, faults {f0}', flush=True)
    ck(ok, 'the host draws the guest\'s own cell (its creation) before')
    for n in range(3):
        print('  guest: ' + guest.call(f'coop.creations sendmeta cell {n + 1}')['out'].strip(), flush=True)
        time.sleep(4)
    time.sleep(6)
    k1, f1 = applied(host, gid), faults(host)
    st = [x for x in host.call('coop.creations')['out'].splitlines() if 'meta_only' in x or 'error=' in x]
    print(f'  host after: the guest\'s cell {k1}, drawn {drawn(host, gid)}, faults {f0} -> {f1}; {st}', flush=True)
    log = host.call('log 300')['out']
    print('  host log: ' + ' | '.join(x.strip() for x in log.splitlines() if 'BUG-0802' in x or 'E83EC8' in x)[:400], flush=True)
    ck(f1 == f0, f'the host\'s game did not fault ({f0} -> {f1})')
    ck(wait(lambda: drawn(host, gid), 10), 'the host still draws the guest\'s cell')
    ck(k1 == k0, f'the host keeps the guest\'s creation it had ({k0} -> {k1})')
    glog = guest.call('log 400')['out']
    sent = re.findall(r'creations: sending our cell \S+: (\d+) record\(s\), (\d+) model\(s\)', glog)
    print(f'  guest log: own cell bundles sent {sent}', flush=True)
    ck(all(int(n) == 0 or int(m) >= 1 for n, m in sent), f'(0 records: a creation everybody has) the guest\'s own share never sent its cell without its model ({sent})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
