#!/usr/bin/env python3
"""Own DNA per player (WP-D of 0.7.16, BUG-1291; owner decision 2026-11-19).

Every gain of the party raises every player's DNA alike (as the shared pool did); what a player's editor spends comes
off its own DNA only; the evolution bar is the party's. The session setting all.shared_dna on brings the old pool back.
Every check reads the GAME's own numbers on each machine (`coop.dna bar`: GetEvolutionPoints and cPlayer +0x10F0),
never the mod's wallet table (printed for the log only).

Creature fixture, pace off (gains whole: `coop.dna add` gives exactly N), spends the game's way (`coop.dna spend N` =
SetEvolutionPoints, as the editor's result):

  --case spend (default)  the guest spends 30: after 6 s (three resends) the host's DNA unchanged, the guest's 30 lower
          and still so; the host spends 20: the guest's unchanged; a gain of 20 (the host's, then the guest's): both
          rise by 20 and keep their difference, the bars equal.  FAIL: SPORE_FIX_OFF=creature.own_dna (one pool: the
          host's DNA drops by the guest's 30)
  --case shared   all.shared_dna on (`coop.settings force`): the guest spends 30: both games 30 lower, the same DNA
          (the old pool).  FAIL twin: DNA_SHARED_TWIN=1 (the setting is not set: own DNA, the host keeps its 30)
  --case reload   the guest spends 25, the host 40 (the guest now has 15 more than the host's game and save); the host
          sends its world again (`coop.share`, the guest's game loads the host's save: its DNA and bar are the host's
          then): 8 s after the guest is back its DNA is its own again (host + 15) and its bar equals the host's.
          FAIL: SPORE_FIX_OFF=creature.dna_bar_set (the 15 go through AddEvolutionPoints: the guest's bar ahead)
  --case rejoin   the guest spends 25, leaves and joins again: its DNA is the host's - 25, the host's unchanged.
          FAIL: SPORE_FIX_OFF=creature.own_dna (the spend left the host's DNA too)
  --case store    the guest spends 25; the host saves, both quit, start again, the host loads, the guest joins
          (twoinst.session_restart): the guest's DNA is the host's - 25; the host's DNA is its own (its save holds only
          its own DNA: no guest spend in it).  FAIL: SPORE_FIX_OFF=creature.own_dna_store (the guest starts again at
          the host's DNA)
  --case editors  the players' route: the guest spends 30 first (the two budgets differ), the host's evolution (vote,
          the host's editor, the guest's Evolve now: its own editor); key frame of both editors' DNA readouts (the
          host's shows its DNA, the guest's 30 less); each adds a part and presses done: each game's DNA fell by its
          own editor's price only: the host presses done first, then the guest: the host's DNA does not move with the
          guest's price, and the guest's fell by its price only (not the host's too); both bars unchanged; key frames.
          FAIL: SPORE_FIX_OFF=creature.own_dna (the guest's price comes off the host's DNA, the host's off the guest's)

  --case host_editor  BUG-1311: 60 DNA (under the brain's next level); the guest spends 30 (its DNA 30 under the
          host's); the host evolves (`coop.vote evolve`: its editor opens at once), the guest takes its Evolve now
          offer (its own editor); the guest presses done first (no change) and, the moment it is back in the stage,
          the host presses done (no change): the guest plays its creature again before the host's game is back (and
          before the 4 s after which a host's editor counts as away, when its totals are not applied). The guest's game
          never takes the host's DNA (no shared total in its log), its DNA stays its own (the host's - 30) and its
          evolution bar the party's. FAIL: SPORE_FIX_OFF=creature.own_dna_edge (the host's first total after its editor
          is the host's own DNA: the guest's game goes 30 up through AddEvolutionPoints, its bar 30 ahead of the
          party's). The host alone in its editor while the guest plays on does not show it (lab 20261119-113728: the
          stray total came while the guest counted the host away and was dropped)

  SPORE_LANE=2 python3 tools/tests/coop_dna_own.py [--case spend|shared|reload|rejoin|store|editors|host_editor]
"""
import os
import re
import sys
import time

import lane
import settings_lib
import twoinst
from harness import waiter
from twoinst import Checks, keyframe, session_restart, shot, start, wait_guest_in

CASE = sys.argv[sys.argv.index('--case') + 1] if '--case' in sys.argv else 'spend'
wait = waiter(step=1.0)


def read(con):
    """(DNA, bar) of this game: the game's own values"""
    out = con.call('coop.dna bar')['out'] or ''
    d = re.search(r'dna=(-?[\d.]+)', out)
    b = re.search(r'bar=(-?[\d.]+|nan|-nan)', out)
    num = lambda m: float(m.group(1)) if m and 'nan' not in m.group(1) else None
    return num(d), num(b)


def calm(host, guest, secs=20, hold=3):
    """both games' (DNA, bar) stay the same for `hold` s (the 2 s resends included): ((dna, bar) host, guest)"""
    t0, since, last = time.time(), None, None
    while time.time() - t0 < secs:
        now = (read(host), read(guest))
        if now[0][0] is not None and now[1][0] is not None and now == last:
            since = since or time.time()
            if time.time() - since >= hold:
                return now
        else:
            since = None
        last = now
        time.sleep(0.5)
    return read(host), read(guest)


def show(what, h, g):
    print(f'  {what}: host dna {h[0]} bar {h[1]} | guest dna {g[0]} bar {g[1]}', flush=True)


def wallets(host, guest):
    for who, con in (('host', host), ('guest', guest)):
        lines = (con.call('coop.dna own')['out'] or '').strip().splitlines()
        print(f'  {who}: ' + ' | '.join(lines[1:4]), flush=True)


def near(a, b, tol=1.01):
    return a is not None and b is not None and abs(a - b) < tol


def prepare(host, guest, ck, need=150):
    """pace off (every gain whole) and enough DNA on both; the same DNA on both at the start"""
    for con in (host, guest):
        con.call('coop.dna pace 0')
    time.sleep(1)
    h, g = calm(host, guest)
    if (h[0] or 0) < need:
        print('  host:', host.call(f'coop.dna add {need}')['out'].strip(), flush=True)
        time.sleep(3)
        # a brain level's cinematic waits for its caption on each game; its Enter is taken only once the caption
        # shows, about 16 s after the gain (BUG-1318: 12 s of Enters left the guest's caption up, its game in the
        # cinematic, and the guest's own editor never opened: lab 20261119-104136, 20261119-111652)
        from coop_editor_sync import cinematic
        t0 = time.time()
        while time.time() - t0 < 60 and any(cinematic(c) for c in (host, guest)):
            for c in (host, guest):
                if cinematic(c):
                    c.call('key enter')
            time.sleep(2)
        print(f'  brain captions closed after {time.time() - t0:.0f} s: host {not cinematic(host)}, '
              f'guest {not cinematic(guest)}', flush=True)
        h, g = calm(host, guest, 30)
    show('start', h, g)
    ck(h[0] is not None and near(h[0], g[0]), f'both games start with the same DNA ({h[0]} / {g[0]})')
    return h, g


def spend_case(host, guest, ck):
    h0, g0 = prepare(host, guest, ck)
    guest.call('coop.dna spend 30')
    time.sleep(6)  # three of the host's 2 s resends
    h1, g1 = calm(host, guest)
    show('the guest spent 30', h1, g1)
    wallets(host, guest)
    ck(near(h1[0], h0[0]), f"the guest's spend left the host's DNA alone ({h0[0]} -> {h1[0]})")
    ck(near(g1[0], g0[0] - 30), f"the guest's DNA is 30 lower and stays so ({g0[0]} -> {g1[0]})")
    ck(near(h1[1], h0[1]) and near(g1[1], g0[1]), f'both bars unchanged (host {h0[1]} -> {h1[1]}, guest {g0[1]} -> {g1[1]})')
    host.call('coop.dna spend 20')
    time.sleep(5)
    h2, g2 = calm(host, guest)
    show('the host spent 20', h2, g2)
    ck(near(h2[0], h1[0] - 20), f"the host's DNA 20 lower ({h1[0]} -> {h2[0]})")
    ck(near(g2[0], g1[0]), f"the host's spend left the guest's DNA alone ({g1[0]} -> {g2[0]})")
    for who, con in (('host', host), ('guest', guest)):
        con.call('coop.dna add 20')
        time.sleep(3)
        h3, g3 = calm(host, guest)
        show(f'a gain of 20 of the {who}', h3, g3)
        ck(near(h3[0], h2[0] + 20) and near(g3[0], g2[0] + 20),
           f"a gain of the {who} counts for both ({h2[0]} -> {h3[0]} / {g2[0]} -> {g3[0]})")
        ck(near(h3[1], g3[1]), f"after a gain of the {who} both bars are the party's ({h3[1]} / {g3[1]})")
        h2, g2 = h3, g3
    keyframe('own_dna', f"Creature: each HUD shows its own DNA: the host about {h2[0]:.0f}, the guest about {g2[0]:.0f} "
             "(10 more: the host spent 20, the guest 30); the evolution bars equally full")


def shared_case(host, guest, ck):
    twin = os.environ.get('DNA_SHARED_TWIN') == '1'
    if not twin:
        ck(settings_lib.force(host, [guest], 'all.shared_dna', 'on'), 'the guest plays with all.shared_dna on')
    else:
        print('  DNA_SHARED_TWIN=1: all.shared_dna stays off (the twin must FAIL)', flush=True)
    h0, g0 = prepare(host, guest, ck)
    guest.call('coop.dna spend 30')
    time.sleep(6)
    h1, g1 = calm(host, guest)
    show('the guest spent 30 (shared pool)', h1, g1)
    wallets(host, guest)
    ck(near(h1[0], h0[0] - 30) and near(g1[0], g0[0] - 30),
       f"shared DNA: the guest's spend left both games 30 lower ({h0[0]} -> {h1[0]} / {g0[0]} -> {g1[0]})")
    host.call('coop.dna spend 20')
    time.sleep(5)
    h2, g2 = calm(host, guest)
    show('the host spent 20 (shared pool)', h2, g2)
    ck(near(h2[0], g2[0]) and near(h2[0], h1[0] - 20), f'shared DNA: the same DNA on both ({h2[0]} / {g2[0]})')
    keyframe('shared_dna', f'Creature, shared DNA: both HUDs show the same DNA, about {h2[0]:.0f}')


def reload_case(host, guest, ck):
    from coop_migration import in_world
    h0, g0 = prepare(host, guest, ck)
    guest.call('coop.dna spend 25')
    time.sleep(4)
    host.call('coop.dna spend 40')
    time.sleep(5)
    h1, g1 = calm(host, guest)
    show('the guest spent 25, the host 40', h1, g1)
    ck(near(g1[0], h1[0] + 15), f"the guest has 15 more than the host's game ({g1[0]} / {h1[0]})")
    print('  host:', host.call('coop.share')['out'].strip()[:120], flush=True)
    time.sleep(5)
    ck(wait(lambda: in_world(guest), 180, 2), "the guest's game loaded the host's world again")
    time.sleep(8)
    h2, g2 = calm(host, guest)
    show('after the guest loaded the host\'s world', h2, g2)
    wallets(host, guest)
    ck(near(h2[0], h1[0]), f"the host's DNA unchanged by the guest's load ({h1[0]} -> {h2[0]})")
    ck(near(g2[0], g1[0]), f"the guest's DNA is its own again after the load ({g1[0]} -> {g2[0]}; the host's save has {h2[0]})")
    ck(near(g2[1], h2[1]), f"the guest's evolution bar is the party's after the load (guest {g2[1]}, host {h2[1]})")
    keyframe('reload', f"Creature after the guest's load: the guest's HUD about {g2[0]:.0f} DNA, the host's about "
             f"{h2[0]:.0f}; the evolution bars equally full")


def rejoin_case(host, guest, ck):
    h0, g0 = prepare(host, guest, ck)
    guest.call('coop.dna spend 25')
    time.sleep(5)
    h1, g1 = calm(host, guest)
    show('the guest spent 25', h1, g1)
    guest.call('coop.leave')
    time.sleep(3)
    print('  guest joins again:', guest.call(lane.JOIN)['out'].strip()[:120], flush=True)
    wait(lambda: not guest.call('coop.avatar')['ok'], 40)
    ck(wait_guest_in(host, guest), "the guest is in the host's world again")
    time.sleep(8)
    h2, g2 = calm(host, guest, 30)
    show('after the rejoin', h2, g2)
    wallets(host, guest)
    ck(near(h2[0], h0[0]), f"the host's DNA is its own ({h0[0]} -> {h2[0]})")
    ck(near(g2[0], h2[0] - 25), f"after the rejoin the guest's DNA is its own, the host's - 25 ({g2[0]} / {h2[0]})")
    keyframe('rejoin', f"Creature after the guest's rejoin: the guest's HUD about {g2[0]:.0f} DNA, the host's about {h2[0]:.0f}")


def store_case(host, guest, ck):
    h0, g0 = prepare(host, guest, ck)
    guest.call('coop.dna spend 25')
    time.sleep(5)
    h1, g1 = calm(host, guest)
    show('the guest spent 25', h1, g1)
    wallets(host, guest)
    host, guest, info = session_restart(host, guest)
    ck(info['saved'], 'the host saved its game before quitting')
    time.sleep(10)
    h2, g2 = calm(host, guest, 30)
    show('next session', h2, g2)
    wallets(host, guest)
    print('  host store:', (host.call('coop.store')['out'] or '').strip().splitlines()[:3], flush=True)
    ck(near(h2[0], h1[0]), f"the host's save holds the host's own DNA, no guest spend in it ({h1[0]} -> {h2[0]})")
    ck(near(g2[0], h2[0] - 25), f"next session: the guest's DNA is its own again, the host's - 25 ({g2[0]} / {h2[0]})")
    keyframe('next_session', f"Creature, next session: the guest's HUD about {g2[0]:.0f} DNA, the host's about {h2[0]:.0f}")
    return host, guest


def editors_case(host, guest, ck):
    from coop_editor_sync import DONE, SAVE_NEW, add_part, back, confirm_captions, editor_ui, in_editor, mode
    from coop_ui import Ui
    gu = Ui(guest, 'guest')
    m0 = mode(host)
    h0, g0 = prepare(host, guest, ck, need=110)
    guest.call('coop.dna spend 30')
    time.sleep(5)
    h0, g0 = calm(host, guest)
    show('the guest spent 30 before the vote', h0, g0)
    ck(near(g0[0], h0[0] - 30), f"the budgets differ by the guest's 30 ({h0[0]} / {g0[0]})")
    print(' ', host.call('coop.vote evolve')['out'].strip(), flush=True)
    time.sleep(2)
    gu.wait(lambda s: s['ready']['vis'], 10, 'ready button')
    gu.click('ready')
    ck(wait(lambda: in_editor(host), 60), 'the host is in its editor')
    ck(wait(lambda: in_editor(guest), 120), "the guest's own editor opened")
    if not (in_editor(host) and in_editor(guest)):
        shot(host, 'dna_own_no_editor_host.png')
        shot(guest, 'dna_own_no_editor_guest.png')
        return
    ck(wait(lambda: editor_ui(host) and editor_ui(guest), 90, 2), 'both editors show their interface')
    host.call('wait.frames 200 60000')
    guest.call('wait.frames 200 60000')
    time.sleep(3)
    keyframe('editors', f"both creature editors: the DNA readout (top) of the host's about {h0[0]:.0f}, of the guest's "
             f"about {g0[0]:.0f} (30 less)", settle=0)
    for con in (host, guest):  # each adds a part (the unlocked mouths are lower in the first column, BUG-0368)
        before = read(con)[0]
        add_part(con, 780)
        if read(con)[0] == before:
            add_part(con, 170)
    time.sleep(2)
    keyframe('editors_part', "both editors with a new part: each DNA readout lower by its own part's price", settle=0)
    # the host presses done first: its own price only; then the guest: its price must not reach the host's DNA
    host.call('click %d %d' % DONE)
    confirm_captions((host,), 30)
    ck(back(host, m0, 90), 'the host is back in the stage')
    confirm_captions((host,), 20)
    time.sleep(6)
    hm = read(host)
    print(f'  the host back from its editor (the guest still in its own): host dna {hm[0]} bar {hm[1]}', flush=True)
    guest.call('click %d %d' % DONE)
    time.sleep(3)
    if in_editor(guest):
        guest.call('click %d %d' % SAVE_NEW)
    confirm_captions((host, guest), 30)
    ck(back(guest, m0, 120), 'the guest is back in the stage')
    confirm_captions((host, guest), 20)
    time.sleep(8)
    h1, g1 = calm(host, guest, 30)
    show('after both editors', h1, g1)
    wallets(host, guest)
    dh, dg = h0[0] - hm[0], g0[0] - g1[0]
    ck(dh > 0.5 and dg > 0.5, f'each editor took a price ({dh:+.0f} from the host, {dg:+.0f} from the guest)')
    ck(near(h1[0], hm[0]), f"the guest's editor price did not reach the host's DNA (after the host's editor {hm[0]}, "
       f"after the guest's {h1[0]})")
    ck(near(g1[0], g0[0] - dg) and dg > 0.5 and not near(g1[0], g0[0] - dg - dh),
       f"the guest's DNA paid only its own price ({g0[0]} -> {g1[0]}, the host's price {dh:.0f})")
    ck(near(h1[1], h0[1]) and near(g1[1], g0[1]), f'both bars unchanged (host {h0[1]} -> {h1[1]}, guest {g0[1]} -> {g1[1]})')
    keyframe('after_editors', f"Creature after both editors: the host's HUD about {h1[0]:.0f} DNA, the guest's about {g1[0]:.0f}")


def host_editor_case(host, guest, ck):
    from coop_editor_sync import DONE, back, cinematic, confirm_captions, editor_ui, in_editor, mode
    from coop_ui import Ui
    from harness import log_since, log_size
    gu = Ui(guest, 'guest')
    m0 = mode(host)
    # 60 DNA, not 150: a gain of 150 grows the brain, whose caption held input into the editor (its done ignored for
    # minutes, lab 20261119-112341); evo_bar and editor_sync stay under it the same way
    h0, g0 = prepare(host, guest, ck, need=60)

    def no_cinematic(secs):  # a caption holds input: the editor's done would be ignored
        t0 = time.time()
        while time.time() - t0 < secs and (cinematic(host) or cinematic(guest)):
            confirm_captions((host, guest), 4)
        return not cinematic(host)
    no_cinematic(60)
    guest.call('coop.dna spend 30')
    time.sleep(6)
    h0, g0 = calm(host, guest)
    show('the guest spent 30', h0, g0)
    ck(near(g0[0], h0[0] - 30), f"the guest has 30 less than the host ({g0[0]} / {h0[0]})")
    off = log_size(lane.GUEST)
    print(' ', host.call('coop.vote evolve')['out'].strip(), flush=True)
    time.sleep(2)
    gu.wait(lambda s: s['ready']['vis'], 10, 'ready button')
    gu.click('ready')
    ok = ck(wait(lambda: in_editor(host), 60), "the host's editor opened")
    ok = ck(wait(lambda: in_editor(guest), 120), "the guest's own editor opened") and ok
    if not ok:
        shot(host, 'dna_own_host_editor_none_host.png')
        shot(guest, 'dna_own_host_editor_none_guest.png')
        return
    ck(wait(lambda: editor_ui(host) and editor_ui(guest), 90, 2), 'both editors show their interface')
    host.call('wait.frames 200 60000')
    guest.call('wait.frames 200 60000')
    time.sleep(3)
    no_cinematic(60)
    guest.call('click %d %d' % DONE)  # the guest first, no change
    ck(wait(lambda: mode(guest) == m0 and guest.call('coop.avatar')['ok'], 90, 0.25), 'the guest is back in the stage first')
    host.call('click %d %d' % DONE)  # at once: the host leaves while the guest plays its creature
    confirm_captions((host, guest), 30)
    ck(back(host, m0, 90), 'the host is back in the stage')
    confirm_captions((host, guest), 20)
    time.sleep(6)
    h1, g1 = calm(host, guest, 30)
    show('after both editors (the guest back first)', h1, g1)
    wallets(host, guest)
    # the guest's game log: a shared total ("dna: a -> b cause=") is the host's DNA applied as the guest's (own ones
    # say "dna: own a -> b")
    stray = [ln.strip() for ln in log_since(lane.GUEST, off) if re.search(r'coop: dna: -?[\d.]+ -> -?[\d.]+ cause=', ln)]
    ck(not stray, f"the guest's game never took the host's DNA when the host left its editor ({stray[:2]})")
    ck(near(g1[0], h1[0] - 30), f"the guest's DNA is still its own, the host's - 30 ({g1[0]} / {h1[0]})")
    ck(near(g1[1], h1[1]), f"the guest's evolution bar is the party's after the editors (guest {g1[1]}, host {h1[1]})")
    keyframe('host_editor', f"Creature after both editors: the guest's HUD about {g1[0]:.0f} DNA, the host's about "
             f"{h1[0]:.0f}; the evolution bars equally full")


def main():
    host, guest = start('creature')
    ck = Checks()
    time.sleep(4)
    forced = CASE == 'shared'
    if forced:
        twoinst.forget_settings()
    try:
        r = {'spend': spend_case, 'shared': shared_case, 'reload': reload_case, 'rejoin': rejoin_case,
             'store': store_case, 'editors': editors_case, 'host_editor': host_editor_case}[CASE](host, guest, ck)
        if CASE == 'store' and r:
            host, guest = r
    finally:
        if forced:
            settings_lib.reset([host, guest], ('all.shared_dna',))
            twoinst.forget_settings()
        for con in (host, guest):
            try:
                con.call('coop.dna pace 1')
            except OSError:
                pass
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
