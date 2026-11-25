#!/usr/bin/env python3
"""WP-081-3 (BUG-1564): Tribe guest consequence abilities + Creature->Tribe pack pet transfer in Tribe Rivals.

Exercises in two real Wine Spore instances (Tribe Rivals):
  1. Enable Tribe Rivals (`coop.rivals mode tribe rivals`), wait for host R2 takeover of an NPC cTribe
     for guest (pid=1) and guest R1 perspective swap (`swapped=1`).
  2. Part A (`tribe.guest_consequence`):
     - Give host (pid=0) Herbivore + Aggressive cards and guest (pid=1) Carnivore + Social cards.
     - Guest activates Traps (52, matching guest Cell Carnivore) -> routed via MSG_TRIBE_CMD
       kCmdConsequenceAbility to host and executed on guest's rival cTribe chieftain
       (`cons_ab_tx > 0`, `cons_ab_ran >= 1`).
     - Guest activates Refreshing Storm (53, Herbivore: host's card, NOT guest's) -> denied on host
       (`cons_ab_denied >= 1`, `cons_ab_ran` unchanged).
     - Guest activates Fireworks (56, matching guest Creature Social) -> executed on host (`cons_ab_ran >= 2`).
  3. Part B (`rivals.tribe_pack_pets`):
     - Record Creature pack species for guest (pid=1) via `coop.tribe rivals set_pack_pets 1 auto`.
     - Verify real domesticated pets spawn at guest's rival village (`pack_pets_spawned > 0`,
       `1 <= dom_count <= 3`).
     - Verify idempotency across subsequent ticks (no duplicate pets spawned).
  4. Zero faults on both host and guest.
  5. Twin (`SPORE_FIX_OFF=tribe.guest_consequence,rivals.tribe_pack_pets`): both paths disabled -> FAIL.

  SPORE_LANE=4 python3 tools/tests/coop_rivals_tribe_abilities_pets.py
"""
import re
import time

import twoinst
from twoinst import Checks, start


def tr_out(con):
    return con.call('coop.tribe rivals').get('out') or ''


def tribe_out(con):
    return con.call('coop.tribe').get('out') or ''


def parse_int(text, key):
    m = re.search(rf'\b{re.escape(key)}=(-?\d+)', text)
    return int(m.group(1)) if m else None


def wait_for(fn, timeout=12.0, step=0.3):
    t0 = time.time()
    while time.time() - t0 < timeout:
        v = fn()
        if v:
            return v
        time.sleep(step)
    return None


def main():
    twoinst.forget_settings()
    host, guest = start('tribe')
    ck = Checks()
    try:
        host.call('coop.drama hold 120')
        time.sleep(2.0)

        # 1. Enable Tribe Rivals and wait for guest tribe assignment + perspective swap
        host.call('coop.rivals mode tribe rivals')
        h_asgn = wait_for(
            lambda: tr_out(host) if 'active=1' in tr_out(host) and 'assign pid=1 ' in tr_out(host) else None,
            15.0,
        )
        ck(bool(h_asgn), f'host Tribe Rivals active and assigned rival cTribe to guest ({(h_asgn or tr_out(host)).splitlines()[0]})')
        if not h_asgn:
            return ck.result()

        g_sw = wait_for(lambda: tr_out(guest) if 'active=1 swapped=1' in tr_out(guest) else None, 12.0)
        ck(bool(g_sw), f'guest perspective swapped to rival tribe ({(g_sw or tr_out(guest)).splitlines()[0]})')

        # 2. Part A: Guest Tribe Consequence Abilities (own cards vs host's cards)
        # Host: Cell Herbivore (A8EC6F99) + Creature Aggressive (17E5EF84)
        # Guest: Cell Carnivore (CFB01B93 -> Traps 52) + Creature Social (FD159D91 -> Fireworks 56)
        host.call('coop.history set 0 0 A8EC6F99')
        host.call('coop.history set 0 1 17E5EF84')
        host.call('coop.history set 1 0 CFB01B93')
        host.call('coop.history set 1 1 FD159D91')
        time.sleep(1.2)

        # Guest activates Traps (52, matches guest Cell Carnivore)
        u1 = guest.call('coop.tribe rivals use_ability 52').get('out') or ''
        ran1 = wait_for(
            lambda: (parse_int(tribe_out(host), 'cons_ab_ran') or 0) >= 1
            and (parse_int(tribe_out(guest), 'cons_ab_tx') or 0) >= 1,
            8.0,
        )
        h_st1 = tribe_out(host)
        g_st1 = tribe_out(guest)
        ck(
            bool(ran1) and 'ok:' in u1,
            f'guest Traps (52) matched guest Carnivore card and ran on host '
            f'(u1={u1.strip()}, tx={parse_int(g_st1, "cons_ab_tx")}, '
            f'rx={parse_int(h_st1, "cons_ab_rx")}, ran={parse_int(h_st1, "cons_ab_ran")})',
        )

        # Guest attempts Refreshing Storm (53, Herbivore: host has it, guest does NOT) -> must be denied
        u2 = guest.call('coop.tribe rivals use_ability 53').get('out') or ''
        den1 = wait_for(lambda: (parse_int(tribe_out(host), 'cons_ab_denied') or 0) >= 1, 8.0)
        h_st2 = tribe_out(host)
        ck(
            bool(den1) and (parse_int(h_st2, 'cons_ab_ran') or 0) == 1,
            f'guest Refreshing Storm (53) denied against guest cards (u2={u2.strip()}, '
            f'denied={parse_int(h_st2, "cons_ab_denied")}, ran={parse_int(h_st2, "cons_ab_ran")})',
        )

        # Guest activates Fireworks (56, matches guest Creature Social) -> runs on host
        u3 = guest.call('coop.tribe rivals use_ability 56').get('out') or ''
        ran2 = wait_for(lambda: (parse_int(tribe_out(host), 'cons_ab_ran') or 0) >= 2, 8.0)
        h_st3 = tribe_out(host)
        ck(
            bool(ran2) and 'ok:' in u3,
            f'guest Fireworks (56) matched guest Social card and ran on host '
            f'(u3={u3.strip()}, ran={parse_int(h_st3, "cons_ab_ran")})',
        )

        # 3. Part B: Creature -> Tribe pack pet transfer in Tribe Rivals
        pets_before = host.call('coop.tribe rivals pets 1').get('out') or ''
        dom_before = parse_int(pets_before, 'dom_count') or 0

        set_out = host.call('coop.tribe rivals set_pack_pets 1 auto').get('out') or ''
        time.sleep(1.0)
        pets_after = host.call('coop.tribe rivals pets 1').get('out') or ''
        dom_after = parse_int(pets_after, 'dom_count') or 0
        spawned_1 = parse_int(tr_out(host), 'pack_pets_spawned') or 0
        ck(
            'ok:' in set_out and spawned_1 > 0 and dom_after > dom_before and dom_after <= 3,
            f'guest Creature pack species spawned as real domesticated village pets '
            f'({set_out.strip()} | {pets_after.strip()})',
        )

        # Idempotency check: wait across multiple ticks and verify no duplicates are created
        time.sleep(2.5)
        pets_later = host.call('coop.tribe rivals pets 1').get('out') or ''
        dom_later = parse_int(pets_later, 'dom_count') or 0
        spawned_2 = parse_int(tr_out(host), 'pack_pets_spawned') or 0
        ck(
            dom_later == dom_after and spawned_2 == spawned_1,
            f'pack pet spawn is idempotent (dom {dom_after}->{dom_later}, spawned {spawned_1}->{spawned_2})',
        )

        ck.faults(host=host, guest=guest)
        return ck.result()
    finally:
        pass


if __name__ == '__main__':
    raise SystemExit(main())
