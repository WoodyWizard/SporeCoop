#!/usr/bin/env python3
"""Screenshots and the animation for the README (docs/media/): every stage with two players, a co-op
mechanic triggered so the picture shows it (the partner's own creature, knockout, pincer toast, city
alarm, a disabled ship with its repair beacon), the co-op window in the galaxy menu (the lobby with the
player colours, the Settings page) and in game (F9), the arrow to a partner off screen and the partner's mark on
the minimap, and an animated GIF of two players walking together in the Creature stage.
Full-size captures (1440x1080), saved as 1280x960 JPEG under MAX_KB.

  SPORE_LANE=3 python3 tools/tests/readme_shots.py [stage ...]
  stages: cell creature tribe civ space gif find menu (default: all)

The GIF needs ffmpeg. The players are the lane's test names Alice (host) and Bob (guest)."""
import os
import re
import shutil
import subprocess
import sys
import threading
import time

import lane
from coop_ui import Ui
from twoinst import ROOT, Console, forget_settings, start, wait_up

OUT = os.path.join(ROOT, 'docs', 'media')
SIZE = (1280, 960)
MAX_KB = 195
GUEST_CREATURE = 'Chitis'  # one of the guest's own test creations (My Creations), drawn unlike the host's species


DEMO_ADDR = '100.64.12.7:47810'  # the README's example address: no PC's own address or test lane port in a picture


def demo(*cons):
    """the window and the overlay show DEMO_ADDR and port 47810 instead of this PC's addresses and the lane's port"""
    for c in cons:
        c.call(f'coop.uistate demo {DEMO_ADDR} {lane.PORT} 47810')


def grab(con, name, keep=True):
    raw = os.path.join(lane.out_dir('shots'), name + '.png')
    con.call('move 1380 1000')  # the pointer off the window's buttons (no hover highlight)
    time.sleep(0.3)
    con.call('shot ' + raw)
    time.sleep(1.0)
    if keep:
        save(name)


def save(name, crop=None, scale=None, src=None):
    """the capture as a JPEG in docs/media: `crop` (x1, y1, x2, y2 of the 1440x1080 capture) and `scale` for a
    detail picture (the minimap), else fitted to SIZE"""
    from PIL import Image
    raw = os.path.join(lane.out_dir('shots'), (src or name) + '.png')
    im = Image.open(raw).convert('RGB')
    if crop:
        im = im.crop(crop)
    if scale:
        im = im.resize((round(im.width * scale), round(im.height * scale)), Image.LANCZOS)
    else:
        im.thumbnail(SIZE, Image.LANCZOS)
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, name + '.jpg')
    for q in (84, 80, 76, 72, 68, 64, 60):  # the best quality under MAX_KB
        im.save(path, quality=q, optimize=True, progressive=True)
        if os.path.getsize(path) <= MAX_KB * 1024:
            break
    print(f'   {name}.jpg {os.path.getsize(path) // 1024} KB (q{q})', flush=True)


def vec(text, key='pos'):
    m = re.search(key + r'=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', text)
    return [float(x) for x in m.groups()] if m else None


def camera_right(con):
    """the first row of the camera matrix (`coop.cam` m[0]): a sideways direction on the screen"""
    m = re.search(r'm\[0\] =\s+(\S+)\s+(\S+)\s+(\S+)', con.call('coop.cam')['out'])
    return [float(x) for x in m.groups()] if m else None


def guest_own_creature(g):
    """the guest plays one of its own creatures (the F9 window's `My creature`)"""
    pick = None
    for line in g.call('coop.creations list creature')['out'].splitlines():
        f = line.split(None, 2)
        if len(f) == 3 and f[0] == 'creature':
            pick = pick or f[1]
            if f[2].strip() == GUEST_CREATURE:
                pick = f[1]
    if pick:
        g.call(f'coop.creations choose creature {pick}')
        time.sleep(8)  # the new avatar spawns and the notifications fade


def beside(h, g, metres):
    """the guest's avatar `metres` to the side of the host's on the host's screen"""
    r, a = camera_right(h), vec(h.call('coop.avatar')['out'])
    if r and a:
        g.call('coop.npc gopos %.2f %.2f %.2f' % tuple(a[i] + metres * r[i] for i in range(3)))
        time.sleep(3)


def stage_cell(h, g):
    # a freshly loaded tide pool opens with the meteor intro (~10 s) that covers the cells
    time.sleep(15)
    for c in (h, g):
        c.call('wheel 720 540 4')  # closer: the cells are small from the default height
    # a few metres apart: next to each other the partner revives at once (and the revive plays the
    # meteor-hatch effect over both cells)
    g.call('coop.pool tp 4 2')
    time.sleep(2)
    h.call('coop.pool hurt 1 999')  # the guest is knocked out: translucent, countdown, arrow
    time.sleep(2.5)
    grab(h, 'cell_host')


def stage_creature(h, g):
    guest_own_creature(g)
    beside(h, g, 3.0)
    grab(h, 'creature_host')


MINIMAP = (0, 895, 235, 1040)  # the Creature stage's minimap in a 1440x1080 capture


def stage_find(h, g):
    """the host's co-op window in game (F9) with a chat line of each player in its colour, then the guest walks
    off the host's screen: the arrow at the screen edge and the partner's mark on the minimap"""
    g.call('cheat coop say Hi Alice, I am right behind you')
    time.sleep(1)
    h.call('cheat coop say Follow my arrow, Bob')
    time.sleep(12)  # the chat notifications fade
    h.call('coop.uistate 1')
    time.sleep(2)
    grab(h, 'ui_ingame')
    h.call('coop.uistate 0')
    time.sleep(1)
    r, a = camera_right(h), vec(h.call('coop.avatar')['out'])
    if r and a:  # ~40 m to the side: off screen, inside the minimap's range
        g.call('coop.npc gopos %.2f %.2f %.2f' % tuple(a[i] + 38 * r[i] for i in range(3)))
    time.sleep(4)
    print('   ' + (h.call('coop.find')['out'].strip().splitlines() or [''])[-1], flush=True)
    grab(h, 'partner_arrow')
    save('partner_minimap', crop=MINIMAP, scale=2, src='partner_arrow')


def stage_gif(h, g):
    """both players hold W at the same moment (their cameras face the same way after the load), a
    short turn halfway: they walk side by side; ~10 frames a second of the host's screen"""
    guest_own_creature(g)
    beside(h, g, 2.3)
    frames = os.path.join(lane.out_dir('shots'), 'gif')
    shutil.rmtree(frames, ignore_errors=True)
    os.makedirs(frames)
    plan = [(0, 0x57, 'down'), (5, 0x41, 'down'), (6, 0x41, 'up'), (10, 0x57, 'up')]

    def keys(t0):
        for t, vk, ud in plan:
            time.sleep(max(0.0, t0 + t - time.time()))
            for c in (h, g):
                c.call(f'key 0x{vk:02X} {ud}')

    t0 = time.time() + 0.5
    th = threading.Thread(target=keys, args=(t0,))
    th.start()
    time.sleep(0.5)
    n = 0
    while time.time() - t0 < 10:
        t = time.time()
        h.call(f'shot {frames}/f{n:04d}.png')
        n += 1
        time.sleep(max(0.0, 0.1 - (time.time() - t)))
    th.join()
    # 8.5 s at 7 fps, the status text and the bottom bar cropped, 112 colours: about 7 MB
    out = os.path.join(OUT, 'coop_creature.gif')
    vf = ('crop=1440:736:0:150,fps=7,scale=640:-1:flags=lanczos,split[a][b];'
          '[a]palettegen=max_colors=112:stats_mode=diff[p];[b][p]paletteuse=dither=none:diff_mode=rectangle')
    subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-framerate', '10', '-start_number', '5',
                    '-i', os.path.join(frames, 'f%04d.png'), '-frames:v', '85', '-vf', vf, out], check=True)
    print(f'   coop_creature.gif {n} frames, {os.path.getsize(out) // 1024} KB', flush=True)


def tribe_ids(con):
    out = {}
    for line in con.call('coop.tribe')['out'].splitlines():
        m = re.match(r'\s+#(\d+) \S+ owner=(-?\d+)', line)
        if m:
            out[int(m.group(1))] = int(m.group(2))
    return out


def stage_tribe(h, g):
    ids = tribe_ids(h)
    mine = [i for i, o in ids.items() if o == 0]
    theirs = [i for i, o in ids.items() if o == 1]
    enemy = [i for i, o in ids.items() if o >= 254]
    if enemy and mine and theirs:
        h.call(f'coop.tribe pincer {enemy[0]} {mine[-1]} 1')
        h.call(f'coop.tribe pincer {enemy[0]} {theirs[-1]} 1')
    time.sleep(2.5)
    grab(h, 'tribe_host')


def stage_civ(h, g):
    m = re.search(r'city (\d+)', h.call('coop.civ')['out'])
    if m:
        h.call(f'coop.civ alarm {m.group(1)}')
    time.sleep(2.5)
    grab(h, 'civ_host')


def stage_space(h, g):
    g.call('coop.space hit 100000')  # disabled: countdown on the guest, REPAIR beacon on the host
    time.sleep(0.7)  # ships start 12 m apart: the repair begins at once and takes 2 s
    grab(h, 'space_host')


def stage_menu():
    """the co-op window in the galaxy menu: Alice hosts, Bob joined from his menu (no world loaded); the lobby with
    the colours, and the Settings page with one changed setting"""
    lane.sh('kill')
    time.sleep(2)
    lane.sh('deploy')
    forget_settings()
    lane.run_guest()
    time.sleep(2)
    lane.run_host()
    for inst in (lane.HOST, lane.GUEST):
        wait_up(inst, 120)
    h, g = Console(lane.HOST), Console(lane.GUEST)
    for c in (h, g):
        c.call('wait.frames 600 120000')  # the galaxy menu and its intro settle
    h.call(lane.HOST_CMD)
    time.sleep(2)
    g.call(lane.JOIN)
    time.sleep(6)
    demo(h, g)
    h.call('coop.uistate 1')
    time.sleep(3)
    grab(h, 'ui_lobby')
    ui = Ui(h, 'host')
    if ui.click('settings'):
        st = ui.show_setting('all.friendly_fire')
        if st and not st['sets']['all.friendly_fire']['v']:
            ui.click('set%d' % st['sets']['all.friendly_fire']['row'])
        ui.show_setting('all.partner_arrows')  # the first page: All stages, Cell
        time.sleep(2)
        grab(h, 'ui_settings')


STAGES = {'creature': stage_creature, 'gif': stage_gif, 'find': stage_find, 'cell': stage_cell,
          'tribe': stage_tribe, 'civ': stage_civ, 'space': stage_space}
FIXTURE = {'gif': 'creature', 'find': 'creature'}


def main():
    for st in sys.argv[1:] or [*STAGES, 'menu']:
        print(st, flush=True)
        if st == 'menu':
            stage_menu()
            continue
        h, g = start(FIXTURE.get(st, st))
        demo(h, g)
        time.sleep(4)
        STAGES[st](h, g)
    lane.sh('kill')


if __name__ == '__main__':
    main()
