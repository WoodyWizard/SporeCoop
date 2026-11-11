#!/usr/bin/env python3
"""UI test: the whole co-op flow through the native Spore UI by real clicks, two instances (0.7.12 F1, the window in
steps, docs/08-ui.md#ui-window-steps).

host (instance 1, profile test, creature save) and guest (instance 2, empty profile), started as at a first start
(the lane's coop.ini without a chosen name; restored at the end):
 1. both: the "Co-op" item is in the main menu; clicking it opens the co-op window (the host clicks while the menu
    intro plays: the mod skips the intro, docs/re/space.md 7.9) on the name step
 2. both type their names (the host clicks Continue, the guest presses Enter): the Host / Join step
 3. host: Host a game, the port, Host: the lobby; the settings page before the start: a session setting changed
    by a click; closes the window and loads the saved game
 4. guest: Join a game, the host appears under "Games on your network", a click joins; it gets and loads the world
 5. in game, F9 opens the lobby on both: two players, colour column, stage, ping, the host's address line
 5b. colours (F2, BUG-0764): every row's swatch has the player's colour, the same on both games; the host holds pink
    (console); the guest's ">" next to its own colour (a real click) asks for pink: refused, the reason in the status
    line; a second click takes green: both games' colour column show the guest in green
 6. guest presses "I'm ready" -> the host sees it; chat line from the guest -> host
 7. settings in play: the session setting set in the menu reached the guest and is locked on both; the host's
    live change reaches the guest's page and its game; the guest's settings buttons change nothing
 8. host: "Ask the players for their reports" is there (only for the host); the pause menu's "Report a problem"
    writes a report
 9. guest presses "Leave" -> the host lobby has one player, the guest is on the Join step; a host name without a
    port says port 47810; the guest joins again by a typed host name (localhost:<port>)
Screenshots: out/shots/ui_*.png (out/laneN/shots on lane N)

usage: coop_ui.py [--no-restart]
"""
import os
import subprocess
import sys
import re
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from sporectl import Console, recompress_png  # noqa: E402
import lane  # noqa: E402
import twoinst  # noqa: E402

VK_END, VK_BACK, VK_RETURN, VK_F9 = 'end', 'back', 'enter', 'f9'  # devkit key names


def sh(*args):
    lane.sh(*args)


def wait_up(inst, t=120):
    t0 = time.time()
    while time.time() - t0 < t:
        try:
            if Console(inst).call('ping')['ok']:
                return True
        except OSError:
            pass
        time.sleep(1)
    return False


def shot(con, name):
    path = os.path.join(lane.out_dir('shots'), name)
    if con.call('shot ' + path)['ok']:
        recompress_png(path, 0.5)
    return path


class Ui:
    """coop.uistate parser + clicks on named widgets"""

    def __init__(self, con, who):
        self.c, self.who = con, who

    def state(self):
        st = {'rows': [], 'chat': []}
        for line in self.c.call('coop.uistate')['out'].splitlines():
            if line.startswith('open='):
                for kv in line.split():
                    k, v = kv.split('=')
                    st[k] = v
                continue
            parts = line.split(' ', 1)
            if len(parts) < 2:
                continue
            key, rest = parts
            if key == 'row':
                st['rows'].append([p for p in rest.split("'")[1::2]])
            elif key == 'setting':  # setting <row> <key> <value> <editable> '<tag>'
                f = rest.split(' ', 4)
                st.setdefault('sets', {})[f[1]] = {'row': int(f[0]), 'v': int(f[2]), 'en': f[3] == '1',
                                                       'tag': f[4].strip("'")}
            elif key in ('pausereport', 'pausebtn'):  # x1 y1 x2 y2 [vis] 'caption' [clicks=N]
                f = rest.split(' ')
                w = {'rect': [int(float(x)) for x in f[:4]], 'text': rest.split("'")[1]}
                if key == 'pausereport':
                    w['vis'] = f[4] == '1'
                    w['clicks'] = int(rest.rsplit('clicks=', 1)[1])
                    st[key] = w
                else:
                    st.setdefault('pausebtns', []).append(w)
            elif key == 'chat':
                st['chat'].append(rest.strip("'"))
            elif key == 'rowcolor':  # rowcolor <row> id=<id> argb=<ARGB>
                m = re.search(r'id=(\d+) argb=([0-9A-F]+)', rest)
                if m:
                    st.setdefault('colors', {})[int(m.group(1))] = m.group(2)
            elif key in ('session', 'lan', 'status', 'world', 'creation', 'hostaddr', 'setline', 'joinline'):
                st[key] = rest.strip("'")
            else:
                f = rest.split(' ', 5)
                if len(f) >= 5 and all(re.fullmatch(r'-?[\d.]+', x) for x in f[:4]):  # (status text can span lines)
                    st[key] = {'rect': [int(float(x)) for x in f[:4]], 'vis': f[4] == '1',
                               'text': f[5].strip("'") if len(f) > 5 else ''}
        return st

    def wait(self, pred, timeout=30, what=''):
        t0 = time.time()
        while time.time() - t0 < timeout:
            st = self.state()
            try:
                if pred(st):
                    return st
            except (KeyError, IndexError, TypeError):
                pass
            time.sleep(0.5)
        print(f'  {self.who}: timeout waiting for {what}')
        return None

    def click(self, name):
        st = self.state()
        w = st.get(name)
        if not w or not w['vis']:
            print(f'  {self.who}: {name} not visible')
            return False
        x1, y1, x2, y2 = w['rect']
        self.c.call(f'click {(x1 + x2) // 2} {(y1 + y2) // 2}')
        self.c.call('wait.frames 12 5000')
        return True

    def show_setting(self, key, timeout=10):
        """the settings page that lists `key` (13 rows a page, BUG-0768): back to the first page, then ">" until it
        shows; the state then, or None"""
        st = self.wait(lambda s: s['page'] == 'settings' and s.get('sets'), timeout, 'the settings page')
        for _ in range(6):
            if not st or not st['setprev']['vis'] or key in st.get('sets', {}):
                break
            self.click('setprev')
            time.sleep(0.5)
            st = self.state()
        for _ in range(6):
            if not st or key in st.get('sets', {}) or not st['setnext']['vis']:
                break
            self.click('setnext')
            time.sleep(0.5)
            st = self.state()
        if not st or key not in st.get('sets', {}):
            print(f'  {self.who}: {key} on no settings page')
            return None
        return st

    def goto(self, step, timeout=20):
        """walk the connect steps by clicks to `step` (choose | host | join): the name step's Continue (the name
        in the field stays), Back, Host a game / Join a game. False when the window is not on the connect page."""
        t0 = time.time()
        while time.time() - t0 < timeout:
            st = self.state()
            if st.get('page') != 'connect':
                return False
            cur = st.get('step')
            if cur == step:
                return True
            nxt = {'name': 'nameok', 'host': 'back', 'join': 'back'}.get(cur)
            if cur == 'choose':
                nxt = 'gohost' if step == 'host' else 'gojoin'
            if nxt:
                self.click(nxt)
            time.sleep(0.5)
        return False

    def type_into(self, name, text):
        if not self.click(name):
            return False
        self.c.call(f'key {VK_END}')
        for _ in range(30):
            self.c.call(f'key {VK_BACK}')
        self.c.call('wait.frames 10 5000')
        self.c.call('text "' + text + '"')
        self.c.call('wait.frames 10 5000')
        return True


INI = os.path.join(lane.game_dir(), 'SporebinEP1', 'mods', 'coop.ini')


def first_start_ini():
    """the lane's coop.ini as at a first start (the default name, no name chosen); returns the old text"""
    old = open(INI, encoding='utf-8').read() if os.path.exists(INI) else None
    if old is not None:
        lines = [ln for ln in old.splitlines() if not ln.startswith('name_chosen=')]
        lines = ['name=Player' if ln.startswith('name=') else ln for ln in lines]
        with open(INI, 'w', encoding='utf-8') as f:
            f.write('\n'.join(lines) + '\n')
    return old


def overlap(a, b):
    return a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]


def reports_since(t0):
    d = os.path.join(lane.game_dir(), 'SporebinEP1', 'mods', 'logs')
    return [f for f in os.listdir(d) if f.startswith('SporeCoop-report-') and f.endswith(f'-i{lane.HOST}.zip')
            and os.path.getmtime(os.path.join(d, f)) >= t0 - 2] if os.path.isdir(d) else []


def main():
    restart = '--no-restart' not in sys.argv
    old_ini = None
    if restart:
        sh('kill')
        time.sleep(2)
        sh('deploy')
        sh('fixture', 'restore', 'creature', lane.HOST_PROFILE)
        subprocess.run(['rm', '-rf', lane.GUEST_DIR + '/Games', lane.GUEST_DIR + '/Games-sporecoop-backup'])
        twoinst.forget_settings()
        old_ini = first_start_ini()
        lane.run_guest()
        assert wait_up(lane.GUEST), f'instance {lane.GUEST} did not start'
        time.sleep(5)
        lane.run_host()
        assert wait_up(lane.HOST), f'instance {lane.HOST} did not start'
    try:
        return run(restart)
    finally:
        if old_ini is not None:
            with open(INI, 'w', encoding='utf-8') as f:
                f.write(old_ini)


def run(restart):
    hc, gc = Console(lane.HOST), Console(lane.GUEST)
    host, guest = Ui(hc, 'host'), Ui(gc, 'guest')
    ok = True

    def check(cond, msg):
        nonlocal ok
        print(('  ok   ' if cond else '  FAIL ') + msg, flush=True)
        ok &= bool(cond)

    fixes = [n for n in (n.strip() for n in os.environ.get('SPORE_FIX_OFF', '').split(',')) if n]
    for con in (hc, gc):
        for name in fixes:  # the twin switches (core.ui_name_first, core.ui_pause_report: the 0.7.11 window)
            print('  SPORE_FIX_OFF:', con.call(f'coop.fix {name} off')['out'].strip(), flush=True)
        for _ in range(180):
            if 'GalaxyGameEntry' in con.call('game.mode')['out']:
                break
            time.sleep(1)
        con.call('wait.frames 700 60000')
        if not restart:
            con.call('coop.uistate first')

    def intro(con):
        return 'menu intro)' in con.call('coop.status')['out']

    def wait_intro(con, playing, t=40):
        t0 = time.time()
        while time.time() - t0 < t and intro(con) != playing:
            time.sleep(0.3)
        return intro(con) == playing

    # ---- 1. the menu item opens the window on the name step ----
    # The galaxy menu's intro cinematic (~10 s from ~16 s after the menu came up, docs/re/space.md
    # 7.9) holds the game's input lock: the game drops every click on the menu. The host clicks
    # "Co-op" while it plays (the mod skips the intro for the player and opens the window: the click
    # used to be lost, GOG 2024 lab runs), the guest after its intro (the game's own click path).
    for ui, during in ((host, True), (guest, False)):
        st = ui.wait(lambda s: s['menu']['vis'], 30, 'menu item')
        check(st is not None, f'{ui.who}: "Co-op" item in the main menu')
        if during:
            # (the intro may be over already: GOG 2016 reaches this point later, ~22 s after the menu)
            during = wait_intro(ui.c, True, 20)
            print(f'  {ui.who}: menu intro playing: {during}')
        if not during:
            wait_intro(ui.c, False)
            time.sleep(4)  # the game drops clicks for ~2-3 s after the intro (docs/re/space.md 7.9)
        ui.click('menu')
        st = ui.wait(lambda s: s['open'] == '1' and s['name']['vis'], 25, 'co-op window')
        check(st is not None, f'{ui.who}: the co-op window opens' + (' (clicked during the menu intro)' if during else ''))
        check(st and st.get('step') == 'name' and st['nameok']['vis'] and not st['host']['vis'] and not st['join']['vis'],
              f'{ui.who}: a first start begins with the name step (step={st and st.get("step")})')
        if during:
            check(not intro(ui.c), f'{ui.who}: the menu intro was skipped')
    shot(hc, 'ui_step_name.png')

    # ---- 2. the names: Continue (host), Enter (guest) ----
    host.type_into('name', 'Alice')
    host.click('nameok')
    guest.type_into('name', 'Bob')
    gc.call(f'key {VK_RETURN}')
    for ui, nm in ((host, 'Alice'), (guest, 'Bob')):
        st = ui.wait(lambda s: s.get('step') == 'choose' and s['gohost']['vis'] and s['gojoin']['vis'], 10, 'Host / Join')
        check(st is not None, f'{ui.who}: the name step goes on to Host / Join')
        name = ui.c.call('coop.name')['out']
        check(nm in name, f'{ui.who}: the typed name is the player\'s name ({name.strip()})')
    ini = open(INI, encoding='utf-8').read() if os.path.exists(INI) else ''
    check('name_chosen=1' in ini, 'the chosen name is kept (coop.ini [ui] name_chosen=1: no name step next time)')
    shot(hc, 'ui_step_choose.png')

    # ---- 3. host: Host a game, port, Host ----
    host.click('gohost')
    st = host.wait(lambda s: s.get('step') == 'host' and s['host']['vis'] and s['port']['vis'], 10, 'the Host step')
    check(st is not None and not st['join']['vis'], 'host: "Host a game" shows the Host step (port, Host)')
    # the port field shows the lane's saved host_port (mods/coop.ini of the lane's install, copied from
    # lane 1 when the lane was made: 47810 on lane 2 too): "bind() failed" while lane 1 hosts
    host.type_into('port', str(lane.PORT))
    shot(hc, 'ui_step_host.png')
    host.click('host')
    st = host.wait(lambda s: s['page'] == 'lobby' and s['rows'], 15, 'host lobby')
    check(st and 'Alice' in st['rows'][0][0], f'host: hosting, lobby shows {st["rows"] if st else None}')
    check(st and f':{lane.PORT}' in st.get('hostaddr', ''), f'host: the lobby gives the address ({st and st.get("hostaddr")!r})')
    check(st and st['askreports']['vis'], 'host: "Ask the players for their reports" in the lobby')
    shot(hc, 'ui_host_lobby.png')

    # the session settings before the start (the galaxy menu: no world played): a session setting by a click
    host.click('settings')
    st = host.wait(lambda s: s['page'] == 'settings' and s.get('sets'), 10, 'the settings page')
    sets = st.get('sets', {}) if st else {}
    check(len(sets) >= 7, f'host: the settings page lists the settings ({sorted(sets)})')
    check(st and st['setnext']['vis'], 'host: the settings have more than one page (">")')
    st = host.show_setting('creature.joint_hunt')  # (13 rows a page: the Creature group is on the second)
    sets = st.get('sets', {}) if st else {}
    jh = sets.get('creature.joint_hunt')
    check(jh and jh['en'] and jh['tag'] == 'session' and jh['v'] == 1,
          f'host: a session setting can be changed before the start ({jh})')
    if jh:
        host.click(f'set{jh["row"]}')
        st = host.wait(lambda s: s['sets']['creature.joint_hunt']['v'] == 0, 5, 'joint hunt off')
        check(st is not None, 'host: a click switched "Joint hunt bonus" off')
        check('joint_hunt=0' in hc.call('coop.settings get creature.joint_hunt')['out'],
              'host: the game plays with it off (coop.settings)')
    shot(hc, 'ui_settings_menu.png')
    host.click('setback')
    host.wait(lambda s: s['page'] == 'lobby', 5, 'back in the lobby')
    host.click('close')
    check(host.wait(lambda s: s['open'] == '0', 10, 'closed') is not None, 'host: the window closes')

    # ---- 4. guest: Join a game, the LAN list ----
    guest.click('gojoin')
    st = guest.wait(lambda s: s.get('step') == 'join' and s['join']['vis'] and s['address']['vis'], 10, 'the Join step')
    check(st is not None and not st['host']['vis'], 'guest: "Join a game" shows the Join step (list, address, Join)')
    # our host among the LAN rows (other lanes' hosts on this machine are listed too, pages of three)
    ours = lambda s: next((r for r in ('lan0', 'lan1', 'lan2') if s[r]['vis'] and 'Alice' in s[r]['text']  # noqa: E731
                           and f':{lane.PORT}' in s[r]['text']), None)
    st = guest.wait(lambda s: s['lan0']['vis'], 30, 'LAN games')
    row = None
    for _ in range(8):
        st = guest.state()
        row = ours(st)
        if row or not st['lanmore']['vis']:
            break
        guest.click('lanmore')
        time.sleep(1)
    shot(gc, 'ui_step_join.png')
    if row is not None:
        check(True, f'guest: host found on the network: {st[row]["text"]}')
        guest.click(row)
    else:  # never click another lane's game (that joins a test of another lane): join ours by its address
        listed = [st[r]['text'] for r in ('lan0', 'lan1', 'lan2') if st[r]['vis']]
        check(False, f'guest: host found on the network (listed: {listed})')
        guest.type_into('address', f'127.0.0.1:{lane.PORT}')
        guest.click('join')
    st = guest.wait(lambda s: s['page'] == 'lobby' and len(s['rows']) >= 2, 20, 'guest lobby')
    check(st is not None, f'guest: joined, lobby {st["rows"] if st else None}')

    # host loads its game; the guest gets the world and loads it
    subprocess.run([sys.executable, os.path.join(HERE, 'load_game.py'), '-i', str(lane.HOST)], capture_output=True,
                   text=True, timeout=600)
    check(hc.call('coop.avatar')['ok'], 'host: game loaded')
    t0 = time.time()
    while time.time() - t0 < 240 and not gc.call('coop.avatar')['ok']:
        time.sleep(2)
    check(gc.call('coop.avatar')['ok'], f'guest: in the host world after {time.time() - t0:.0f}s')
    gc.call('wait.frames 200 60000')

    # ---- 5. in game: F9 lobby ----
    for ui in (host, guest):
        if ui.state().get('open') != '1':
            ui.c.call(f'key {VK_F9}')
        st = ui.wait(lambda s: s['open'] == '1' and s['page'] == 'lobby' and len(s['rows']) == 2, 20, 'F9 lobby')
        check(st is not None, f'{ui.who}: F9 lobby in game: {st["rows"] if st else None}')
    st = guest.state()
    check(all(len(r) == 5 for r in st['rows']), 'guest: the rows have name, colour, stage, ping, ready')
    check(any('Creature' in r[2] for r in st['rows']), 'guest: stages shown')
    check(any(r[3].endswith('ms') for r in st['rows']), 'guest: ping shown')
    check(f':{lane.PORT}' in st.get('hostaddr', ''), f'guest: the host\'s address line ({st.get("hostaddr")!r})')
    check(not st['askreports']['vis'], 'guest: no "Ask the players for their reports" (host only)')

    # ---- 5b. colours (F2): the column, a refused pick and a taken one by real clicks ----
    PINK, GREEN = 'FFFF5FC8', 'FF5CE05C'
    hs = host.wait(lambda s: len(s.get('colors', {})) == 2 and all(v != '00000000' for v in s['colors'].values()), 10,
                   'host colours')
    gs = guest.state()
    check(hs and hs.get('colors') == gs.get('colors') and len(set(hs['colors'].values())) == 2,
          f"the colour column: distinct, the same on both games ({hs and hs.get('colors')} / {gs.get('colors')})")
    print('  host:', hc.call('coop.find color pink')['out'].strip(), flush=True)
    st = guest.wait(lambda s: s.get('colors', {}).get(0) == PINK, 10, 'the host in pink on the guest')
    check(st is not None, "the guest's colour column shows the host's new colour (pink)")
    guest.click('colour')
    st = guest.wait(lambda s: 'another player' in s.get('status', ''), 5, 'the refusal')
    check(st is not None and st.get('colors', {}).get(1) != PINK,
          f"the guest's click on pink (the host's) is refused, the reason in the status line ({st and st.get('status')!r})")
    guest.click('colour')
    gs = guest.wait(lambda s: s.get('colors', {}).get(1) == GREEN, 10, 'the guest in green')
    hs = host.wait(lambda s: s.get('colors', {}).get(1) == GREEN, 10, 'the guest in green on the host')
    check(gs is not None and hs is not None,
          f"the guest's second click: green, in both games' colour column ({gs and gs.get('status')!r})")
    shot(gc, 'ui_guest_colours.png')
    shot(hc, 'ui_host_colours.png')

    # ---- 6. ready, chat ----
    guest.click('ready')
    st = host.wait(lambda s: any('Bob' in r[0] and r[4] == 'ready' for r in s['rows']), 10, 'ready')
    check(st is not None, 'host: sees the guest ready')
    guest.type_into('chatedit', 'hello from Bob')
    gc.call(f'key {VK_RETURN}')
    st = host.wait(lambda s: any('hello from Bob' in c for c in s['chat']), 10, 'chat')
    check(st is not None, 'host: chat line from the guest')
    shot(hc, 'ui_host_ingame.png')
    shot(gc, 'ui_guest_ingame.png')

    # ---- 7. settings in play ----
    for ui in (host, guest):
        ui.click('settings')
    hs = host.show_setting('creature.joint_hunt')
    gs = guest.show_setting('creature.joint_hunt')
    hj = hs and hs['sets'].get('creature.joint_hunt')
    gj = gs and gs['sets'].get('creature.joint_hunt')
    check(gj and gj['v'] == 0, f'guest: the session setting the host set in the menu came with the session ({gj})')
    check(hj and hj['tag'] == 'locked' and not hj['en'] and gj and gj['tag'] == 'locked',
          f'a session setting is locked in play (host {hj}, guest {gj})')
    if hj:
        host.click(f'set{hj["row"]}')  # a click on the locked button changes nothing
        time.sleep(1.5)
        st = host.state()
        check(st['sets']['creature.joint_hunt']['v'] == 0 and 'joint_hunt=0' in
              hc.call('coop.settings get creature.joint_hunt')['out'], 'host: the locked setting stays as it was')
    hs = host.show_setting('cell.regen')  # the first page: Cell, All stages
    guest.show_setting('cell.regen')
    hr = hs and hs['sets'].get('cell.regen')
    if hr:
        host.click(f'set{hr["row"]}')  # a live setting: off in play
        st = host.wait(lambda s: s['sets']['cell.regen']['v'] == 0, 5, 'regen off')
        check(st is not None, 'host: a live setting changes in play (cell.regen off)')
        st = guest.wait(lambda s: s['sets']['cell.regen']['v'] == 0, 10, 'the guest sees it')
        check(st is not None, 'guest: the host\'s change shows on the guest\'s settings page')
        check('regen=0' in gc.call('coop.settings get cell.regen')['out'], 'guest: its game plays with the host\'s value')
    shot(hc, 'ui_settings_host_ingame.png')
    shot(gc, 'ui_settings_guest.png')
    gs = guest.state()
    ga = gs.get('sets', {}).get('all.partner_arrows')
    check(ga and not ga['en'] and not any(v['en'] for v in gs['sets'].values()),
          'guest: every settings button is greyed out (the host decides)')
    if ga:
        guest.click(f'set{ga["row"]}')
        time.sleep(1.5)
        check('arrows=1' in gc.call('coop.settings get all.partner_arrows')['out'] and
              'arrows=1' in hc.call('coop.settings get all.partner_arrows')['out'],
              'guest: its click changes nothing (guest and host keep partner arrows on)')
    if hr:
        host.click(f'set{hr["row"]}')  # back on
        host.wait(lambda s: s['sets']['cell.regen']['v'] == 1, 5, 'regen on again')
    for ui in (host, guest):
        ui.click('setback')
        ui.wait(lambda s: s['page'] == 'lobby', 5, 'the lobby')

    # ---- 8. reports: the host's "Ask the players", "Report a problem" in the pause menu ----
    host.click('askreports')
    st = host.wait(lambda s: s.get('status'), 5, 'the ask line')
    check(st is not None, f'host: "Ask the players for their reports" answers ({st and st.get("status")!r})')
    host.click('close')
    host.wait(lambda s: s['open'] == '0', 10, 'closed')
    t_rep = time.time()
    hc.call('key esc')
    st = host.wait(lambda s: s.get('pausereport', {}).get('vis'), 10, 'the pause menu')
    check(st is not None, 'host: "Report a problem" in the game\'s pause menu')
    if st:
        r = st['pausereport']['rect']
        others = [b for b in st.get('pausebtns', [])]
        hit = [b['text'] for b in others if overlap(r, b['rect'])]
        below = all(b['rect'][3] <= r[1] for b in others)
        check(not hit and below and len(others) >= 4, f'the item is the menu\'s last button, over no other ({hit}, '
              f'{len(others)} buttons)')
        hc.call('wait.frames 30 5000')  # (the menu is drawn a few frames after its flag)
        shot(hc, 'ui_pause_report.png')
        hc.call(f'move {(r[0] + r[2]) // 2} {(r[1] + r[3]) // 2}')
        time.sleep(0.3)
        hc.call(f'click {(r[0] + r[2]) // 2} {(r[1] + r[3]) // 2}')
        st = host.wait(lambda s: s['pausereport']['clicks'] >= 1, 5, 'the click')
        t1 = time.time()
        while time.time() - t1 < 20 and not reports_since(t_rep):
            time.sleep(1)
        made = reports_since(t_rep)
        check(made, f'host: the click wrote a report ({made})')
        logs = os.path.join(lane.game_dir(), 'SporebinEP1', 'mods', 'logs')
        for z in made:  # made on purpose: not a report for the lab to find (its .txt next to it too)
            stamp = z[len('SporeCoop-report-'):-len('.zip')]
            for f in (z, f'report-{stamp}.txt', f'report-{stamp}.dmp'):
                if os.path.exists(os.path.join(logs, f)):
                    os.remove(os.path.join(logs, f))
    import uifind
    if not uifind.press(hc, 'Resume Game'):
        hc.call('key esc')
    hc.call(f'key {VK_F9}')  # the host's lobby again (it sees the guest leave)
    host.wait(lambda s: s['open'] == '1' and s['page'] == 'lobby', 10, 'the host lobby')

    # ---- 9. leave, the Join step again, join by a typed host name ----
    guest.click('leave')
    st = host.wait(lambda s: len(s['rows']) == 1, 20, 'guest left')
    check(st is not None or 'players=1' in hc.call('coop.status')['out'], 'host: the guest left the session')
    st = guest.wait(lambda s: s['page'] == 'connect' and s.get('step') == 'join' and s['address']['vis'], 10,
                    'guest Join step')
    check(st is not None, 'guest: back on the Join step')
    guest.type_into('address', 'nosuchhost.invalid')
    guest.click('join')
    st = guest.wait(lambda s: 'nosuchhost.invalid:47810' in s.get('joinline', ''), 5, 'the default port')
    check(st is not None, f'guest: a host name without a port goes to port 47810 ({st and st.get("joinline")!r})')
    st = guest.wait(lambda s: s['page'] == 'connect' and s['address']['vis'], 30, 'the unknown name fails')
    check(st is not None, f'guest: an unknown host name ends the join ({st and st.get("status")!r})')
    guest.type_into('address', f'localhost:{lane.PORT}')
    shot(gc, 'ui_join_by_name.png')
    guest.click('join')
    st = guest.wait(lambda s: s['page'] == 'lobby' and len(s['rows']) >= 2, 30, 'joined by name')
    check(st is not None, f'guest: joined by a typed host name (localhost:{lane.PORT}): {st and st["rows"]}')
    st = host.wait(lambda s: len(s['rows']) == 2, 20, 'the guest is back')
    check(st is not None, 'host: the guest is back in the lobby')
    t0 = time.time()
    while time.time() - t0 < 240 and not (gc.call('coop.avatar')['ok'] and 'players=2' in hc.call('coop.status')['out']):
        time.sleep(2)

    for name, con in (('host', hc), ('guest', gc)):
        f = con.call('exc')['out'].splitlines()[0]
        print(f'{name} faults: {f}')
        ok &= 'total faults: 0' in f
    print('RESULT:', 'OK' if ok else 'FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
