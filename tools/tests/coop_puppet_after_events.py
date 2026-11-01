#!/usr/bin/env python3
"""The partner's creature is shown where the partner is after the things that rebuild or move it (audit 17).

Two players stand in the Creature stage. After each event both games must show the other's creature again
(the game object of the puppet exists: `coop.status` `remote N ... puppet=<object>`) and draw it within NEAR m
of where the partner's own game has its avatar (`coop.track`: the drawn position, before the mod's driving),
within WAIT s of the event's end:

  rejoin     the guest leaves and joins again
  editor     the evolution vote's editors (`coop.vote editor`, the guest's ready, "done" on both)
  death      the guest dies (`coop.team die`, the game's death, caption, the respawn as a hatchling at the
             nest), then the host
  altenter   Alt+Enter twice on each game (fullscreen and back: the device is made again)
  cinematic  a cinematic on the host, then on the guest (CRG_ClaimNest, closed with Enter)
  share      the host sends its world again (`coop.share`): the guest loads it

  coop_puppet_after_events.py [--only rejoin,editor,...]
  PUPPET_EVENTS_PLANT=hold: after the first event the host stops moving the guest's puppet (`coop.puppet hold
  on`, a planted bug: the next checks must FAIL)"""
import argparse
import math
import os
import re
import time

import lane
import uifind
from coop_alt_enter import fullscreen, keys as alt_enter
from coop_creature_respawn import close_pause
from coop_ui import Ui
from twoinst import Checks, shot, start, wait_guest_in

NEAR = 2.0
WAIT = 20.0
EVENTS = ('rejoin', 'editor', 'death', 'altenter', 'cinematic', 'share')
DONE = (1416, 1062)  # the creature editor's "done" (coop_guest_editor_locks.py)


def own_pos(con):
    m = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', con.call('coop.avatar')['out'])
    return tuple(map(float, m.groups())) if m else None


def puppet_obj(con, pid):
    m = re.search(rf"remote {pid} '[^']*' puppet=([0-9A-Fa-f]+)", con.call('coop.status')['out'])
    return m.group(1) if m and int(m.group(1), 16) else None


def drawn(con, pid, secs=0.6):
    """the last drawn position of player pid's puppet in this game (coop.track d columns)"""
    con.call('coop.track start')
    time.sleep(secs)
    con.call('coop.track stop')
    n = int(re.search(r'samples=(\d+)', con.call('coop.track count')['out']).group(1))
    last = None
    for line in con.call(f'coop.track dump {max(0, n - 200)} 200')['out'].splitlines():
        f = line.split()
        if len(f) >= 23 and int(f[1]) == pid:
            last = tuple(map(float, f[7:10]))
    return last


def mode(con):
    return con.call('game.mode')['out']


def seen(ck, observer, partner, pid, oname, pname, label, wait=WAIT):
    """observer's game shows partner's creature within NEAR m of partner's own position"""
    t0 = time.time()
    last = None
    while time.time() - t0 < wait:
        obj = puppet_obj(observer, pid)
        d = drawn(observer, pid) if obj else None
        p = own_pos(partner)
        dist = math.dist(d, p) if d and p else None
        last = (obj, dist)
        if obj and dist is not None and dist <= NEAR:
            break
        time.sleep(0.5)
    obj, dist = last
    ck(obj is not None and dist is not None and dist <= NEAR,
       f'{label}: the {oname}\'s game shows the {pname}\'s creature (object {obj}) where it is '
       f'({"none drawn" if dist is None else f"{dist:.2f} m"} <= {NEAR} m, {time.time() - t0:.0f} s)')


def both(ck, host, guest, label, wait=WAIT):
    seen(ck, host, guest, 1, 'host', 'guest', label, wait)
    seen(ck, guest, host, 0, 'guest', 'host', label, wait)


def together(host, guest):
    p = own_pos(host)
    guest.call('coop.npc gopos %.2f %.2f %.2f' % (p[0] + 3, p[1], p[2]))
    time.sleep(2)


def ev_rejoin(host, guest, ck):
    guest.call('coop.leave')
    time.sleep(4)
    guest.call(lane.JOIN)
    ck(wait_guest_in(host, guest, timeout=90), 'rejoin: the guest is in the host\'s world again')
    time.sleep(2)


def settled(host, guest, secs=6, timeout=120):
    """both games in the Creature stage and together for `secs` seconds in a row (a share's load is over)"""
    t0, run = time.time(), 0
    while time.time() - t0 < timeout and run < secs:
        ok = 'Creature' in mode(host) and 'Creature' in mode(guest) and wait_guest_in(host, guest, timeout=2)
        run = run + 1 if ok else 0
        time.sleep(0.3)
    return run >= secs


def ev_editor(host, guest, ck):
    # both in the stage and the guest in the host's world first (an evolution's world share reloads the guest:
    # a vote then found it in the galaxy menu or loading, not in its editor)
    settled(host, guest)
    m0 = mode(host)
    gu = Ui(guest, 'guest')
    print('  ' + host.call('coop.vote editor')['out'].strip(), flush=True)
    time.sleep(2)
    gu.wait(lambda s: s['ready']['vis'], 10, 'ready button')
    gu.click('ready')
    t0 = time.time()
    while time.time() - t0 < 60 and not ('Editor' in mode(host) and 'Editor' in mode(guest)):
        time.sleep(1)
    ck('Editor' in mode(host) and 'Editor' in mode(guest), f'editor: both games in an editor ({mode(host)} / {mode(guest)})')
    host.call('wait.frames 200 60000')
    guest.call('wait.frames 200 60000')
    for con in (host, guest):
        t0 = time.time()
        while time.time() - t0 < 60 and mode(con) != m0:  # (clicked again: an editor still building its UI)
            con.call('click %d %d' % DONE)
            time.sleep(3)
    ck(mode(host) == m0 and mode(guest) == m0, 'editor: both back in the stage')
    # the evolution's cinematic after the editor (F4F96E43 on the host): a player watches it or skips it with
    # Enter. A next vote while it plays opened an editor that the cinematic's input lock kept without its
    # buttons (coop_editor_soak.py, BUG-0404)
    t0 = time.time()
    while time.time() - t0 < 40 and (cinematic_playing(host) or cinematic_playing(guest)):
        for con in (host, guest):
            if cinematic_playing(con):
                con.call('key enter')
        time.sleep(2)
    time.sleep(2)


def die(con, name, ck):
    out = ''
    t0 = time.time()
    while time.time() - t0 < 15 and 'lethal' not in out:  # (no avatar for a moment after a share's load)
        out = con.call('coop.team die')['out']
        if 'lethal' not in out:
            time.sleep(1)
    ck('lethal' in out, f'death: {name} takes a lethal hit')
    died = back = False
    t0 = time.time()
    while time.time() - t0 < 60:
        cap = uifind.shown(con, 'You were killed')
        if cap:
            con.call('key enter')
        time.sleep(1)
        close_pause(con)
        out = con.call('coop.avatar')['out']
        m = re.search(r'hp=([-\d.]+)/', out)
        died |= bool(cap) or not m or float(m.group(1)) <= 0
        if died and time.time() - t0 > 6 and m and float(m.group(1)) > 0 and not uifind.shown(con, 'You were killed'):
            back = True
            break
    ck(died and back, f'death: {name} died and respawned ({time.time() - t0:.0f} s)')
    time.sleep(2)


def ev_death(host, guest, ck):
    settled(host, guest)
    die(guest, 'the guest', ck)
    both(ck, host, guest, 'death (guest)')
    die(host, 'the host', ck)
    time.sleep(1)


def ev_altenter(host, guest, ck):
    for con, name in ((guest, 'guest'), (host, 'host')):
        states = [fullscreen(con)]
        for _ in range(2):
            alt_enter(con, False)
            time.sleep(4)
            states.append(fullscreen(con))
        ck(states[1] != states[0] and states[2] == states[0], f'altenter: the {name}\'s game went fullscreen and back {states}')
    time.sleep(2)


def cinematic_playing(con):
    return re.search(r'cinematic ([0-9A-F]{8}) playing', con.call('coop.status')['out'])


def ev_cinematic(host, guest, ck):
    for con, name in ((host, 'host'), (guest, 'guest')):
        m = re.search(r'eax=0x([0-9A-Fa-f]+)', con.call('call Simulator::cCinematicManager::Get cdecl')['out'])
        if not m:
            ck(False, f'cinematic: the {name}\'s cinematic manager')
            continue
        con.call(f'call Simulator::cCinematicManager::PlayCinematic thiscall {int(m.group(1), 16):#x} s:CRG_ClaimNest 1 1 0 0 0')
        t0 = time.time()
        while time.time() - t0 < 10 and not cinematic_playing(con):
            time.sleep(0.5)
        ck(cinematic_playing(con) is not None, f'cinematic: the {name} plays CRG_ClaimNest')
        time.sleep(3)
        t0 = time.time()
        while time.time() - t0 < 40 and cinematic_playing(con):
            con.call('key enter')
            time.sleep(2)
        if con is host:
            ck(cinematic_playing(con) is None, f'cinematic: the {name}\'s cinematic is over')
        else:  # (CRG_ClaimNest played on a guest by the test did not end with Enter in 2 of 3 runs: printed only, the
            # puppets are judged while or after it; the share's load ends it)
            print(f'  cinematic: the guest\'s cinematic {"is over" if cinematic_playing(con) is None else "still plays"}', flush=True)
        time.sleep(2)


def ev_share(host, guest, ck):
    print('  ' + host.call('coop.share')['out'].strip()[:100], flush=True)
    time.sleep(5)
    ck(wait_guest_in(host, guest, timeout=90), 'share: the guest is in the host\'s world again')
    time.sleep(3)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--only', default='')
    a = ap.parse_args()
    events = [e for e in (a.only.split(',') if a.only else EVENTS) if e]
    host, guest = start('creature')
    ck = Checks()
    together(host, guest)
    both(ck, host, guest, 'start')
    plant = os.environ.get('PUPPET_EVENTS_PLANT', '')
    for i, ev in enumerate(events):
        print(f'  --- {ev}', flush=True)
        globals()['ev_' + ev](host, guest, ck)
        together(host, guest)
        both(ck, host, guest, f'after {ev}')
        shot(guest, f'after_{ev}_guest')
        shot(host, f'after_{ev}_host')
        if plant == 'hold' and i == 0:
            print('  PLANT: ' + host.call('coop.puppet hold on')['out'].strip(), flush=True)
    if plant == 'hold':
        host.call('coop.puppet hold off')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
