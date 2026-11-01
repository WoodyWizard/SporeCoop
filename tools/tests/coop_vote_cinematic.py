#!/usr/bin/env python3
"""An evolution vote that ends while a cinematic plays: the editor opens after it, with its buttons (BUG-0404).

The editor soak (coop_editor_soak.py, first run) started a vote while the host's game still played the
evolution's cinematic CRG_Fade_From_Black_Egg (F4F96E43, ~7 s after every editor): the host's editor came up
without its buttons and the cinematic never ended in the editor mode, its input lock stayed on, and the
host could not leave the editor. A vote ends when the others are ready or its time is up, whatever the
host's game plays (a caption, a brain upgrade, the evolution's cinematic).

1. host: `coop.vote editor`; the cinematic starts on the host (by hand, PlayCinematic: deterministic); the
   guest clicks "I'm ready" while it plays. The host's game enters its editor only after the cinematic is
   over (in the editor: no cinematic, the input lock free), and "done" takes it back to the stage.
2. the players' way: the guest leaves its editor first, the host clicks "done", and as soon as the host's
   game is back (the evolution's own cinematic starts) the next vote and the guest's ready. The same checks
   (inconclusive, not failed, when the cinematic was over before the vote ended).
3. the guest's own cinematic: the cinematic plays on the guest when the vote ends; the guest's own editor
   opens after it, with its buttons (own_editor_tick already waits; no switch).

SPORE_FIX_OFF=core.editor_cinematic: the host's editor opens during the cinematic and stays without its
buttons (the test fails; the cinematic is then ended by hand so that the games are usable again).
"""
import re
import time

from coop_puppet_after_events import DONE, mode, settled
from coop_ui import Ui
from twoinst import Checks, shot, start

FADE = 'CRG_Fade_From_Black_Egg'
FADE_ID = 'F4F96E43'


def eax(con, what):
    m = re.search(r'eax=0x([0-9A-Fa-f]+)', con.call('call ' + what)['out'])
    return int(m.group(1), 16) if m else None


def cinematic(con):
    m = re.search(r'cinematic ([0-9A-F]{8}) playing', con.call('coop.status')['out'])
    return m.group(1) if m else None


def lock(con):
    try:
        return int(Ui(con, '').state().get('input_lock', '0'))
    except ValueError:
        return -1


def vote_counters(con):
    out = con.call('coop.vote')['out']
    m = re.search(r'editor held for a cinematic (\d+), cinematics ended (\d+)', out)
    return (int(m.group(1)), int(m.group(2))) if m else (None, None)


def play(con, cm):
    con.call(f'call Simulator::cCinematicManager::PlayCinematic thiscall {cm:#x} s:{FADE} 1 1 0 0 0')


def end_cinematic(con, cm):
    con.call(f'call SporeCoop::cCinematicManager_EndCinematic thiscall {cm:#x}')


def editor_state(con, who, t_limit=90):
    """waits for the game's editor; returns (entered, cinematic id when it entered, input lock then)"""
    t0 = time.time()
    while time.time() - t0 < t_limit:
        if 'Editor' in mode(con):
            time.sleep(1)  # (the mode flips before the editor's first frame)
            return True, cinematic(con), lock(con)
        time.sleep(0.5)
    return False, None, None


def leave_editor(con, m0, secs=60):
    con.call('wait.frames 100 30000')
    t0 = time.time()
    while time.time() - t0 < secs and mode(con) != m0:
        con.call('click %d %d' % DONE)
        time.sleep(3)
    return mode(con) == m0


def vote_and_ready(host, gu):
    """the host's vote (an editor request through the hook), the guest's ready button shown"""
    print('  ' + host.call('coop.vote editor')['out'].strip().splitlines()[0], flush=True)
    return gu.wait(lambda s: s['ready']['vis'], 15, 'ready button') is not None


def ended_during(host, who_cine):
    """the guest's click ended the vote; returns whether the cinematic still played at that moment"""
    t0 = time.time()
    while time.time() - t0 < 8:
        if 'vote none' in host.call('coop.vote')['out']:
            return True, cinematic(who_cine) is not None
        time.sleep(0.2)
    return False, False


def host_case(host, guest, gu, ck, cm_h, name, by_hand, m0):
    """returns False when the host stayed stuck (the rest of the test cannot run)"""
    held0, ended0 = vote_counters(host)
    if not vote_and_ready(host, gu):
        ck(False, f'{name}: the guest sees the vote and its ready button')
        return True
    if by_hand:
        play(host, cm_h)
        time.sleep(0.5)
    gu.click('ready')
    ended, during = ended_during(host, host)
    ck(ended, f'{name}: the guest\'s ready ends the vote')
    if not during:
        print(f'  {name}: inconclusive: the host\'s cinematic was over when the vote ended', flush=True)
    else:
        print(f'  {name}: the vote ended while the host\'s cinematic played', flush=True)
    entered, cin, lk = editor_state(host, 'host')
    print(f'  {name}: host in its editor={entered}, cinematic then={cin}, input lock then={lk}', flush=True)
    guest_in, _, _ = editor_state(guest, 'guest')
    shot(host, f'vote_cinematic_{name}_host_editor')
    if during:
        ck(entered, f'{name}: the host\'s game enters its editor')
        ck(entered and cin is None and lk == 0,
           f'{name}: the host\'s editor opened after the cinematic (in the editor: cinematic={cin}, input lock={lk})')
    back = leave_editor(host, m0) if entered else False
    if during:
        ck(back, f'{name}: "done" takes the host back to the stage ({mode(host)})')
    if not back and 'Editor' in mode(host):
        # the bug: the cinematic never ends in the editor; end it by hand so that the games are usable
        print(f'  {name}: host stuck in the editor with cinematic {cinematic(host)}: ending it by hand', flush=True)
        end_cinematic(host, cm_h)
        time.sleep(2)
        leave_editor(host, m0)
    if guest_in:
        ck(leave_editor(guest, m0), f'{name}: the guest\'s editor opened and "done" took it back')
    held1, ended1 = vote_counters(host)
    print(f'  {name}: editor held for a cinematic {held0} -> {held1}, cinematics ended by the mod {ended0} -> {ended1}',
          flush=True)
    if during and held1 is not None:
        ck(ended1 == ended0, f'{name}: the cinematic played to its end (not ended by the mod)')
    return mode(host) == m0


def wait_quiet(host, guest, secs=40):
    t0 = time.time()
    while time.time() - t0 < secs and (cinematic(host) or cinematic(guest)):
        time.sleep(1)


def main():
    host, guest = start('creature')
    ck = Checks()
    gu = Ui(guest, 'guest')
    settled(host, guest)
    m0 = mode(host)
    cm_h = eax(host, 'Simulator::cCinematicManager::Get cdecl')
    cm_g = eax(guest, 'Simulator::cCinematicManager::Get cdecl')
    ck(cm_h and cm_g, 'both games have their cinematic manager')

    # 1. by hand: the cinematic starts on the host while the vote is open
    ok = host_case(host, guest, gu, ck, cm_h, 'host_cinematic', True, m0)

    # 2. the players' way: the evolution's own cinematic after the host's "done"
    if ok:
        wait_quiet(host, guest)
        settled(host, guest)
        if vote_and_ready(host, gu):
            gu.click('ready')
            editor_state(host, 'host')
            editor_state(guest, 'guest')
            ck(leave_editor(guest, m0), 'evolution: the guest leaves its editor first')
            host.call('wait.frames 100 30000')
            t0 = time.time()
            while time.time() - t0 < 60 and mode(host) != m0:
                host.call('click %d %d' % DONE)
                time.sleep(0.5)
            print(f'  evolution: host back in the stage={mode(host) == m0}, cinematic {cinematic(host)}', flush=True)
            ok = host_case(host, guest, gu, ck, cm_h, 'evolution_cinematic', False, m0)
        else:
            ck(False, 'evolution: the first vote shows the guest its ready button')

    # 3. the guest's own cinematic when the vote ends: its own editor opens after it
    if ok:
        wait_quiet(host, guest)
        settled(host, guest)
        if vote_and_ready(host, gu):
            play(guest, cm_g)
            time.sleep(0.5)
            g_cine = cinematic(guest)
            gu.click('ready')
            ended, during = ended_during(host, guest)
            ck(ended and during and g_cine == FADE_ID,
               f'guest_cinematic: the vote ended while the guest\'s cinematic played ({g_cine}, during={during})')
            entered, cin, lk = editor_state(guest, 'guest')
            print(f'  guest_cinematic: guest in its editor={entered}, cinematic then={cin}, input lock then={lk}',
                  flush=True)
            shot(guest, 'vote_cinematic_guest_editor')
            ck(entered and cin is None and lk == 0,
               f'guest_cinematic: the guest\'s own editor opened after its cinematic (cinematic={cin}, lock={lk})')
            h_in, _, _ = editor_state(host, 'host')
            ck(h_in and leave_editor(host, m0), 'guest_cinematic: the host went through its editor and back')
            ck(entered and leave_editor(guest, m0), f'guest_cinematic: "done" takes the guest back ({mode(guest)})')
        else:
            ck(False, 'guest_cinematic: the guest sees the vote and its ready button')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
