#!/usr/bin/env python3
"""BUG-0507: the partner's Creature puppet freed by the game behind the mod's back (guest).

A guest crashed on 0.7.3.6 in drive_puppet -> creature_drive -> walk_place -> game::set_speed on a
puppet whose memory held another object (a cModelObject vtable at +0xC0), right after a 4 s hitch and a
device Reset: the game had freed the partner's puppet, and the Creature adapter had no liveness check,
so the mod drove the freed pointer every frame. With creature.puppet_alive the puppet is checked against
the game's own object list (and its cGameData serial) and a freed one is replaced by a new puppet.

1. The guest toggles fullscreen twice (Alt+Enter: two device Resets, as in the report): the puppet of
   the host stays a live creature of the guest's game, at the host's position.
2. ROUNDS times the guest's game destroys the host's puppet itself (`coop.puppet gamekill`:
   cGameNounManager::DestroyInstance, none of the mod's bookkeeping). Within SHOW_S the puppet the
   guest's mod holds must be a live creature in the guest's game's object list (`puppet_listed`), placed
   at the host's own position (read from the host's game), and neither game faults.

  coop_puppet_freed.py   (SPORE_FIX_OFF=creature.puppet_alive: the freed puppet stays in use, FAILS)"""
import math
import re
import time

from coop_alt_enter import keys
from twoinst import Checks, start

ROUNDS = 3
SHOW_S = 4.0
NEAR_M = 4.0


def host_pos(host):
    p = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', host.call('coop.avatar')['out'])
    return tuple(float(x) for x in p.groups()) if p else None


def puppet(guest):
    """(pointer, listed by the game, its position) of the host's puppet on the guest"""
    st = guest.call('coop.status')['out']
    m = re.search(r"remote 0 '[^']*' puppet=([0-9A-Fa-fx]+)", st)
    ptr = int(m.group(1), 16) if m and m.group(1) not in ('00000000', '0') else 0
    li = re.search(r'puppet_listed 0 (\d)', st)
    pp = re.search(r"remote 0 [^\n]*\n\s+puppet pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)", st)
    return ptr, bool(li and li.group(1) == '1'), tuple(float(x) for x in pp.groups()) if pp else None


def shown(host, guest, secs):
    """the host's puppet on the guest is a live creature at the host's position within secs: (ok, text)"""
    t0 = time.time()
    last = ''
    while time.time() - t0 < secs:
        ptr, listed, pp = puppet(guest)
        hp = host_pos(host)
        d = math.dist(pp, hp) if pp and hp else None
        last = f'puppet {ptr:08X} listed={int(listed)} {f"{d:.1f} m" if d is not None else "no pos"} from the host'
        if ptr and listed and d is not None and d < NEAR_M:
            return True, last
        time.sleep(0.25)
    return False, last


def main():
    host, guest = start()
    ck = Checks()
    ok, txt = shown(host, guest, 20)
    ck(ok, f'the host\'s creature is shown on the guest ({txt})')
    if not ok:
        return ck.result()
    # 1. device Resets on the guest (fullscreen on and off)
    for i in range(2):
        keys(guest, False)
        time.sleep(4)
        ok, txt = shown(host, guest, SHOW_S)
        ck(ok, f'after the guest\'s Alt+Enter #{i + 1} (a device Reset) the host is still shown ({txt})')
    # 2. the game frees the puppet behind the mod's back
    for i in range(ROUNDS):
        p0, _, _ = puppet(guest)
        print('  ' + guest.call('coop.puppet gamekill')['out'].strip(), flush=True)
        time.sleep(0.5)
        ok, txt = shown(host, guest, SHOW_S)
        p1, _, _ = puppet(guest)
        ck(ok, f'round {i + 1}: the puppet the game destroyed ({p0:08X}) is replaced by a live one at the host ({txt})')
        st = guest.call('coop.status')['out']
        m = re.search(r'gone_by_game=(\d+)', st)
        print(f'  round {i + 1}: puppet {p0:08X} -> {p1:08X}, {m.group(0) if m else "no counter"}', flush=True)
        time.sleep(1)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
