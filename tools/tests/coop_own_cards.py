#!/usr/bin/env python3
"""Every player's own stage history (WP-H1 of docs/plan-0.7.17.md, BUG-1470, BUG-1471; ideas from PR #207 / #217 by
IltonBJSilva; docs/re/creations.md#creations-own-history).

The consequence cards (cPlayer +0x10FC.. : Cell, Creature, Tribe, Civ, the Space archetype) and the Creature trait
progress (+0x1278 social, +0x127C combat) are each player's own in creature.mode own (the session default). Every check
reads the GAME's values on each machine: `coop.history`'s "game cards" line is cPlayer::GetConsequenceTrait 0xC75880
(what the game's History and powers read) and the two progress floats; the host's records are printed for the log only.

  --case kills (default, creature)  the guest kills an animal of another species (`coop.npc hit <id> 100000`: the hit
          message a real bite sends; the host lands the lethal hit as its avatar's): the guest's game's combat progress
          rises, the host's stays.  FAIL: SPORE_FIX_OFF=core.trait_credit (the host's combat rises, the guest's not)
  --case social (creature)  the guest impresses an animal (`coop.social goto` / `act`, the social game's messages):
          the guest's game's social progress rises by the creature's worth, the host's stays.
          FAIL: SPORE_FIX_OFF=core.trait_credit (the replica of the social win paid nobody's progress)
  --case cards (creature)  the guest's progress goes aggressive (`coop.history credit <guest> combat 9000`: the game's
          AddTraitProgress inside the guest's Actor scope, the hook's path), the host's social (`credit 0 social
          9000`); the host's Creature stage ends (`coop.history end creature`: the host's card by the game's rule, as
          0xD2C7D3 writes it): the host's game shows FD159D91 social, the guest's 17E5EF84 aggressive; then the guest
          leaves and joins again: still its own.  FAIL: SPORE_FIX_OFF=core.own_history (the guest's game shows the
          host's card)
  --case store (creature)  as cards, then the host saves, both quit, start again (twoinst.session_restart): the
          guest's card and progress come back from the host's per-player store; the host's game has its own card
          (its save holds the host's cards only).  FAIL: SPORE_FIX_OFF=core.own_history
  --case space (space)  the guest's Cell and Creature cards are set to a pair whose archetype differs from the
          host's (`coop.history set`, `space`: the game's SpaceArchetype_FromTraits); the host decides the guest's
          Space card from them: the guest's game shows its own archetype card and its empire its index (+0x58), the
          host's game keeps its own.  FAIL: SPORE_FIX_OFF=space.own_archetype (the guest's empire keeps the host's
          archetype index)
  --case shared (creature)  creature.mode host: one history for everybody: the guest's kill raises the host's
          combat, the guest's game shows the host's cards and progress (0.7.16.1's behaviour, kept in host mode)

  SPORE_LANE=2 python3 tools/tests/coop_own_cards.py [--case kills|social|cards|store|space|shared]
"""
import re
import sys
import time

import lane
import settings_lib
import twoinst
from harness import waiter
from twoinst import Checks, keyframe, session_restart, start, wait_guest_in

CASE = sys.argv[sys.argv.index('--case') + 1] if '--case' in sys.argv else 'kills'
wait = waiter(step=1.0)

AGGRESSIVE, SOCIAL = '17E5EF84', 'FD159D91'
CELL = ['CFB01B93', 'A8EC6F99', '5ECE4770']        # carnivore, herbivore, omnivore
CREATURE = ['17E5EF84', 'FD159D91', 'B9DE15F2']    # aggressive, social, mixed


def game(con):
    """this game's own cards (the game's getter) and progress: ([5 hex], social, combat, archetype index or None)"""
    out = con.call('coop.history')['out'] or ''
    m = re.search(r'game cards ((?:[0-9A-F]{8} ?){5}) social=(-?[\d.]+) combat=(-?[\d.]+)', out)
    a = re.search(r'empire archetype=(-?\d+)', out)
    if not m:
        return [], 0.0, 0.0, None
    return m.group(1).split(), float(m.group(2)), float(m.group(3)), int(a.group(1)) if a else None


def show(what, host, guest):
    h, g = game(host), game(guest)
    print(f'  {what}: host {h} | guest {g}', flush=True)
    for who, con in (('host', host), ('guest', guest)):
        lines = (con.call('coop.history')['out'] or '').strip().splitlines()
        print(f'    {who}: ' + ' | '.join(x.strip() for x in lines[1:4] + lines[5:7]), flush=True)
    return h, g


def guest_id(guest):
    m = re.search(r'client id=(\d+)', guest.call('coop.status')['out'] or '')
    return int(m.group(1)) if m else 1


def owns(guest):
    return 'owns=1' in (guest.call('coop.history')['out'] or '')


def prey(host, guest):
    fam = re.search(r'key=0x([0-9A-F]+)', host.call('coop.avatar')['out'])
    fam = fam.group(1).zfill(8) if fam else ''
    mirrored = set(re.findall(r'#(\d+) .*spawned', guest.call('coop.npc list')['out']))
    return [m.group(1) for m in re.finditer(r'#(\d+) (?:[0-9A-Fa-f]{8} )?species=([0-9A-F]+) hp=([\d.]+)',
                                            host.call('coop.npc list')['out'])
            if m.group(1) in mirrored and m.group(2) != fam and float(m.group(3)) > 0]


def impressed(host):
    m = re.search(r'impressed=(\d+)', host.call('coop.social')['out'] or '')
    return int(m.group(1)) if m else 0


def ready(host, guest, ck, own=True):
    """the guest has its record (own history) and both games show cards"""
    if own:
        ck(wait(lambda: owns(guest), 30), "the guest's own stage history is in force (the host's record arrived)")
    time.sleep(2)
    h, g = show('start', host, guest)
    ck(len(h[0]) == 5 and len(g[0]) == 5, 'both games show their cards (coop.history game cards)')
    return h, g


def kills_case(host, guest, ck):
    h0, g0 = ready(host, guest, ck)
    ids = []
    t0 = time.time()
    while time.time() - t0 < 30 and len(ids) < 3:
        ids = prey(host, guest)
        time.sleep(1)
    ck(ids, f'prey of another species mirrored on the guest ({ids[:4]})')
    killed = None
    for nid in ids[:4]:
        guest.call(f'coop.npc hit {nid} 100000')
        t1 = time.time()
        while time.time() - t1 < 4:
            if (game(guest)[2] or 0) > g0[2] + 0.5 or (game(host)[2] or 0) > h0[2] + 0.5:
                killed = nid
                break
            time.sleep(0.3)
        if killed:
            break
    time.sleep(2)  # the record's next send
    h1, g1 = show(f'the guest killed #{killed}', host, guest)
    ck(killed is not None, 'a guest kill paid trait progress on some game')
    ck(g1[2] > g0[2] + 0.5, f"the guest's game's combat progress rose with its kill ({g0[2]} -> {g1[2]})")
    ck(abs(h1[2] - h0[2]) < 0.01, f"the host's game's combat progress stayed ({h0[2]} -> {h1[2]})")
    keyframe('own_kill', "Creature after the guest's kill: the animal dead on both games (no visible change of the "
             "history: the progress is a hidden number)")


def social_case(host, guest, ck):
    h0, g0 = ready(host, guest, ck)
    imp0 = impressed(host)
    done = None
    for nid in prey(host, guest)[:4]:
        guest.call(f'coop.social goto {nid}')
        time.sleep(1)
        for _ in range(4):
            guest.call(f'coop.social act {nid} 0')
            time.sleep(3)
            if impressed(host) > imp0:
                done = nid
                break
        if done:
            break
    time.sleep(2)
    h1, g1 = show(f'the guest impressed #{done}', host, guest)
    ck(done is not None, 'the guest impressed an animal (the host applied the social win)')
    ck(g1[1] > g0[1] + 0.5, f"the guest's game's social progress rose with its social win ({g0[1]} -> {g1[1]})")
    ck(abs(h1[1] - h0[1]) < 0.01, f"the host's game's social progress stayed ({h0[1]} -> {h1[1]})")
    keyframe('own_social', 'Creature after the guest\'s social win: the impressed animal plays its reaction on both games')


def cards_setup(host, guest, ck):
    h0, g0 = ready(host, guest, ck)
    ck(len(h0[0]) == 5 and h0[0][1] == '00000000', f"the host's Creature stage is not done yet ({h0[0]})")
    gid = guest_id(guest)
    print('  ', host.call(f'coop.history credit {gid} combat 9000')['out'].strip(), flush=True)
    print('  ', host.call(f'coop.history credit 0 social 9000')['out'].strip(), flush=True)
    time.sleep(2)
    h1, g1 = show('progress: the guest aggressive, the host social', host, guest)
    ck(g1[2] >= g0[2] + 8999 and h1[1] >= h0[1] + 8999,
       f"the guest's game has its combat 9000 up, the host's its social ({g1[2]} / {h1[1]})")
    print('  ', host.call('coop.history end creature')['out'].strip(), flush=True)
    ok = wait(lambda: (game(guest)[0] or ['00000000'] * 5)[1] != '00000000', 15)
    h2, g2 = show("the host's Creature stage ended", host, guest)
    ck(ok, "the guest's game got a Creature card")
    ck(h2[0][1] == SOCIAL, f"the host's game shows its own Creature card, social ({h2[0][1]})")
    ck(g2[0][1] == AGGRESSIVE, f"the guest's game shows its own Creature card, aggressive ({g2[0][1]}, the host's "
       f"{h2[0][1]})")
    return h2, g2


def cards_case(host, guest, ck):
    cards_setup(host, guest, ck)
    guest.call('coop.leave')
    time.sleep(3)
    print('  guest joins again:', guest.call(lane.JOIN)['out'].strip()[:120], flush=True)
    wait(lambda: not guest.call('coop.avatar')['ok'], 40)
    ck(wait_guest_in(host, guest), "the guest is in the host's world again")
    ck(wait(lambda: owns(guest), 30), 'its own history in force again')
    time.sleep(3)
    h3, g3 = show('after the rejoin', host, guest)
    ck(g3[0][1] == AGGRESSIVE and h3[0][1] == SOCIAL,
       f"after the rejoin each game shows its own Creature card (guest {g3[0][1]}, host {h3[0][1]})")


def store_case(host, guest, ck):
    h2, g2 = cards_setup(host, guest, ck)
    host, guest, info = session_restart(host, guest)
    ck(info['saved'], 'the host saved its game before quitting')
    ck(wait(lambda: owns(guest), 40), 'next session: its own history in force')
    time.sleep(3)
    h3, g3 = show('next session', host, guest)
    print('  host store:', (host.call('coop.store')['out'] or '').strip().splitlines()[:3], flush=True)
    ck(h3[0][1] == SOCIAL, f"the host's save holds the host's own Creature card ({h3[0][1]})")
    ck(g3[0][1] == AGGRESSIVE, f"next session the guest's game shows its own Creature card again ({g3[0][1]})")
    ck(g3[2] >= g2[2] - 1, f"and its own combat progress ({g2[2]} -> {g3[2]})")
    return host, guest


def space_case(host, guest, ck):
    h0, g0 = ready(host, guest, ck)
    # Space-start fixtures (e.g. Madas.spo) have 00000000 for stages 0..3: seed a full stage history on the host so
    # both players have four stage cards and distinct Space archetypes can be derived from Cell/Creature cards.
    if len(h0[0]) == 5 and any(c == '00000000' for c in h0[0][:4]):
        defaults = ('CFB01B93', '17E5EF84', '0CAC124D', '0A35D0F5')
        for stage, v in enumerate(defaults):
            if h0[0][stage] == '00000000':
                host.call(f'coop.history set 0 {stage} {v}')
        wait(lambda: all(c != '00000000' for c in (game(host)[0] or ['00000000'] * 5)[:4]), 10)
        time.sleep(2)
        h0, g0 = show('seeded host cards', host, guest)
    ck(len(h0[0]) == 5 and all(c != '00000000' for c in h0[0]), f"the host's Space save has all five cards ({h0[0]})")
    if not h0[0]:
        return
    host_arch = h0[0][4]
    pick = None
    for c in CELL:
        for k in CREATURE:
            m = re.search(r'space card ([0-9A-F]{8}) index (-?\d+)',
                          host.call(f'coop.history space {c} {k} {h0[0][2]} {h0[0][3]}')['out'] or '')
            if m and m.group(1) != host_arch and int(m.group(2)) >= 0:
                pick = (c, k, m.group(1), int(m.group(2)))
                break
        if pick:
            break
    ck(pick, f"a Cell / Creature pair with another archetype than the host's {host_arch} ({pick})")
    if not pick:
        return
    gid = guest_id(guest)
    for stage, v in ((0, pick[0]), (1, pick[1])):
        print('  ', host.call(f'coop.history set {gid} {stage} {v}')['out'].strip(), flush=True)
    ok = wait(lambda: (game(guest)[0] or [''] * 5)[4] == pick[2], 20)
    ck(ok, f"the guest's game shows its own archetype card {pick[2]}")
    wait(lambda: game(guest)[3] == pick[3], 20)
    time.sleep(2)
    h1, g1 = show('the guest has its own archetype', host, guest)
    ck(g1[3] == pick[3], f"the guest's empire plays its own archetype (index {g1[3]}, want {pick[3]}, the host's "
       f"{h1[3]})")
    ck(h1[0][4] == host_arch and h1[3] == h0[3], f"the host's game keeps its own archetype ({h1[0][4]}, index {h1[3]})")
    keyframe('own_archetype', f"Space: the guest's tool bar shows the archetype tools of {pick[2]} (index {pick[3]}), the "
             f"host's those of {host_arch}")


def shared_case(host, guest, ck):
    ck(settings_lib.force(host, [guest], 'creature.mode', 'host'), 'the guest plays with creature.mode host')
    time.sleep(3)
    h0, g0 = ready(host, guest, ck, own=False)
    ck(not owns(guest), 'host mode: no own history on the guest')
    for nid in prey(host, guest)[:4]:
        guest.call(f'coop.npc hit {nid} 100000')
        time.sleep(3)
        if (game(host)[2] or 0) > h0[2] + 0.5:
            break
    time.sleep(2)
    h1, g1 = show("the guest's kill, host mode", host, guest)
    ck(h1[2] > h0[2] + 0.5, f"host mode: the guest's kill counts for the one shared history ({h0[2]} -> {h1[2]})")
    ck(g1[0] == h1[0], f"host mode: the guest's game shows the host's cards ({g1[0]} / {h1[0]})")


def main():
    fixture = 'space' if CASE == 'space' else 'creature'
    host, guest = start(fixture)
    ck = Checks()
    time.sleep(4)
    forced = CASE == 'shared'
    if forced:
        twoinst.forget_settings()
    try:
        r = {'kills': kills_case, 'social': social_case, 'cards': cards_case, 'store': store_case,
             'space': space_case, 'shared': shared_case}[CASE](host, guest, ck)
        if CASE == 'store' and r:
            host, guest = r
    finally:
        if forced:
            settings_lib.reset([host, guest], ('creature.mode',))
            twoinst.forget_settings()
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
