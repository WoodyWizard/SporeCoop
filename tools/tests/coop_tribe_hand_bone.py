#!/usr/bin/env python3
"""Tribe: a hand item on a bone the member's body does not have yet makes no fault (BUG-1335, creature.fx_bone).

Lab 20261119-214823 (late_species, guest): 0xC0000005 at Simulator::cCreatureBase::CreateEffect+0x146, called from
SporeCoop::Citizen_SetHandheldItem < 0xC26470 < cTribe_SetChieftain < tribe::apply_leader (tribe.leader_sync): the
guest's mirror of the host's chief, spawned at the world's opening, was made its tribe's chief 0.65 s later and its
staff went onto its hand bone (+0xFC4: 64, from its species' limb table) while its body's bone array (+0xB54 -> +0x17C
-> +0x2E4 .. +0x2E8, 700 bytes a bone) was shorter: CreateEffect has no check but -1 and read a float as a bone
(docs/re/tribe.md#tribe-hand-bone).

Tribe fixture:
  1. every member of the guest's player tribe has its hand bone inside its body (`coop.tribe handbone`: state ok);
  2. the lab's race on purpose (`coop.tribe hand_race` on the guest): a member made in the player tribe and made chief
     in the same frame; prints its hand bone and its body's bones at that moment (evidence: is the body built later?),
     the guest's game must not fault; the member has no host member and goes with the next snapshot;
  3. the fault's state on purpose (`coop.tribe hand_plant <chief>` on the guest): the mirror of the host's chief gets
     its staff with its hand bone set past its body's bones (put back right after). On: the effect is not made
     (fx_skips +1, no fault) and within 8 s the staff is in its hand again (the repair: its item 17 and a drawn model
     on the guest's game, as on the host's); key frame of the chief on both games;
  4. no fault in either game, the guest's game keeps drawing.

SPORE_FIX_OFF=creature.fx_bone (suite tribe_hand_bone_off, the twin): step 3 faults on the guest (CreateEffect reads
past the bones): must FAIL.

  SPORE_LANE=3 python3 tools/tests/coop_tribe_hand_bone.py
"""
import re
import time

import twoinst
from coop_tribe_gameover import frames_go_on
from coop_tribe_looks import members as looks
from coop_tribe_own_species import closeup
from twoinst import Checks, start

SWITCH = 'creature.fx_bone'
ROW = re.compile(r'#(\d+) hand=(-?\d+) bones=(-?\d+) state=(\w+) item=(-?\d+) model=(-?\d+) age=(\d+) leader=(\d)')


def handbones(con, mid=None):
    out = (con.call('coop.tribe handbone' + (f' {mid}' if mid is not None else '')).get('out') or '')
    rows = {int(m.group(1)): dict(hand=int(m.group(2)), bones=int(m.group(3)), state=m.group(4), item=int(m.group(5)),
                                  model=int(m.group(6)), age=int(m.group(7)), leader=m.group(8) == '1')
            for m in (ROW.search(l) for l in out.splitlines()) if m}
    m = re.search(r'fix=(\d) waits=(\d+) repairs=(\d+) fx_skips=(\d+)', out)
    stats = dict(fix=int(m.group(1)), waits=int(m.group(2)), repairs=int(m.group(3)), skips=int(m.group(4))) if m else {}
    return rows, stats


def main():
    host, guest = start('tribe')
    ck = Checks()
    off_twin = SWITCH in twoinst.fix_off_names()
    time.sleep(8)
    rows, st = handbones(guest)
    print(f'  guest: {rows}; {st}', flush=True)
    ck(rows and all(r['state'] == 'ok' for i, r in rows.items() if i), 'every member of the guest\'s player tribe has '
       'its hand bone inside its body')

    # 2. the race (evidence and a natural try): made and made chief in one frame
    race = (guest.call('coop.tribe hand_race').get('out') or '').strip()
    print(f'  guest: {race}', flush=True)
    m = re.search(r'hand=(-?\d+) bones=(-?\d+) state=(\w+)', race)
    if m:
        print(f'  the race\'s member at its spawn: hand bone {m.group(1)}, its body {m.group(2)} bones ({m.group(3)})',
              flush=True)
    time.sleep(3)

    # 3. the fault's state on purpose on the mirror of the host's chief
    hl, gl = looks(host), looks(guest)
    chief = next((i for i, v in hl.items() if v['player'] and v['leader'] and v['item'] == 17 and i in gl), None)
    ck(chief is not None, f'the host\'s chief holds its staff and the guest has its mirror ({chief})')
    if chief is None:
        return ck.result()
    _, st0 = handbones(guest)
    try:
        plant = (guest.call(f'coop.tribe hand_plant {chief}').get('out') or '').strip()
    except (OSError, TimeoutError) as e:  # the twin: the fault unwinds past the command
        plant = f'no answer ({e!r})'
    print(f'  guest: {plant}', flush=True)
    if not off_twin:
        ck('fx_skips +1' in plant, f'the staff\'s effect on the planted bone is not made ({plant})')
    t0, row = time.time(), None
    while time.time() - t0 < 8:
        rows, st = handbones(guest, chief)
        row = rows.get(chief)
        if row and row['item'] == 17 and row['model'] == 1 and row['state'] == 'ok':
            break
        time.sleep(0.5)
    print(f'  guest after {time.time() - t0:.1f} s: #{chief} {row}; {st} (before {st0})', flush=True)
    ck(row and row['item'] == 17 and row['model'] == 1, f'the guest\'s mirror of the chief holds its staff again '
       f'(item 17, model drawn: {row})')
    if not off_twin:
        ck(st.get('repairs', 0) > st0.get('repairs', 0), 'the staff came back by the repair (BUG-1335)')
    closeup(host, guest, chief, 'chief_staff', 'Tribe, same view 12 m from the host\'s chief: on both games the chief '
            'holds its staff (the guest\'s mirror after its hand bone was planted past its body and put back)')
    ck(frames_go_on(guest, 5), 'the guest\'s game keeps drawing')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
