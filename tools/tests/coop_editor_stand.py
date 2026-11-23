#!/usr/bin/env python3
"""A game that stands in an editor while its partner waits (0.7.16.1: PR #216 by IltonBJSilva, its reverse case, CORE-2).

Creature fixture, two games. Each part forces the stand the players saw on slow PCs (`kit.test.hang`: the main thread
sleeps, nothing is sent, as in an editor's first load; `kit.test.dialog`: a message box, the window pumps messages and
the keepalive answers for the game) and checks what the other game does (its session, its vote, its editor).

  --part host     BUG-9304 (PR #216): the host's evolution vote (`coop.vote editor`, the guest's real I'm ready); the
                  host's editor request goes to the game; once the guest has been told that the host is in an editor
                  the host's game stands 45 s (`kit.test.hang 45000`, longer than the session's 30 s timeout). The
                  guest stays in the session all along and after it (its status never offline, the host still has two
                  players). FAIL twin: SPORE_FIX_OFF=core.editor_long_wait (the guest ends its session after 30 s:
                  "connection to host lost (timeout)").
  --part guest    BUG-9305 (the reverse, the owner's rule: a guest stuck in its own editor is removed after 30 s and
                  may join again; the host is not held). Both evolve (`coop.vote editor`), the host leaves its editor,
                  the guest stays in its own:
                  1. vote: in its editor, the guest's game shows a message box for 50 s (`kit.test.dialog 50000`: no
                     state goes out, the keepalive keeps the link) while the host's action vote (`coop.vote action`)
                     waits for it (BUG-1369). Within 40 s of the box the host has removed the guest and the vote has
                     ended (not by its 60 s); the guest's session ends with the host's reason; it joins again (still
                     in its editor). FAIL twin: SPORE_FIX_OFF=core.vote_stuck_kick (the vote runs to its 60 s, the
                     guest is never removed).
                  2. hang: the guest's game stands 45 s in its editor. The host plays on (its frames go on), drops the
                     guest after ~30 s; the guest, when its game goes on, ends its session within 10 s with the host's
                     reason ("rejected: the host heard nothing from this game for 30 s ..."), joins again, leaves its
                     editor and is back in the host's world. FAIL twin: SPORE_FIX_OFF=net.timeout_told (the guest
                     reads the host's older packets and stays "connected" to a host that dropped it).
  --part landing  BUG-9306 (audit CORE-2): the guest in the Creature stage without its avatar yet when the first
                  Creature editor's vote (the landfall editor FD4902BD) ends: `coop.vote noavatar 90` on the guest
                  makes that vote end see no avatar (the moment of its landing; the editor's own rule then waits for
                  the real avatar, 15 s). The guest's game opens its own editor within 60 s of the vote's end.
                  FAIL twin: SPORE_FIX_OFF=core.own_editor_no_avatar (no own editor: the guest plays on).

  SPORE_LANE=N python3 tools/tests/coop_editor_stand.py --part host|guest|landing
"""
import re
import sys
import threading
import time

import lane
from coop_editor_sync import DONE, SAVE_NEW, confirm_captions, in_editor
from coop_ui import Ui
from harness import log_since, log_size, mode, waiter
from sporectl import Console
from twoinst import Checks, host_frame, keyframe, shot, start, wait_guest_in

PART = sys.argv[sys.argv.index('--part') + 1] if '--part' in sys.argv else 'guest'
wait = waiter(step=1.0)


def status(con):
    try:
        return con.call('coop.status')['out'] or ''
    except OSError:
        return ''


def online(con):
    s = status(con)
    return 'client id=' in s and ' connected' in s


def players(host):
    m = re.search(r'players=(\d+)/', status(host))
    return int(m.group(1)) if m else -1


def both_evolve(host, guest, ck, gu):
    """the host's evolution vote, the guest's real I'm ready: both games in their editors"""
    print(' ', host.call('coop.vote editor')['out'].strip().splitlines()[0], flush=True)
    time.sleep(2)
    gu.wait(lambda s: s['ready']['vis'], 10, 'ready button')
    gu.click('ready')
    return ck(wait(lambda: in_editor(host), 60), "the host's editor opened") and \
        ck(wait(lambda: in_editor(guest), 120), "the guest's own editor opened")


def leave_editor(con, m0, secs=90):
    con.call('wait.frames 100 30000')
    con.call('click %d %d' % DONE)
    time.sleep(3)
    if in_editor(con):
        con.call('click %d %d' % SAVE_NEW)
    confirm_captions((con,), 10)
    return wait(lambda: mode(con) == m0 and con.call('coop.avatar')['ok'], secs)


def rejoin(guest):
    guest.call('coop.leave')
    time.sleep(2)
    guest.call(lane.JOIN)
    return wait(lambda: online(guest), 40)


def host_part(host, guest, ck):
    gu = Ui(guest, 'guest')
    if not both_evolve(host, guest, ck, gu):
        return
    told = wait(lambda: re.search(r'view heard=1 host=editor', status(guest)), 30)
    print('  guest:', [ln for ln in status(guest).splitlines() if ln.startswith('host busy')][:1], flush=True)
    ck(told is not None, 'the guest knows the host is in an editor')
    print('  host:', host.call('kit.test.hang 45000')['out'].strip(), flush=True)
    t0 = time.time()
    dropped = None
    while time.time() - t0 < 60:
        if not online(guest):
            dropped = time.time() - t0
            break
        time.sleep(2)
    print(f'  the host stood 45 s; the guest {"left the session after %.0f s" % dropped if dropped else "stayed"}',
          flush=True)
    ck(dropped is None, 'the guest stays in the session while the host\'s game stands 45 s in its editor')
    ck(wait(lambda: players(host) == 2, 20), f'the host still has both players ({players(host)})')
    shot(guest, 'editor_stand_host_guest.png')
    keyframe('host_stood', "the guest's game after the host's 45 s stand in its editor: still in the session")


def guest_part(host, guest, ck):
    gu = Ui(guest, 'guest')
    m0 = mode(host)
    if not both_evolve(host, guest, ck, gu):
        return
    ck(leave_editor(host, m0), 'the host is back in the stage, the guest still in its own editor')
    time.sleep(3)
    # 1. a box on the guest's main thread while the host's action vote waits for it
    off = log_size(lane.GUEST)
    print(' ', host.call('coop.vote action')['out'].strip().splitlines()[0], flush=True)
    time.sleep(3)
    ck('vote active' in host.call('coop.vote')['out'], 'the action vote waits for the guest in its editor')
    box = {}

    def show_box():
        c = Console(lane.GUEST, timeout=75)
        try:
            box['out'] = c.call('kit.test.dialog 50000')['out']
        except OSError as e:
            box['out'] = f'({e})'
    th = threading.Thread(target=show_box, daemon=True)
    t_d = time.time()
    th.start()
    removed = vote_end = None
    while time.time() - t_d < 58:
        if removed is None and players(host) == 1:
            removed = time.time() - t_d
        if vote_end is None and 'vote active' not in host.call('coop.vote')['out']:
            vote_end = time.time() - t_d
        if removed is not None and vote_end is not None:
            break
        time.sleep(1)
    print(f'  host: the guest removed after {removed} s, the vote ended after {vote_end} s of the box', flush=True)
    ck(removed is not None and removed <= 40, f'the host removes the guest whose game stands ({removed} s)')
    ck(vote_end is not None and vote_end <= 40, f'the vote does not wait for it to its 60 s ({vote_end} s)')
    th.join(70)
    print('  guest box:', (box.get('out') or '').strip()[:160], flush=True)
    ck(wait(lambda: not online(guest), 15), "the guest's session has ended")
    lines = [ln.strip() for ln in log_since(lane.GUEST, off) if 'rejected:' in ln]
    print(f'  guest: {lines[:1]}', flush=True)
    ck(any('your game stood' in ln for ln in lines), "the guest is told why (its game stood while the vote waited)")
    ck(rejoin(guest), 'the guest joins again (still in its editor)')
    ck(in_editor(guest), 'the guest is still in its own editor')
    time.sleep(5)
    # 2. the guest's game stands 45 s in its editor (no packet goes out)
    off = log_size(lane.GUEST)
    f0 = host_frame(host)
    print('  guest:', guest.call('kit.test.hang 45000')['out'].strip(), flush=True)
    t0 = time.time()
    gone = None
    while time.time() - t0 < 43:
        if gone is None and players(host) == 1:
            gone = time.time() - t0
        time.sleep(1)
    f1 = host_frame(host)
    print(f'  host: frames {f0} -> {f1}, the guest dropped after {gone} s', flush=True)
    ck(f1 > f0 + 300, f"the host plays on while the guest's game stands ({f1 - f0} frames)")
    ck(gone is not None and 25 <= gone <= 40, f'the host drops the guest after about 30 s ({gone} s)')
    t_on = t0 + 45
    ended = None
    while time.time() - t_on < 20:
        if time.time() > t_on and not online(guest):
            ended = time.time() - t_on
            break
        time.sleep(1)
    lines = [ln.strip() for ln in log_since(lane.GUEST, off) if 'rejected:' in ln]
    print(f'  guest: session ended {ended} s after its game went on; {lines[:1]}', flush=True)
    ck(ended is not None and ended <= 10, f"the guest's session ends when its game goes on ({ended} s)")
    ck(any('heard nothing from this game' in ln for ln in lines), "the guest is told why (the host's reason)")
    ck(rejoin(guest), 'the guest joins again')
    ck(leave_editor(guest, m0, 30) or not in_editor(guest), 'the guest leaves its editor')
    ck(wait_guest_in(host, guest, 180), "the guest is back in the host's world")
    keyframe('guest_back', "both games in the Creature stage together after the guest's stands")


def landing_part(host, guest, ck):
    gu = Ui(guest, 'guest')
    m0 = mode(host)
    print('  guest:', guest.call('coop.vote noavatar 90')['out'].strip().splitlines()[0], flush=True)
    print(' ', host.call('coop.vote editor FD4902BD')['out'].strip().splitlines()[0], flush=True)
    time.sleep(2)
    gu.wait(lambda s: s['ready']['vis'], 10, 'ready button')
    gu.click('ready')
    ck(wait(lambda: in_editor(host), 60), "the host's landfall editor opened")
    t0 = time.time()
    own = wait(lambda: in_editor(guest), 60)
    print(f'  guest: own editor after {time.time() - t0:.0f} s: {own is not None}', flush=True)
    ck(own is not None, "the guest's game opens its own first Creature editor (its avatar came after the vote)")
    shot(guest, 'editor_stand_landing_guest.png')
    keyframe('landing', "the guest's own first Creature editor, opened after its avatar came")
    for con in (host, guest):
        if in_editor(con):
            leave_editor(con, m0)
    ck(wait_guest_in(host, guest, 180), "both play the host's world again")


def main():
    host, guest = start('creature')
    ck = Checks()
    time.sleep(4)
    {'host': host_part, 'guest': guest_part, 'landing': landing_part}[PART](host, guest, ck)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
