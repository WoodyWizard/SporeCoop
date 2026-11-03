#!/usr/bin/env python3
"""BUG-0467: a Cell part one player collects is unlocked for the whole party.

Cell fixture. The guest's game collects a part token (`coop.parts cell collect <i>`: the game's own
0xE5D7B0, what touching a part token runs: state 2, the mating-call button, the callout, the cell
editor's palette items rebuilt). Checked on the host's game: its own part state for that part
(cCellSerializableData +0x34 + 4i, the state the cell editor builds its palette from) is no longer 0
and its mating-call button (S+0x69) is up. Then the other way: the host collects another part and
the guest's game has it.

  coop_cell_parts.py   (SPORE_FIX_OFF=parts.shared_cell must FAIL)
"""
import re
import time

from twoinst import Checks, start


def states(con):
    m = re.search(r'cell parts=(\d{13})', con.call('coop.parts cell')['out'])
    return m.group(1) if m else None


def mate_button(con):
    m = re.search(r'mate_button=(-?\d+)', con.call('coop.pool mate')['out'])
    return int(m.group(1)) if m else None


def wait(cond, secs, step=1.0):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return cond()


def main():
    host, guest = start('cell')
    ck = Checks()
    time.sleep(5)
    h0, g0 = states(host), states(guest)
    print(f'  part states before: host {h0} guest {g0}; mate button host {mate_button(host)}', flush=True)
    ck(h0 and g0, 'both games are in a Cell game')
    if not (h0 and g0):
        return ck.result()
    free = [i for i in range(13) if h0[i] == '0' and g0[i] == '0']
    ck(len(free) >= 2, f'two parts nobody has yet ({free})')
    if len(free) < 2:
        return ck.result()
    a, b = free[0], free[1]

    out = guest.call(f'coop.parts cell collect {a}')['out'].strip()
    print(f'  guest collects part {a}: {out}', flush=True)
    ck(states(guest)[a] != '0', f"the guest's game has part {a}")
    got = wait(lambda: states(host)[a] != '0', 15)
    print(f'  host after 15 s: {states(host)} mate button {mate_button(host)}', flush=True)
    ck(got, f"the host's game has the part the guest collected (state of part {a})")
    ck(mate_button(host) == 1, "the host's mating-call button is up (as after its own part)")

    out = host.call(f'coop.parts cell collect {b}')['out'].strip()
    print(f'  host collects part {b}: {out}', flush=True)
    got = wait(lambda: states(guest)[b] != '0', 15)
    print(f'  guest after 15 s: {states(guest)}', flush=True)
    ck(got, f"the guest's game has the part the host collected (state of part {b})")
    print('  host:', host.call('coop.parts cell')['out'].strip(), flush=True)
    print('  guest:', guest.call('coop.parts cell')['out'].strip(), flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
