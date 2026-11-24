#!/usr/bin/env python3
"""Tribe: who owns which tribe member stays the same when the host loads its world again with the guest in the game
(TR-1 and TR-6 of the external audit, BUG-1429, BUG-1430; the reload part of E1, BUG-1154).

The host keeps a claim per member of the player tribe (its owner's stable key, by position, in the per-player store
with every save). Every 2 s the claims followed the owners, and that ran first in the host's tick, before the members
got their entries: right after a load (the game over's reload, any load in a session) with a guest present no member
had an entry yet, so every present player's claim was dropped (the host's too) and all members were dealt anew in
turns (TR-1). And the share of a player without claims was given before the members had entries, so after a load a
player whose members were all claimed by others got none until somebody joined or left (TR-6).

1. Tribe fixture. Default: the host gives all but one of its own members to the guest (`coop.give` with them selected).
   --back: the guest gives all of its members to the host (the guest owns none, every member is claimed by the host).
   The owners are noted (the host's table, the guest's game).
2. The host saves (the claims go into its store), goes to the galaxy menu (`coop.menu`) and loads the same save again
   (`coop.load`); the guest stays in the session and follows into the reloaded world.
3. PASS (default): on the host and in the guest's game each player owns as many members as before the load.
   SPORE_FIX_OFF=tribe.claims_order: the claims are dropped at the first tick, the members dealt in turns: FAIL.
   PASS (--back): the guest owns its share again (the members / the players, at least one) on both games.
   SPORE_FIX_OFF=tribe.claims_balance: the guest owns no member after the load: FAIL.

--shared: the same with the session setting all.shared_dna on (`coop.settings force`; the default is own DNA per
player, 0.7.16).

--fresh-store (NET-3 of the audit, BUG-1499): right before the save the host's store is closed (`coop.store hold`: as
before the world had its name, or a save within a second of a load), so the save opens the world's store itself.
PASS: the claims are in that save as usual (the owners after the load as before). SPORE_FIX_OFF=store.save_opened: the
save writes no tribe.owners of this moment (the store's serial was new to the tribe code): FAIL.

  SPORE_LANE=4 python3 tools/tests/coop_tribe_owners_reload.py [--back] [--shared] [--fresh-store]
"""
import sys
import time

import settings_lib
import twoinst

from harness import waiter
from coop_tribe import members
from twoinst import Checks, keyframe, played_slot, save_taken, start, wait_guest_in

wait = waiter(step=1.0, final=True)


def owners(con):
    """{owner: count} of the player tribe's members in this game"""
    out = {}
    for m in members(con).values():
        if m[3] == 'player':
            out[m[0]] = out.get(m[0], 0) + 1
    return out


def mode(con):
    try:
        return con.call('game.mode')['out']
    except OSError:
        return ''


def claims(con):
    return next((ln for ln in con.call('coop.status')['out'].splitlines() if ln.startswith('claims:')), '-')


def give(frm, ids, to):
    """frm selects exactly ids of the player tribe and gives them to player `to`"""
    for i in members(frm):
        frm.call(f'coop.tribe select {i} 0')
    for i in ids:
        frm.call(f'coop.tribe select {i} 1')
    print(f'  {"host" if to else "guest"} gives {len(ids)}:', frm.call(f'coop.give {to}')['out'].strip()[:160], flush=True)


def main():
    host, guest = start('tribe')
    ck = Checks()
    shared = '--shared' in sys.argv
    if shared:
        ck(settings_lib.force(host, [guest], 'all.shared_dna', 'on'), 'the guest plays with all.shared_dna on')
    try:
        return run(host, guest, ck, '--back' in sys.argv)
    finally:
        if shared:
            settings_lib.reset([host, guest], ('all.shared_dna',))
            twoinst.forget_settings()


def run(host, guest, ck, back):
    time.sleep(5)
    hm = members(host)
    print(f'  members at the start: host {owners(host)}, guest {owners(guest)}', flush=True)
    if back:
        theirs = [i for i, v in hm.items() if v[0] == 1 and v[3] == 'player']
        if not ck(theirs, f'the guest owns members to give ({len(theirs)})'):
            return ck.result()
        give(guest, theirs, 0)
        ck(wait(lambda: owners(host).get(1, 0) == 0, 15), f'the guest owns no member now ({owners(host)})')
    else:
        mine = [i for i, v in hm.items() if v[0] == 0 and v[3] == 'player']
        if not ck(len(mine) >= 2, f'the host owns members to give ({len(mine)})'):
            return ck.result()
        give(host, mine[:len(mine) - 1], 1)  # all but one: the guest owns more than its share of the turns
        ck(wait(lambda: owners(host).get(0, 0) == 1, 15), f'the host owns one member now ({owners(host)})')
    before = owners(host)
    total = sum(before.values())
    print(f'  before the load: host {before}, guest {owners(guest)} | host {claims(host)}', flush=True)
    ck(wait(lambda: owners(guest) == before, 10), f'the guest\'s game shows the same owners ({owners(guest)})')
    time.sleep(3)  # the claims follow every 2 s

    # 2. save (the store gets the claims), the galaxy menu, the same save again
    if '--fresh-store' in sys.argv:  # BUG-1499: the save opens the store itself
        print('  host:', host.call('coop.store hold')['out'].strip(), flush=True)
    out, refused, _ = save_taken(host, 30, 'host save')
    if '--fresh-store' in sys.argv:
        # Spore writes the save asynchronously (~1-2 s after coop.save returns): wait for after_save() to open the store.
        wait(lambda: any('opened_by_save=' in ln and 'opened_by_save=0' not in ln
                         for ln in (host.call('coop.store')['out'] or '').splitlines()[:1]), 15)
        st = (host.call('coop.store')['out'] or '').strip().splitlines()
        print('  host store after the save:', ' | '.join(st[:4]), flush=True)
        ck(any('opened_by_save=' in ln and 'opened_by_save=0' not in ln for ln in st[:1]),
           'the save opened the host\'s store itself (the case is reached)')
    print(f'  host save: {out[:120]}' + (f' (after {refused} refusals)' if refused else ''), flush=True)
    time.sleep(3)
    host.call('key enter')  # the game's "game saved" box
    slot, planet = played_slot()
    if not ck(slot and planet, f'the host\'s save slot is known ({slot} {planet})'):
        return ck.result()
    print('  host menu:', host.call('coop.menu')['out'].strip(), flush=True)
    ck(wait(lambda: 'GalaxyGameEntry' in mode(host), 90), 'the host is in the galaxy menu')
    host.call('wait.frames 240 90000')
    print(f'  host load {slot} {planet}:', host.call(f'coop.load {slot} {planet}')['out'].strip(), flush=True)
    ck(wait(lambda: 'Tribe' in mode(host) and owners(host), 180, 2), 'the host plays its Tribe world again')
    ck(wait_guest_in(host, guest, 180), 'the guest is in the reloaded world')
    time.sleep(8)

    # 3. the owners after the load
    after = owners(host)
    print(f'  after the load: host {after}, guest {owners(guest)} (before: {before}) | host {claims(host)}', flush=True)
    if back:
        share = max(1, sum(after.values()) // 2)
        ck(after.get(1, 0) >= share, f'after the load the guest owns its share again: {after.get(1, 0)} of '
           f'{sum(after.values())} members (at least {share}; before the load it owned none)')
    else:
        ck(after.get(1, 0) == before.get(1, 0) and after.get(0, 0) == before.get(0, 0),
           f'after the load the guest owns {after.get(1, 0)} members as before ({before.get(1, 0)}, the gift included), '
           f'the host {after.get(0, 0)} ({before.get(0, 0)}; {total} in all before)')
    ck(wait(lambda: owners(guest) == owners(host), 15), f'the guest\'s game shows the host\'s owners ({owners(guest)})')
    keyframe('reloaded', "Tribe after the host's load: the guest's members (its colour on the co-op window's list) "
             + ('are its share again' if back else 'are the ones it was given'))
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
