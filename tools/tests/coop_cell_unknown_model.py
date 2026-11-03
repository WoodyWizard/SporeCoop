#!/usr/bin/env python3
"""BUG-0455 (the class of BUG-0004): a guest mirrors a host cell whose model is a creation it does not have.

A player's game (the guest) crashed at 0xE83EC8 (read 0x9C) when the host left its Cell editor:
cellsync made mirrors of the host's cells of the host's new creation (CreateCellObject with the
received model key) before that creation's records reached the guest; the game built the key's cell
model data (GetCellModelData 0xE679E0 -> 0xE83E30), LoadCreatureData found nothing and the game read
the missing data.

The test puts that state in directly: the host makes a cell next to itself (`coop.cell spawn`, a copy
of one of its NPC cells) and sends it to the guest with a model instance no game has (0x0BADCE11).
Then the host sends the same cell with its real model.
Checks:
* the guest's game goes on: no fault (its exception counter, `ck.faults`), it still answers;
* the guest made no cell with the unknown model (`unknown_models` counts the refused ones);
* the guest mirrors the cell once its model is one it has (the mirror count goes up).

  coop_cell_unknown_model.py   (SPORE_FIX_OFF=cell.unknown_model: the guest faults at 0xE83EC8, FAILS)"""
import re
import time

from twoinst import Checks, start

FAKE = '0BADCE11'


def counter(con, name):
    m = re.search(name + r'=(\d+)', con.call('coop.status')['out'])
    return int(m.group(1)) if m else -1


def created(con):
    m = re.search(r'cell\(guest\): food=-?\d+ mirrors=\d+ created=(\d+)', con.call('coop.status')['out'])
    return int(m.group(1)) if m else -1


def main():
    host, guest = start('cell')
    ck = Checks()
    time.sleep(5)
    src = None
    for line in host.call('coop.cell list')['out'].splitlines():
        m = re.match(r'\s#(\d+) idx=([0-9A-F]+) .* res=([0-9A-Fa-fx]+) model=([0-9A-F]+) scale=(-?\d+) hp=(-?\d+)', line)
        if m and int(m.group(6)) > 0 and int(m.group(3), 16) and int(m.group(4), 16) and int(m.group(5)) >= 0:
            src = m
            break
    ck(src is not None, f'a host NPC cell to copy ({src.group(0).strip()[:90] if src else None})')
    if not src:
        return ck.result()
    u0, c0 = counter(guest, 'unknown_models'), created(guest)
    out = host.call(f'coop.cell spawn {src.group(1)} 5 0 {src.group(5)} {FAKE}')['out']
    m = re.search(r'created ([0-9A-F]+)', out)
    ck(m is not None and int(m.group(1), 16) != 0, f'the host made the cell ({out.strip()})')
    time.sleep(4)  # state rounds go out several times a second
    alive = 'total faults' in guest.call('exc')['out']
    u1 = counter(guest, 'unknown_models')
    print('  guest', guest.call('exc')['out'].splitlines()[0], f'| unknown_models {u0} -> {u1}', flush=True)
    ck(alive, 'the guest still answers after the host sent a cell of an unknown creation')
    ck(u1 > u0, f'the guest made no cell with the unknown model ({u1 - u0} refused)')
    # the host's cell with its real model now (the spawn of another copy clears the test model)
    host.call(f'coop.cell spawn {src.group(1)} -5 0 {src.group(5)} 0')
    time.sleep(4)
    c1 = created(guest)
    ck(c1 >= c0 + 2, f'the guest mirrors the cell once its model is known ({c0} -> {c1} created, both copies)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
