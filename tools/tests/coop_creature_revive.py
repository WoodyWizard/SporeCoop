#!/usr/bin/env python3
"""Revive a downed player in the Creature stage (WP-R1 of 0.7.17, BUG-1472; idea, rules and flow from PR #214 by
IltonBJSilva). The pack rescue stays the first line; a lethal hit it does not take, while a partner plays in the same
world, downs the creature (1 hp, held, 60 s); a partner within 6 m for 1.5 s revives it at 40 %; otherwise it dies the
game's way and pays its own price (10 % of its own DNA, or a debt on its next gains under own progress).

Every check reads the GAME's state on the machine named: the own creature's health and dead flag (`coop.revive`
game_hp / game_dead: game::health / is_dead; `coop.avatar` hp= and flags), the other game's creature of that player
(`coop.hp` puppet N: the puppet's game health, what its health bar draws), the game's DNA (`coop.dna`:
GetEvolutionPoints), the host's save refusal (`coop.save`). Key frames where the screen changes.

  --case main   (default) the guest is downed by a real NPC bite (a host NPC provoked, the guest one bite from death,
                the host 30 m away: no pack rescue): alive at 1 health on its game and on the host's puppet, the NPC's
                bites and a further hit (`coop.team hit 50`) are not dealt, the host's save waits; the host walks next to
                it: 40 % health on both games. Twins: creature_revive_setting_off (`--setting off`: the bite kills),
                creature_revive_hold_off (SPORE_FIX_OFF=creature.revive_hold: the next bite kills it under the hold).
  --case price --dna wallet|progress|shared   the guest is downed 40 m from the host and nobody comes: after 60 s its
                game's dead flag, the caption's OK, the respawn at the nest; the price: wallet: the guest's own DNA
                -10 %, the host's the same as before; shared: both -10 %; progress: the guest's DNA stays, a debt of
                10 % that its next gain of 30 pays first (its DNA rises by 30 - debt). Twin creature_revive_debt_off
                (SPORE_FIX_OFF=creature.revive_debt with --dna wallet: the host pays, the guest not).
  --case first_line   the host next to the guest: the first lethal hit is the pack rescue (40 %, not down), the second
                one within the minute downs it, the host standing there revives it.
  --case wipe   both down 30 m apart: both games' dead flags within a few seconds (the party wipe), both respawn.
  --case paths  the downed guest put 40 m up (a fall) and into deep water (the sea monster's death, cause 9): the hold
                keeps it at 1 health where it fell, or the game's own death ends its window (no second death, no stuck
                window on the host).
  --case old    the guest plays as a 0.7.16.1 peer (`coop.manifest plant creature 1 2` before its join): its lethal hit
                kills it at once, the host opens no window.

  SPORE_LANE=2 python3 tools/tests/coop_creature_revive.py [--case ...] [--dna ...] [--setting on|off]
"""
import argparse
import math
import re
import time

import lane
import settings_lib
import twoinst
from coop_team_real_damage import beside, mirrored, own_hp, provoke, puppet_hp
from harness import avatar_pos as pos, waiter
from twoinst import Checks, keyframe, start, wait_guest_in

wait = waiter(step=0.5)
KEY = 'creature.revive'


def rv(con):
    """the key=value fields of `coop.revive` (the game's health and dead flag of the own creature among them)"""
    out = con.call('coop.revive')['out'] or ''
    return dict(re.findall(r'(\w+)=(-?[\w.]+)', out)), out


def game_hp(con):
    f = rv(con)[0]
    try:
        return float(f.get('game_hp', 'nan')), f.get('game_dead') == '1'
    except ValueError:
        return None, False


def dna(con):
    m = re.search(r'dna=(-?[\d.]+)', con.call('coop.dna')['out'] or '')
    return float(m.group(1)) if m else None


def debt(con):
    m = re.search(r'\bdebt=(-?[\d.]+)', con.call('coop.progress')['out'] or '')
    return float(m.group(1)) if m else None


def dead_now(con):
    hp, dead = game_hp(con)
    return dead or (hp is not None and hp <= 0)


def ok_caption(*cons):
    for con in cons:
        if 'paused' in ''.join(ln for ln in (con.call('coop.status')['out'] or '').splitlines() if ln.startswith('busy')):
            con.call('key enter')


def respawned(con, fell):
    """after its death: alive again away from where it fell (the nest); the death caption is OKed on the way"""
    ok_caption(con)
    hp, dead = game_hp(con)
    p = pos(con)
    return hp is not None and hp > 1.5 and not dead and p and fell and math.dist(p, fell) > 10


def held_hp(con, secs):
    """the own creature's game health read every 0.5 s for secs: (lowest, ever dead)"""
    low, died = 1e9, False
    t0 = time.time()
    while time.time() - t0 < secs:
        hp, dead = game_hp(con)
        if hp is not None:
            low = min(low, hp)
        died |= dead or (hp is not None and hp <= 0)
        time.sleep(0.5)
    return low, died


def put(con, p):
    con.call('coop.abilities put %.2f %.2f %.2f' % tuple(p))


def down_by_bite(ck, host, guest):
    """the guest one bite from death while a host NPC fights it, the host 30 m away: the bite downs it. Returns
    (npc id, the guest's max health) or None"""
    _, mx = own_hp(guest)
    tried = set()
    got = provoke(host, guest, 'guest', tried)
    if not ck(got is not None, 'a host NPC fights the guest (its own bites)'):
        return None
    nid, bite, _ = got
    print(f'  the host {beside(host, guest, 30):.0f} m away (no pack rescue)', flush=True)
    target = max(0.05, 0.4 * bite)
    guest.call('coop.team hp %.2f' % target)
    t0 = last_set = last_hit = time.time()
    while time.time() - t0 < 30:
        hp, dead = game_hp(guest)
        if dead or (hp is not None and (hp <= 0 or abs(hp - 1.0) < 0.01)):
            break
        now = time.time()
        if hp is not None and hp > target + 0.01 and now - last_set >= 1.0:
            guest.call('coop.team hp %.2f' % target)
            last_set = now
        if now - last_hit >= 3.0:
            guest.call(f'coop.npc hit {nid} 0.1')
            last_hit = now
        time.sleep(0.2)
    print(f'  {time.time() - t0:.0f} s until the lethal bite', flush=True)
    return nid, mx


def case_main(ck, host, guest):
    got = down_by_bite(ck, host, guest)
    if not got:
        return
    nid, mx = got
    hp, dead = game_hp(guest)
    ck(hp is not None and abs(hp - 1.0) < 0.01 and not dead,
       f'guest: the NPC\'s lethal bite leaves its creature alive at 1 health, the game\'s dead flag off ({hp}, dead={dead})')
    php, _ = puppet_hp(host, 1)
    ck(php is not None and 0 < php < 0.2 * mx, f'host: the guest\'s creature is alive and nearly empty on its game ({php})')
    keyframe('down', 'host: "DOWN 5xs: <guest>" in red over the guest\'s creature, its arrow says DOWN; guest: the red '
             'line "DOWN 5xs: a partner next to you revives you" in the middle of the screen', settle=0)
    low, died = held_hp(guest, 6)  # the NPC still bites
    ck(not died and low >= 0.99, f'guest: the NPC\'s next bites are not dealt (lowest {low}, died={died})')
    guest.call('coop.team hit 50')
    hp, dead = game_hp(guest)
    ck(hp is not None and abs(hp - 1.0) < 0.01 and not dead, f'guest: a further hit through the damage filter is not dealt ({hp})')
    say = host.call('coop.save')['out'] or ''
    ck('a player is down' in say, f'host: no world save while a player is down ({say.strip()[:120]})')
    host.call(f'coop.npc hosthit {nid} 100000')  # the attacker dies (a revived creature at 40 % is not bitten again)
    time.sleep(1)
    print(f'  the host next to it: {beside(host, guest, 3):.1f} m', flush=True)
    up = wait(lambda: (game_hp(guest)[0] or 0) >= 0.38 * mx, 8)
    hp, _ = game_hp(guest)
    ck(up, f'guest: revived by the host standing next to it: 40 % health on its game ({hp} of {mx})')
    ok = wait(lambda: (puppet_hp(host, 1)[0] or 0) >= 0.3 * mx, 5)
    ck(ok, f'host: the guest\'s creature has that health on its game ({puppet_hp(host, 1)[0]})')
    keyframe('revived', 'both: the guest\'s creature standing next to the host\'s, no DOWN tag or line any more')


def set_dna_mode(ck, host, guest, mode):
    value = {'wallet': '0', 'shared': '1', 'progress': '2'}[mode]
    twoinst.forget_settings()
    ck(settings_lib.force(host, [guest], 'all.shared_dna', value), f'the guest plays all.shared_dna {value} ({mode})')
    if mode == 'progress':
        wait(lambda: 'active=1' in (guest.call('coop.progress')['out'] or ''), 15)
    time.sleep(3)


def case_price(ck, host, guest, mode):
    set_dna_mode(ck, host, guest, mode)
    guest.call('coop.dna add 100')
    time.sleep(5)
    print(f'  the host {beside(host, guest, 40):.0f} m away', flush=True)
    time.sleep(3)
    h0, g0 = dna(host), dna(guest)
    print(f'  DNA before: host {h0}, guest {g0}', flush=True)
    fell = pos(guest)
    guest.call('coop.revive down')
    hp, dead = game_hp(guest)
    ck(hp is not None and abs(hp - 1.0) < 0.01 and not dead, f'guest: down at 1 health ({hp})')
    ck(wait(lambda: dead_now(guest), 75, 1.0), 'guest: nobody came: after the window its game\'s dead flag (the game\'s death)')
    keyframe('timeout', 'guest: the game\'s death caption ("You were killed"); host: no DOWN tag any more', settle=0)
    ck(wait(lambda: respawned(guest, fell), 120, 2.0), f'guest: respawned at the nest ({pos(guest)}, fell at {fell})')
    time.sleep(5)
    h1, g1 = dna(host), dna(guest)
    cost = math.ceil(0.1 * g0) if g0 else None
    print(f'  DNA after: host {h1}, guest {g1}; the price should be {cost}', flush=True)
    if mode == 'wallet':
        ck(g0 is not None and g1 is not None and abs((g0 - g1) - cost) < 1.01, f'guest: its own DNA paid the price ({g0} -> {g1})')
        ck(h0 is not None and h1 is not None and abs(h1 - h0) < 1.01, f'host: its DNA did not pay ({h0} -> {h1})')
    elif mode == 'shared':
        ck(h0 is not None and h1 is not None and abs((h0 - h1) - math.ceil(0.1 * h0)) < 1.01,
           f'host: the shared pool paid the price ({h0} -> {h1})')
        ck(wait(lambda: abs(dna(guest) - dna(host)) < 1.01, 10), f'guest: the same shared DNA ({dna(guest)})')
    else:
        d = debt(guest)
        ck(g0 is not None and g1 is not None and abs(g1 - g0) < 1.01, f'guest: its DNA stays ({g0} -> {g1}): the price is a debt')
        ck(d is not None and cost is not None and abs(d - cost) < 1.01, f'guest: a debt of 10 % of its own DNA ({d}, want {cost})')
        ck(h0 is not None and h1 is not None and abs(h1 - h0) < 1.01, f'host: its DNA did not pay ({h0} -> {h1})')
        before = dna(guest)
        guest.call('coop.dna add 30')
        time.sleep(2)
        after = dna(guest)
        want = max(0, 30 - (d or 0))
        ck(before is not None and after is not None and abs((after - before) - want) < 1.01,
           f'guest: its next gain of 30 pays the debt first: DNA +{want:.0f} ({before} -> {after})')
        print(f'  debt now: guest {debt(guest)}, host record: '
              + ' '.join(ln.strip() for ln in (host.call('coop.progress')['out'] or '').splitlines() if 'record' in ln),
              flush=True)
    keyframe('price', f'both: the guest alive at the nest; the notification line names the price ({mode})')


def case_first_line(ck, host, guest):
    _, mx = own_hp(guest)
    print(f'  the host next to the guest: {beside(host, guest, 4):.1f} m', flush=True)
    time.sleep(3)
    guest.call('coop.revive down')
    hp, dead = game_hp(guest)
    ck(hp is not None and hp >= 0.35 * mx and not dead, f'guest: the first lethal hit is the pack rescue (40 %: {hp} of {mx})')
    time.sleep(3)
    guest.call('coop.revive down')
    hp, dead = game_hp(guest)
    ck(hp is not None and abs(hp - 1.0) < 0.01 and not dead, f'guest: the second within the minute downs it (1 health: {hp})')
    up = wait(lambda: (game_hp(guest)[0] or 0) >= 0.38 * mx, 8)
    ck(up, f'guest: the host standing there revives it ({game_hp(guest)[0]} of {mx})')
    keyframe('first_line', 'both: the guest\'s creature up next to the host\'s')


def case_wipe(ck, host, guest):
    print(f'  the host {beside(host, guest, 30):.0f} m away', flush=True)
    time.sleep(3)
    hf, gf = pos(host), pos(guest)
    guest.call('coop.revive down')
    host.call('coop.revive down')
    ck(abs((game_hp(host)[0] or 0) - 1.0) < 0.01 and abs((game_hp(guest)[0] or 0) - 1.0) < 0.01, 'both down at 1 health')
    ck(wait(lambda: dead_now(host) and dead_now(guest), 12, 0.5), 'the party wipe: both games\' dead flags within seconds')
    keyframe('wipe', 'both: the game\'s death caption', settle=0)
    ck(wait(lambda: respawned(host, hf) and respawned(guest, gf), 150, 2.0), 'both respawned at the nest')


def case_paths(ck, host, guest):
    print(f'  the host {beside(host, guest, 40):.0f} m away', flush=True)
    time.sleep(3)
    fell = pos(guest)
    guest.call('coop.revive down')
    # a fall: 40 m over where it fell
    n = math.sqrt(sum(x * x for x in fell))
    put(guest, [x + 40 * x / n for x in fell])
    time.sleep(4)
    hp, dead = game_hp(guest)
    p = pos(guest)
    moved = math.dist(p, fell) if p else -1.0
    ck(not dead and hp is not None and abs(hp - 1.0) < 0.01 and 0 <= moved < 1.5,
       f'guest: put 40 m up, the hold keeps it at 1 health where it fell ({hp}, {moved:.1f} m)')
    # deep water (the sea monster's death, cause 9, past the damage filter)
    try:
        from coop_creature_sea_monster import deep_point
        deep = deep_point(guest)
    except Exception as e:  # noqa: BLE001 (no deep water near: the row is skipped, said so)
        print(f'  no deep water found near the guest: {e}', flush=True)
        deep = None
    if deep:
        put(guest, deep)
        time.sleep(8)
        hp, dead = game_hp(guest)
        p = pos(guest)
        held = not dead and hp is not None and abs(hp - 1.0) < 0.01 and p and math.dist(p, fell) < 1.5
        if not held:
            ck(dead_now(guest) and wait(lambda: 'player 1: knockout' not in (host.call('coop.revive')['out'] or ''), 5),
               'guest: the game\'s own death in deep water ended its window on the host (no stuck window)')
            ck(wait(lambda: respawned(guest, fell), 120, 2.0), 'guest: respawned at the nest after it')
        else:
            ck(True, 'guest: put into deep water, the hold kept it at 1 health where it fell')
    if not dead_now(guest):
        guest.call('coop.revive giveup')
        ck(wait(lambda: dead_now(guest), 10), 'guest: given up: the game\'s death')
        wait(lambda: respawned(guest, fell), 120, 2.0)


def case_old(ck, host, guest):
    guest.call('coop.leave')
    time.sleep(2)
    print('  guest:', (guest.call('coop.manifest plant creature 1 2')['out'] or '').strip(), flush=True)
    guest.call(lane.JOIN)
    if not ck(wait_guest_in(host, guest), 'the 0.7.16.1-like guest joined'):
        return
    t0 = time.time()
    while time.time() - t0 < 30 and not mirrored(guest):
        time.sleep(1)
    print(f'  the host {beside(host, guest, 30):.0f} m away', flush=True)
    time.sleep(3)
    guest.call('coop.revive down')
    ck(wait(lambda: dead_now(guest), 5), 'the old guest\'s lethal hit kills it (as with 0.7.16.1)')
    ck('player 1: knockout' not in (host.call('coop.revive')['out'] or ''), 'host: no window for the old guest')
    ok_caption(guest)
    guest.call('coop.manifest reset')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--case', default='main', choices=('main', 'price', 'first_line', 'wipe', 'paths', 'old'))
    ap.add_argument('--dna', default='wallet', choices=('wallet', 'progress', 'shared'))
    ap.add_argument('--setting', default='on', choices=('on', 'off'))
    a = ap.parse_args()
    host, guest = start('creature')
    ck = Checks()
    t0 = time.time()
    while time.time() - t0 < 30 and not mirrored(guest):
        time.sleep(1)
    time.sleep(2)
    twoinst.forget_settings()
    ck(settings_lib.force(host, [guest], KEY, a.setting), f'the guest plays {KEY} {a.setting}')
    try:
        if a.case == 'main':
            case_main(ck, host, guest)
        elif a.case == 'price':
            case_price(ck, host, guest, a.dna)
        elif a.case == 'first_line':
            case_first_line(ck, host, guest)
        elif a.case == 'wipe':
            case_wipe(ck, host, guest)
        elif a.case == 'paths':
            case_paths(ck, host, guest)
        else:
            case_old(ck, host, guest)
    finally:
        print('  host:', (host.call('coop.revive')['out'] or '').strip().replace('\n', ' | ')[:600], flush=True)
        print('  guest:', (guest.call('coop.revive')['out'] or '').strip().replace('\n', ' | ')[:600], flush=True)
        settings_lib.reset([host, guest], (KEY, 'all.shared_dna'))
        twoinst.forget_settings()
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
