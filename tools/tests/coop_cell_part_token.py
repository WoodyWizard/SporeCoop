#!/usr/bin/env python3
"""BUG-0316: Cell: does a guest that swims onto a part token of the host's world get the part and the
mating-call button?

The game collects a part token in the collision code 0xE7B630 of the collecting player's own game:
CollectPart 0xE5D7B0 (the part's state S+0x34+4*part = 2, the mating-call button S+0x69 = 1), then
DestroyCell of the token (docs/re/cell.md#cell-mate-button, #cell-part-token). On a guest the token
is a mirror of the host's cell.

Two games in the Cell fixture. The guest's mating-call button is hidden first (`coop.pool mate
nobutton`, as before a first part). The host moves the nearest part token of its pool 4 m next to
the guest's cell (`coop.pool tokens to 1`; the host swims around until its pool has one); the guest
swims onto its mirror (`coop.pool swimto c<key> tp`, like a click). Checks, in the games' own state:
the guest's game shows the mating-call button (S+0x69), its part states gained the part (`coop.parts
cell`), the token is gone in the host's game, and the host's game has the part too (the party's
shared set, parts.shared_cell).

  coop_cell_part_token.py"""
import re
import time

from twoinst import Checks, start


def tokens(con, extra=''):
    out = con.call(f'coop.pool tokens {extra}'.strip())['out']
    return re.findall(r'^token (c\d+) pos=\((-?[\d.]+) (-?[\d.]+)\) dist=([\d.]+)', out, re.MULTILINE), out


def button(con):
    m = re.search(r'mate_button=(-?\d+)', con.call('coop.pool mate')['out'])
    return int(m.group(1)) if m else None


def parts(con):
    m = re.search(r'cell parts=(\d+)', con.call('coop.parts cell')['out'])
    return m.group(1) if m else None


def playing(con):
    m = re.search(r'input_lock=(-?\d+)', con.call('coop.uistate')['out'])
    return m and int(m.group(1)) == 0 and 'cine=0' in con.call('coop.pool')['out']


def wait(cond, secs, step=0.5):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return None


def main():
    host, guest = start('cell')
    ck = Checks()
    ck(wait(lambda: playing(host) and playing(guest), 40), 'both games play (the hatch is over)')
    # a part token in the host's pool: swim around until the tiles give one
    found = []
    for i in range(16):
        found, _ = tokens(host)
        if found:
            break
        dx, dy = [(40, 0), (0, 40), (-40, 0), (0, -40)][i % 4]
        host.call(f'coop.pool goto {dx} {dy}')
        time.sleep(4)
        guest.call('coop.pool tp 3 0')
    print(f'  host part tokens: {len(found)} {found[:3]}', flush=True)
    ck(bool(found), 'the host\'s pool has a part token')
    if not found:
        return ck.result()
    guest.call('coop.pool tp 3 0')
    time.sleep(2)
    guest.call('coop.pool mate nobutton')
    b0, gp0, hp0 = button(guest), parts(guest), parts(host)
    print(f'  before: guest button {b0} parts {gp0} | host parts {hp0}', flush=True)
    _, out = tokens(host, 'to 1')
    print('  host:', out.splitlines()[0], flush=True)
    moved = re.search(r'moved (c\d+)', out)
    ck(moved, 'the host moved a token next to the guest')
    key = moved.group(1) if moved else None
    seen = wait(lambda: key in [t[0] for t in tokens(guest)[0]], 6)
    ck(seen, f'the guest\'s game has the token {key} (a mirror)')
    print('  guest:', guest.call(f'coop.pool swimto {key} tp')['out'].strip(), flush=True)
    got = wait(lambda: button(guest) == 1, 6)
    time.sleep(4)  # the shared set (resent every 3 s)
    b1, gp1, hp1 = button(guest), parts(guest), parts(host)
    gone = not host.call(f'coop.pool cell {key}')['out'].startswith('cell ')
    print(f'  after: guest button {b1} parts {gp1} | host parts {hp1} | token gone on the host {gone}', flush=True)
    ck(got and b1 == 1, f'the guest\'s game shows the mating-call button after its touch ({b0} -> {b1})')
    changed = [i for i in range(min(len(gp0 or ''), len(gp1 or ''))) if gp0[i] != gp1[i]]
    ck(bool(changed), f'the guest\'s game has the part ({gp0} -> {gp1})')
    ck(gone, 'the token is gone in the host\'s game')
    ck(bool(changed) and hp1 is not None and all(hp1[i] != '0' for i in changed),
       f'the host\'s game has the part too ({hp0} -> {hp1}, part {changed})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
