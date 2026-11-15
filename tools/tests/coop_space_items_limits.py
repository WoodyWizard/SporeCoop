#!/usr/bin/env python3
"""Space: the limits of the kept guest hold (BUG-0679, after BUG-0678's space.keep_items).

The host kept each guest's own cargo hold in its memory only and gave it back on a rejoin. Not covered were: a host
that restarts its game between the guest's leave and join (nothing kept: the guest got the host's hold), what changed
in the guest's hold while it was away, a guest with fewer cargo slots than the host (it kept the host's extra ones)
and plant / animal / artifact cargo the host's copy lacks (it could not be made again).

1. Both play the `space` fixture. The holds are made different: the guest gets spice A x7 and an artifact (type 5
   cargo, the game's own factory: `coop.space trade cargo 5 <instance>`); the host gets spice B x5 and 3 more cargo
   slots than the guest.
2. The guest leaves; while away it gets 3 more of spice A (its hold changes after the session ended).
3. Both games are closed and started again (the host restarts: nothing in its memory), the host loads the fixture,
   gets spice B x5 and 3 more slots again, the guest joins and loads the host's world.
4. In the guest's game (`coop.space trade inv`: the game's cPlayerInventory items) the hold is its own, as it was when
   it left plus the change while away: spice A x10, the artifact, no spice B of the host, its own number of slots.
PASS: 4. SPORE_FIX_OFF=space.keep_items_local: the guest has the host's hold (FAIL); space.keep_items_slots: the host's
extra slots (FAIL); space.keep_items_cargo: no artifact (FAIL). (The switches go off on the restarted guest before it
joins.)

  SPORE_LANE=9 python3 tools/tests/coop_space_items_limits.py
"""
import os
import time

from coop_space_items_rejoin import inv, spice_keys
from coop_space_map import no_movie, sp
import lane
from twoinst import Checks, Console, start

ARTIFACT = '10D5D4D3'  # a property file of group 0x037D494E (the artifact cargo the game's factory makes)
OFF = [n.strip() for n in os.environ.get('SPORE_FIX_OFF', '').split(',') if n.strip()]


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(3)
    no_movie(guest)
    no_movie(host)
    keys = spice_keys(guest)
    ck(len(keys) >= 2, f'the game knows at least two spices ({len(keys)})')
    if len(keys) < 2:
        return ck.result()
    sa, sb, art = (4, keys[0]), (4, keys[1]), (5, ARTIFACT)
    print('  guest: ' + sp(guest, 'trade spice 0 7') + ' | ' + sp(guest, f'trade cargo 5 {ARTIFACT}'), flush=True)
    print('  host: ' + sp(host, 'trade spice 1 5') + ' | ' + sp(host, 'trade slots 3'), flush=True)
    g1, gslots = inv(guest)
    h1, hslots = inv(host)
    print(f'  guest {g1} slots {gslots}; host slots {hslots}', flush=True)
    ck(g1.get(sa, (0,))[0] == 7 and art in g1 and sb not in g1, 'the guest has spice A x7 and the artifact, no spice B')
    ck(hslots > gslots and sb in h1 and art not in h1, f'the host has more slots ({hslots} > {gslots}), spice B, no artifact')
    time.sleep(4)  # the guest's hold reaches the host and the guest's record
    print('  guest: ' + sp(guest, 'trade kept').splitlines()[0], flush=True)

    # 2. away: the hold changes after the session ended
    guest.call('coop.leave')
    time.sleep(3)
    print('  guest away: ' + sp(guest, 'trade spice 0 3'), flush=True)
    time.sleep(3)
    g2, _ = inv(guest)
    print(f'  guest away: spice A {g2.get(sa)}; ' + sp(guest, 'trade kept').splitlines()[0], flush=True)
    ck(g2.get(sa, (0,))[0] == 10, 'away, the guest has spice A x10')

    # 3. the host restarts (both games of the lane: one bottle), the guest joins again
    def before_join(g):
        h = Console(lane.HOST)  # the restarted host's hold differs again: its spice B, its 3 more slots
        print('  restarted host: ' + sp(h, 'trade spice 1 5') + ' | ' + sp(h, 'trade slots 3'), flush=True)
        for n in OFF:
            print('  SPORE_FIX_OFF before the join: ' + g.call(f'coop.fix {n} off')['out'].strip(), flush=True)
    host, guest = start('space', keep_holds=True, before_join=before_join)
    no_movie(guest)

    def own():
        g, _ = inv(guest)
        return g if g.get(sa, (0,))[0] == 10 and art in g else None
    t0 = time.time()
    while time.time() - t0 < 40 and not own():
        time.sleep(2)
    g3, gslots3 = inv(guest)
    print(f'  rejoined guest after the host restart: {g3} slots {gslots3}; ' + sp(guest, 'trade kept').splitlines()[0],
          flush=True)
    ck(g3.get(sa, (0,))[0] == 10, f'the guest has its spice A x10 (its hold when it left and the change while away): {g3.get(sa)}')
    ck(sb not in g3, f"the host's spice B is not in the guest's hold ({g3.get(sb)})")
    ck(art in g3, 'the guest has its artifact back (plant / animal / artifact cargo the host lacks)')
    ck(gslots3 == gslots, f"the guest has its own cargo slots ({gslots3}, before {gslots}; the host's {hslots})")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
