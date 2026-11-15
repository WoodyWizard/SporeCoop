#!/usr/bin/env python3
"""BUG-0826: cargo a rejoin could not put back into a guest's hold stays in its kept hold for the next try.

The guest keeps its own hold on its disk per host world (BUG-0679: <AppData>\\Spore\\SporeCoop\\holds\\<crc>.txt) and
the host keeps a copy (BUG-0678); a rejoin puts that hold into the guest's game. An item the game cannot make there
(a plant or animal its factory does not know in this galaxy, spice without a free slot) failed, and right after the
restore the record and the host's copy took the reduced hold: the item was gone for good.

1. Both play the `space` fixture; the guest's record of the host world is written (its hold is its own).
2. The guest leaves; a plant cargo item the game does not know (type 0, key 0DEC0826) is added to its record.
3. The guest joins again: its restore cannot make that item (`coop.space trade kept`: failed > 0).
4. 6 s later the record still has the item, and the hold the host keeps for the guest has one item more than the
   guest's game: it is tried again at the next rejoin.
SPORE_FIX_OFF=space.keep_items_missed: the record and the host's copy lose it (FAIL).

  SPORE_LANE=2 python3 tools/tests/coop_space_items_missed.py
"""
import glob
import os
import re
import time

import lane
from coop_space_map import no_movie, sp, wait
from twoinst import Checks, start, wait_guest_in

ITEM = 'item 0 0DEC0826 00000000 00000000 3 -1'


def record_file():
    """the guest's record of the host world (the one naming Alice's world)"""
    for f in glob.glob(os.path.join(lane.GUEST_DIR, 'SporeCoop', 'holds', '*.txt')):
        try:
            with open(f, encoding='utf-8', errors='replace') as fh:
                first = fh.readline()
        except OSError:
            continue
        if first.startswith('world Alice|'):
            return f
    return None


def record_lines(f):
    try:
        with open(f, encoding='utf-8', errors='replace') as fh:
            return [line.rstrip('\n') for line in fh]
    except OSError:
        return []


def epoch(con):
    m = re.search(r'world=(\d+) open=1', sp(con, 'world'))
    return int(m.group(1)) if m else -1


def guest_items(con):
    return len(re.findall(r'^item type=', sp(con, 'trade inv'), re.M))


def kept_items(host):
    m = re.search(r"kept 'Bob' id=-?\d+ have=1 restoring=\d items=(\d+)", sp(host, 'trade kept'))
    return int(m.group(1)) if m else None


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(3)
    no_movie(guest)
    print('  guest: ' + sp(guest, 'trade spice 0 6'), flush=True)  # a change: the record is written
    f = wait(record_file, 20)
    ck(f is not None, f'the guest keeps a record of its hold for the host world ({f})')
    if not f:
        return ck.result()
    print(f'  record {os.path.basename(f)}: {len(record_lines(f))} lines', flush=True)
    guest.call('coop.leave')
    time.sleep(4)
    with open(f, 'a', encoding='utf-8') as fh:
        fh.write(ITEM + '\n')
    print(f'  added to the record: {ITEM}', flush=True)
    e0 = epoch(guest)
    guest.call(lane.JOIN)
    loaded = wait(lambda: epoch(guest) > e0 and 'Space' in guest.call('game.mode')['out'], 180, 2)
    ck(loaded is not None, f'the guest loaded the host\'s world again (Space world {e0} -> {epoch(guest)})')
    ck(wait_guest_in(host, guest, timeout=120), 'the guest is back in the host\'s world')
    no_movie(guest)

    def failed():
        m = re.search(r'\bfailed=(\d+)', sp(guest, 'trade kept'))
        return int(m.group(1)) if m and int(m.group(1)) > 0 else None
    n = wait(failed, 45, 2)
    print('  guest: ' + sp(guest, 'trade kept').replace('\n', ' | '), flush=True)
    ck(n is not None, f'the guest\'s restore could not make the unknown plant ({n} failed)')
    time.sleep(6)  # the record is written within 1 s of a change, the hold goes to the host within 1 s
    lines = record_lines(f)
    print(f'  record after the rejoin: {len(lines)} lines; the item there: {ITEM in lines}', flush=True)
    ck(ITEM in lines, 'the record still holds the item that could not come back (tried again at the next rejoin)')
    gi, ki = guest_items(guest), kept_items(host)
    print(f'  the guest\'s game holds {gi} items, the host keeps {ki} for it: ' + sp(host, 'trade kept').replace('\n', ' | '),
          flush=True)
    ck(ki is not None and ki == gi + 1, f'the hold the host keeps for the guest has that item too ({ki} = {gi} + 1)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
