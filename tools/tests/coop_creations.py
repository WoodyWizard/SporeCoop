#!/usr/bin/env python3
"""Own creatures (BUG-0345 rule): a guest plays its own creation when it chose it, or by default in a
world it already played it in; in a world it never played its own creation in, it plays the world's
species (as the host does since BUG-0344). Each player sees the other with the other's creature, and a
guest's own creature is announced on both games ("Bob plays its own creature").

Checked in the games' own state (the guest's avatar species key, the host's puppet of the guest):
  1. no choice, no record for this world (`coop.creations unchoose creature`, `worlds forget`: a world
     the guest never played its creature in): the guest plays the world's species, the host shows it so;
  2. the guest chooses its newest own creature: it plays it, the host shows it with it, both games got
     the notice, and the world records it;
  3. the choice is dropped again (nothing chosen, the same world): it still plays its own creature;
  4. the record is forgotten (as a new world): the world's species again;
  5. the co-op window: "My creature: <world's own> < >", the next one is the own creature, everywhere.

  coop_creations.py   (SPORE_FIX_OFF=creations.guest_world_species: in 1 the guest plays its newest own
                       creation in a world it never played it in: it FAILS)"""
import re
import time

import lane
import creations_state
from coop_ui import Ui
from twoinst import Checks, shot, start


def field(out, pat):
    m = re.search(pat, out)
    return m.group(1) if m else None


def avatar(con):
    return field(con.call('coop.avatar')['out'], r'key=0x([0-9A-F]{8})')


def puppet(con, rid):
    return field(con.call('coop.status')['out'], r"remote %d '[^']*'.*\n.*species=([0-9A-F]{8})" % rid)


def wait(cond, secs, step=1.0):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return cond()


def told(inst, text, start):
    """the text in the game's log after <start> bytes (a warm game's log has older runs)"""
    try:
        with open(lane.game_log(inst), 'rb') as f:
            f.seek(start)
            return text in f.read().decode('utf-8', 'replace')
    except OSError:
        return False


def size(inst):
    try:
        with open(lane.game_log(inst), 'rb') as f:
            return f.seek(0, 2)
    except OSError:
        return 0


def main():
    host, guest = start()
    ck = Checks()
    ck(wait(lambda: avatar(guest) and avatar(host) and puppet(host, 1), 30), 'both games play, the host shows the guest')
    h_av = avatar(host)
    st0 = creations_state.save(guest)  # put back at the end: later tests see the profile as it was
    print(f'  guest profile before: {st0}', flush=True)
    own = [l.split()[1] for l in guest.call('coop.creations list')['out'].splitlines() if l.startswith('creature ')]
    ck(own, f'the guest profile has own creatures ({len(own)})')
    newest = own[0] if own else None

    # 1. nothing chosen, a world the guest never played its own creature in
    print(' ', guest.call('coop.creations unchoose creature')['out'].strip(), '|',
          guest.call('coop.creations worlds forget')['out'].strip(), flush=True)
    ok = wait(lambda: avatar(guest) == h_av and puppet(host, 1) == h_av, 30)
    print(f'  1. guest avatar {avatar(guest)}, host avatar {h_av}, host shows the guest as {puppet(host, 1)}', flush=True)
    ck(ok, f'nothing chosen in a new world: the guest plays the world\'s species {h_av} (guest {avatar(guest)}, '
           f'newest own {newest})')

    # 2. the guest chooses its own creature
    hs0, gs0 = size(lane.HOST), size(lane.GUEST)
    print(' ', guest.call(f'coop.creations choose creature {newest}')['out'].strip(), flush=True)
    ok = wait(lambda: avatar(guest) == newest and puppet(host, 1) not in (None, h_av), 30)
    hp = puppet(host, 1)
    print(f'  2. guest avatar {avatar(guest)}, host shows the guest as {hp}', flush=True)
    ck(ok, f'the guest plays its chosen own creature {newest} and the host shows it ({hp})')
    rec = wait(lambda: field(guest.call('coop.creations worlds')['out'], r'creature=([0-9A-F]{8})') == newest, 10)
    ck(rec, f'the world records the own creature ({guest.call("coop.creations worlds")["out"].strip()})')
    ck(wait(lambda: told(lane.HOST, 'plays its own creature', hs0) and told(lane.GUEST, 'plays its own creature', gs0), 10),
       'both games got the notice "<name> plays its own creature"')

    # 3. no choice again, the same world: still its own
    print(' ', guest.call('coop.creations unchoose creature')['out'].strip(), flush=True)
    time.sleep(8)
    print(f'  3. guest avatar {avatar(guest)}, host shows the guest as {puppet(host, 1)}', flush=True)
    ck(avatar(guest) == newest and puppet(host, 1) == hp,
       f'nothing chosen, the same world: the guest still plays its own creature ({avatar(guest)})')

    # 4. the record forgotten: as a world it never played its creature in
    print(' ', guest.call('coop.creations worlds forget')['out'].strip(), flush=True)
    ok = wait(lambda: avatar(guest) == h_av and puppet(host, 1) == h_av, 30)
    print(f'  4. guest avatar {avatar(guest)}, host shows the guest as {puppet(host, 1)}', flush=True)
    ck(ok, f'a world without the record: the world\'s species again ({avatar(guest)})')

    # 5. the co-op window: the next creation is the own one, everywhere (the host knows it under the
    #    shared key it gave it, not the guest's own id)
    gu = Ui(guest, 'guest')
    guest.call('coop.uistate 1')
    st = gu.wait(lambda s: s['open'] == '1' and s.get('creation'), 15, 'the creation row')
    print('  window:', st and st.get('creation'), flush=True)
    gu.click('cnext')
    new = wait(lambda: (lambda n: n if n and n != h_av and avatar(guest) == n and puppet(host, 1) not in (None, h_av) else None)(
        field(guest.call('coop.creations')['out'], r"mine creature: ([0-9A-F]{8})")), 30)
    print(f'  after >: mine {new}, guest avatar {avatar(guest)}, host shows {puppet(host, 1)}', flush=True)
    ck(new, 'the co-op window\'s next creation is an own creature, the guest plays it and the host shows it')
    guest.call('coop.uistate 0')
    creations_state.restore(guest, st0)
    print('  left:', guest.call('coop.creations worlds')['out'].strip(), flush=True)
    time.sleep(6)
    shot(host, 'creations_host.png')
    shot(guest, 'creations_guest.png')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
