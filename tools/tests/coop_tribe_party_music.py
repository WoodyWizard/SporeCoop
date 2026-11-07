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

  SPORE_LANE=4 python3 tools/tests/coop_tribe_party_music.py
"""
import re
import time

from coop_tribe_gameover import player_members
from twoinst import Checks, start

MUSIC = '19DC06DF'


def audio(con):
    """(plays of the party music since the mark, tracks of it playing now)"""
    out = con.call('coop.audio list 20000')['out']
    plays = [ln for ln in out.splitlines() if ln.startswith('ev ') and 'cmd=03475365' in ln and f'sound={MUSIC}' in ln]
    now = [ln for ln in out.splitlines() if ln.startswith('playing ') and f'sound={MUSIC}' in ln]
    return plays, now


def main():
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
