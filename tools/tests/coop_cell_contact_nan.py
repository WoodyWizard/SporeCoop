#!/usr/bin/env python3
"""BUG-0307: a cell on the centre of the own cell: the game's contact solver makes both velocities NaN.

The first natural NaN of the own cell (lab 20261105-142923 cell_new_game#1, the guest, mask 0x104: the cell's
velocity and its particles' velocities, positions finite, 17 frames "at guest tick, finite at collision") is
written by the contact solver that Collisions 0xE7F260 runs after its pair loop (ContactSolvePass 0xE654D0,
docs/re/cell.md#cell-contact-nan). Two cells whose centres are closer than 0.004 (squared distance under
1.5e-5) get a contact whose normal the collision code never sets: it holds the other cell's centre
coordinates, so far from the world's origin it is ~100 long instead of 1, and the solver's 10 rounds of
impulses grow by ~|n|^2 each until they overflow (inf, then NaN).

Two games in the Cell fixture. On each game in turn the own cell swims well away from the origin, then the
nearest cell is put onto its centre for 20 cell steps (`coop.pool stack 20`; nothing of ours writes a
velocity: the game's own collision and solver do the rest). Checks on that game: its own cell stayed finite
(`own_nf=`: the own-cell probes inside the cell step and our tick saw no non-finite field) and the cell
that was put there too (its pose from `coop.pool see`/`coop.pool`), and the game still runs.

  coop_cell_contact_nan.py   (SPORE_FIX_OFF=cell.contact_finite: the own cell goes NaN, it FAILS)"""
import math
import re
import time

from twoinst import Checks, start

SPEED = 10.0  # m/s: a cell swims ~1-3, a coincident contact with the unset normal launched them at ~70


def stats(con):
    out = con.call('coop.pool')['out']

    def num(key):
        m = re.search(r'\b' + key + r'=(-?[\d.]+)', out)
        return float(m.group(1)) if m else None
    m = re.search(r'^avatar .*?pos=\((\S+) (\S+)\)', out, re.MULTILINE)
    p = (float(m.group(1)), float(m.group(2))) if m else None
    return {'own_nf': num('own_nf') or 0, 'bad': num('contacts_bad') or 0, 'pos': p}


def far_out(con, who):
    """swim until the own cell is at least 40 m from the world's origin"""
    for _ in range(30):
        p = stats(con)['pos']
        if p and math.isfinite(p[0]) and math.hypot(*p) >= 40:
            return p
        dx, dy = (p[0], p[1]) if p and math.hypot(*p) > 1 else (1, 1)
        n = math.hypot(dx, dy)
        con.call(f'coop.pool goto {12 * dx / n:.1f} {12 * dy / n:.1f}')
        time.sleep(1.5)
    return stats(con)['pos']


def stacked(ck, con, who, n):
    out = con.call(f'coop.pool stack2 12 {n}')['out'].strip()
    print(f'  {who}: {out}', flush=True)
    time.sleep(1.5)
    st = con.call('coop.pool stack2 ?')['out'].strip()
    print(f'  {who}: {st}', flush=True)
    m = re.search(r'pairs=(\d+) nf=0x([0-9A-F]+) vmax=(\S+)', st)
    pairs, nf, vmax = (int(m.group(1)), int(m.group(2), 16), float(m.group(3))) if m else (0, -1, math.inf)
    ck(out.startswith('stack2 ') and pairs > 0, f'{who}: {n} cells on one centre collided ({pairs} pairs)')
    ck(nf == 0 and vmax < SPEED, f'{who}: {n} cells on one centre stay finite and are pushed apart at a cell\'s pace '
                                 f'(non-finite 0x{nf:X}, fastest {vmax:.1f} m/s, limit {SPEED})')


def main():
    ck = Checks()
    host, guest = start('cell')
    time.sleep(4)
    # the host runs the collisions of its whole pool (a guest only its own cell's pairs, BUG-0205)
    for n in (2, 4):
        stacked(ck, host, 'host', n)
    for who, con in (('guest', guest), ('host', host)):
        p = far_out(con, who)
        print(f'  {who}: own cell at {p}', flush=True)
        s0 = stats(con)
        out = con.call('coop.pool stack 20')['out'].strip()
        print(f'  {who}: {out}', flush=True)
        time.sleep(2)
        s1 = stats(con)
        nf, bad = s1['own_nf'] - s0['own_nf'], s1['bad'] - s0['bad']
        print(f'  {who}: own_nf +{nf:.0f}, non-finite contacts +{bad:.0f}, own cell now at {s1["pos"]}', flush=True)
        ck(out.startswith('stack '), f'{who}: a cell was put onto its own cell ({out})')
        ck(nf == 0, f'{who}: its own cell stayed finite with a cell on its centre (own_nf +{nf:.0f})')
        ck(s1['pos'] and all(math.isfinite(v) for v in s1['pos']), f'{who}: its own cell has a place ({s1["pos"]})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
