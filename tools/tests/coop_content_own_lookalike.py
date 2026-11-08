#!/usr/bin/env python3
"""BUG-0728 (class BUG-0424): a player's own cell or ship with a part the other game lacks is shown with the most
similar part that game has (the lookalike of BUG-0559), tested for cell and UFO parts (content_lookalike covers the
creature parts).

One lane has one install for both games, so the host is made to lack a part (`coop.creations lack <id>`: the mod's
checks, substitutes and lookalike search treat that part as one the host's game does not have).
`cell`: two games on the cell fixture; the guest chooses one of its own cells the host does not have; the host lacks
its first part that is not a fixed fallback; the guest's bundle (with its parts' descriptions) reaches the host.
`ufo`: two games on the space fixture; the host's My Creations hold a ship design X (space_host_ship_design's
set-up), the guest lacks its first part, the host's ships fly X: the guest builds its mirrors from the host's design
(BUG-0724) with the design's part descriptions (BUG-0725).

PASS: the model the receiving game reads names no lacked part, and in its place the part its lookalike chose from the
sender's description (`coop.creations looks`), not a fixed fallback.
FAIL (SPORE_FIX_OFF=content.lookalike): a fixed fallback part.

  SPORE_LANE=7 python3 tools/tests/coop_content_own_lookalike.py cell|ufo
"""
import re
import sys
import time

import lane
import twoinst
from twoinst import Checks, start

FALLBACKS = {'cell': {0x026D3C30, 0x12599FD4, 0x21C38321},
             'ufo': {0x00046BAE, 0x011015FB, 0x01568CD8, 0x0AE2FEE9, 0x1ED67DFE}}
FIXTURE = {'cell': 'cell', 'ufo': 'space'}


def mparts(con, kind, inst):
    out = con.call(f'coop.creations mparts {kind} {inst:08X}')['out'].split()
    return [int(x[:8], 16) for x in out[2:] if re.match(r'[0-9A-F]{8}', x)]


def looks(con):
    out = {}
    for x in con.call('coop.creations looks')['out'].split()[1:]:
        m = re.match(r'(\w+)/(\w+)=(\w+)', x)
        if m:
            out[int(m.group(2), 16)] = int(m.group(3), 16)
    return out


def mine(con, kind):
    m = re.search(rf'^\s+mine {kind}: ([0-9A-F]{{8}})', con.call('coop.creations')['out'], re.M)
    return int(m.group(1), 16) if m else 0


def applied(con, player, kind):
    m = re.search(rf'^\s+player {player} {kind}: ([0-9A-F]{{8}})/', con.call('coop.creations')['out'], re.M)
    return int(m.group(1), 16) if m else 0


def options(con, kind):
    return re.findall(rf'^{kind} ([0-9A-F]{{8}}) ', con.call('coop.creations list')['out'], re.M)


CX = 0x2903A65A  # the test's own cell in the guest's My Creations (a base game cell model)


def cell_model():
    import os
    sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    import part_substitutes as ps
    path = os.path.join(lane.game_dir(lane.LANE), 'Data', 'Spore_Content.package')
    best = None
    for e in ps.entries(path):
        if e['type'] == 0x1A99B06B and e['group'] == 0x40616200:
            d = ps.read(path, e)
            if best is None or len(d) > len(best):
                best = d
    assert best, 'no cell model in Spore_Content.package'
    return best


def restore_with_cell(guest=True):
    import os
    from coop_nest_species import fixtures_dir, read_pkg, write_pkg
    _orig_restore(guest)
    p = os.path.join(lane.GUEST_DIR, 'EditorSaves.package')
    base = os.path.join(fixtures_dir(), 'creations-guest.package')
    recs = [r for r in read_pkg(p if os.path.exists(p) else base) if r[2] != CX]
    recs.append((0x1A99B06B, 0x40616200, CX, CELL[0]))
    write_pkg(p, recs)


CELL = []
_orig_restore = twoinst.restore_creations


def cell_case(host, guest, ck):
    """the guest's own cell (one the host does not have) reaches the host with a part the host lacks"""
    cur = mine(guest, 'cell')
    theirs = set(options(host, 'cell'))
    cands = [int(o, 16) for o in options(guest, 'cell') if o not in theirs and int(o, 16) != cur]
    print(f'  guest cells {options(guest, "cell")}, host cells {sorted(theirs)}, mine {cur:08X}', flush=True)
    ck(cands, 'the guest has a cell of its own the host does not have')
    for c2 in cands:
        parts = mparts(guest, 'cell', c2)
        lack = next((p for p in parts if p not in FALLBACKS['cell']), None)
        if lack is not None:
            break
    else:
        return None, []
    print(f'  guest cell {c2:08X}: parts {[f"{p:08X}" for p in parts]}', flush=True)
    print('  host:', host.call(f'coop.creations lack {lack:08X}')['out'].strip(), flush=True)
    print('  guest:', guest.call(f'coop.creations choose cell {c2:08X}')['out'].strip(), flush=True)
    hp, key = [], 0
    t0 = time.time()
    while time.time() - t0 < 40:
        key = applied(host, 1, 'cell')
        hp = mparts(host, 'cell', key) if key else []
        if key and hp and lack not in hp:
            break
        time.sleep(1)
    print(f'  host: player 1 cell {key:08X} parts {[f"{p:08X}" for p in hp]}', flush=True)
    return lack, hp


def ufo_case(host, guest, ck):
    """a host's own ship design (BUG-0724) with a part the guest lacks reaches the guest's mirrors"""
    import coop_space_host_ship_design as sd
    X = sd.X
    parts = mparts(host, 'ufo', X)
    lack = next((p for p in parts if p not in FALLBACKS['ufo']), None)
    print(f'  host ship design {X:08X}: parts {[f"{p:08X}" for p in parts]}', flush=True)
    if lack is None:
        return None, []
    print('  guest:', guest.call(f'coop.creations lack {lack:08X}')['out'].strip(), flush=True)
    print(' ', host.call(f'coop.fleet fakemodel 2 {X:08X}')['out'].strip(), flush=True)
    for args in ('3 own', '2'):
        print('  host:', host.call(f'coop.fleet spawn {args}')['out'].strip(), flush=True)
        time.sleep(4)
    gp, mk = [], 0
    t0 = time.time()
    while time.time() - t0 < 30:
        ms = sd.mirrors(guest)
        if ms:
            mk = ms[0][3]
            gp = mparts(guest, 'ufo', mk)
            if gp and lack not in gp:
                break
        time.sleep(1)
    host.call('coop.fleet fakemodel 0')
    print(f'  guest mirror model {mk:08X} parts {[f"{p:08X}" for p in gp]}', flush=True)
    return lack, gp


def main():
    kind = sys.argv[1] if len(sys.argv) > 1 else 'cell'
    if kind == 'cell':
        CELL[:] = [cell_model()]
        twoinst.restore_creations = restore_with_cell
    if kind == 'ufo':
        import coop_space_host_ship_design as sd
        sd.MODELS[:] = sd.two_ufo_models()
        twoinst.restore_creations = sd.restore_with_ship
    host, guest = start(FIXTURE[kind])
    ck = Checks()
    time.sleep(3)
    lack, parts = (cell_case if kind == 'cell' else ufo_case)(host, guest, ck)
    ck(lack is not None, f'a part to lack ({lack and f"{lack:08X}"})')
    if lack is None:
        ck.faults(host=host, guest=guest)
        return ck.result()
    who = host if kind == 'cell' else guest
    sub = looks(who).get(lack)
    print(f'  lookalike of {lack:08X}: {sub and f"{sub:08X}"};', who.call('coop.creations content')['out'].strip().replace('\n', ' | '), flush=True)
    ck(parts and lack not in parts, f'the receiving game\'s model names no part it lacks ({lack:08X})')
    ck(sub, f'the receiving game chose a lookalike for {lack:08X} from the sender\'s description ({sub})')
    ck(sub and sub in parts, f'the {kind} shows that lookalike in the receiving game')
    who.call('coop.creations lack clear')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
