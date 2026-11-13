#!/usr/bin/env python3
"""Civilization: the game's own herds go into a save and come back with it (BUG-1067).

savestrip keeps the mod's objects out of saves; since BUG-0255 also the empty herds made for a partner's puppet (no
archetype +0x88, no nest, not the avatar's). In Civ the game's own herds of wild creatures look the same (no archetype,
no nest: docs/re/civ.md#civ-wild-sync), so every empty one of them was left out of every Civ save (a guest's save:
`empty_herds_stripped=77`, fix/w49-civ lab 20261113-043251).

1. The civ fixture with the guest in the session (its game shows the host's wild creatures with proxies).
2. The host's game: every herd (`coop.npc archless all`: archetype, nest, species, members, home).
3. The host saves with the game's own save (`coop.save`); savestrip's counters before and after.
4. Both games restart, the host loads that save alone: every herd again, compared by (species, home within 2 m).
   Every herd of the game must be back, the empty ones too; no fault for 20 s.
PASS: 4. SPORE_FIX_OFF=savestrip.our_herds (the empty-herd rule by the look alone, 0.7.13): the empty game herds are
missing after the load (FAIL). BUG-0255's own case (an empty herd the mod made for a puppet is still stripped):
save_hygiene_leave.

  SPORE_LANE=5 python3 tools/tests/coop_civ_save_herds.py
"""
import math
import re
import time

import lane
import twoinst
from coop_creature_e84 import close_boxes
from sporectl import Console
from twoinst import Checks, lane_state, sh, start, wait_up

HERD = re.compile(r'herd (\S+) arch=([0-9A-F]+) species=\S+/([0-9A-F]+) nest=(\S+) dist=\S+ en=(\d) members=(\d+) '
                  r'in_world=(\d+) near_home=\d+ home=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)')


def herds(con):
    """[dict(arch, species, nest, en, members, home)] of every herd of this game"""
    out = []
    for m in HERD.finditer(con.call('coop.npc archless all')['out']):
        out.append(dict(ptr=m.group(1), arch=m.group(2), species=m.group(3), nest=m.group(4) not in ('00000000', '(nil)', '0'),
                        en=int(m.group(5)), members=int(m.group(6)), in_world=int(m.group(7)),
                        home=tuple(float(m.group(i)) for i in (8, 9, 10))))
    return out


def plain(h):
    """a herd like the mod's own (BUG-0255's look): no archetype, no nest"""
    return int(h['arch'], 16) == 0 and not h['nest']


def summary(hs):
    pl = [h for h in hs if plain(h)]
    return (f'{len(hs)} herds, {len(pl)} without an archetype and a nest ({sum(1 for h in pl if h["members"] == 0)} '
            f'empty), {sum(h["members"] for h in hs)} members')


def missing(before, after):
    """herds of `before` with no herd of the same species within 2 m of its home in `after`"""
    left = list(after)
    out = []
    for h in before:
        m = next((a for a in left if a['species'] == h['species'] and math.dist(a['home'], h['home']) < 2.0), None)
        if m is None:
            out.append(h)
        else:
            left.remove(m)
    return out


def counters(con):
    s = con.call('coop.savestrip')['out']
    g = {k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', s)}
    return g, s.strip()


def restart_and_load_civ(slot, planet):
    """both games again (guest first, like twoinst.start), the host loads GameN/Planet alone and plays Civ"""
    for m in twoinst._monitors:
        m.stop = True
    sh('kill')
    time.sleep(2)
    lane.run_guest()
    time.sleep(2)
    lane.run_host()
    for inst, run in ((lane.GUEST, lane.run_guest), (lane.HOST, lane.run_host)):
        if not wait_up(inst, 90):
            run()
            assert wait_up(inst), f'instance {inst} did not start'
    host, guest = Console(lane.HOST), Console(lane.GUEST)
    for _ in range(180):
        if 'GalaxyGameEntry' in host.call('game.mode')['out']:
            break
        time.sleep(1)
    host.call('wait.frames 240 90000')
    print('  host: ' + host.call(f'coop.load {slot} {planet}')['out'].strip(), flush=True)
    t0 = time.time()
    while time.time() - t0 < 240 and 'Civ' not in host.call('game.mode')['out']:
        time.sleep(2)
    host.call('wait.frames 300 90000')
    twoinst.watch(lane.HOST, lane.GUEST)
    return host, guest


def main():
    host, guest = start('civ')
    ck = Checks()
    st = lane_state()
    slot = 'Game0' if st.get('slot', 9) == 9 else f'Game{st["slot"]}'
    planet = (st.get('planet') or 'Game0/Zorispe').split('/')[1]
    time.sleep(10)  # the guest's proxies of the host's wild creatures are made (npc.civ_wild)
    h0 = herds(host)
    print(f'  host before the save: {summary(h0)}', flush=True)
    for h in [h for h in h0 if plain(h)][:6]:
        print(f'    {h}', flush=True)
    print(f'  guest: {summary(herds(guest))}', flush=True)
    ck(any(plain(h) and h['members'] == 0 for h in h0),
       "the host's Civ world has empty herds without an archetype and a nest (the game's: no puppet in Civ)")
    c0, _ = counters(host)
    out = host.call('coop.save')['out']
    close_boxes(host, 12)
    ck('saved' in out, f'the host saved ({slot}/{planet}: {out.strip()})')
    c1, line = counters(host)
    print(f'  {line}', flush=True)
    stripped = c1.get('empty_herds_stripped', 0) - c0.get('empty_herds_stripped', 0)
    kept = c1.get('empty_game_herds_kept', 0) - c0.get('empty_game_herds_kept', 0)
    print(f'  the save: empty herds stripped {stripped}, empty game herds kept {kept}', flush=True)

    host, guest = restart_and_load_civ(slot, planet)
    ck('Civ' in host.call('game.mode')['out'], 'the host loaded the save into the Civ stage')
    h1 = herds(host)
    print(f'  host after the load: {summary(h1)}', flush=True)
    gone = missing(h0, h1)
    gone_plain_empty = [h for h in gone if plain(h) and h['members'] == 0]
    print(f'  herds of the save not back: {len(gone)} ({len(gone_plain_empty)} of them empty without an archetype)',
          flush=True)
    for h in gone[:8]:
        print(f'    gone: {h}', flush=True)
    ck(stripped == 0, f"the host's save left out no empty herd (it had none of the mod's: {stripped} stripped)")
    ck(not gone, f"every herd of the host's game is back after the load ({len(gone)} missing, "
                 f"{len(gone_plain_empty)} of them the game's empty herds)")
    time.sleep(20)  # the simulator runs over the loaded world
    h2 = herds(host)
    print(f'  host 20 s after the load: {summary(h2)}', flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
