#!/usr/bin/env python3
"""The species line (WP-0 of docs/plan-0.7.16.md, BUG-1270; docs/01-architecture.md#arch-species-line): a guest's own
tribe outfit and Civ design reach the host's game and come back to the guest from the host, survive a rejoin and a
new evening (the host's per-player store), and the host's creature.mode host empties every guest's line.

No look changes by itself (the Tribe and Civ packages apply the line); what is checked is the game's own state: the
resource manager of each game answers the line's key, and the model it answers on the host is the guest's own creation
(its bem hash, creations::model_hash) as the guest's game has it.

1. Two games on the creature fixture, the guest plays its own creature (creations_state.choose_own). The host's line of
   the guest names its creature (0x7C......, known on the host).
2. The guest sets a tribe outfit and a City Hall design (`coop.species test`: an own creature of its My Creations as
   the stand-in). On both games key(guest, tribe) is the same 0x7B...... key, known there, and the host's model of it is
   the guest's own model; the Civ line is the tribe outfit (no Civ outfit); the design is there on both games.
3. The guest leaves and joins again: the host's store gives the line back (kStored), the same key on both games.
4. The host switches creature.mode to host: every key of the guest is 0 on both games; back to own: the line is back.
   (`coop.settings force`: creature.mode is a session setting, refused while a world is open since 0.7.16; the
   lab's force stands in for the host's choice before the start, lab 20261119-095414)
5. A new evening (twoinst.session_restart): the host saves, both quit and start again, the guest joins: the same key on
   both games.
SPORE_FIX_OFF=species.line: no line (the guest's setter is refused, the host shows nothing): FAIL.

  SPORE_LANE=9 python3 tools/tests/coop_species_line.py
"""
import re
import time

import creations_state
import lane
from harness import field, waiter
from twoinst import Checks, keyframe, session_restart, start, wait_guest_in

CITY_HALL = '99E92F05'


def guest_id(guest):
    m = re.search(r'client id=(\d+)', guest.call('coop.status')['out'] or '')
    return int(m.group(1)) if m else 1


def key(con, pid, stage):
    out = con.call(f'coop.species key {pid} {stage}')['out'] or ''
    m = re.search(r'key=([0-9A-F]{8})/\S+ known=(\d) model=(\S+)', out)
    return (m.group(1), m.group(2) == '1', m.group(3)) if m else ('00000000', False, 'none')


def design(con, pid):
    return field(con.call('coop.species')['out'] or '', rf"player {pid} '[^\n]*? design {CITY_HALL}=([0-9A-F]{{8}})")


wait = waiter(60, step=1.0, final=True)


def show(con, who):
    for ln in (con.call('coop.species')['out'] or '').splitlines():
        print(f'  {who}: {ln[:230]}', flush=True)


def same_line(host, guest, pid, want, what):
    """the tribe key `want` on both games, known on both; returns (host key, guest key)"""
    got = wait(lambda: key(host, pid, 'tribe')[0] == want and key(guest, pid, 'tribe')[0] == want, 90)
    hk, gk = key(host, pid, 'tribe'), key(guest, pid, 'tribe')
    if not got:
        show(host, 'host')
        show(guest, 'guest')
    return got, hk, gk


def main():
    host, guest = start()
    ck = Checks()
    st0 = creations_state.save(guest)
    print('  guest profile before:', st0, '| chose', creations_state.choose_own(guest), flush=True)
    pid = guest_id(guest)
    creature = wait(lambda: (k := key(host, pid, 'creature'))[0].startswith('7C') and k[1] and k, 60)
    ck(creature, f'the host\'s line of the guest names its own creature ({creature})')
    creature = creature or ('none', False, 'none')

    # 2. the guest's tribe outfit and City Hall design
    out = guest.call('coop.species test tribe next')['out'].strip()
    print('  guest:', out, flush=True)
    print('  guest:', guest.call(f'coop.species test design {CITY_HALL} next')['out'].strip(), flush=True)
    own_model = field(guest.call('coop.species own tribe')['out'] or '', r'model=(\S+)')
    got = wait(lambda: (k := key(host, pid, 'tribe'))[0].startswith('7B') and k[1], 60)
    hk = key(host, pid, 'tribe')
    print(f'  host: tribe {hk}; guest\'s own model {own_model}', flush=True)
    ck(got, f'the guest\'s tribe outfit is a shared line key the host\'s game knows ({hk[0]})')
    tribe = hk[0]
    ok, hk, gk = same_line(host, guest, pid, tribe, 'set')
    ck(ok and gk[1], f'the guest\'s game has the same key from the host ({gk[0]}, known {gk[1]})')
    ck(own_model and hk[2] == own_model and gk[2] == own_model,
       f'the model the host\'s game answers for it is the guest\'s own creation (host {hk[2]}, guest {gk[2]}, own {own_model})')
    ck(key(host, pid, 'civ')[0] == tribe and key(guest, pid, 'civ')[0] == tribe,
       'without a Civ outfit the Civ line is the tribe outfit on both games')
    ck(key(host, pid, 'creature')[0] == creature[0], 'the creature line stays the guest\'s creature')
    hd = wait(lambda: design(host, pid), 30)
    gd = wait(lambda: design(guest, pid), 30)
    ck(hd and hd.startswith('7B') and hd == gd, f'the guest\'s City Hall design is the same key on both games ({hd}, {gd})')
    ck(key(host, 0, 'tribe')[0] == '00000000', 'the host has no line (its species is the world\'s own)')
    keyframe('line', 'no look change (WP-0 is bookkeeping): both games in the Creature world as before, the guest with '
             'its own creature')

    # 3. a rejoin: the host's store gives it back
    print('  guest:', guest.call('coop.leave')['out'].strip(), flush=True)
    time.sleep(3)
    guest.call(lane.JOIN)
    ck(wait_guest_in(host, guest), 'the guest is back in the host\'s world')
    pid = guest_id(guest)
    ok, hk, gk = same_line(host, guest, pid, tribe, 'rejoin')
    taken = field(guest.call('coop.species')['out'] or '', r'taken=(\d+)')
    ck(ok, f'after the rejoin the guest\'s tribe outfit is the same key on both games (host {hk[0]}, guest {gk[0]}, '
           f'taken from the host\'s store {taken})')

    # 4. host mode empties it
    print('  host:', host.call('coop.settings force creature.mode host')['out'].strip()[:120], flush=True)
    empty = wait(lambda: all(key(c, pid, s)[0] == '00000000' for c in (host, guest) for s in ('creature', 'tribe', 'civ'))
                 and not design(host, pid) and not design(guest, pid), 30)
    ck(empty, 'creature.mode host: every key of the guest\'s line is 0 on both games')
    print('  host:', host.call('coop.settings force creature.mode own')['out'].strip()[:120], flush=True)
    ok, hk, gk = same_line(host, guest, pid, tribe, 'own again')
    ck(ok, f'creature.mode own again: the line is back on both games ({hk[0]}, {gk[0]})')

    # 5. a new evening
    host, guest, info = session_restart(host, guest)
    ck(info['saved'], 'the host saved its game before quitting')
    pid = guest_id(guest)
    ok, hk, gk = same_line(host, guest, pid, tribe, 'restart')
    ck(ok and hk[1] and gk[1], f'next session: the guest\'s tribe outfit is the same key on both games and known there '
                               f'(host {hk}, guest {gk})')
    ck(hk[2] == own_model, f'next session: the host\'s game builds it from the guest\'s own model ({hk[2]})')
    hd = wait(lambda: design(host, pid), 30)
    ck(hd and hd.startswith('7B'), f'next session: the City Hall design is back on the host ({hd})')
    keyframe('next_session', 'no look change (WP-0): the next session in the Creature world')

    creations_state.restore(guest, st0)  # the profile as it was (BUG-0345)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
