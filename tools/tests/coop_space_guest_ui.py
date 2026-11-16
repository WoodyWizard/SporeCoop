#!/usr/bin/env python3
"""A guest who joins while its galaxy menu plays the intro cinematic (GGE_Intro) gets a normal game:
no movie left over in Space (the HUD is there), its real clicks reach the co-op window, and its Space
page works. Before the fix the intro stayed "playing" after the world load: no HUD, every click
eaten by the cinematic's input lock, jumps refused ("a cinematic is playing"); docs/re/space.md 7.9.

  SPORE_LANE=10 python3 tools/tests/coop_space_guest_ui.py
"""
import re
import time

from harness import sp
from coop_ui import VK_F9, Ui
from twoinst import Checks, shot, start


def movie(con):
    m = re.search(r'movie=(\d)', sp(con, 'hp'))
    return int(m.group(1)) if m else -1


def main():
    host, guest = start('space', join_in_intro=True)
    ck = Checks()
    st = guest.call('coop.status')['out']
    ended = re.search(r'menu intro ended (\d+)', st)
    print('  guest:', ' | '.join(l for l in st.splitlines() if 'cinematic' in l or 'intro' in l) or '(no cinematic)')
    # the guest started after the host was in its world and joined at the start of its menu intro:
    # the world arrived while the intro played (the race of the bug), and the mod ended it
    ck(ended and int(ended.group(1)) >= 1, 'the world arrived during the menu intro and the mod ended the intro')
    t0 = time.time()
    while time.time() - t0 < 15 and (movie(guest) or movie(host)):
        time.sleep(1)
    print('  host', sp(host, 'hp'), '| guest', sp(guest, 'hp'))
    ck(movie(guest) == 0, 'the guest has no movie left over in Space')
    ck('(menu intro)' not in guest.call('coop.status')['out'], 'the menu intro does not play on the guest')
    shot(guest, 'guest_ui_hud.png')

    # real clicks of the guest: F9, "Space >", the page, "< Lobby"
    ui = Ui(guest, 'guest')
    if ui.state().get('open') != '1':
        guest.call(f'key {VK_F9}')
    s = ui.wait(lambda s: s['open'] == '1' and s['space']['vis'], 20, 'lobby with the Space button')
    ck(s is not None, 'F9 on the guest: the lobby shows the "Space >" button')
    ui.click('space')
    s = ui.wait(lambda s: s['page'] == 'space' and s['sback']['vis'], 10, 'Space page')
    ck(s is not None, 'a real click of the guest opens the Space page')
    rows = sp(guest, 'rows')
    print('  guest rows:', rows.replace('\n', ' | '))
    ck('Alice:' in rows, 'the guest page lists the host')
    shot(guest, 'guest_ui_page.png')
    ui.click('sback')
    ck(ui.wait(lambda s: s['page'] == 'lobby', 10, 'lobby') is not None, 'the guest\'s "< Lobby" goes back')
    guest.call(f'key {VK_F9}')
    ck(ui.wait(lambda s: s['open'] == '0', 10, 'closed') is not None, 'F9 closes the guest window')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
