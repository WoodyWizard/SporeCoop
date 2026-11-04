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

4. host_paused: the host's game time stands (the Sporepedia, a real click on its HUD button) when the
   vote ends: the host's editor does not open while it is up, and opens after Esc, with its buttons.
5. guest_paused: the guest's pause menu is open when its own editor after the vote is due (the host ends
   the vote with `coop.vote go`): it waits for the pause and opens after Resume (it was dropped before).
--part gate (suite busy_gate), the other actions at the same gate:
6. action_cinematic / action_paused: an action vote's result (`coop.vote action`) waits for the host's
   cinematic / pause and runs after it.
7. share_cinematic: a share due as after an evolution (`coop.share due`) does not reach the guest while the
   host's cinematic plays and reaches it after (the guest loads; BUG-0019's wait, now through the gate).
   share_paused: the same share during the host's pause goes out at once (the game sets the pause flag
   itself at a stage start, after an editor and under captions: a share held for it came 120 s late,
   guest_editor_hold in lab 20261101-172428; a save during a pause is the game's own way to save).

SPORE_FIX_OFF=core.editor_cinematic: cases 1, 2, 4 fail (the host's editor opens during the cinematic and
stays without its buttons; the cinematic is then ended by hand so that the games are usable again).
SPORE_FIX_OFF=core.own_editor_busy: case 5 fails. SPORE_FIX_OFF=core.vote_action_busy: case 6 fails.
share_cinematic fails with SPORE_FIX_OFF=npc.sharecine (BUG-0019's switch).

  coop_vote_cinematic.py [--part vote|gate|all]
"""
import argparse
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


SPOREPEDIA = (84, 1054)  # the 3rd small HUD button at the bottom left (1440x1080): pauses the game time


def busy(con):
    m = re.search(r'^busy: (\S+)', con.call('coop.status')['out'], re.M)
    return m.group(1) if m else '?'


def wait_free(con, secs=30):
    """the game is not busy (right after a load the game time stands for some seconds: busy=paused; an Esc
    or a click then does something else than opening the pause menu / the Sporepedia)"""
    return within(lambda: busy(con) == '-', secs)


def pause_host_ui(con, ck, name):
    """the Sporepedia by a real click (the game time stands: busy=paused); True when it is up"""
    wait_free(con)
    for _ in range(3):
        con.call('click %d %d' % SPOREPEDIA)
        time.sleep(2)
        if 'paused' in busy(con):
            return True
    ck(False, f'{name}: the Sporepedia pauses the game (busy={busy(con)})')
    return False


def unpause(con):
    for _ in range(3):
        if 'paused' not in busy(con):
            return True
        con.call('key esc')
        time.sleep(2)
    return 'paused' not in busy(con)


def stays(cond, secs):
    """cond() holds for secs seconds (polled); returns the seconds it held"""
    t0 = time.time()
    while time.time() - t0 < secs:
        if not cond():
            return time.time() - t0
        time.sleep(0.5)
    return secs


def within(cond, secs):
    t0 = time.time()
    while time.time() - t0 < secs:
        if cond():
            return True
        time.sleep(0.5)
    return False


def host_paused(host, guest, gu, ck, m0):
    name = 'host_paused'
    if not vote_and_ready(host, gu):
        ck(False, f'{name}: the guest sees the vote and its ready button')
        return True
    if not pause_host_ui(host, ck, name):
        unpause(host)
        host.call('coop.vote go')
        return True
    gu.click('ready')
    ended, _ = ended_during(host, host)
    ck(ended, f'{name}: the guest\'s ready ends the vote while the host\'s Sporepedia is up')
    held = stays(lambda: 'Creature' in mode(host), 10)
    print(f'  {name}: host stayed in the stage {held:.0f} s of 10 while paused (busy={busy(host)})', flush=True)
    shot(host, f'vote_cinematic_{name}_paused')
    ck(held >= 10, f'{name}: the host\'s editor does not open under the Sporepedia ({mode(host)})')
    unpause(host)
    entered, cin, lk = editor_state(host, 'host', 30)
    print(f'  {name}: after Esc: host in its editor={entered}, cinematic={cin}, input lock={lk}', flush=True)
    ck(entered and lk == 0, f'{name}: the host\'s editor opens after the pause')
    back = leave_editor(host, m0) if 'Editor' in mode(host) else False
    ck(back, f'{name}: "done" takes the host back to the stage ({mode(host)})')
    g_in, _, _ = editor_state(guest, 'guest', 30)
    if g_in:
        ck(leave_editor(guest, m0), f'{name}: the guest\'s editor opened and "done" took it back')
    if 'Editor' in mode(host):  # (the bug's state: an editor under the Sporepedia)
        unpause(host)
        leave_editor(host, m0)
    return mode(host) == m0


def guest_paused(host, guest, gu, ck, m0):
    name = 'guest_paused'
    if not vote_and_ready(host, gu):
        ck(False, f'{name}: the guest sees the vote and its ready button')
        return True
    wait_free(guest)
    for _ in range(2):
        guest.call('key esc')  # the guest's pause menu
        time.sleep(2)
        if 'paused' in busy(guest):
            break
    ck('paused' in busy(guest), f'{name}: the guest\'s pause menu stops its game time (busy={busy(guest)})')
    host.call('coop.vote go')  # the host starts it: the guest's own editor is due now
    h_in, _, _ = editor_state(host, 'host', 30)
    held = stays(lambda: 'Creature' in mode(guest), 20)
    print(f'  {name}: guest stayed in the stage {held:.0f} s of 20 while paused; '
          f'{guest.call("coop.vote")["out"].splitlines()[1]}', flush=True)
    ck(held >= 20, f'{name}: the guest\'s editor does not open under its pause menu ({mode(guest)})')
    if h_in:
        leave_editor(host, m0)
    unpause(guest)
    entered, cin, lk = editor_state(guest, 'guest', 60)
    print(f'  {name}: after Resume: guest in its editor={entered}, cinematic={cin}, input lock={lk}', flush=True)
    shot(guest, f'vote_cinematic_{name}_editor')
    ck(entered and lk == 0, f'{name}: the guest\'s own editor opens after the pause (it was dropped before)')
    if entered:
        ck(leave_editor(guest, m0), f'{name}: "done" takes the guest back ({mode(guest)})')
    return mode(host) == m0


def actions_run(con):
    m = re.search(r'actions run (\d+)', con.call('coop.vote')['out'])
    return int(m.group(1)) if m else -1


def action_case(host, gu, ck, cm_h, name, paused):
    n0 = actions_run(host)
    print('  ' + host.call('coop.vote action')['out'].strip().splitlines()[0], flush=True)
    if gu.wait(lambda s: s['ready']['vis'], 15, 'ready button') is None:
        ck(False, f'{name}: the guest sees the vote and its ready button')
        return
    if paused:
        if not pause_host_ui(host, ck, name):
            unpause(host)
            host.call('coop.vote go')
            return
    else:
        play(host, cm_h)
        time.sleep(0.5)
    gu.click('ready')
    ended, _ = ended_during(host, host)
    ck(ended, f'{name}: the guest\'s ready ends the action vote')
    if paused:
        held = stays(lambda: actions_run(host) == n0, 8)
        ck(held >= 8, f'{name}: the action does not run while the host\'s game time stands (busy={busy(host)})')
        unpause(host)
    else:
        held = stays(lambda: actions_run(host) == n0 and cinematic(host), 20)
        ck(not cinematic(host) or actions_run(host) == n0,
           f'{name}: the action does not run while the host\'s cinematic plays')
        if cinematic(host):
            end_cinematic(host, cm_h)
    ran = within(lambda: actions_run(host) == n0 + 1, 15)
    print(f'  {name}: actions run {n0} -> {actions_run(host)} (held {held:.0f} s)', flush=True)
    ck(ran, f'{name}: the action runs once the host\'s game goes on')


def guest_loading(guest):
    return 'Creature' not in mode(guest) or 'receiving world' in guest.call('coop.status')['out']


def share_case(host, guest, ck, cm_h, name, paused):
    if paused:
        if not pause_host_ui(host, ck, name):
            unpause(host)
            return
        print('  ' + host.call('coop.share due')['out'].strip(), flush=True)
        got = within(lambda: guest_loading(guest), 30)
        print(f'  {name}: the share reached the guest during the host\'s pause={got} (busy={busy(host)})', flush=True)
        ck(got, f'{name}: a share is not held by a pause (the guest loads while the host\'s game time stands)')
        unpause(host)
        ck(settled(host, guest, timeout=150), f'{name}: the guest is back in the host\'s world')
        return
    else:
        play(host, cm_h)
        time.sleep(0.5)
        ck(cinematic(host) == FADE_ID, f'{name}: the host plays the cinematic')
    print('  ' + host.call('coop.share due')['out'].strip(), flush=True)
    secs = 10 if paused else 5
    held = stays(lambda: not guest_loading(guest) and (paused or cinematic(host)), secs)
    still = 'paused' in busy(host) if paused else bool(cinematic(host))
    print(f'  {name}: the guest did not load for {held:.0f} s while the host was busy (still busy={still})', flush=True)
    ck(held >= secs or not still, f'{name}: no share reaches the guest while the host is busy ({mode(guest)})')
    if paused:
        unpause(host)
    got = within(lambda: guest_loading(guest), 40)
    ck(got, f'{name}: the share reaches the guest after the host\'s game goes on (the guest loads)')
    ck(settled(host, guest, timeout=150), f'{name}: the guest is back in the host\'s world')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--part', default='vote', choices=('vote', 'gate', 'all'))
    a = ap.parse_args()
    host, guest = start('creature')
    ck = Checks()
    gu = Ui(guest, 'guest')
    settled(host, guest)
    m0 = mode(host)
    cm_h = eax(host, 'Simulator::cCinematicManager::Get cdecl')
    cm_g = eax(guest, 'Simulator::cCinematicManager::Get cdecl')
    ck(cm_h and cm_g, 'both games have their cinematic manager')
    if a.part in ('gate', 'all'):
        for nm, pz in (('action_cinematic', False), ('action_paused', True)):
            action_case(host, gu, ck, cm_h, nm, pz)
            wait_quiet(host, guest)
        for nm, pz in (('share_cinematic', False), ('share_paused', True)):
            share_case(host, guest, ck, cm_h, nm, pz)
            wait_quiet(host, guest)
            # BUG-0439: no wait between the two shares: the monitor leaves out the trace lines of a world transfer
            # (and 15 s after it, BUG-0415), two transfers 18 s apart are no unhealthy link
        if a.part == 'gate':
            ck.faults(host=host, guest=guest)
            return ck.result()

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
    # 4. the host's game time stands (the Sporepedia) when the vote ends
    if ok:
        wait_quiet(host, guest)
        settled(host, guest)
        ok = host_paused(host, guest, gu, ck, m0)
    # 5. the guest's pause menu when its own editor is due
    if ok:
        wait_quiet(host, guest)
        settled(host, guest)
        ok = guest_paused(host, guest, gu, ck, m0)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
