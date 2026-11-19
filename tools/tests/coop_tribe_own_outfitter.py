#!/usr/bin/env python3
"""Tribe: a guest's own Tribal Outfitter dresses the guest's members on both games, not the host's (BUG-1272, WP-T2
of docs/plan-0.7.16.md, docs/re/tribe.md#tribe-own-outfitter).

In creature.mode own (the default) a guest plays its own creature and its tribe members are of its species line on
every game (WP-T1, BUG-1271). The tribe card's edit badge on the guest opens the outfitter for that line (the request's
key is the guest's own creature, not the card's slot key), the return on the guest does not reload the tribe's species
slot (the host's species) and the new creation becomes the guest's tribe outfit (species::set_tribe_outfit): its
bundle goes to the host and from there to every game, and every game dresses the guest's members in it.

1. Two games on the tribe fixture; the host's tribe gets 2 more adults. The guest plays a creature of its own My
   Creations (`coop.creations choose creature`); within 25 s every guest-owned member is of its key on both games
   (the precondition of WP-T1).
2. The players' route on the guest (docs/re/creations.md#creations-stage-designs): a click on its hut, the planner's
   second tab, the card's edit badge. PASS: the guest is in the outfitter within 20 s, the request asked for the
   guest's own species (`coop.designs`: asked = `coop.species own tribe`), the card's key was the slot's.
3. In the outfitter: the third tab, a body piece dragged onto the chest, Done. PASS: back in Tribe, a new creation in
   the guest's My Creations, the return skipped the slot reload (skipped=1) and the guest's tribe species slot is
   still the host's (its key and its model as before, on the guest).
4. Within 40 s on both games: player 1's Tribe key (`coop.species key 1 tribe`) is one shared key, not the
   creature's, drawn from the guest's new creation (its model hash = the hash of the new creation on the guest's own
   game), every guest-owned member is of that key, every host-owned member of the slot's key and drawn from the
   slot's model. Key frames: both games' cameras, the same view 12 m from a guest's member (the guest's village part)
   and from a host's member (the host's), on both screens.
5. The host saves (`coop.save`): no file of its save slot names the outfit's key, the guest's new creation or its
   creature (keys_in_slot of coop_tribe_own_species: every record unpacked; an instance that is only an id of the
   host's own resources, given out by the host's id counter, is not the guest's key: BUG-1326).
The guest's creature choice is put back at the end.

SPORE_FIX_OFF=tribe.guest_outfitter: step 2 FAILs (BUG-1158's refusal, 0.7.15: the guest stays in Tribe); the test
leaves the planner and ends there.
SPORE_FIX_OFF=savestrip.keys (tribe_own_outfitter_keys_off): step 5 FAILs (the guest's members write the outfit's
line key into the host's .spo, BUG-1315).

  SPORE_LANE=4 python3 tools/tests/coop_tribe_own_outfitter.py
"""
import re
import time

import lane
import twoinst
from coop_content_civ import save_slot, wait_saved
from coop_tribe_looks import members as looks
from coop_tribe_outfit import (BADGE, DONE, EDITOR, ITEM, ONTO, TAB3, TRIBE, busy, close_planner, drag, mode,
                               my_creations, open_outfitter, profile)
from coop_tribe_own_species import closeup, keys_in_slot, line_key, mix_ok, model_hash, species
from twoinst import Checks, Console, start

SWITCH = 'tribe.guest_outfitter'


def designs(con):
    """the own outfitter's line of `coop.designs` as a dict (asked, card, skipped, result, set, ...)"""
    out = con.call('coop.designs').get('out') or ''
    m = re.search(r'^own outfitter: (.*)$', out, re.M)
    d = {}
    for k, v in re.findall(r'(\w+)=([0-9A-Fa-f/]+)', m.group(1) if m else ''):
        d[k] = v
    return d, out


def tribe_line(con, pid=1):
    """(key instance, model hash) of player pid's Tribe line on this game"""
    out = con.call(f'coop.species key {pid} tribe').get('out') or ''
    m = re.search(r'key=(\w+)/\w+/\w+ known=(\d) model=(\S+)', out)
    return (int(m.group(1), 16), m.group(3)) if m else (0, '?')


def own_tribe(con):
    m = re.search(r'own=(\w+)/', con.call('coop.species own tribe').get('out') or '')
    return int(m.group(1), 16) if m else 0


def outfitter_route(guest, ck, gk0, own0, mine0):
    """steps 2-3: the guest's own Tribal Outfitter the players' way (a click on its hut, the planner's second tab, the
    card's edit badge), the third tab, a body piece dragged onto the chest, Done, with the checks of the route. gk0: the
    guest's slot key before, own0: its own Tribe line (`coop.species own tribe`), mine0: its My Creations before.
    None when the outfitter did not open (the planner closed again); else {'back': back in Tribe, 'new': the new
    creations of its My Creations, 'result': the outfitter's result instance, 'designs': the `coop.designs` line}.
    Also coop_line_stages.py (BUG-1277: the outfit made in the Tribe part of the stage line)"""
    ck(open_outfitter(guest), 'the guest\'s hut planner opened')
    t0 = time.time()
    while time.time() - t0 < 20 and mode(guest) != EDITOR:
        if time.time() - t0 > 8 and mode(guest) == TRIBE and 'input_lock' in busy(guest):
            guest.call('click %d %d' % BADGE)  # (once more: the tab took the click)
            time.sleep(4)
        time.sleep(1)
    d, out = designs(guest)
    print(f'  guest after the badge: mode {mode(guest)}; {d}', flush=True)
    if not ck(mode(guest) == EDITOR, f'the guest\'s own Tribal Outfitter opens ({mode(guest)}, refused '
              f'{re.search(r"refused=(\d+)", out).group(1) if "refused=" in out else "?"})'):
        close_planner(guest)
        time.sleep(3)
        print(f'  guest busy after: {busy(guest) or "-"}', flush=True)
        return None
    asked = int(d.get('asked', '0/').split('/')[0], 16)
    card = int(d.get('card', '0'), 16)
    ck(asked == (own0 or gk0), f'the outfitter was asked for the guest\'s own species ({asked:08X}, its own tribe line '
       f'{own0:08X})')
    ck(card == gk0, f'the card\'s key was the tribe\'s slot ({card:08X}, slot {gk0 or 0:08X})')

    # 3. a piece of clothing, Done
    guest.call('wait.frames 120 30000')
    guest.call('click %d %d' % TAB3)
    time.sleep(1.5)
    drag(guest, ITEM, ONTO)
    time.sleep(1)
    guest.call('move %d %d' % DONE)
    time.sleep(0.5)
    guest.call('click %d %d' % DONE)
    t0 = time.time()
    while time.time() - t0 < 60 and mode(guest) != TRIBE:
        time.sleep(1)
    if not ck(mode(guest) == TRIBE, f'the guest is back in Tribe ({mode(guest)})'):
        return dict(back=False, new=set(), result=0, designs=d)
    time.sleep(3)
    close_planner(guest)
    new = my_creations(lane.GUEST_DIR) - mine0
    d, _ = designs(guest)
    result = int(d.get('result', '0'), 16) or next(iter(new), 0)
    print(f'  guest: new in its My Creations {sorted("%08X" % n for n in new)}; {d}', flush=True)
    ck(new, 'the outfitter saved a new creation in the guest\'s My Creations')
    ck(int(d.get('skipped', '0')) >= 1, f'the guest\'s return skipped the slot reload (skipped={d.get("skipped")})')
    return dict(back=True, new=new, result=result, designs=d)


def main():
    host, guest = start('tribe')
    ck = Checks()
    off_twin = SWITCH in twoinst.fix_off_names()
    rec0 = (guest.call('coop.creations worlds').get('out') or '').strip()
    m = re.search(r'creature=(\w+)', rec0)
    rec_creature = m.group(1) if m else '00000000'
    try:
        return run(host, guest, ck, off_twin)
    finally:
        try:  # the guest's lane profile as it was (the next test of the tribe fixture plays the world's species)
            g = Console(lane.GUEST)
            print('  guest:', g.call('coop.creations unchoose creature')['out'].strip(), '|',
                  g.call(f'coop.creations worlds set creature {rec_creature}')['out'].strip(), flush=True)
        except OSError:
            pass


def run(host, guest, ck, off_twin):
    time.sleep(4)
    print(' ', host.call('coop.tribe spawn 2')['out'].strip(), flush=True)
    time.sleep(4)
    slot, _, _ = species(host)
    own = [k for k in re.findall(r'^creature ([0-9A-F]{8}) ', guest.call('coop.creations list')['out'], re.M)
           if int(k, 16) != slot]
    if not ck(own and slot, f'the guest has creatures of its own ({own[:4]}), the tribe a species ({slot or 0:08X})'):
        return ck.result()
    orig = int(own[0], 16)
    print('  guest:', guest.call(f'coop.creations choose creature {own[0]}')['out'].strip(), flush=True)
    t0 = time.time()
    gkey = 0
    while time.time() - t0 < 30:
        gkey = line_key(guest, 1)
        if gkey and line_key(host, 1) == gkey:
            break
        time.sleep(1)
    if not ck(gkey and line_key(host, 1) == gkey, f'both games have the guest\'s creature under one key '
              f'({gkey:08X}, its own {orig:08X})'):
        return ck.result()

    # 1. the precondition: the guest's members of its creature (WP-T1)
    t0 = time.time()
    res = {}
    while time.time() - t0 < 25:
        res = {r: mix_ok(c, gkey) for r, c in (('host', host), ('guest', guest))}
        if all(ok for ok, _ in res.values()):
            break
        time.sleep(1)
    for r, (ok, txt) in res.items():
        print(f'  {r} before: {txt}', flush=True)
        ck(ok, f'{r}: before the outfitter the guest\'s members are of its creature {gkey:08X}, the host\'s of the '
               f'tribe\'s {slot:08X}')
    gk0, gm0, _, _ = profile(guest)
    hk0, hm0, _, _ = profile(host)
    own0 = own_tribe(guest)
    hline0 = tribe_line(host)
    print(f'  slot: host {hk0 or 0:08X} model {hm0}, guest {gk0 or 0:08X} model {gm0}; the guest\'s own tribe line '
          f'{own0:08X}; player 1\'s tribe line on the host {hline0[0]:08X} ({hline0[1]})', flush=True)
    mine0 = my_creations(lane.GUEST_DIR)

    # 2-3. the guest's own outfitter, the players' way, and a piece of clothing
    got = outfitter_route(guest, ck, gk0, own0, mine0)
    if got is None:
        ck.faults(host=host, guest=guest)
        return ck.result()
    if not got['back']:
        return ck.result()
    new, result = got['new'], got['result']
    gk1, gm1, _, _ = profile(guest)
    ck(gk1 == hk0 and gm1 == gm0, f'the guest\'s tribe slot is still the host\'s species ({gk1 or 0:08X} model {gm1}, '
       f'before {gk0 or 0:08X} model {gm0}, the host\'s {hk0 or 0:08X})')
    rhash = model_hash(guest, result) if result else '?'

    # 4. the guest's members wear it on both games, the host's do not
    t0 = time.time()
    lines = {}
    while time.time() - t0 < 40:
        lines = {r: tribe_line(c) for r, c in (('host', host), ('guest', guest))}
        k = lines['host'][0]
        if k and k != gkey and lines['guest'][0] == k:
            res = {r: mix_ok(c, k) for r, c in (('host', host), ('guest', guest))}
            if all(ok for ok, _ in res.values()):
                break
        time.sleep(1)
    print(f'  player 1\'s tribe line: {lines} (its creature {gkey:08X}, the outfit {result:08X} -> {rhash})', flush=True)
    okey = lines.get('host', (0, '?'))[0]
    ck(okey and okey != gkey and lines['guest'][0] == okey,
       f'both games have one tribe line for the guest that is not its creature ({okey:08X})')
    ck(rhash != '?' and all(lines[r][1] == rhash for r in lines),
       f'the line is drawn from the guest\'s new creation on both games ({lines}, the creation {rhash})')
    slot_hash = {}
    for r, con in (('host', host), ('guest', guest)):
        ok, txt = mix_ok(con, okey)
        print(f'  {r}: {txt}', flush=True)
        ck(ok, f'{r}: the guest\'s members are of its outfit {okey:08X}, the host\'s of the tribe\'s {slot:08X}')
        s_key, s_model, ms = species(con)
        gmodels = {model_hash(con, x['model']) for x in ms.values() if x['owner'] == 1 and not x['dead']}
        hmodels = {model_hash(con, x['model']) for x in ms.values() if x['owner'] == 0 and not x['dead']}
        slot_hash[r] = model_hash(con, s_model or 0)
        print(f'  {r}: guest members drawn from {sorted(gmodels)}, host members from {sorted(hmodels)}, the slot\'s '
              f'{slot_hash[r]}', flush=True)
        ck(gmodels == {rhash}, f'{r}: every guest member is drawn from its outfit ({sorted(gmodels)}, {rhash})')
        ck(hmodels == {slot_hash[r]} and rhash not in hmodels,
           f'{r}: every host member is drawn from the tribe\'s slot, none in the guest\'s outfit ({sorted(hmodels)})')
        ck(s_key == hk0, f'{r}: the tribe\'s slot is the host\'s species ({s_key or 0:08X}, {hk0 or 0:08X})')
    twoinst.keyframe('own_outfit', 'after the guest\'s own Tribal Outfitter: on both games the guest\'s members are '
                                   'its own creature with the new piece of clothing (a leaf on the chest), the host\'s '
                                   'members the tribe\'s species without it; both in Tribe play')
    _, _, ms = species(host)
    lk = looks(host)
    gm = sorted(i for i, x in ms.items() if x['owner'] == 1 and x['age'] == 1 and not x['dead'])
    hm = sorted(i for i, x in ms.items() if x['owner'] == 0 and x['age'] == 1 and not x['dead'] and
                not lk.get(i, {}).get('leader'))
    if gm:
        closeup(host, guest, gm[0], 'guest_village', 'same view 12 m from a guest\'s member (the guest\'s part of the '
                                                     'village): the guest\'s own creature with the leaf on its chest on '
                                                     'both screens')
    if hm:
        closeup(host, guest, hm[0], 'host_village', 'same view 12 m from a host\'s member (the host\'s part of the '
                                                    'village): the tribe\'s species without the guest\'s leaf on both '
                                                    'screens')

    # 5. the host's save: no key of the guest's
    slotname, folder, _ = save_slot(host, lane.HOST_DIR)
    since = time.time()
    print('  host:', host.call('coop.save')['out'].strip()[:80], f'(slot {slotname})', flush=True)
    saved = wait_saved(folder, since - 1, 90)
    time.sleep(3)
    own = set()  # (BUG-1326: instances that are also ids of the host's own resources of the slot, not counted)
    hits = keys_in_slot(folder, [okey, result, gkey, orig] + sorted(new), own) if saved else ['not saved']
    print(f'  host save slot {folder}: {hits or "no key of the guest"}; the host\'s own ids among them: '
          f'{[f"{i:08X}" for i in sorted(own)]}', flush=True)
    ck(saved and not hits, f'the host\'s save names no key of the guest\'s (outfit {okey:08X}, creation '
       f'{result:08X}, creature {gkey:08X} / {orig:08X}: {hits})')
    ok, txt = mix_ok(host, okey)
    ck(ok, f'host: the guest\'s members wear its outfit again after the save ({txt})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
