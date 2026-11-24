#!/usr/bin/env python3
"""Own progress: the third DNA mode (all.shared_dna 2; WP-P1 / P2 of 0.7.17, BUG-1469; idea from PR #209 by
IltonBJSilva). Every player earns alone in Cell and Creature (its own DNA, food, evolution bar, brain level); nobody
evolves alone: the stage moves on when every bar is full and the vote agrees.

The setting is forced on the host in play (`coop.settings force all.shared_dna 2`, the guest follows). Every check reads
the GAME's own numbers on each machine: the Cell food S+0x1C (`coop.pool`: food=), the Creature DNA and bar
(`coop.dna bar`: GetEvolutionPoints, cPlayer +0x10F0), the brain level (creature vt 0xD8, `coop.progress` level=), the
game's mode, the host's vote, the screen's white share (screenshots); the mod's counters are printed for the log only.

  --part cell [--first host|guest]  Cell fixture. Each game's food its own: the host to 300, the guest to 120: 6 s
          later each game still has its own (FAIL twin own_progress_cell_off, SPORE_FIX_OFF=core.own_progress: the
          guest's food becomes the host's total). Then the first player fills its bar (1000) and clicks History's check:
          its game stays in Cell, drawn (the near-white share of its screen 8 s later under 0.5), no vote starts; the
          other fills its bar: the vote starts by itself (the host's kept click, or the host's click after the guest's),
          the guest's I'm ready, both games reach Creature. FAIL twin own_progress_white_off
          (SPORE_FIX_OFF=cell.end_click: the click is held after the game's white end sequence: 0.5 white or more).
  --part afk  Cell fixture: the host full, the guest not (it never presses anything): the host clicks History's check
          twice within 10 s: the vote starts (moving on without the guest), it runs out, both reach Creature.
  --part levels  Cell fixture: host food 80, guest 20; an own meal of 40 of the host (`coop.pool meal 40`, AvatarEat's
          way) stops at 99 (level 1 while the guest is on level 1); the guest reaches 99: the host's kept 21 come in
          (120). FAIL twin own_progress_levels_off (SPORE_FIX_OFF=cell.level_together: the host crosses to 120 alone).
  --part creature  Creature fixture, pace off: a gain of 30 of the host and of 20 of the guest each stay on that game
          (the bars differ by 10). FAIL twin own_progress_creature_off (SPORE_FIX_OFF=core.own_progress: own wallet,
          every gain counts for both). Then the host's brain is fully grown (3000 DNA, level 4) and it asks for the stage
          end: refused for 30 s (the host's game in Creature, no vote) while the guest's level is below 4; the guest
          grows its brain too, the host asks again: the vote, the guest's I'm ready, the host's editor opens.
  --part deeds  Creature fixture: the guest kills one of the host's animals (`coop.npc hit <id> 100000`): the host's
          game pays it, the mod passes it on: the guest's DNA rises, the host's not. FAIL twin own_progress_deeds_off.
  --part rejoin  Creature fixture: the guest earns 40 (its DNA the host's + 40), leaves and joins again (its game loads
          the host's world: the host's DNA): within 20 s its own DNA is back. FAIL twin own_progress_rejoin_off
          (SPORE_FIX_OFF=core.progress_restore: the guest keeps the host's DNA).
  --part store  the same over a host restart (twoinst.session_restart: the host saves, both quit, the host loads, the
          guest joins): the record `progress` of the host's per-player store gives it back. Twin own_progress_store_off.

Key frames at every visible step (both HUD bars, the held click's drawn pool, the vote).

  SPORE_LANE=2 python3 tools/tests/coop_own_progress.py --part cell --first host
"""
import re
import sys
import time

import lane
import settings_lib
import twoinst
from harness import mode_strip as mode, npc_list, pool, waiter
from twoinst import Checks, keyframe, session_restart, shot, start, wait_guest_in

PART = sys.argv[sys.argv.index('--part') + 1] if '--part' in sys.argv else 'cell'
FIRST = sys.argv[sys.argv.index('--first') + 1] if '--first' in sys.argv else 'host'
wait = waiter(step=0.5)
KEY = 'all.shared_dna'


def progress(con):
    """the first lines of `coop.progress` as a dict of its key=value fields"""
    out = con.call('coop.progress')['out'] or ''
    return dict(re.findall(r'(\w+)=(-?[\w.]+)', ' '.join(out.splitlines()[:3]))), out


def food(con):
    m = re.search(r'^cell\((?:host|guest)\): food=(-?\d+)', pool(con), re.MULTILINE)
    return int(m.group(1)) if m else None


def dna_bar(con):
    out = con.call('coop.dna bar')['out'] or ''
    d = re.search(r'dna=(-?[\d.]+)', out)
    b = re.search(r'bar=(-?[\d.]+)', out)
    return (float(d.group(1)) if d else None), (float(b.group(1)) if b else None)


def near(a, b, tol=1.01):
    return a is not None and b is not None and abs(a - b) < tol


def turn_on(host, guest, ck):
    twoinst.forget_settings()
    ck(settings_lib.force(host, [guest], KEY, '2'), 'the guest plays with all.shared_dna 2 (own progress)')
    on = wait(lambda: progress(host)[0].get('active') == '1' and progress(guest)[0].get('active') == '1', 15)
    print(f"  host: {progress(host)[1].splitlines()[0][:200]}", flush=True)
    print(f"  guest: {progress(guest)[1].splitlines()[0][:200]}", flush=True)
    # (the mod's word, printed only: the twins switch it off and must fail on the games' own numbers below)
    print(f"  own progress active on both games: {bool(on)}", flush=True)


def turn_off(host, guest):
    settings_lib.reset([host, guest], (KEY,))
    twoinst.forget_settings()


def set_food(con, want):
    cur = food(con)
    if cur is not None and cur != want:
        con.call(f'coop.pool food {want - cur}')  # the game's AddFoodPoints, exactly N (not paced, not clipped)
    return wait(lambda: food(con) == want, 6)


def vote_active(host):
    return 'vote active' in host.call('coop.vote')['out']


def click_check(con, until, secs=90):
    """History's check the players' way (its caption first when a cinematic waits), until until()"""
    from coop_evo_screens import CAPTION_OK, HISTORY_OK, STAGE_BTN
    t0 = time.time()
    while time.time() - t0 < secs:
        if until():
            return True
        if 'cine=1' in pool(con):
            con.call('click %d %d' % CAPTION_OK)
            time.sleep(3)
            continue
        con.call('click %d %d' % STAGE_BTN)
        time.sleep(3)
        con.call('click %d %d' % HISTORY_OK)
        time.sleep(4)
    return until()


def white_now(con, name):
    from coop_evo_screens import white_share
    path = lane.out_dir('shots') + f'/{name}.png'
    if not con.call('shot ' + path).get('ok'):
        return -1
    return white_share(path)


def both_creature(host, guest, secs=300):
    t0 = time.time()
    while time.time() - t0 < secs:
        mh, mg = mode(host), mode(guest)
        if mh.endswith('Creature') and mg.endswith('Creature'):
            return True
        for con, m in ((host, mh), (guest, mg)):
            if 'Editor' in m:
                con.call('click 1416 1058')  # the first Creature editor's done
        time.sleep(3)
    return False


# ---- Cell
def cell_part(host, guest, ck):
    from coop_evo_screens import press_ready
    turn_on(host, guest, ck)
    ck(set_food(host, 300) and set_food(guest, 120), 'the host at food 300, the guest at 120 (each game its own)')
    time.sleep(6)  # three of the host's 2 s total resends
    fh, fg = food(host), food(guest)
    print(f'  6 s later: host food {fh}, guest food {fg}', flush=True)
    ck(fh == 300 and fg == 120, f"each game keeps its own food (host {fh}, want 300; guest {fg}, want 120)")
    keyframe('own_bars', f'Cell, own progress: each HUD bar its own: the host about 30 % (food {fh}), the guest about 12 % '
                         f'(food {fg})')
    first, second = (host, guest) if FIRST == 'host' else (guest, host)
    ck(set_food(first, 1000), f'the {FIRST} fills its bar (food 1000)')
    time.sleep(3)
    p0 = progress(first)[0]
    held = click_check(first, lambda: vote_active(host) or not mode(first).endswith('Cell') or
                       int(progress(first)[0].get('holds' if FIRST == 'host' else 'waits_told', 0)) >
                       int(p0.get('holds' if FIRST == 'host' else 'waits_told', 0)), 60)
    ck(held and not vote_active(host), f"the {FIRST}'s click on History's check is held (no vote: "
                                       f"{host.call('coop.vote')['out'].splitlines()[0][:80]})")
    time.sleep(8)
    w = white_now(first, f'own_progress_{FIRST}_held')
    print(f"  the {FIRST}'s screen 8 s after the held click: near-white share {w:.2f}; mode {mode(first)}", flush=True)
    ck(mode(first).endswith('Cell'), f"the {FIRST} plays on in Cell ({mode(first)})")
    ck(0 <= w < 0.5, f"the {FIRST}'s screen is drawn, not the white end picture (near-white share {w:.2f}, limit 0.5)")
    keyframe('held_click', f"Cell, own progress: the {FIRST}'s bar full, its click on History's check kept: its pool "
                           f"drawn (not white), the notice 'Not yet ... waiting for ...'; no vote on either screen")
    ck(not vote_active(host), 'still no vote while the other bar is not full')
    ck(set_food(second, 1000), 'the other player fills its bar')
    if FIRST == 'guest':  # the host's own click starts the vote now
        click_check(host, lambda: vote_active(host) or not mode(host).endswith('Cell'), 90)
    started = wait(lambda: vote_active(host) or not mode(host).endswith('Cell'), 30)
    print(f"  {host.call('coop.vote')['out'].splitlines()[0][:120]}; host {progress(host)[1].splitlines()[1][:160]}",
          flush=True)
    ck(started, 'every bar full: the switch vote starts' + (' by itself (the kept click)' if FIRST == 'host' else ''))
    keyframe('vote', 'Cell, own progress, both bars full: the switch vote on both screens', settle=0)
    if vote_active(host):
        ck(press_ready(guest), "the guest clicks I'm ready")
    ck(both_creature(host, guest), f'both games reach Creature ({mode(host)} / {mode(guest)})')


def afk_part(host, guest, ck):
    turn_on(host, guest, ck)
    ck(set_food(host, 1000) and set_food(guest, 200), 'the host full, the guest at 200')
    time.sleep(3)
    p0 = int(progress(host)[0].get('holds', 0))
    click_check(host, lambda: int(progress(host)[0].get('holds', 0)) > p0 or vote_active(host), 60)
    ck(not vote_active(host), 'the first click is held')
    # the second click within 10 s: moving on without the guest
    click_check(host, lambda: vote_active(host) or not mode(host).endswith('Cell'), 9)
    ck(vote_active(host) or not mode(host).endswith('Cell'), 'a second click within 10 s starts the vote without the guest')
    keyframe('afk_vote', "Cell, own progress: the host moves on without the guest's full bar: the vote runs", settle=0)
    ck(both_creature(host, guest, 240), f'the vote ran out, both games reach Creature ({mode(host)} / {mode(guest)})')


def levels_part(host, guest, ck):
    turn_on(host, guest, ck)
    ck(set_food(host, 80) and set_food(guest, 20), 'host food 80, guest 20 (both on level 1)')
    time.sleep(2)
    host.call('coop.pool meal 40')  # an own meal as AvatarEat makes it (pace 1/1 with own progress)
    time.sleep(3)
    fh = food(host)
    p = progress(host)[0]
    print(f"  host food {fh} after its meal of 40; cap {p.get('cap')} bank {p.get('bank')}", flush=True)
    ck(fh == 99, f"the host's meal stops at the level line while the guest is below it (food {fh}, want 99)")
    keyframe('level_line', f'Cell, own progress: the host waits at the end of level 1 (food {fh}); the notice "Nobody '
                           f'grows alone ..."')
    ck(set_food(guest, 99), 'the guest reaches the line (99)')
    ok = wait(lambda: food(host) == 120, 12)
    print(f'  host food {food(host)} after the guest reached the line', flush=True)
    ck(ok, f"the host's kept 21 come in once the guest is at the line (food {food(host)}, want 120)")
    guest.call('coop.pool meal 10')
    ck(wait(lambda: food(guest) == 109, 6), f'the guest crosses too ({food(guest)})')


# ---- Creature
def creature_part(host, guest, ck):
    from coop_stage_end_vote import caption_ok, history, in_creature
    for con in (host, guest):
        con.call('coop.dna pace 0')
    turn_on(host, guest, ck)
    time.sleep(2)
    h0, g0 = dna_bar(host), dna_bar(guest)
    host.call('coop.dna add 30')
    time.sleep(4)
    guest.call('coop.dna add 20')
    time.sleep(5)
    h1, g1 = dna_bar(host), dna_bar(guest)
    print(f'  host {h0} -> {h1}; guest {g0} -> {g1}', flush=True)
    ck(near(h1[0], h0[0] + 30) and near(g1[0], g0[0] + 20),
       f"each gain stays on its own game (host {h0[0]} -> {h1[0]} want +30; guest {g0[0]} -> {g1[0]} want +20)")
    ck(near(h1[1] - h0[1], 30) and near(g1[1] - g0[1], 20), f'each bar its own (host +{h1[1] - h0[1]}, guest +{g1[1] - g0[1]})')
    keyframe('own_dna', f'Creature, own progress: the host about {h1[0]:.0f} DNA, the guest about {g1[0]:.0f}; the bars differ')
    # the host's brain fully grown, the guest's not: the stage end is refused
    host.call('coop.dna add 3000')
    t0 = time.time()
    while time.time() - t0 < 60 and int(progress(host)[0].get('level', -1)) < 4:
        if 'movie=1' in host.call('coop.status')['out']:
            caption_ok(host)
        time.sleep(3)
    lh, lg = int(progress(host)[0].get('level', -1)), int(progress(guest)[0].get('level', -1))
    print(f'  brain levels: host {lh}, guest {lg}', flush=True)
    ck(lh >= 4 and 0 <= lg < 4, f"the host's brain is fully grown, the guest's not (host {lh}, guest {lg})")
    holds0 = int(progress(host)[0].get('holds', 0))
    for _ in range(3):
        if 'movie=1' in host.call('coop.status')['out']:
            caption_ok(host)
            time.sleep(4)
        history(host)
        time.sleep(4)
        if int(progress(host)[0].get('holds', 0)) > holds0:
            break
    time.sleep(12)  # past the 10 s of a second click
    stayed = wait(lambda: not in_creature(host) or vote_active(host), 30) is None
    print(f"  host {mode(host)}; {host.call('coop.vote')['out'].splitlines()[0][:100]}; holds "
          f"{progress(host)[0].get('holds')}", flush=True)
    ck(stayed and in_creature(host), "the host's stage end waits for the guest's bar (still Creature, no vote)")
    keyframe('stage_end_held', 'Creature, own progress: the host fully grown, its stage end refused with the notice '
                               '"Not yet ... waiting for Bob ..."; both play on', settle=0)
    guest.call('coop.dna add 3000')
    t0 = time.time()
    while time.time() - t0 < 60 and int(progress(guest)[0].get('level', -1)) < 4:
        if 'movie=1' in guest.call('coop.status')['out']:
            caption_ok(guest)
        time.sleep(3)
    ck(int(progress(guest)[0].get('level', -1)) >= 4, "the guest's brain is fully grown too")
    # Dismiss any remaining brain-upgrade caption on the guest so its ready click is unobstructed.
    for _ in range(3):
        if 'movie=1' in guest.call('coop.status')['out']:
            caption_ok(guest)
        time.sleep(1)
    wait(lambda: 'all_full=1' in (host.call('coop.progress')['out'] or ''), 15)
    t0 = time.time()
    while time.time() - t0 < 90 and not vote_active(host) and in_creature(host):
        if 'movie=1' in host.call('coop.status')['out']:
            caption_ok(host)
        history(host)
        time.sleep(20)
    ck(vote_active(host), 'every bar full: the stage-end vote starts')
    from coop_ui import Ui
    gu = Ui(guest, 'guest')
    st = gu.wait(lambda s: s.get('open') == '1' and s['ready']['vis'], 15, 'the vote')
    if st:
        gu.click('ready')
    ck(wait(lambda: not in_creature(host), 60), f"the host's editor opens after the guest's ready ({mode(host)})")


def deeds_part(host, guest, ck):
    for con in (host, guest):
        con.call('coop.dna pace 0')
    turn_on(host, guest, ck)
    time.sleep(2)
    ids = [n['id'] for n in npc_list(host) if n['spawned'] is None and not n['dead'] and n['hp'] > 1 and not n['family']]
    mirrored = {n['id'] for n in npc_list(guest) if n['spawned']}
    ids = [i for i in ids if i in mirrored][:6]
    got = False
    for nid in ids:
        h0, g0 = dna_bar(host), dna_bar(guest)
        guest.call(f'coop.npc hit {nid} 100000')
        if wait(lambda: (dna_bar(guest)[0] or 0) > (g0[0] or 0) + 0.5 or (dna_bar(host)[0] or 0) > (h0[0] or 0) + 0.5, 5):
            time.sleep(2)
            h1, g1 = dna_bar(host), dna_bar(guest)
            print(f'  the guest killed {nid}: host {h0} -> {h1}, guest {g0} -> {g1}', flush=True)
            ck(near(h1[0], h0[0], 0.3), f"the host's DNA is not raised by the guest's kill ({h0[0]} -> {h1[0]})")
            ck((g1[0] or 0) > (g0[0] or 0) + 0.5, f"the guest's own DNA rises by its kill ({g0[0]} -> {g1[0]})")
            got = True
            break
    ck(got, f'a guest kill paid DNA (tried {ids})')
    keyframe('deed', "Creature, own progress: the guest's kill raised the guest's DNA only")


def rejoin_part(host, guest, ck):
    for con in (host, guest):
        con.call('coop.dna pace 0')
    turn_on(host, guest, ck)
    time.sleep(2)
    guest.call('coop.dna add 40')
    time.sleep(5)
    h1, g1 = dna_bar(host), dna_bar(guest)
    print(f'  before: host {h1}, guest {g1}', flush=True)
    ck(near(g1[0], h1[0] + 40, 1.5) or (g1[0] or 0) > (h1[0] or 0) + 10, f'the guest has its own 40 more ({g1} / {h1})')
    if PART == 'store':
        host, guest, info = session_restart(host, guest)
        ck(info['saved'], 'the host saved its game before quitting')
        turn_on(host, guest, ck)
    else:
        guest.call('coop.leave')
        time.sleep(3)
        print('  guest joins again:', guest.call(lane.JOIN)['out'].strip()[:120], flush=True)
        wait(lambda: not guest.call('coop.avatar')['ok'], 40)
        ck(wait_guest_in(host, guest), "the guest is in the host's world again")
    ok = wait(lambda: near(dna_bar(guest)[0], g1[0], 1.5), 25)
    h2, g2 = dna_bar(host), dna_bar(guest)
    print(f'  after: host {h2}, guest {g2}; guest {progress(guest)[1].splitlines()[1][:160]}', flush=True)
    ck(ok, f"the guest's own DNA is back ({g2[0]}, want {g1[0]}; the host's {h2[0]})")
    ck(near(h2[0], h1[0], 1.5), f"the host's DNA is its own ({h1[0]} -> {h2[0]})")
    keyframe(PART, f"Creature, own progress after the guest's {PART}: the guest's HUD about {g2[0]:.0f} DNA, the host's "
                   f"about {h2[0]:.0f}")
    return host, guest


def main():
    fixture = 'cell' if PART in ('cell', 'afk', 'levels') else 'creature'
    host, guest = start(fixture)
    ck = Checks()
    time.sleep(4)
    try:
        r = {'cell': cell_part, 'afk': afk_part, 'levels': levels_part, 'creature': creature_part, 'deeds': deeds_part,
             'rejoin': rejoin_part, 'store': rejoin_part}[PART](host, guest, ck)
        if isinstance(r, tuple):
            host, guest = r
    finally:
        turn_off(host, guest)
        for con in (host, guest):
            try:
                con.call('coop.dna pace 1')
            except OSError:
                pass
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
