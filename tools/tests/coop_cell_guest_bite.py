#!/usr/bin/env python3
"""Cell stage: a guest's bite on the host's NPC is judged as the host's own bite (BUG-0078).

DamageCell 0xE7A7C0 does nothing when ShouldNotAttack(victim, attacker) is true (the victim's
species "won't attack the player", same species, friend group) or the victim has +0x17C / +0x111
set. The host applied a guest's bite (MSG_CELL_HIT) without these checks and asked ShouldNotAttack
with the arguments swapped, so the guest hurt cells the host's own bite cannot.

On one NPC creature (the one nearest the guest):
  1. a plain guest bite takes hp on the host (control);
  2. its species gets "won't attack the player" (`coop.pool npcflag c<key> spare 1`, host): the
     host's own bite leaves its hp as it was (the game's rule works), and so does the guest's;
  3. the same with the NPC's own +0x111 instead (`npcflag c<key> invuln 1`);
  4. both flags off: the guest's bite hurts it again.
The NPC's hp is read in the host's game (`coop.pool cell`).

SPORE_CELL_FIX=0 switches the fix off on the host (`coop.pool fix guestbite 0`): the run that must FAIL.

BUG-0240: DamageCell's IsDamageable (0xE52940) also wants the resource's cell type 2 (solid) and an
AI type other than 0 (meat). On a non-solid cell (a plant, given hp to lose) and on meat when one
is there (`coop.pool kill <key> anyhit`, DamageCell on a cell of any kind):
  5. the host's own bite takes nothing (the game's rule);
  6. the guest's bite takes nothing in the host's game.
SPORE_FIX_OFF=pool.guestbite_type must FAIL at 6.
"""
import os
import re
import time

from twoinst import Checks, start


def hp(host, key):
    m = re.search(r'\bhp=(-?\d+)', host.call(f'coop.pool cell {key}')['out'])
    return int(m.group(1)) if m else None


def bite(con, key):
    return con.call(f'coop.pool kill {key} hit')['out']


def main():
    fix = os.environ.get('SPORE_CELL_FIX', '1')
    host, guest = start('cell')
    ck = Checks()
    key = None
    try:
        out = host.call(f'coop.pool fix guestbite {fix}')['out']
        ck(f'guestbite={fix}' in out, f'fix set to {fix}: {out.strip()}')
        time.sleep(3)
        # an NPC with enough hp for a few bites: the nearest creature of the guest
        for _ in range(10):
            r = guest.call('coop.pool kill - hit')['out']
            m = re.match(r'bit (c\d+) .* hp=(\d+)', r)
            if m and 'dying' not in r:
                key = m.group(1)
                break
            time.sleep(1)
        ck(key is not None, f'the guest found an NPC creature ({r})')
        if not key:
            return ck.result()

        def guest_bite(label, expect_hurt):
            host.call(f'coop.pool swimto {key} tp')  # it fled: both stay next to it (in the host's state area)
            guest.call(f'coop.pool swimto {key} tp')
            time.sleep(2.0)  # the hurt lock (+0x17C) of the last bite is over
            h0 = hp(host, key)
            r = bite(guest, key)
            time.sleep(1.5)
            h1 = hp(host, key)
            print(f'  {label}: guest {r}; host hp {h0} -> {h1}')
            if expect_hurt:
                ck(h0 is not None and h1 is not None and h1 < h0, f'{label}: the guest\'s bite took hp ({h0} -> {h1})')
            else:
                ck(h0 is not None and h1 is not None and h1 >= h0, f'{label}: the guest\'s bite took nothing ({h0} -> {h1})')

        def host_bite(label):
            time.sleep(2.0)
            h0 = hp(host, key)
            r = bite(host, key)
            time.sleep(0.5)
            h1 = hp(host, key)
            print(f'  {label}: host {r}; host hp {h0} -> {h1}')
            ck(h0 is not None and h1 is not None and h1 >= h0, f'{label}: the host\'s own bite took nothing ({h0} -> {h1})')

        print('  ' + host.call(f'coop.pool npcflag {key} hp 60')['out'])  # room for the bites (NPCs have 6 or so)
        # 1. control: a guest bite hurts it
        guest_bite('plain', True)
        # 2. the species won't attack the player
        print('  ' + host.call(f'coop.pool npcflag {key} spare 1')['out'])
        host_bite('spare')
        guest_bite('spare', False)
        print('  ' + host.call(f'coop.pool npcflag {key} spare 0')['out'])
        # 3. the NPC is invulnerable (+0x111)
        print('  ' + host.call(f'coop.pool npcflag {key} invuln 1')['out'])
        host_bite('invuln')
        guest_bite('invuln', False)
        print('  ' + host.call(f'coop.pool npcflag {key} invuln 0')['out'])
        # 4. back to normal
        guest_bite('again', True)
        print('  host: ' + ' '.join(re.findall(r'hits_(?:in|aggro|spared)=\d+', host.call('coop.pool')['out'])))

        # 5, 6 (BUG-0240): cells IsDamageable refuses
        def other_kind(kind):
            r = host.call(f'coop.pool kill - anyhit {kind}')['out']  # also the host's first bite on it
            m = re.match(r'bit (c\d+) ', r)
            print(f'  {kind}: host {r}')
            if not m:
                return False
            k = m.group(1)
            others.append(k)
            print('  ' + host.call(f'coop.pool npcflag {k} hp 60')['out'])
            time.sleep(2.0)
            h0 = hp(host, k)
            r = host.call(f'coop.pool kill {k} anyhit')['out']
            time.sleep(0.5)
            h1 = hp(host, k)
            print(f'  {kind}: host {r}; host hp {h0} -> {h1}')
            ck(h0 is not None and h1 is not None and h1 >= h0, f'{kind}: the host\'s own bite took nothing ({h0} -> {h1})')
            r = 'gone'
            for _ in range(3):
                r = guest.call(f'coop.pool kill {k} anyhit')['out']
                if r.startswith('bit '):
                    break
                guest.call(f'coop.pool swimto {k}')  # not mirrored yet: closer to it
                time.sleep(2.0)
            time.sleep(1.5)
            h2 = hp(host, k)
            print(f'  {kind}: guest {r}; host hp {h1} -> {h2}')
            ck(r.startswith('bit '), f'{kind}: the guest bit its mirror ({r})')
            ck(h1 is not None and h2 is not None and h2 >= h1, f'{kind}: the guest\'s bite took nothing ({h1} -> {h2})')
            return True
        others = []
        # cell type not 2: a rock or the like first (a plant may be eaten while the test runs)
        ck(any(other_kind(k) for k in ('rock', 'poison', 'bubble', 'nanite', 'plant')), 'a non-solid cell in the pool')
        other_kind('meat')  # AI type 0, when the pool has meat
    finally:
        if key:
            host.call(f'coop.pool npcflag {key} spare 0')
            host.call(f'coop.pool npcflag {key} invuln 0')
        host.call('coop.pool fix guestbite 1')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
