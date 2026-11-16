#!/usr/bin/env python3
"""The Space page of the co-op window (space_ext.h): the feature registry is up on both games,
the page lists where each partner is (space_map.cpp), and its "Fly there" button (a real click) takes the player
to the partner's star system; "< Lobby" goes back. The guest's side: no cinematic left over from the
join (a guest stuck in the galaxy menu's intro had no HUD and its clicks were eaten, docs/re/space.md
7.9), its own jump is not refused, and its real clicks open its Space page.

  SPORE_LANE=2 python3 tools/tests/coop_space_page.py
"""
import re
import time

from harness import sp
from coop_ui import VK_F9, Ui
from twoinst import Checks, shot, start


def bob_row(con):
    """(index, text, button) of the guest's place row of the host's page (space_map.cpp), or None"""
    for m in re.finditer(r'^(\d+): (Bob: [^|]*?)(?: \| (.*))?$', sp(con, 'rows'), re.M):
        return int(m.group(1)), m.group(2), m.group(3) or ''
    return None


def star(con):
    m = re.search(r'star (\d+)', sp(con, 'star'))
    return int(m.group(1)) if m else 0


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(5)
    for who, c in (('host', host), ('guest', guest)):
        f = sp(c, 'features')
        print(f'  {who} features:', f.replace('\n', ' | '))
        ck('squad: on' in f, f'{who}: the space feature registry is up')
    rows = ''
    for _ in range(30):  # the guest's first state updates after its world loaded
        rows = sp(host, 'rows')
        b = bob_row(host)
        if b and not b[2]:
            break
        time.sleep(1)
    print('  host rows:', rows.replace('\n', ' | '), '| host', sp(host, 'star'), '| guest', sp(guest, 'star'))
    b = bob_row(host)
    ck(b is not None and not b[2], f'the host page shows the guest in its system, no button ({b})')

    for who, c in (('host', host), ('guest', guest)):
        hp = sp(c, 'hp')
        ck('movie=0' in hp, f'{who}: no cinematic plays after the join ({hp})')

    # the guest flies to another star: the host's page offers to jump there
    s0 = star(host)
    if not ck(s0 != 0, 'the host is in a star system'):
        return ck.result()
    for d in (1, 2, 3, -1, -2, 4096, -4096):  # a neighbouring id that is a star of this galaxy
        r = sp(guest, 'jumpto', s0 + d)  # not refused: no cinematic plays on the guest
        print(f'  guest jumpto {s0 + d}:', r)
        if r == 'jumping':
            break
    ck(r != 'a cinematic is playing', 'the guest\'s jump is not refused by a cinematic')
    away = 0
    for _ in range(30):
        time.sleep(1)
        away = star(guest)
        b = bob_row(host)
        if away and away != s0 and b and b[2] == 'Fly there':
            break
    rows = sp(host, 'rows')
    print('  host rows:', rows.replace('\n', ' | '))
    b = bob_row(host)
    ck(away and away != s0 and b and b[2] == 'Fly there',
       f'the guest is in another system ({away}) and the host can fly there ({b})')

    # real clicks: F9, "Space >", "Jump there"
    ui = Ui(host, 'host')
    if ui.state().get('open') != '1':
        host.call(f'key {VK_F9}')
    st = ui.wait(lambda s: s['open'] == '1' and s['space']['vis'], 20, 'lobby with the Space button')
    ck(st is not None, 'F9 in Space: the lobby shows the "Space >" button')
    ui.click('space')
    # the guest's row (other features have rows too: "Mode: Allies", ...)
    n = (bob_row(host) or (0,))[0]
    btn = f'sbtn{n}'
    st = ui.wait(lambda s: s['page'] == 'space' and s[btn]['vis'], 10, 'Space page')
    ck(st is not None and 'Fly' in st[btn]['text'], f'the Space page has the "Fly there" button (row {n})')
    shot(host, 'space_page.png')
    ui.click(btn)
    for _ in range(30):
        time.sleep(1)
        if star(host) == away:
            break
    ck(star(host) == away, f'the button jumps the host to the guest\'s star ({star(host)} == {away})')
    st = ui.wait(lambda s: (bob_row(host) or (0, '', 'x'))[2] == '', 10, 'together again')
    ck(st is not None, 'the page shows the guest in the host\'s system again')
    ui.click('sback')
    ck(ui.wait(lambda s: s['page'] == 'lobby', 10, 'lobby') is not None, '"< Lobby" goes back')
    host.call(f'key {VK_F9}')

    # the guest's real clicks: F9, "Space >" (its page lists the host, together again), "< Lobby"
    gui = Ui(guest, 'guest')
    if gui.state().get('open') != '1':
        guest.call(f'key {VK_F9}')
    st = gui.wait(lambda s: s['open'] == '1' and s['space']['vis'], 20, 'lobby with the Space button')
    ck(st is not None, 'guest: F9 in Space shows the "Space >" button')
    gui.click('space')
    st = gui.wait(lambda s: s['page'] == 'space' and s['sback']['vis'], 10, 'Space page')
    ck(st is not None, 'guest: a real click opens the Space page')
    rows = sp(guest, 'rows')
    print('  guest rows:', rows.replace('\n', ' | '))
    ck(re.search(r'^\d+: Alice: [^|]*$', rows, re.M) is not None, 'the guest page shows the host in its system (no button)')
    shot(guest, 'space_page_guest.png')
    gui.click('sback')
    ck(gui.wait(lambda s: s['page'] == 'lobby', 10, 'lobby') is not None, 'guest: "< Lobby" goes back')
    guest.call(f'key {VK_F9}')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
