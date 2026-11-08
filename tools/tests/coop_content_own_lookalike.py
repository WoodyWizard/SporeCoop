#!/usr/bin/env python3
"""BUG-0728 (class BUG-0424): a player's own cell or ship with a part the other game lacks is shown with the most
similar part that game has (the lookalike of BUG-0559), tested for cell and UFO parts (content_lookalike covers the
creature parts).

One lane has one install for both games, so the host is made to lack a part (`coop.creations lack <id>`: the mod's
checks, substitutes and lookalike search treat that part as one the host's game does not have).
1. Two games on the cell fixture (`cell`) or the space fixture (`ufo`).
2. The guest's own creation of that kind (`coop.creations` mine ...) and its parts (`mparts`); the host lacks the
   first part that is not one of the fixed fallbacks of that kind.
3. The guest leaves and joins again: its creation's bundle (with the descriptions of its parts) reaches the host again.

PASS: the model the host's game reads for the guest's creation names no lacked part, and in its place the part the
host's lookalike chose from the guest's description (`coop.creations looks`), not a fixed fallback.
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


def main():
    kind = sys.argv[1] if len(sys.argv) > 1 else 'cell'
    host, guest = start(FIXTURE[kind])
    ck = Checks()
    time.sleep(3)
    own = mine(guest, kind)
    parts = mparts(guest, kind, own) if own else []
    print(f'  guest own {kind} {own:08X}: parts {[f"{p:08X}" for p in parts]}', flush=True)
    lack = next((p for p in parts if p not in FALLBACKS[kind]), None)
    ck(lack is not None, f'the guest\'s own {kind} names a part to lack ({lack and f"{lack:08X}"})')
    if lack is None:
        ck.faults(host=host, guest=guest)
        return ck.result()
    guest.call('coop.leave')
    time.sleep(3)
    print('  host:', host.call(f'coop.creations lack {lack:08X}')['out'].strip(), flush=True)
    guest.call(lane.JOIN)
    ck(twoinst.wait_guest_in(host, guest, 180), 'the guest is back in the host\'s world')
    key, hp = 0, []
    t0 = time.time()
    while time.time() - t0 < 40:
        key = applied(host, 1, kind)
        hp = mparts(host, kind, key) if key else []
        if hp and lack not in hp:
            break
        time.sleep(1)
    lk = looks(host)
    sub = lk.get(lack)
    print(f'  host: player 1 {kind} {key:08X} parts {[f"{p:08X}" for p in hp]}; lookalike of {lack:08X}: '
          f'{sub and f"{sub:08X}"}', flush=True)
    print('  host:', host.call('coop.creations content')['out'].strip().replace('\n', ' | '), flush=True)
    ck(hp and lack not in hp, f'the host\'s model of the guest\'s {kind} names no part the host lacks ({lack:08X})')
    ck(sub, f'the host chose a lookalike for {lack:08X} from the guest\'s description ({sub})')
    ck(sub and sub in hp, f'the guest\'s {kind} shows that lookalike in the host\'s game')
    host.call('coop.creations lack clear')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
