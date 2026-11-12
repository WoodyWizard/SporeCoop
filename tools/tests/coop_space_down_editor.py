#!/usr/bin/env python3
"""BUG-0836: the disabled state's invincibility and a trip to an editor that rebuilds the player's ship.

The guest's ship is disabled (`coop.space disable 1`; nobody repairs it: `repairdist 0`). The guest opens the captain
screen (a real click on the rank badge) and enters the outfitter ("Enter Outfitter"), leaves it again and is back in
Space. When the disabled state ends (its 20 s stand still in the editor), the guest's own ship must take damage again:
`coop.space hit 20` through the game's damage path takes HP and the ship's invincible byte is 0.

Before the fix the state wrote the invincible byte every frame on whatever ship the player had, and the end cleared
it only on the ship the state was set on: a new ship object made by the trip stayed invincible until a load.
SPORE_FIX_OFF=space.down_ship_only: FAIL when the trip made a new ship object (the test prints both ships).

  SPORE_LANE=2 python3 tools/tests/coop_space_down_editor.py
"""
import re
import time

import play_space
import uifind
from twoinst import Checks, shot, start

BADGE = (1390, 890)  # the rank badge over the ship's picture, 1440x1080


def sp(con, *a):
    return con.call('coop.space ' + ' '.join(str(x) for x in a))['out'].strip()


def ds(con):
    return dict(re.findall(r'(\w+)=(\S+)', sp(con, 'downstate')))


def mode(con):
    return con.call('game.mode')['out'].strip()


def wait(cond, secs, step=0.5):
    t0 = time.time()
    while time.time() - t0 < secs:
        v = cond()
        if v:
            return v
        time.sleep(step)
    return cond()


def click_caption(con, caption):
    """the centre of the first window with this caption (also one under a hidden parent), clicked like a player"""
    hits = uifind.find(con, caption)
    if not hits:
        return False
    x, y, _ = next((h for h in hits if h[2]), hits[0])
    con.call(f'move {x} {y}')
    time.sleep(0.4)
    con.call(f'click {x} {y}')
    return True


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(4)
    for c in (host, guest):
        sp(c, 'repairdist 0')
    print('  guest:', sp(guest, 'disable 1'), flush=True)
    time.sleep(1)
    d0 = ds(guest)
    print('  guest disabled:', sp(guest, 'downstate'), flush=True)
    ck(d0.get('down') == '1' and d0.get('inv') == '1', f'the guest\'s ship is disabled and invincible ({d0})')

    guest.call(f'move {BADGE[0]} {BADGE[1]}')
    time.sleep(0.5)
    guest.call(f'click {BADGE[0]} {BADGE[1]}')
    ck(wait(lambda: play_space.captain_open(guest), 10), 'guest: the rank badge opened the captain screen')
    time.sleep(1)
    print('  Enter Outfitter:', uifind.find(guest, 'Enter Outfitter'), flush=True)
    ck(click_caption(guest, 'Enter Outfitter'), 'guest: "Enter Outfitter" is on the captain screen')
    ok = wait(lambda: 'Space' not in mode(guest) or 'Edit' in mode(guest), 30, 1)
    print('  guest mode:', mode(guest), flush=True)
    ck(ok, 'guest: the outfitter (an editor) opened')
    time.sleep(6)
    shot(guest, 'down_editor_outfitter.png')
    # leave the editor without changes: its exit (the red X at the bottom right, as the creature editors) or Esc
    for _ in range(6):
        if 'Space' in mode(guest) and 'Edit' not in mode(guest):
            break
        guest.call('click 1380 1062')
        time.sleep(3)
        if 'Space' in mode(guest) and 'Edit' not in mode(guest):
            break
        guest.call('key enter')
        time.sleep(3)
    ok = wait(lambda: 'Space' in mode(guest) and 'Edit' not in mode(guest), 60, 1)
    print('  guest mode:', mode(guest), flush=True)
    ck(ok, 'guest: back in Space from the outfitter')
    time.sleep(4)
    d1 = ds(guest)
    print('  guest back:', sp(guest, 'downstate'), flush=True)
    new_ship = d1.get('ship') != d0.get('ship') or d1.get('serial') != d0.get('serial')
    print(f'  the own ship before {d0.get("ship")}/{d0.get("serial")}, after {d1.get("ship")}/{d1.get("serial")}: '
          f'{"a new ship object" if new_ship else "the same ship"}', flush=True)
    ck(d1.get('down') == '1', f'the disabled state goes on after the editor ({d1.get("left")} s left)')
    ok = wait(lambda: ds(guest).get('down') == '0', 40, 1)
    ck(ok, 'the disabled state ended')
    time.sleep(2)
    for _ in range(5):  # a death caption, if the state ended with one (not expected)
        if 'movie=0' in sp(guest, 'hp'):
            break
        guest.call('key enter')
        time.sleep(2)
    d2 = ds(guest)
    out = sp(guest, 'hit 20')
    print('  guest after the state:', sp(guest, 'downstate'), '| hit 20:', out, flush=True)
    m = re.search(r'hp ([\d.]+) -> ([\d.]+) .* invincible=(\d)', out)
    ck(d2.get('inv') == '0' and m and m.group(3) == '0',
       f'the own ship is not invincible after the state ended (inv={d2.get("inv")}, new ship: {new_ship})')
    ck(m and float(m.group(2)) < float(m.group(1)) - 10, f'a hit takes its HP ({out})')
    for c in (host, guest):
        sp(c, 'repairdist 60')
    # only the game's own captain screen fault (BUG-0304) on the guest, none on the host (suite: reports=True)
    ck(not [ln for ln in host.call('exc')['out'].splitlines() if ln.startswith('0x')], 'host: no caught fault')
    ck(all('0x005971D4' in ln for ln in guest.call('exc')['out'].splitlines() if ln.startswith('0x')),
       'guest: no caught fault other than the captain screen one (0x5971D4)')
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
