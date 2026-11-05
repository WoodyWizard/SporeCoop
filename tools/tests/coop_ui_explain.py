#!/usr/bin/env python3
"""The co-op window says why a button does nothing and how to get a choice (BUG-0597, BUG-0598).

Two instances, creature fixture, the guest without own creations (a player who never used an editor).
The window's own label texts and the button's enabled flag are checked, through `coop.uistate`:

 1. BUG-0598, the guest's lobby: the creation row says whose creature it is ("the host's species") and how to
    get a choice ("make one in an editor"), with no arrows. It used to say "My creature: the world's own" and
    nothing else. (The Russian texts once, for the layout: screenshot ui_explain_creation_ru.png.)
 2. BUG-0597, the host in its world with the guest in: "Send my world" is enabled.
 3. the guest leaves: the host is alone in its world: "Send my world" is greyed out and the lobby line says that
    nobody has joined yet; a click on it never says "Sending the world to everyone".
 4. the host goes to the main menu (no world loaded): greyed out, the lobby line says that a host can start or
    load a game now and friends join any time; a click never says "Sending".
Screenshots: out/shots/ui_explain_*.png (the layout of the new texts).

  SPORE_FIX_OFF=core.ui_share_explain,core.ui_creation_hint: the old texts and the always enabled button: FAILS
"""
import time

from coop_ui import Ui
from twoinst import Checks, shot, start

NOT_SENDING = 'Sending the world'


def opened(ui):
    ui.c.call('coop.uistate 1')
    return ui.wait(lambda s: s['open'] == '1' and s['page'] == 'lobby', 15, 'the lobby')


def main():
    host, guest = start('creature', guest_creations=False)
    ck = Checks()
    hui, gui = Ui(host, 'host'), Ui(guest, 'guest')

    # ---- 1. the guest's creation row (BUG-0598) ----
    st = opened(gui)
    st = gui.wait(lambda s: s.get('creation', '').startswith('My creature'), 20, 'the creation row')
    row = st.get('creation', '') if st else ''
    print(f'  guest: creation row {row!r}', flush=True)
    ck("the host's species" in row, f"guest: the row says whose creature it is ({row!r})")
    ck('make one in an editor' in row, f'guest: the row says how to get a choice ({row!r})')
    ck(st and not st['cprev']['vis'] and not st['cnext']['vis'], 'guest: no arrows without own creations')
    shot(guest, 'ui_explain_creation.png')
    print(' ', guest.call('coop.uistate lang ru')['out'].strip(), flush=True)
    time.sleep(1.5)
    shot(guest, 'ui_explain_creation_ru.png')
    print('  guest (ru):', gui.state().get('creation'), flush=True)
    guest.call('coop.uistate lang en')

    # ---- 2. the host in its world with the guest in ----
    st = opened(hui)
    st = hui.wait(lambda s: len(s['rows']) == 2 and s['share']['vis'], 15, 'two players and Send my world')
    ck(st and st.get('share_enabled') == '1', f'host: Send my world is enabled with a guest in '
                                               f'({st.get("share_enabled") if st else None})')

    # ---- 3. the guest leaves: the host alone in its world ----
    guest.call('coop.leave')
    st = hui.wait(lambda s: len(s['rows']) == 1, 30, 'the guest left')
    st = hui.wait(lambda s: s.get('world', '') != '', 10, 'a lobby line') or st
    world = st.get('world', '') if st else ''
    print(f'  host alone in its world: world line {world!r}, share_enabled={st.get("share_enabled") if st else None}',
          flush=True)
    ck(st and st.get('share_enabled') == '0', 'host alone in its world: Send my world is greyed out')
    ck('Nobody has joined yet' in world, f'host alone in its world: the lobby says nobody has joined ({world!r})')
    shot(host, 'ui_explain_alone_world.png')
    hui.click('share')
    time.sleep(1)
    st = hui.state()
    said = st.get('world', '') + ' | ' + st.get('status', '')
    ck(NOT_SENDING not in said, f'host alone in its world: a click does not say it sends ({said!r})')

    # ---- 4. the host in the main menu ----
    print(' ', host.call('coop.menu')['out'].strip(), flush=True)
    t0 = time.time()
    while time.time() - t0 < 90 and 'GalaxyGameEntry' not in host.call('game.mode')['out']:
        time.sleep(1)
    host.call('wait.frames 240 60000')
    st = opened(hui)
    st = hui.wait(lambda s: s.get('world', '') != '' and NOT_SENDING not in s.get('world', ''), 15, 'a lobby line') or st
    world = st.get('world', '') if st else ''
    print(f'  host alone in the menu: world line {world!r}, share_enabled={st.get("share_enabled") if st else None}',
          flush=True)
    ck(st and st.get('share_enabled') == '0', 'host in the menu: Send my world is greyed out')
    ck('Start or load a game now' in world and 'join any time' in world,
       f'host in the menu: the lobby says a game can start now, friends join later ({world!r})')
    shot(host, 'ui_explain_menu.png')
    hui.click('share')
    time.sleep(1)
    st = hui.state()
    said = st.get('world', '') + ' | ' + st.get('status', '')
    ck(NOT_SENDING not in said, f'host in the menu: a click does not say it sends ({said!r})')
    print(' ', host.call('coop.uistate lang ru')['out'].strip(), flush=True)
    time.sleep(1.5)
    shot(host, 'ui_explain_menu_ru.png')
    print('  host (ru):', hui.state().get('world'), flush=True)
    host.call('coop.uistate lang en')
    host.call('coop.uistate 0')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
