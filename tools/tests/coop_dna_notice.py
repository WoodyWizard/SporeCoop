#!/usr/bin/env python3
"""Shared DNA says where it came from (player request 2026-11-01: "we get randomly a ton of DNA in
Cell and Creature"; BUG-0351: the pack's DNA is shared by design, nothing told the players).

The host's game pays every gain of the pack and sends the total; with it now the cause (dna_ledger.h).
Checked on the screen of the player who did NOT earn it (the notification panels the game shows,
read back from the panel windows: `coop.status` "toast ..." lines):
  * the guest kills one of the host's NPCs (`coop.npc hit <id> 100000`, a forwarded hit as a real
    attack is): the host's game pays it; the HOST shows "+N DNA from Bob's hunt", the guest
    "+N DNA from your hunt";
  * the host kills one (`coop.npc hosthit`, the host avatar's attack): the GUEST shows
    "+N DNA shared by Alice's pack";
  * both totals are the same afterwards, and each game's log has `dna: a -> b cause=...` lines.
`--cell` (Cell, the shared food that is the cell stage's DNA): a guest's meal (`coop.pool food 10`,
the meal message a real meal sends) shows "+10 food (DNA) from Bob's meals" on the host, a meal of the
host "+N food (DNA) from your partners' meals" on the guest (gathered 15 s), `dna: food a -> b
cause=meal:...` in the logs.
A feature, not a fix: no switch (an older host sends no causes, the guest then writes cause=unknown
and shows nothing)."""
import re
import sys
import time

import lane
from twoinst import Checks, shot, start


def toasts(con):
    # (a text may hold an apostrophe: up to the quote before the " | " lines or the line's end)
    return re.findall(r"^\s*toast \S* \([^)]*\) '(.*?)'(?: \||$)", con.call('coop.status')['out'], re.M)


def wait_toast(con, pattern, secs=10.0):
    t0 = time.time()
    while time.time() - t0 < secs:
        for t in toasts(con):
            if re.search(pattern, t):
                return t
        time.sleep(0.25)
    return None


def dna(con):
    m = re.search(r'dna=([\d.]+)', con.call('coop.dna')['out'])
    return float(m.group(1)) if m else None


def prey(host, guest):
    fam = re.search(r'key=0x([0-9A-F]+)', host.call('coop.avatar')['out'])
    fam = fam.group(1).zfill(8) if fam else ''
    mirrored = set(re.findall(r'#(\d+) .*spawned', guest.call('coop.npc list')['out']))
    return [m.group(1) for m in re.finditer(r'#(\d+) (?:[0-9A-Fa-f]{8} )?species=([0-9A-F]+) hp=([\d.]+)',
                                            host.call('coop.npc list')['out'])
            if m.group(1) in mirrored and m.group(2) != fam and float(m.group(3)) > 0]


def ledger(path, since, pat=r'dna: (-?\d+) -> (-?\d+) cause=(\S+)'):
    try:
        with open(path, 'rb') as f:  # (bytes: `since` is a byte offset)
            f.seek(since)
            return re.findall(pat, f.read().decode('utf-8', 'replace'))
    except OSError:
        return []


def size(path):
    try:
        with open(path, 'rb') as f:
            return f.seek(0, 2)
    except OSError:
        return 0


def kill(host, guest, ids, how):
    """the first of `ids` whose kill pays the host DNA; (id, before, after)"""
    for nid in ids:
        d0 = dna(host)
        if how == 'guest':
            guest.call(f'coop.npc hit {nid} 100000')
        else:
            host.call(f'coop.npc hosthit {nid} 100000')
        t0 = time.time()
        while time.time() - t0 < 3:
            if (dna(host) or 0) > (d0 or 0):
                return nid, d0, dna(host)
            time.sleep(0.2)
    return None, None, None


def cell_food(con):
    m = re.search(r'^cell\((?:host|guest)\): food=(-?\d+)', con.call('coop.pool')['out'], re.MULTILINE)
    return int(m.group(1)) if m else None


def main_cell():
    host, guest = start('cell')
    ck = Checks()
    hlog, glog = lane.game_log(lane.HOST), lane.game_log(lane.GUEST)
    h0, g0 = size(hlog), size(glog)
    t0 = time.time()
    while time.time() - t0 < 20 and (cell_food(host) is None or cell_food(host) != cell_food(guest)):
        time.sleep(0.5)
    f0 = cell_food(host)
    guest.call('coop.pool food 10')
    th = wait_toast(host, r"^\+10 food \(DNA\) from Bob's meals", 22)
    print(f'  the guest ate 10: host food {f0} -> {cell_food(host)}, host shows {th!r}', flush=True)
    if th:
        shot(host, 'dna_notice_cell_host.png')
    time.sleep(4)
    f1 = cell_food(host)
    host.call('coop.pool food 6')
    tg = wait_toast(guest, r"^\+6 food \(DNA\) from your partners' meals", 22)
    print(f'  the host ate 6: guest food {f1} -> {cell_food(guest)}, guest shows {tg!r}', flush=True)
    ck(th is not None, 'the host\'s screen says the food came from the guest\'s meals')
    ck(tg is not None, 'the guest\'s screen says the food came from its partners\' meals')
    food_pat = r'dna: food (-?\d+) -> (-?\d+) cause=(\S+)'
    lh, lg = ledger(hlog, h0, food_pat), ledger(glog, g0, food_pat)
    print(f'  ledger host: {lh[-3:]}; guest: {lg[-3:]}', flush=True)
    ck(any(c == 'meal:1+10' for _, _, c in lh), 'the host\'s log names the guest\'s meal')
    ck(any(c == 'meal:255+6' for _, _, c in lg), 'the guest\'s log names the partners\' meal')
    ck.faults(host=host, guest=guest)
    return ck.result()


def main():
    if '--cell' in sys.argv:
        return main_cell()
    host, guest = start()
    ck = Checks()
    hlog, glog = lane.game_log(lane.HOST), lane.game_log(lane.GUEST)
    h0, g0 = size(hlog), size(glog)
    ids = []
    t0 = time.time()
    while time.time() - t0 < 30 and len(ids) < 4:
        ids = prey(host, guest)
        time.sleep(1)
    print(f'  prey mirrored on the guest: {ids[:6]}', flush=True)

    # 1. the guest's kill: the host's screen
    nid, a, b = kill(host, guest, ids[:3], 'guest')
    print(f'  the guest killed #{nid}: host DNA {a} -> {b}', flush=True)
    th = wait_toast(host, r"^\+\d+ DNA from Bob's hunt")
    tg = wait_toast(guest, r'^\+\d+ DNA from your hunt', 4)
    print(f'  host shows: {th!r}; guest shows: {tg!r}', flush=True)
    if th:
        shot(host, 'dna_notice_host.png')
    ck(nid is not None, 'a kill by the guest paid the pack DNA')
    ck(th is not None, 'the host\'s screen says the DNA came from the guest\'s hunt')
    ck(tg is not None, 'the guest\'s screen says the DNA came from its own hunt')

    # 2. the host's kill: the guest's screen
    time.sleep(5)  # the notices' gap
    rest = [i for i in ids if i != nid]
    nid2, a2, b2 = kill(host, guest, rest[:3], 'host')
    print(f'  the host killed #{nid2}: host DNA {a2} -> {b2}', flush=True)
    tg2 = wait_toast(guest, r'^\+\d+ DNA shared by Alice')
    print(f'  guest shows: {tg2!r}', flush=True)
    if tg2:
        shot(guest, 'dna_notice_guest.png')
    ck(nid2 is not None, 'a kill by the host paid the pack DNA')
    ck(tg2 is not None, 'the guest\'s screen says the DNA was shared by the host')
    if tg2 and a2 is not None and b2 is not None:
        n = int(re.match(r'\+(\d+)', tg2).group(1))
        ck(abs(n - (b2 - a2)) <= 1, f'the notice names the amount the host\'s game paid ({n} of {b2 - a2:.0f})')

    time.sleep(2)
    dh, dg = dna(host), dna(guest)
    ck(dh is not None and dg is not None and abs(dh - dg) < 0.5, f'the same DNA on both ({dh}, {dg})')
    lh, lg = ledger(hlog, h0), ledger(glog, g0)
    print(f'  ledger host: {lh[-4:]}', flush=True)
    print(f'  ledger guest: {lg[-4:]}', flush=True)
    ck(any(c.startswith('hunt:1') for _, _, c in lh), 'the host\'s log names the guest\'s hunt (dna: ... cause=hunt:1)')
    ck(any('hunt:1' in c for _, _, c in lg) and any('host:0' in c for _, _, c in lg),
       'the guest\'s log names both causes')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
