#!/usr/bin/env python3
"""Tribe: the guest hears the music of the dance at its tribe's fire pit (BUG-0646).

Player report (guest, 0.7.8): "the guest doesn't hear the music which plays during dancing around the fire". The
party at the fire pit (CitizenAction 12 Party, the game behaviour cTribeGameBehaviorParty) plays its music straight
through Audio::PlayProceduralAudio(0x19DC06DF) from its tick (return address 0xD79E81) once the members stand at the
fire, and stops it in its exit. The behaviour runs on the host's game only (the guest's members are mirrors). The lab
games run muted: the test reads each game's own audio commands (`coop.audio`), not our bookkeeping.

1. Two games on the tribe fixture, 4 more members; both audio lists marked.
2. The host's own members are ordered to party at the fire pit (`coop.tribe party`: the order a click on the fire
   pit gives, through the game's DoActionForObjects); the game's party behaviour does the rest.
3. Up to 60 s: the host's game plays 0x19DC06DF; then the guest's game. Up to 90 s more: the host's game stops it.
PASS: the guest's game played 0x19DC06DF while the host's did, and does not play it any more after the host's stop.

SPORE_FIX_OFF=tribe.party_music: the guest's game plays nothing (FAIL).

--editor (BUG-0890): once both games play it the host opens an editor (`coop.vote editor`; the guest's real click on
its "I'm ready" / "Evolve now" opens the guest's own editor too). The host leaves its editor with the red X; its
party ends in Tribe (the game stops the music on the editor's return, or later) while the guest is still in its
editor; then the guest leaves its editor. The guest's game must not play the party music then. Before the fix a guest
dropped a stop that came while it was not in Tribe (and the host sent one only while its game was in Tribe): the
guest's music played on until its next world change. SPORE_FIX_OFF=tribe.party_stop: the guest plays on (FAIL).
(Seen 20261112-085745: the host's game plays the party music on through its own editor; it stops on the return.)

  SPORE_LANE=4 python3 tools/tests/coop_tribe_party_music.py [--editor]
"""
import argparse
import re
import time

from coop_ui import Ui

from coop_tribe_gameover import player_members
from twoinst import Checks, start

MUSIC = '19DC06DF'
CANCEL = (1380, 1062)  # the red X of the creature editor (1440x1080 test window)
TRIBE = '01654C02'


def mode(con):
    out = con.call('game.mode')['out'].split()
    return out[0] if out else ''


def audio(con):
    """(plays of the party music since the mark, tracks of it playing now)"""
    out = con.call('coop.audio list 20000')['out']
    plays = [ln for ln in out.splitlines() if ln.startswith('ev ') and 'cmd=03475365' in ln and f'sound={MUSIC}' in ln]
    now = [ln for ln in out.splitlines() if ln.startswith('playing ') and f'sound={MUSIC}' in ln]
    return plays, now


def leave_editor(con, who):
    con.call('wait.frames 100 30000')
    for _ in range(3):
        con.call('move %d %d' % CANCEL)
        time.sleep(0.5)
        con.call('click %d %d' % CANCEL)
        t0 = time.time()
        while time.time() - t0 < 30 and TRIBE not in mode(con):
            time.sleep(1)
        if TRIBE in mode(con):
            return True
    print(f'  {who} still in its editor ({mode(con)})', flush=True)
    return False


def editor_part(host, guest, ck):
    """BUG-0890: the guest in its own editor while the host's party ends in Tribe; back in Tribe the guest must not
    play the party music on"""
    hu = Ui(host, 'host')
    print('  guest: ' + guest.call('coop.vote editor')['out'].strip().splitlines()[0], flush=True)
    t0 = time.time()
    while time.time() - t0 < 60 and TRIBE in mode(guest):
        if TRIBE in mode(host) and hu.wait(lambda s: s['ready']['vis'], 1, 'ready button'):
            hu.click('ready')  # the guest's request as a vote: the host is ready too
        time.sleep(1)
    print(f'  modes: host {mode(host)} guest {mode(guest)} ({time.time() - t0:.0f} s)', flush=True)
    if not ck(TRIBE not in mode(guest), 'the guest is in its editor'):
        return
    time.sleep(3)
    if TRIBE not in mode(host):  # the vote's editor of the host too: left at once (the party ends on the return)
        ck(leave_editor(host, 'host'), 'the host leaves its editor (the red X) and plays Tribe again')
    print(f'  host in {mode(host)}; guest in {mode(guest)}', flush=True)
    t1, hnow = time.time(), True
    while time.time() - t1 < 90:
        _, hnow = audio(host)
        if not hnow:
            break
        time.sleep(2)
    gmode = mode(guest)
    print(f'  host stopped the party music after {time.time() - t1:.0f} s: {not hnow}; the guest is in {gmode}', flush=True)
    if not ck(not hnow and TRIBE not in gmode, f'the host\'s party music stopped while the guest was in its editor ({gmode})'):
        return
    time.sleep(2)
    ck(leave_editor(guest, 'guest'), 'the guest leaves its editor and plays Tribe again')
    time.sleep(5)
    _, gnow = audio(guest)
    print(f'  guest back in Tribe: playing {gnow}', flush=True)
    ck(not gnow, 'the guest\'s game does not play the party music on after the host\'s stop')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--editor', action='store_true', help='BUG-0890: the host stops it in its editor')
    a = ap.parse_args()
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(5)
    print(' ', host.call('coop.tribe spawn 4')['out'].strip(), flush=True)
    time.sleep(6)
    own, _ = player_members(host)
    ck(len(own) >= 2, f'the host has members ({len(own)})')
    for c in (host, guest):
        print(' ', c.call('coop.audio mark')['out'].strip(), flush=True)
    for i in own:
        host.call(f'coop.tribe select {i} 1')
    print('  party:', host.call('coop.tribe party')['out'].strip(), flush=True)
    t0, hp, gp = time.time(), [], []
    while time.time() - t0 < 60:
        hp, _ = audio(host)
        if hp:
            break
        time.sleep(2)
    print(f'  host party music after {time.time() - t0:.0f} s: {hp[:2]}', flush=True)
    ck(bool(hp), 'the host\'s game plays the party music (its party behaviour, the members at the fire)')
    if not hp:
        ck.faults(host=host, guest=guest)
        return ck.result()
    caller = 'D79E81' in hp[0]
    ck(caller, 'the host\'s play comes from the party behaviour\'s tick (0xD79E81)')
    time.sleep(3)
    gp, gnow = audio(guest)
    print(f'  guest party music: {gp[:2]} playing {gnow}', flush=True)
    ck(bool(gp) and bool(gnow), 'the guest\'s game plays the party music too')
    if a.editor:
        editor_part(host, guest, ck)
        ck.faults(host=host, guest=guest)
        return ck.result()
    t1, hnow = time.time(), True
    while time.time() - t1 < 90:
        _, hnow = audio(host)
        if not hnow:
            break
        time.sleep(3)
    print(f'  host stopped it after {time.time() - t1:.0f} s: {not hnow}', flush=True)
    if not hnow:
        time.sleep(3)
        _, gnow = audio(guest)
        print(f'  guest playing after the host\'s stop: {gnow}', flush=True)
        ck(not gnow, 'the guest\'s game stops it when the host\'s does')
    print('  host', host.call('coop.tribe anims')['out'].splitlines()[-1], flush=True)
    print('  guest', guest.call('coop.tribe anims')['out'].splitlines()[-1], flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
