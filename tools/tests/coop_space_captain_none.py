#!/usr/bin/env python3
"""A Space world whose player empire has no captain: the captain screen must not crash the game (BUG-1111).

GitHub #202 (0.7.13, a guest in the host's Space world): the game crashed at 0xA055B5 (a write of 0x54) when the
player opened the captain screen early in Space. The dump: the player empire's captain key (cEmpire +0xC4) was
0/0/0, the portrait's Anim::IAnimWorld::LoadCreature found nothing and SporeCoop::CreatureDisplay_Show passed the
null creature on (docs/re/space.md#space-captain-portrait-null).

A Space save without a captain makes the game ask "Name Your Captain" right after the load; its check button
copies the world's player species into the new captain, and when that species cannot be copied the game sets an
all-zero captain key and does not ask again (lab 2026-11-14). Here:
  1. the host's world loses its captain (key 0) and is saved and shared, as the reporter's world was;
  2. the guest's game asks "Name Your Captain" after the load; while its species manager points at a species this
     game does not have (`coop.captain species`, the reporter's uncopyable species), the guest names the captain
     with real clicks (the dice, the check): the game sets no captain;
  3. the screen is opened as the game's Space update opens it (`coop.captain open`: a Space UI whose rank badge was
     never clicked builds the Galactic Adventures mission screen, Decline / Accept, and goes on to the captain's
     portrait, the reporter's crash site; the badge itself first runs the captain screen's equipment panel, which
     faults and stops for a captain-less empire in the lab, BUG-0304 family);
  4. the rank badge with a real click, then the screen is closed.
Both games must keep running. `SPORE_FIX_OFF=game.captain_portrait` (the game's code) must FAIL: the guest crashes
at 0xA055B5 in step 3.

  SPORE_LANE=2 python3 tools/tests/coop_space_captain_none.py
"""
import re
import time

import coop_space_finale as fz
import play_space
import uifind
from twoinst import Checks, shot, start

BADGE = (1390, 890)            # the rank badge over the ship's picture, 1440x1080
DICE, CHECK = (916, 555), (937, 631)  # "Name Your Captain": a random name, the check button
UNKNOWN_SPECIES = '1BADBEEF'   # an instance no database of the game has
# the game's own handled faults of the captain screen without a captain (BUG-0304 family)
HANDLED = ('0x005971D4', '0x00C02A14')


def captain(con):
    out = con.call('coop.captain')['out']
    m = re.search(r'captain=([0-9A-F]{8})/([0-9A-F]{8})/([0-9A-F]{8}) species=([0-9A-F]{8})', out)
    return (m.group(1), m.group(4), out.strip()) if m else (None, None, out.strip())


def hits(con, sym):
    m = re.search(re.escape(sym) + r'.*?hits=(\d+)', con.call('probe.list')['out'])
    return int(m.group(1)) if m else 0


def alive(con):
    try:
        return bool(con.call('ping').get('ok'))
    except (OSError, ValueError):
        return False


def other_faults(con):
    try:
        out = con.call('exc')['out']
    except (OSError, ValueError):
        return ['(no answer)']
    return [ln for ln in out.splitlines() if ln.startswith('0x') and not any(h in ln for h in HANDLED)]


def main():
    host, guest = start('space', planet=fz.fixture_planet())
    ck = Checks()
    time.sleep(2)

    # 1. the host's world without a captain, saved and shared
    m = re.search(r'eax=(0x[0-9A-Fa-f]+)', host.call('call Simulator::GetPlayerEmpire cdecl')['out'])
    if not ck(m is not None, 'host: the player empire'):
        return ck.result()
    host.call(f'mem.write {int(m.group(1), 16) + 0xC4:#x} 000000000000000000000000')
    print('  host:', captain(host)[2])
    print('  host:', host.call('coop.save')['out'].strip())
    time.sleep(5)
    guest.call('probe.clear')
    guest.call('probe SporeCoop::NameCaptainDialog_Show')
    guest.call('probe SporeCoop::NameCaptainDialog_OnButton')
    print('  host:', host.call('coop.share')['out'].strip())
    asked = fz.wait(lambda: hits(guest, 'NameCaptainDialog_Show') > 0, 120)
    if not ck(asked, 'guest: its game asks "Name Your Captain" after loading the world without a captain'):
        return ck.result()
    time.sleep(3)
    shot(guest, 'captain_name.png')

    # 2. the guest names its captain while its game cannot copy the world's species
    print('  guest:', guest.call(f'coop.captain species {UNKNOWN_SPECIES}')['out'].strip())
    for p in (DICE, CHECK):
        guest.call(f'move {p[0]} {p[1]}')
        time.sleep(0.3)
        guest.call(f'click {p[0]} {p[1]}')
        time.sleep(1.5)
    named = fz.wait(lambda: hits(guest, 'NameCaptainDialog_OnButton') > 0, 10)
    print('  guest:', guest.call('coop.captain species restore')['out'].strip())
    ck(named, 'guest: the check button of "Name Your Captain" was pressed')
    cap, species, line = captain(guest)
    ck(cap == '00000000', f'guest: the game set no captain (the species could not be copied): {line}')
    guest.call('exc clear')

    # 3. the captain screen as the game's Space update opens it: the portrait of no captain
    try:
        print('  guest:', guest.call('coop.captain open')['out'].strip())
    except (OSError, ValueError) as e:
        print('  guest: no answer to coop.captain open:', e)
    time.sleep(3)
    if not ck(alive(guest), 'guest: still runs after the captain screen opened (no crash at 0xA055B5)'):
        return ck.result()
    # this path of the screen is the Galactic Adventures mission screen with the captain's portrait (Decline, Accept)
    up = fz.wait(lambda: uifind.shown(guest, 'Decline') is not None, 10)
    ck(up, 'guest: the screen with the captain\'s portrait is up (Decline, Accept)')
    print('  guest:', captain(guest)[2])
    shot(guest, 'captain_none_open.png')
    p = uifind.shown(guest, 'Decline')
    if p:
        guest.call(f'move {p[0]} {p[1]}')
        time.sleep(0.3)
        guest.call(f'click {p[0]} {p[1]}')
        time.sleep(2)
    if uifind.shown(guest, 'Decline') is not None:  # opened without an adventure offer, Decline may do nothing
        guest.call('key escape')
        time.sleep(2)
    print('  guest: the mission screen', 'closed' if uifind.shown(guest, 'Decline') is None else 'still up (no offer behind it)')

    # 4. the rank badge with a real click
    guest.call(f'move {BADGE[0]} {BADGE[1]}')
    time.sleep(0.5)
    guest.call(f'click {BADGE[0]} {BADGE[1]}')
    fz.wait(lambda: play_space.captain_open(guest), 10)
    time.sleep(2)
    ck(alive(guest), 'guest: still runs after the rank badge')
    shot(guest, 'captain_none_badge.png')
    play_space.close_captain(guest)

    for who, c in (('host', host), ('guest', guest)):
        ck(alive(c), f'{who}: still answers')
        bad = other_faults(c)
        ck(not bad, f'{who}: no fault other than the captain screen\'s handled ones {HANDLED}: {bad[:3]}')
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
