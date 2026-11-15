#!/usr/bin/env python3
"""BUG-0833: a guest's mirrors of the host's eggs are given back when its world changes (no egg blocks kept alive).

The guest shows the host's eggs at another species' nest as egg objects of its own (nest.cpp mirrors, BUG-0384).
The world counter (game::world_epoch) also goes up when the guest's avatar is replaced inside the stage (the own
creature chosen in the co-op window, a new body after an editor): the mirrors were forgotten, not removed, and the
next snapshot made them again, so the guest's game held two eggs per host egg (more after every change), the old ones
never hatching or going away (and held by our references).

1. Two games on the creature fixture; both avatars at the nearest nest of another species; the host's herd there misses
   two members and lays eggs (as coop_nest_eggs.py: `coop.nest target`, the game's own AI lays them).
2. The guest's game mirrors them (`coop.nest objs`: the egg objects of its game near the nest).
3. The host sends its world again (`coop.share`): the guest's game loads it, the old world's objects are destroyed.
PASS: 10 s after the load each old mirror egg (destroyed, +0x21 set, our serial) holds no more than the references
the game itself keeps on a destroyed egg (3 in every run, with or without the fix: its view and others, not looked
into), or its block is no egg any more, and the guest mirrors the host's eggs again. FAIL
(SPORE_FIX_OFF=nest.mirror_epoch): 4, the mod's reference keeps the destroyed egg (and its herd) for good.
(A live mirror egg holds ~19 references, so "fell by two" from before the load proves nothing: lab 20261112-075547.)

  SPORE_LANE=7 python3 tools/tests/coop_nest_mirror_epoch.py
"""
import re
import time

from coop_nest_eggs import eggs, nest_herds, short
from coop_migration import in_world, mem, u32, wait
from twoinst import Checks, start

GAME_KEEPS = 3  # references the game keeps on a destroyed mirror egg (lab 20261112-074532, -075549)
EGG_VTABLE = 0x014719B0  # cEgg (its ctor 0xC6A410 writes it), refcount +8, mbIsDestroyed +0x21, mID +0x24


def main():
    host, guest = start('creature')
    ck = Checks()
    time.sleep(3)
    ns = nest_herds(host)
    if not ck(ns, 'a nest of another species with members on the host'):
        ck.faults(host=host, guest=guest)
        return ck.result()
    dist, herd, members, home = ns[0]
    hx = '%.2f %.2f %.2f' % home
    host.call(f'coop.nest look {hx} 6')
    guest.call(f'coop.nest look {hx} 8')
    time.sleep(3)
    host.call(f'coop.nest respawn {herd} 2000')
    host.call(f'coop.nest target {herd} {members + 2}')
    ho = []
    t0 = time.time()
    while time.time() - t0 < 90:
        ho = eggs(host, home)
        lay = re.search(r'eggs_to_lay=(-?\d+)', host.call(f'coop.nest herd {herd}')['out'])
        if len(ho) >= 2 and lay and int(lay.group(1)) <= 0:
            break
        time.sleep(2)
    time.sleep(6)
    ho, go = eggs(host, home), eggs(guest, home)
    print('  host eggs:', short(ho), flush=True)
    print('  guest eggs (ptr, state, over ground, serial, host egg):', short(go), flush=True)
    ck(ho, "the host's herd laid eggs at its nest")
    ck(go and len(go) == len(ho), f'the guest mirrors them ({len(go)} / {len(ho)})')
    if not (ho and go):
        ck.faults(host=host, guest=guest)
        return ck.result()
    print('  guest:', guest.call('coop.nest')['out'].strip()[:300], flush=True)
    old = [(int(e['p'], 16), e['serial']) for e in go]
    rc0 = {ptr: u32(guest, ptr + 8) for ptr, _ in old}
    print('  mirror eggs\' reference counts before the load:', {hex(k): v for k, v in rc0.items()}, flush=True)
    # the host sends its world again: the guest's game loads it (the old world's objects are destroyed)
    print('  host:', host.call('coop.share')['out'].strip()[:120], flush=True)
    time.sleep(5)
    ck(wait(lambda: in_world(guest), 180, 2), "the guest loaded the host's world again")
    guest.call(f'coop.nest look {hx} 8')
    time.sleep(10)
    held = []
    for ptr, serial in old:
        vt, rc, dead, ser = u32(guest, ptr), u32(guest, ptr + 8), mem(guest, ptr + 0x21, 1), u32(guest, ptr + 0x24)
        print(f'  old mirror egg {ptr:#x}: vtable {vt and hex(vt)} refcount {rc} destroyed {dead and dead[0]} serial {ser} '
              f'(ours {serial})', flush=True)
        # the load's destroy gives back the game's references; ours (make_mirror's AddRef) must go too
        if vt == EGG_VTABLE and ser == serial and rc and rc > GAME_KEEPS:
            held.append(ptr)
    ho2, go2 = eggs(host, home), eggs(guest, home)
    print('  host eggs after:', short(ho2), flush=True)
    print('  guest eggs after:', short(go2), flush=True)
    print('  guest:', guest.call('coop.nest')['out'].strip()[:400], flush=True)
    ck(not held, f"the old world's egg mirrors are not kept alive by the mod's references ({len(held)} of {len(old)} are)")
    ck(len(go2) == len(ho2), f"the guest's game has as many eggs at the nest as the host's ({len(go2)} / {len(ho2)})")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
