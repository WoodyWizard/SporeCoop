#!/usr/bin/env python3
"""Creature -> Tribe waits for the vote; a guest cannot start it alone (BUG-1085, docs/09-coop-design.md 9.3).

core.editor_now (BUG-0516) let the host's editor request with the tag 0x64AA168 through without a vote, taking it for
the mating call's. That tag is the stage-end editor's (the Timeline's evolve button, Creature -> Tribe,
docs/re/creature.md#creature-mating-call): the stage change went on without asking the guests (they got a pointless
"Evolve now"), while the mating evolutions (tag 0x12191CA) waited for the vote.

Two games on the creature fixture; the stage end the players' way, as the campaign does it (coop_campaign.py
kick_creature): `coop.dna add 3000` (brain upgrade 4, the bar full; its caption "Your brain is fully grown ..." answered
with its check), then History and its check, retried every 25 s while no cinematic plays.

default (the host ends the stage):
  1. the host's game builds the stage-end request (its log: "editor request in Creature: ... tag 064AA168");
  2. the host's game stays in the Creature stage for 10 s (no editor) and its vote "wants to move on to the Tribe
     stage" is open; the guest's co-op window shows "I'm ready", not "Evolve now";
  3. the guest's real click on I'm ready: the host's editor opens.
  SPORE_FIX_OFF=core.editor_now_tag: the host's editor opens at once, the guest gets "Evolve now": FAILS at 2.
--guest (the guest ends the stage first, the host plays on):
  1. the guest's game builds the stage-end request (its log line, as above): the game offers it to a guest;
  2. for 40 s after it the guest's game stays in the Creature stage (no editor, no Tribe of its own, its stage drawn:
     the editor-pending flag 0) and the host's game too; the guest's panels say that the host moves everybody on.
  SPORE_FIX_OFF=core.editor_now_tag: the guest's editor opens and its game leaves for Tribe alone: FAILS at 2.

  SPORE_LANE=6 python3 tools/tests/coop_stage_end_vote.py [--guest]
"""
import re
import sys
import time

import lane
from coop_campaign import CAPTION_OK, EDITOR_DONE, HISTORY_BTN, HISTORY_OK
from coop_ui import Ui
from twoinst import Checks, keyframe, start

READY = ("I'm ready", 'Я готов')
EVOLVE = ('Evolve now', 'Эволюция')


def mode(con):
    return con.call('game.mode')['out'].strip()


def in_creature(con):
    return mode(con).endswith('Creature')


def log_lines(inst, pat):
    try:
        with open(lane.game_log(inst), 'rb') as f:
            return [ln for ln in f.read().decode('utf-8', 'replace').splitlines() if pat in ln]
    except OSError:
        return []


def stage_end_requests(inst):
    return [ln for ln in log_lines(inst, 'editor request in Creature:') if 'tag 064AA168' in ln]


def history(con):
    con.call(f'move {HISTORY_BTN[0]} {HISTORY_BTN[1]}')
    time.sleep(0.4)
    con.call(f'click {HISTORY_BTN[0]} {HISTORY_BTN[1]}')
    time.sleep(3)
    con.call(f'move {HISTORY_OK[0]} {HISTORY_OK[1]}')
    time.sleep(0.4)
    con.call(f'click {HISTORY_OK[0]} {HISTORY_OK[1]}')
    time.sleep(2)


def press_stage_end(con, inst, until, secs=180):
    """DNA 3000, then History and its check every 25 s while no cinematic plays, until until() or a request"""
    print('  ' + con.call('coop.dna add 3000')['out'].strip()[:100], flush=True)
    n0 = len(stage_end_requests(inst))
    t0, last, movie, ok_at = time.time(), 0.0, None, 0.0
    while time.time() - t0 < secs:
        if len(stage_end_requests(inst)) > n0 or until():
            return True
        if 'movie=1' in con.call('coop.status')['out']:
            # the brain upgrade's caption ("Your brain is fully grown ...") waits for its check, as a player reads it
            movie = movie or time.time()
            if time.time() - movie >= 6 and time.time() - ok_at >= 10:
                caption_ok(con)
                ok_at = time.time()
        else:
            movie = None
            if in_creature(con) and time.time() - last > 25:
                history(con)
                last = time.time()
        time.sleep(2)
    return False


def caption_ok(con):
    con.call(f'move {CAPTION_OK[0]} {CAPTION_OK[1]}')
    time.sleep(0.4)
    con.call(f'click {CAPTION_OK[0]} {CAPTION_OK[1]}')


def host_case(host, guest, ck):
    gu = Ui(guest, 'guest')
    hinst = lane.HOST
    asked = press_stage_end(host, hinst, lambda: not in_creature(host) or 'vote active' in host.call('coop.vote')['out'])
    reqs = stage_end_requests(hinst)
    print(f'  host: {reqs[-1].strip()[-120:] if reqs else "no stage-end request seen"}', flush=True)
    ck(asked and reqs, 'the host\'s game built the stage-end editor request (Timeline\'s evolve)')
    t0 = time.time()
    stayed = True
    while time.time() - t0 < 10:
        if not in_creature(host):
            stayed = False
            break
        time.sleep(1)
    v = host.call('coop.vote')['out']
    print(f'  host {mode(host)}; vote: {v.splitlines()[0][:120]}', flush=True)
    ck(stayed and 'move on to the Tribe stage' in v, f'the host\'s editor waits for the vote "wants to move on to the '
                                                     f'Tribe stage" ({mode(host)}; {v.splitlines()[0][:80]})')
    st = gu.wait(lambda s: s.get('open') == '1' and s['ready']['vis'], 15, 'the vote')
    txt = st and st['ready']['text']
    ck(txt in READY, f'the guest\'s co-op window asks for its I\'m ready, not Evolve now ({txt!r})')
    keyframe('stage_end_vote', 'the host still in Creature with the vote "Alice wants to move on to the Tribe stage"; '
                               'the guest\'s co-op window with I\'m ready', settle=0)
    if txt in READY:
        gu.click('ready')
        t1 = time.time()
        while time.time() - t1 < 60 and in_creature(host):
            time.sleep(1)
        ck(not in_creature(host), f'the host\'s editor opens after the guest\'s ready ({mode(host)})')


def guest_case(host, guest, ck):
    ginst = lane.GUEST
    asked = press_stage_end(guest, ginst, lambda: not in_creature(guest))
    reqs = stage_end_requests(ginst)
    print(f'  guest: {reqs[-1].strip()[-120:] if reqs else "no stage-end request seen"}; mode {mode(guest)}', flush=True)
    ck(asked and reqs, 'the guest\'s game offers the stage end to a guest (its stage-end editor request)')
    t0, left, flag = time.time(), None, None
    while time.time() - t0 < 40:
        if not in_creature(guest) and left is None:
            left = mode(guest)
        time.sleep(2)
    m = re.search(r'editor-pending flag (-?\d+)', guest.call('coop.vote')['out'])
    flag = m and int(m.group(1))
    print(f'  40 s later: guest {mode(guest)} (left for {left}), host {mode(host)}, flag {flag}; '
          f'{[ln.strip()[-110:] for ln in log_lines(ginst, "stage-end request")][-2:]}', flush=True)
    ck(left is None and in_creature(guest) and guest.call('coop.avatar')['ok'],
       f'the guest\'s game stays in the Creature stage (no editor, no Tribe of its own: {left or mode(guest)})')
    ck(flag == 0, f'the guest\'s stage is drawn (the game\'s editor-pending flag cleared: {flag})')
    ck(in_creature(host), f'the host plays on in the Creature stage ({mode(host)})')
    if left and 'Editor' in mode(guest):  # the bug: how far the guest's own stage end goes (observed, not asserted)
        time.sleep(5)
        guest.call(f'click {EDITOR_DONE[0]} {EDITOR_DONE[1]}')
        t1, seen = time.time(), []
        while time.time() - t1 < 180:
            m = mode(guest)
            if not seen or seen[-1][1] != m:
                seen.append((round(time.time() - t1), m))
            if m.endswith('Tribe'):
                break
            if 'movie=1' in guest.call('coop.status')['out']:
                caption_ok(guest)
            time.sleep(3)
        keyframe('stage_end_guest_alone', 'the bug: the guest after its own stage-end editor (Tribe of its own?) while '
                                          'the host plays Creature', settle=0)
        print(f'  the guest after "done" in its stage-end editor: {seen}; host {mode(host)}', flush=True)
        return
    keyframe('stage_end_guest', 'both games in the Creature stage; the guest\'s panel "the host moves everybody on to '
                                'the Tribe stage (after a vote)"', settle=0)


def main():
    host, guest = start('creature')
    ck = Checks()
    time.sleep(3)
    (guest_case if '--guest' in sys.argv else host_case)(host, guest, ck)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
