#!/usr/bin/env python3
"""BUG-0723 (class BUG-0424): Space, a host ship whose model key is all zeros.

The host mirrors its NPC ships on the guest with the model key each ship carries (cGameDataUFO +0x780). A key of
zeros went to SpawnUFO as it was: SpawnUFO then takes its own default key 0xCB89C3D6 and the ship gfx the default
UFO 0x9D5DB21A (docs/re/space.md#space-npc-model), models the GOG 2024 install does not have: the guest's game made
a ship gfx without a model (the BUG-0546 / BUG-0686 guards keep it from faulting at 0xE98DD0 / 0xE99524).
Forced here: `coop.fleet fakemodel zero` makes the host send a key of zeros for every ship, and the host spawns NPC
ships next to both players (a pirate, an escort of the players' empire).

PASS: the guest's game makes no ship gfx without a model and needs no default model (the game fixes' counters of
the guest: `ship models defaulted`, `SetVisible without a model skipped`, `ship gfx made without a model` do not
move), and no fault. A mirror, when one is made, has a model key this game has.
FAIL (`SPORE_FIX_OFF=fleet.zero_key`): the guest spawns the mirrors with the zeros, and its game makes ships
without a model.

  SPORE_LANE=7 python3 tools/tests/coop_space_zero_key.py   (suite: space_zero_key, space_zero_key_slow at 20 fps
  over the player network)
"""
import re
import time

from twoinst import Checks, start

GUARDS = ('ship models defaulted', 'SetVisible without a model skipped', 'ship gfx made without a model')


def guards(con):
    st = con.call('coop.status')['out']
    out = []
    for g in GUARDS:
        m = re.search(re.escape(g) + r' (\d+)', st)
        out.append(int(m.group(1)) if m else -1)
    return tuple(out)


def fleet(con):
    out = con.call('coop.fleet list')['out']
    m = re.search(r'mirrors=(\d+) created=(\d+) .*unknown_models=(\d+)', out)
    z = re.search(r'zero_keys=(\d+)', out)
    ships = re.findall(r'ship #(\d+) \S+ type=(\d+) pol=(\w+) .* model=(\w+)', out)
    return (tuple(int(x) for x in m.groups()) if m else (-1, -1, -1)), (int(z.group(1)) if z else -1), ships


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(3)
    g0 = guards(guest)
    f0 = fleet(guest)
    print('  guest before:', g0, f0[0], 'zero', f0[1], flush=True)
    print(' ', host.call('coop.fleet fakemodel zero')['out'].strip(), flush=True)
    for args in ('2', '3 own', '2'):
        print('  host:', host.call(f'coop.fleet spawn {args}')['out'].strip(), flush=True)
        time.sleep(4)
    time.sleep(8)
    g1 = guards(guest)
    f1 = fleet(guest)
    print('  guest after:', g1, f1[0], 'zero', f1[1], flush=True)
    for s in f1[2]:
        print('   ', s, flush=True)
    for ln in host.call('coop.fleet list')['out'].splitlines()[1:8]:
        print('  host', ln.strip()[:150], flush=True)
    host.call('coop.fleet fakemodel 0')
    ck(f1[1] > f0[1], f'the guest got host ships with a key of zeros (zero_keys {f0[1]} -> {f1[1]})')
    ck(all(b >= 0 for b in g1), f"the guest's game reports its ship model guards ({g1})")
    ck(g1 == g0, f"the guest's game made no ship without a model for them (defaulted / SetVisible skipped / gfx "
                 f"without a model: {g0} -> {g1})")
    zero_mirrors = [s for s in f1[2] if s[3] == '00000000']
    ck(not zero_mirrors, f'no mirror of the guest has a key of zeros ({zero_mirrors})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
