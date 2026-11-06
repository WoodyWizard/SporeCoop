#!/usr/bin/env python3
"""A DNA spend of one player leaves the other player's evolution bar alone (BUG-0630).

The game's evolution bar is cPlayer +0x10F0 (the total earned in the stage: the brain-level and migration
thresholds read it; `coop.dna` prints it as bar=). The game adds to it in AddEvolutionPoints (every reward) and
never lowers it: its editor spends with SetEvolutionPoints (the DNA only). 0.7.8 moved the shared DNA with
AddEvolutionPoints for spends too, so each editor spend of one player lowered the other's bar by the amount
(players, Discord 2026-11-06: "our evolution bar just got reset to almost 0", "my evolution bar was halved").

Creature fixture. Checked on both games, the game's own values (GetEvolutionPoints and cPlayer +0x10F0):
  --case script (default): spends made the game's way (`coop.dna spend N` = SetEvolutionPoints, as the
      editor's result), no editor:
      1. a gain of the host and of the guest (a game reward, `coop.dna gain`) raises BOTH bars by the DNA gained;
      2. the host spends 30: the shared DNA -30 on both, the guest's bar unchanged;
      3. the guest spends 30: the shared DNA -30 on both, the host's bar unchanged;
      4. both spend 20 at the same moment: -40 on both, both bars unchanged;
      5. both spend more than the pool at once: the DNA 0 on both (never below), both bars unchanged.
  --case editors: the reported case: an evolution vote, the host's evolution editor and the guest's own
      editor open at once, each adds a part (real input) and presses done: both bars unchanged, the same DNA
      on both, the pool lower and not below 0 (two mouths of 25 on a pool of 45: the players' overspend).

  coop_evo_bar.py [--case script|editors]    (SPORE_FIX_OFF=creature.bar_spend fails)
"""
import re
import sys
import time

from coop_editor_sync import CANCEL, DONE, SAVE_NEW, add_part, back, confirm_captions, editor_ui, in_editor, mode
from coop_ui import Ui
from twoinst import Checks, shot, start

CASE = sys.argv[sys.argv.index('--case') + 1] if '--case' in sys.argv else 'script'


def read(con):
    """(DNA, bar) of this game"""
    out = con.call('coop.dna')['out'] or ''
    d = re.search(r'dna=(-?[\d.]+)', out)
    b = re.search(r'bar=(-?[\d.]+|nan|-nan)', out)
    num = lambda m: float(m.group(1)) if m and 'nan' not in m.group(1) else None
    return num(d), num(b)


def settle(host, guest, secs=20):
    """both games show the same DNA, and nothing moves for 3 s: ((dna, bar) host, (dna, bar) guest)"""
    t0, since, last = time.time(), None, None
    while time.time() - t0 < secs:
        h, g = read(host), read(guest)
        now = (h, g)
        if h[0] is not None and g[0] is not None and abs(h[0] - g[0]) < 1 and now == last:
            since = since or time.time()
            if time.time() - since >= 3:
                return h, g
        else:
            since = None
        last = now
        time.sleep(0.5)
    return read(host), read(guest)


def show(what, h, g):
    print(f'  {what}: host dna {h[0]} bar {h[1]} | guest dna {g[0]} bar {g[1]}', flush=True)


def near(a, b, tol=1.01):
    return a is not None and b is not None and abs(a - b) < tol


def script_case(host, guest, ck):
    h0, g0 = settle(host, guest)
    show('start', h0, g0)
    ck(h0[1] is not None and g0[1] is not None, 'both games have an evolution bar to read (cPlayer +0x10F0)')
    if (h0[0] or 0) < 120:
        print('  host:', host.call('coop.dna add 120')['out'].strip(), flush=True)
        time.sleep(2)
        confirm_captions((host, guest), 10)  # a brain level's cinematic waits for its caption
        h0, g0 = settle(host, guest)
        show('after +120 on the host', h0, g0)
    # 1. gains raise both bars by the DNA gained (the game's rewards; the party's pace may scale them)
    for who, con in (('host', host), ('guest', guest)):
        print(f'  {who}:', con.call('coop.dna gain 10')['out'].strip(), flush=True)
        time.sleep(1.5)
        confirm_captions((host, guest), 4)
        h1, g1 = settle(host, guest)
        show(f'after a gain of the {who}', h1, g1)
        dh, dg = h1[0] - h0[0], g1[0] - g0[0]
        ck(dh > 0.5 and near(h1[1] - h0[1], dh), f"a gain of the {who}: the host's bar rose with the DNA ({h1[1] - h0[1]:+.1f} / {dh:+.1f})")
        ck(dg > 0.5 and near(g1[1] - g0[1], dg), f"a gain of the {who}: the guest's bar rose with the DNA ({g1[1] - g0[1]:+.1f} / {dg:+.1f})")
        h0, g0 = h1, g1
    # 2.-4. spends the game's way
    steps = (('the host spends 30', ((host, 30),)), ('the guest spends 30', ((guest, 30),)),
             ('both spend 20 at once', ((host, 20), (guest, 20))))
    for what, spends in steps:
        for con, n in spends:
            con.call(f'coop.dna spend {n}')
        total = sum(n for _, n in spends)
        h1, g1 = settle(host, guest)
        show(what, h1, g1)
        ck(near(h1[0], h0[0] - total) and near(g1[0], g0[0] - total),
           f'{what}: the shared DNA fell by {total} on both games ({h0[0]} -> {h1[0]} / {g0[0]} -> {g1[0]})')
        ck(near(h1[1], h0[1]), f"{what}: the host's evolution bar unchanged ({h0[1]} -> {h1[1]})")
        ck(near(g1[1], g0[1]), f"{what}: the guest's evolution bar unchanged ({g0[1]} -> {g1[1]})")
        h0, g0 = h1, g1
    # 5. more than the pool at once: never below 0
    n = int(h0[0] * 0.75) + 1
    host.call(f'coop.dna spend {n}')
    guest.call(f'coop.dna spend {n}')
    h1, g1 = settle(host, guest)
    show(f'both spend {n} of {h0[0]} at once', h1, g1)
    ck(h1[0] is not None and g1[0] is not None and h1[0] >= 0 and g1[0] >= 0 and near(h1[0], g1[0]),
       f'an overspend: the DNA is not below 0 and the same on both ({h1[0]} / {g1[0]})')
    ck(near(h1[1], h0[1]) and near(g1[1], g0[1]),
       f'an overspend: both bars unchanged (host {h0[1]} -> {h1[1]}, guest {g0[1]} -> {g1[1]})')


def editors_case(host, guest, ck):
    gu = Ui(guest, 'guest')
    m0 = mode(host)
    h0, g0 = settle(host, guest)
    if (h0[0] or 0) < 45:  # a mouth costs 25: one for each (two of them overspend the pool, as the players' did;
        # more DNA grows the brain, whose caption holds the vote)
        print('  host:', host.call('coop.dna add 45')['out'].strip(), flush=True)
        time.sleep(3)
        confirm_captions((host, guest), 12)
        h0, g0 = settle(host, guest)
    show('before the vote', h0, g0)
    print(' ', host.call('coop.vote evolve')['out'].strip(), flush=True)
    time.sleep(2)
    gu.wait(lambda s: s['ready']['vis'], 10, 'ready button')
    gu.click('ready')
    ok_h = ck(wait(lambda: in_editor(host), 60), 'the host is in its editor')
    ok_g = ck(wait(lambda: in_editor(guest), 120), "the guest's own editor opened")
    if not (in_editor(host) and in_editor(guest)):
        shot(host, 'evo_bar_no_editor_host.png')
        shot(guest, 'evo_bar_no_editor_guest.png')
        print('  host status:', host.call('coop.status')['out'][:1500], flush=True)
        return
    ck(wait(lambda: editor_ui(host) and editor_ui(guest), 90, 2), 'both editors show their interface')
    host.call('wait.frames 200 60000')
    guest.call('wait.frames 200 60000')
    time.sleep(3)
    in_h, in_g = read(host)[0], read(guest)[0]
    for con in (host, guest):  # each adds a part (the unlocked mouths are lower in the first column, BUG-0368)
        add_part(con, 780)
        if read(con)[0] == (in_h if con is host else in_g):
            add_part(con, 170)
    time.sleep(2)
    e_h, e_g = read(host)[0], read(guest)[0]
    shot(host, 'evo_bar_host_editor.png')
    shot(guest, 'evo_bar_guest_editor.png')
    print(f'  in the editors: host DNA {in_h} -> {e_h}, guest {in_g} -> {e_g}', flush=True)
    # (the DNA read inside an editor can stay at the stage's value until the editor's result: lab
    # 20261106-175242 showed 45 -> 45 in both editors and 0 after them; the prices are checked by the pool below)
    host.call('click %d %d' % DONE)  # both press done (the players' case: two editors on one pool)
    guest.call('click %d %d' % DONE)
    time.sleep(3)
    if in_editor(guest):
        guest.call('click %d %d' % SAVE_NEW)
    confirm_captions((host, guest), 30)
    ck(back(host, m0, 90), 'the host is back in the stage')
    ck(back(guest, m0, 120), 'the guest is back in the stage')
    confirm_captions((host, guest), 20)
    time.sleep(8)
    h1, g1 = settle(host, guest, 30)
    show('after both editors', h1, g1)
    shot(host, 'evo_bar_host_after.png')
    shot(guest, 'evo_bar_guest_after.png')
    ck(near(h1[0], g1[0]), f'both games show the same DNA ({h1[0]} / {g1[0]})')
    ck(h1[0] is not None and h0[0] is not None and 0 <= h1[0] < h0[0] - 0.5,
       f'the two editors took their prices from the shared pool, not below 0 ({h0[0]} -> {h1[0]})')
    ck(near(h1[1], h0[1]), f"the host's evolution bar unchanged by the two editors ({h0[1]} -> {h1[1]})")
    ck(near(g1[1], g0[1]), f"the guest's evolution bar unchanged by the two editors ({g0[1]} -> {g1[1]})")


def wait(cond, secs, step=1.0):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return None


def main():
    host, guest = start('creature')
    ck = Checks()
    time.sleep(4)
    (editors_case if CASE == 'editors' else script_case)(host, guest, ck)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
