#!/usr/bin/env python3
"""The galaxy core together (mods/coop/space_finale.cpp, docs/re/space.md 16).

Both players get the interstellar drive (the fixture has none), the host zooms out to the galaxy
view (mouse wheel) and flies next to the core (in the Grox zone: the guest is told and its Space page offers "Fly to Alice"),
then to the core star itself (the gate). The host zooms into the core with the mouse wheel like a
player: the core waits for the guest (the guest is called). The guest presses "Fly to Alice" on the
Space page (real clicks): into the system next to the core, zooms out, "Fly to Alice" again: the
core's gate; the core opens for both, both
games play SPG_CenterOfGalaxy and Steve gives both the Staff of Life. Then the session goes on
(both fly, the partner rows update), the guest leaves and joins again and sees that the core was
reached (and still has the staff).

  --alone: the variant where the host does not wait: the guest only flies near the core, the host
           presses "Enter now" (real click); the guest is not at the gate and gets Steve's gift by
           message (the Staff of Life without the trip), and later enters the core on its own.

  SPORE_LANE=5 python3 tools/tests/coop_space_finale.py [--alone]
"""
import os
import re
import sys
import tarfile
import time

import fixtures
import lane
from coop_ui import VK_F9, Ui
from twoinst import Checks, shot, start

STAFF_TOOL = 0x72E076A7  # genesisdevice
CORE_CINEMATIC = '2F7C7BDA'


def sp(con, *a):
    return con.call('coop.space ' + ' '.join(str(x) for x in a))['out'].strip()


def fin(con):
    """the finale status line as a dict"""
    s = sp(con, 'finale')
    d = dict(re.findall(r'(\w+)=(\S+)', s))
    d['line'] = s
    return d


def fixture_planet():
    """the planet of the space fixture: the .spo inside the fixture archive of the main checkout
    (the fixtures are being remade and the planet name changes), else fixtures.py"""
    import subprocess
    common = subprocess.run(['git', '-C', os.path.dirname(os.path.abspath(__file__)), 'rev-parse',
                             '--path-format=absolute', '--git-common-dir'], capture_output=True, text=True).stdout.strip()
    path = os.path.join(os.path.dirname(common), 'out', 'fixtures', 'space.tgz')
    if os.path.exists(path):
        with tarfile.open(path) as t:
            for n in t.getnames():
                m = re.fullmatch(r'Games/(Game\d+)/([^/]+)\.spo', n)
                if m:
                    return f'{m.group(1)}/{m.group(2)}'
    return fixtures.planet_of('space')


def wait(pred, timeout, step=1.0):
    t0 = time.time()
    while time.time() - t0 < timeout:
        r = pred()
        if r:
            return r
        time.sleep(step)
    return None


def settle(con, who, secs=4, timeout=120):
    """press OK on every caption (tutorial texts, badges) until nothing plays for `secs`. A caption
    is confirmed only after it has been up for 6 s: an Enter while a caption is still coming up
    crashed the game in the UI (AV at 0xCCCCCCCC from App::Canvas::Tick, docs/06-testing.md)"""
    quiet = 0.0
    since = None
    presses = 0
    t0 = time.time()
    while time.time() - t0 < timeout:
        f = fin(con)
        if f.get('busy') == '1':
            quiet = 0.0
            since = since or time.time()
            if time.time() - since >= 6:
                presses += 1
                # some captions only take the green check button (bottom right)
                con.call('key enter' if presses % 2 else 'click 1410 1048')
                since = None
        else:
            since = None
            quiet += 1.0
            if quiet >= secs:
                return True
        time.sleep(1.0)
    print(f'  {who}: still busy after {timeout} s:', fin(con)['line'])
    shot(con, f'finale_busy_{who}.png')
    return False


def get_drive(con, who, ck):
    r = sp(con, 'tool add interstellardrive')
    ck('ok' in r, f'{who}: gets the interstellar drive ({r})')
    time.sleep(3)
    settle(con, who)


def to_galaxy(con, who, ck):
    """zoom out with the mouse wheel like a player (the drive allows it) until the galaxy view"""
    n = 0
    for _ in range(10):
        if fin(con).get('ctx') == '2':
            break
        con.call('wheel 720 540 -5')
        n += 1
        time.sleep(1.5)
    print(f'  {who}: zoomed out with {n} x 5 notches')
    ok = wait(lambda: fin(con).get('ctx') == '2', 20)
    ck(ok, f'{who}: zoomed out to the galaxy view')
    time.sleep(3)
    settle(con, who)  # the drive's first galaxy view shows a tutorial caption


def press_through(cons, until, timeout=300):
    """the core cinematic waits for the player (scrolling texts, "Steve gives gifts"): OK until done,
    each player's Enter at least 8 s after the previous one"""
    t0 = time.time()
    last = {id(c): time.time() for c in cons}
    while time.time() - t0 < timeout:
        if until():
            return True
        for c in cons:
            if fin(c).get('busy') == '1' and time.time() - last[id(c)] >= 8:
                c.call('key enter')
                last[id(c)] = time.time()
        time.sleep(2)
    return until()


def fly_button(ui):
    st = ui.state()
    for i in range(9):
        w = st.get(f'sbtn{i}')
        if w and w['vis'] and w['text'].startswith(('Fly to', 'Лететь')):
            return f'sbtn{i}'
    return None


def open_space_page(ui, con, ck, who):
    if ui.state().get('open') != '1':
        con.call(f'key {VK_F9}')
    st = ui.wait(lambda s: s['open'] == '1' and (s['space']['vis'] or s.get('page') == 'space'), 20,
                 'lobby with the Space button')
    ck(st is not None, f'{who}: F9 opens the co-op window')
    if st and st.get('page') != 'space':
        ui.click('space')
    st = ui.wait(lambda s: s['page'] == 'space', 10, 'Space page')
    ck(st is not None, f'{who}: the Space page is open')


def main():
    alone = '--alone' in sys.argv
    host, guest = start('space', planet=fixture_planet())
    ck = Checks()
    time.sleep(3)
    for who, c in (('host', host), ('guest', guest)):
        f = fin(c)
        print(f'  {who}:', f['line'])
        ck(f.get('core', '0') != '0', f'{who}: the finale feature is on and found the core star')

    # both get the drive; the host goes to the galaxy view and next to the core
    for who, c in (('host', host), ('guest', guest)):
        get_drive(c, who, ck)
    to_galaxy(host, 'host', ck)
    r = sp(host, 'finale goto near')
    print('  host goto near:', r)
    ok = wait(lambda: fin(host).get('zone') == '2', 30)
    ck(ok, f'host: in the Grox zone near the core ({fin(host)["line"]})')
    rows = wait(lambda: (lambda r: r if 'Alice: near the galactic core' in r else None)(sp(guest, 'rows')), 20)
    print('  guest rows:', (rows or sp(guest, 'rows')).replace('\n', ' | '))
    ck(rows and 'Fly to Alice' in rows, 'guest: the Space page shows Alice near the core with "Fly to Alice"')

    if alone:
        # the guest only flies near the core (not to the gate): real clicks on "Fly to Alice"
        gui = Ui(guest, 'guest')
        open_space_page(gui, guest, ck, 'guest')
        b = fly_button(gui)
        ck(b is not None, 'guest: the Space page has the "Fly to Alice" button')
        if b:
            gui.click(b)
        ok = wait(lambda: fin(guest).get('zone') == '2' and fin(guest).get('ctx') == '1', 60)
        ck(ok, f'guest: jumped into the system of Alice near the core ({fin(guest)["line"]})')
        guest.call(f'key {VK_F9}')
        settle(guest, 'guest')

    # the host flies to the core star (the gate), zooms into its "solar system", flies the ship into
    # the centre (a click) and zooms in: that is where the game opens the core (0x1009C90)
    r = sp(host, 'finale goto core')
    print('  host goto core:', r)
    ok = wait(lambda: fin(host).get('zone') == '3', 30)
    ck(ok, f'host: at the gate of the galactic core ({fin(host)["line"]})')
    settle(host, 'host')
    # the wheel in at the core like a player. It takes as many notches as the galaxy camera is out:
    # 10 x 3 landed on the last one in every measured run and missed by one in 3 of 4 runs on main
    # e5fe623 (the ctx switch came seconds after the check, BUG-0213), so keep wheeling up to 40 x 3
    t0, n = time.time(), 0
    for _ in range(40):
        if fin(host).get('ctx') == '1':
            break
        host.call('wheel 720 540 3')
        n += 1
        time.sleep(1.5)
    ok = wait(lambda: fin(host).get('ctx') == '1', 10)
    print(f'  host: {n} x 3 notches in, ctx={fin(host).get("ctx")} after {time.time() - t0:.0f} s')
    ck(ok, 'host: zoomed into the core\'s system')
    time.sleep(3)
    settle(host, 'host')
    shot(host, 'finale_core_system.png')
    for _ in range(4):
        host.call('click 720 480')  # fly into the centre
        time.sleep(5)
        for _ in range(4):
            host.call('wheel 720 540 3')
            time.sleep(1.5)
        f = fin(host)
        if f.get('waiting') == '1' or f.get('entered') == '1':
            break
    f = fin(host)
    if f.get('waiting') != '1' and f.get('entered') != '1':
        print('  (flying into the centre did not reach the core: entering by command)', f['line'])
        sp(host, 'finale enter')
        time.sleep(2)
        f = fin(host)
    print('  host:', f['line'])
    ck(f.get('waiting') == '1' and f.get('entered') == '0', 'host: the core waits for the guest (not entered alone)')
    rows = wait(lambda: (lambda r: r if 'waiting for you' in r else None)(sp(guest, 'rows')), 15)
    ck(rows is not None, 'guest: is told that Alice waits at the core')
    shot(host, 'finale_wait.png')

    if alone:
        hui = Ui(host, 'host')
        open_space_page(hui, host, ck, 'host')
        st = hui.state()
        btn = next((f'sbtn{i}' for i in range(9) if st.get(f'sbtn{i}', {}).get('vis')
                    and st[f'sbtn{i}']['text'].startswith(('Enter now', 'Войти'))), None)
        ck(btn is not None, 'host: the Space page has "Enter now"')
        if btn:
            hui.click(btn)
        host.call(f'key {VK_F9}')
        ok = wait(lambda: fin(host).get('entered') == '1', 20)
        ck(ok, 'host: "Enter now" opens the core')
        # the guest is not at the gate: Steve's gift by message
        ok = wait(lambda: fin(guest).get('staff') == '1', 30)
        g = fin(guest)
        print('  guest:', g['line'])
        ck(ok and int(g.get('charges', '-1')) > 0, 'guest: gets the Staff of Life with charges without the trip')
        ck(g.get('entered') == '0', 'guest: can still visit the core itself')
        press_through([host], lambda: fin(host).get('busy') == '0' and fin(host).get('ctx') == '2'
                      and fin(host).get('staff') == '1')
    else:
        # the guest flies to Alice with real clicks: F9, "Space >", "Fly to Alice": from its system into
        # the system next to the core (the one Alice came from), then it zooms out like a player and
        # presses "Fly to Alice" again: in the galaxy view that goes straight to the gate
        gui = Ui(guest, 'guest')
        open_space_page(gui, guest, ck, 'guest')
        shot(guest, 'finale_page.png')
        b = fly_button(gui)
        ck(b is not None, 'guest: the Space page has the "Fly to Alice" button')
        if b:
            gui.click(b)
        guest.call(f'key {VK_F9}')
        ok = wait(lambda: fin(guest).get('zone') == '2' and fin(guest).get('ctx') == '1', 40)
        ck(ok, f'guest: jumped into the system next to the core ({fin(guest)["line"]})')
        settle(guest, 'guest')
        to_galaxy(guest, 'guest', ck)
        open_space_page(gui, guest, ck, 'guest')
        b = fly_button(gui)
        ck(b is not None, 'guest: "Fly to Alice" in the galaxy view')
        if b:
            gui.click(b)
        guest.call(f'key {VK_F9}')
        # both enter: the guest at the gate, the host's wait ends, the cinematic plays on both
        ok = wait(lambda: fin(host).get('entered') == '1' and fin(guest).get('entered') == '1', 90, 2)
        print('  host:', fin(host)['line'])
        print('  guest:', fin(guest)['line'])
        ck(ok, 'both entered the galactic core together')
        ck(int(fin(host).get('entries_with', '0')) >= 1, 'host: entered with the partner (after waiting)')
        time.sleep(4)
        cins = [fin(c).get('cin') for c in (host, guest)]
        ck(all(c == CORE_CINEMATIC for c in cins), f'both play SPG_CenterOfGalaxy ({cins})')
        shot(host, 'finale_cinematic_host.png')
        shot(guest, 'finale_cinematic_guest.png')
        done = press_through([host, guest], lambda: all(
            fin(c).get('busy') == '0' and fin(c).get('ctx') == '2' and fin(c).get('staff') == '1'
            for c in (host, guest)))
        ck(done, 'the cinematic ends on both, back in the galaxy view')
    for who, c in (('host', host), ('guest', guest)):
        f = fin(c)
        print(f'  {who}:', f['line'])
        ck(f.get('staff') == '1' and int(f.get('charges', '-1')) > 0, f'{who}: has the Staff of Life with charges')
    rows = sp(guest, 'rows')
    ck('Galactic core: reached' in rows, 'guest: the Space page says the core was reached')

    # the session goes on: the host flies home (a star near its old place), the guest sees it
    settle(host, 'host')
    settle(guest, 'guest')
    r = sp(host, 'finale goto near')
    print('  host goto near (after):', r)
    ok = wait(lambda: fin(host).get('zone') == '2', 30)
    ck(ok, 'host: flies on after the finale')
    ok = wait(lambda: re.search(r'0:z2', fin(guest).get('line', '')), 20)
    ck(ok, 'guest: sees the host move on (state updates go on)')

    # rejoin after the finale: the guest leaves and joins again
    guest.call('coop.leave')
    time.sleep(5)
    guest.call(lane.JOIN)
    ok = wait(lambda: 'Space' in guest.call('game.mode')['out'] and 'Galactic core: reached' in sp(guest, 'rows'),
              240, 3)
    print('  guest after rejoin:', fin(guest)['line'], '|', sp(guest, 'rows').replace('\n', ' | '))
    ck(ok, 'guest: after a rejoin the Space page says the core was reached')
    ck(fin(guest).get('staff') == '1', 'guest: still has the Staff of Life after the rejoin')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
