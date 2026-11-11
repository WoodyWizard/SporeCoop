#!/usr/bin/env python3
"""Golden images of the co-op window (docs/06-testing.md#testing-ui-golden): each step of the window is brought up by
real clicks in a fixed state (names Alice / Bob, the host pink and the guest green, English), the window is cropped
out of a full-size screenshot and compared with its reference tools/tests/golden/<name>.png (half size) with a
tolerance; the texts that vary between runs (addresses, ports, pings, the version, chat lines, the LAN list) are
masked. A control that moves or vanishes fails the step; the run folder gets the crop and a diff image.

Steps: name, choose (Host / Join), host_step, lobby_menu (the host's lobby before the start), settings_menu,
join_step (the guest), ingame_host / ingame_guest (F9 in Space), settings_host / settings_guest, space_host (the Space
page), pause_report (the pause menu with "Report a problem").

  coop_ui_golden.py [--update] [--only name,name]
--update writes the references from this run instead of comparing (made on the lab's 1440x1080 games; look at them
before a commit). Twin: UI_GOLDEN_TWIN="nudge <control> <dx> <dy>" or "hide <control>" (coop.uistate, a test
switch) on both games once the window is built: the steps that show that control must FAIL.
"""
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, HERE)
from sporectl import Console  # noqa: E402
import coop_ui  # noqa: E402
import fixtures  # noqa: E402
import frames  # noqa: E402
import golden  # noqa: E402
import lane  # noqa: E402
import twoinst  # noqa: E402
from coop_ui import Ui  # noqa: E402

FIXTURE = 'space'  # in game: the Space stage, so the lobby has its Space page too
PINK, GREEN = 'FFFF5FC8', 'FF5CE05C'
# per step: the uistate areas / named controls whose text varies between runs
VARY = {
    'name': ['version', 'statusline'],
    'choose': ['version', 'statusline', 'meline'],
    'host_step': ['version', 'statusline', 'port', 'hosthint'],
    'lobby_menu': ['version', 'session', 'hostaddr', 'world', 'creationline'] + [f'chatline{i}' for i in range(5)]
                  + [f'ping{i}' for i in range(4)],
    'settings_menu': ['version', 'setline'],
    'join_step': ['version', 'statusline', 'lanhdr', 'lan0', 'lan1', 'lan2', 'lanmore', 'address'],
    'ingame_host': ['version', 'session', 'hostaddr', 'world', 'creationline'] + [f'chatline{i}' for i in range(5)]
                   + [f'ping{i}' for i in range(4)],
    'settings_host': ['version', 'setline'],
    'space_host': ['version'],
    'pause_report': [],
}
VARY['ingame_guest'] = VARY['ingame_host']
FRAC = {'pause_report': 0.006}  # the game's pause menu is translucent: the scene behind it shows (0.19 % in the lab)
VARY['settings_guest'] = VARY['settings_host']


class Golden:
    def __init__(self, update, only):
        self.update, self.only = update, only
        self.results = []
        self.dir = os.path.join(frames.frames_dir(lane.out_dir()), 'golden')
        os.makedirs(self.dir, exist_ok=True)

    def raw(self, con):
        """the window rect and the areas of coop.uistate"""
        out = con.call('coop.uistate').get('out') or ''
        win, areas = None, {}
        for ln in out.splitlines():
            f = ln.split()
            if f[:1] == ['window'] and len(f) >= 5:
                win = [int(float(v)) for v in f[1:5]]
            elif f[:1] == ['area'] and len(f) >= 7 and f[6] == '1':
                areas[f[1]] = [int(float(v)) for v in f[2:6]]
        return win, areas

    def capture(self, ui, name, region=None):
        """the step's crop against its reference; region: a screen rect instead of the window"""
        if self.only and name not in self.only:
            return None
        ui.c.call('move 4 540')  # the pointer off the window: no hover highlight on a button
        ui.c.call('wait.frames 20 5000')
        time.sleep(0.5)  # the window's refresh (4 a second while open) has run
        win, areas = self.raw(ui.c)
        st = ui.state()
        rect = region or win
        if not rect:
            self.results.append((name, False, 'no window rect'))
            print(f'  FAIL {name}: no window rect in coop.uistate', flush=True)
            return False
        png = os.path.join(lane.out_dir('shots'), f'golden_{name}.png')
        if not ui.c.call('shot ' + png).get('ok'):
            self.results.append((name, False, 'no shot'))
            print(f'  FAIL {name}: no shot', flush=True)
            return False
        img = golden.crop(png, rect)
        masks = [(0, 0, img.width, 3), (0, img.height - 3, img.width, img.height), (0, 0, 3, img.height),
                 (img.width - 3, 0, img.width, img.height)]  # the frame's edge shows the game behind it
        for v in VARY.get(name, []):
            if v in areas:
                masks.append(golden.to_local(areas[v], rect))
            elif isinstance(st.get(v), dict) and st[v].get('vis'):
                masks.append(golden.to_local(st[v]['rect'], rect))
        img.save(os.path.join(self.dir, f'{name}.png'))
        ref = golden.ref_path(name)
        if self.update:
            golden.save_ref(img, ref, blank=masks[4:])  # (the varying texts: addresses, ports, pings)
            print(f'  updated {name}: {os.path.relpath(ref, ROOT)} ({img.width}x{img.height}, '
                  f'{os.path.getsize(ref)} bytes, {len(masks) - 4} masks)', flush=True)
            self.results.append((name, True, 'updated'))
            return True
        if not os.path.exists(ref):
            self.results.append((name, False, 'no reference'))
            print(f'  FAIL {name}: no reference {os.path.relpath(ref, ROOT)} (make it with --update)', flush=True)
            return False
        r = golden.compare(img, golden.load_ref(ref), masks, frac=FRAC.get(name, golden.FRAC))
        where = ''
        if r['bbox']:
            x1, y1, x2, y2 = r['bbox']
            s = golden.SCALE
            where = (f'; differing region screen ({rect[0] + x1 / s:.0f},{rect[1] + y1 / s:.0f})-'
                     f'({rect[0] + x2 / s:.0f},{rect[1] + y2 / s:.0f})')
        line = (f'{name}: {r["n"]} of {r["total"]} pixels differ ({r["frac"]:.3%}), densest block '
                f'{r["worst_block"]:.0%}{where}')
        if not r['ok']:
            diff = golden.diff_image(img, golden.load_ref(ref), masks, os.path.join(self.dir, f'{name}_diff.png'))
            line += f' -> {r["why"]}; diff {diff}'
        print(('  ok   ' if r['ok'] else '  FAIL ') + line, flush=True)
        self.results.append((name, r['ok'], line))
        return r['ok']


def pause_region(st):
    rects = [b['rect'] for b in st.get('pausebtns', [])]
    if st.get('pausereport'):
        rects.append(st['pausereport']['rect'])
    if not rects:
        return None
    return [min(r[0] for r in rects) - 16, min(r[1] for r in rects) - 16, max(r[2] for r in rects) + 16,
            max(r[3] for r in rects) + 16]


def main():
    update = '--update' in sys.argv
    only = None
    if '--only' in sys.argv:
        only = set(sys.argv[sys.argv.index('--only') + 1].split(','))
    if not golden.have_pil():
        print('  Pillow is missing (python3 -m pip install --user pillow): no golden images')
        print('RESULT: FAIL')
        return 1
    lane.sh('kill')
    time.sleep(2)
    lane.sh('deploy')
    lane.sh('fixture', 'restore', FIXTURE, lane.HOST_PROFILE)
    twoinst.restore_creations()
    subprocess.run(['rm', '-rf', lane.GUEST_DIR + '/Games', lane.GUEST_DIR + '/Games-sporecoop-backup'])
    twoinst.forget_settings()
    twoinst.forget_holds()
    old_ini = coop_ui.first_start_ini()
    try:
        lane.run_guest()
        assert coop_ui.wait_up(lane.GUEST), f'instance {lane.GUEST} did not start'
        time.sleep(5)
        lane.run_host()
        assert coop_ui.wait_up(lane.HOST), f'instance {lane.HOST} did not start'
        return run(Golden(update, only))
    finally:
        if old_ini is not None:
            with open(coop_ui.INI, 'w', encoding='utf-8') as f:
                f.write(old_ini)


def run(g):
    hc, gc = Console(lane.HOST), Console(lane.GUEST)
    host, guest = Ui(hc, 'host'), Ui(gc, 'guest')
    ck = twoinst.Checks()
    twin = os.environ.get('UI_GOLDEN_TWIN', '').strip()
    for con in (hc, gc):
        for _ in range(180):
            if 'GalaxyGameEntry' in con.call('game.mode')['out']:
                break
            time.sleep(1)
        con.call('wait.frames 700 60000')
        con.call('coop.uistate lang en')

    def intro(con):
        return 'menu intro)' in con.call('coop.status')['out']

    # the window by the menu's Co-op item (after the menu intro: the game's own click path, coop_ui.py)
    for ui in (host, guest):
        ck(ui.wait(lambda s: s['menu']['vis'], 30, 'menu item') is not None, f'{ui.who}: the Co-op item in the menu')
        t0 = time.time()
        while time.time() - t0 < 40 and intro(ui.c):
            time.sleep(0.3)
        time.sleep(4)
        ui.click('menu')
        ck(ui.wait(lambda s: s['open'] == '1' and s['name']['vis'], 25, 'co-op window') is not None,
           f'{ui.who}: the window opens on the name step')
    if twin:
        for ui in (host, guest):
            print(f'  TWIN ({ui.who}): ' + ui.c.call('coop.uistate ' + twin)['out'].strip(), flush=True)

    host.type_into('name', 'Alice')
    guest.type_into('name', 'Bob')
    g.capture(host, 'name')
    host.click('nameok')
    gc.call('key enter')
    ck(host.wait(lambda s: s.get('step') == 'choose' and s['gohost']['vis'], 10, 'Host / Join') is not None,
       'host: the Host / Join step')
    g.capture(host, 'choose')

    host.click('gohost')
    ck(host.wait(lambda s: s.get('step') == 'host' and s['host']['vis'], 10, 'the Host step') is not None,
       'host: the Host step')
    host.type_into('port', str(lane.PORT))
    g.capture(host, 'host_step')
    host.click('host')
    ck(host.wait(lambda s: s['page'] == 'lobby' and s['rows'], 15, 'host lobby') is not None, 'host: hosting, its lobby')
    hc.call('coop.find color pink')
    host.wait(lambda s: s.get('colors', {}).get(0) == PINK, 5, 'pink')
    g.capture(host, 'lobby_menu')
    host.click('settings')
    ck(host.wait(lambda s: s['page'] == 'settings' and s.get('sets'), 10, 'settings') is not None, 'host: the settings page')
    g.capture(host, 'settings_menu')
    host.click('setback')
    host.wait(lambda s: s['page'] == 'lobby', 5, 'the lobby')
    host.click('close')
    host.wait(lambda s: s['open'] == '0', 10, 'closed')

    guest.wait(lambda s: s.get('step') == 'choose' and s['gojoin']['vis'], 10, 'Host / Join')
    guest.click('gojoin')
    ck(guest.wait(lambda s: s.get('step') == 'join' and s['address']['vis'], 10, 'the Join step') is not None,
       'guest: the Join step')
    guest.type_into('address', f'127.0.0.1:{lane.PORT}')  # (by the address: the LAN list holds other lanes' games)
    g.capture(guest, 'join_step')
    guest.click('join')
    ck(guest.wait(lambda s: s['page'] == 'lobby' and len(s['rows']) >= 2, 20, 'guest lobby') is not None,
       'guest: joined, its lobby')
    gc.call('coop.find color green')

    slot, planet = fixtures.planet_of(FIXTURE).split('/')
    hc.call(f'coop.load {slot} {planet}')
    t0 = time.time()
    while time.time() - t0 < 240 and not hc.call('coop.avatar')['ok']:
        time.sleep(2)
    ck(hc.call('coop.avatar')['ok'], 'host: in its Space game')
    ck(twoinst.wait_guest_in(hc, gc, 240), 'guest: in the host\'s world')
    for c in (hc, gc):
        c.call('wait.frames 200 60000')
    time.sleep(3)
    for ui in (host, guest):
        if ui.state().get('open') != '1':
            ui.c.call('key f9')
        ck(ui.wait(lambda s: s['open'] == '1' and s['page'] == 'lobby' and len(s['rows']) == 2, 20, 'F9 lobby')
           is not None, f'{ui.who}: F9 lobby in game')
    for ui in (host, guest):  # (a reference made before the colours came would hold other swatches)
        ck(ui.wait(lambda s: s.get('colors', {}).get(0) == PINK and s.get('colors', {}).get(1) == GREEN, 15, 'colours')
           is not None, f'{ui.who}: the host pink, the guest green in the colour column')
    g.capture(host, 'ingame_host')
    g.capture(guest, 'ingame_guest')
    for ui, nm in ((host, 'settings_host'), (guest, 'settings_guest')):
        ui.click('settings')
        ui.wait(lambda s: s['page'] == 'settings' and s.get('sets'), 10, 'settings')
        g.capture(ui, nm)
        ui.click('setback')
        ui.wait(lambda s: s['page'] == 'lobby', 5, 'the lobby')
    if host.click('space'):
        ck(host.wait(lambda s: s['page'] == 'space', 10, 'the Space page') is not None, 'host: the Space page')
        g.capture(host, 'space_host')
        host.click('sback')
        host.wait(lambda s: s['page'] == 'lobby', 5, 'the lobby')
    else:
        ck(False, 'host: the lobby has "Space >" in Space')
    host.click('close')
    host.wait(lambda s: s['open'] == '0', 10, 'closed')
    hc.call('key esc')
    st = host.wait(lambda s: s.get('pausereport', {}).get('vis'), 10, 'the pause menu')
    ck(st is not None, 'host: the pause menu with "Report a problem"')
    if st:
        hc.call('wait.frames 30 5000')
        g.capture(host, 'pause_report', region=pause_region(st))
        import uifind
        if not uifind.press(hc, 'Resume Game'):
            hc.call('key esc')
    if twin:
        for c in (hc, gc):
            c.call('coop.uistate nudge off')
    bad = [n for n, ok, _ in g.results if not ok]
    print(f'  golden: {len(g.results)} steps, {len(bad)} differ' + (f' ({", ".join(bad)})' if bad else '')
          + f'; crops and diffs in {g.dir}', flush=True)
    ck(g.results and not bad, 'every step looks like its reference')
    ck.faults(host=hc, guest=gc)
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
