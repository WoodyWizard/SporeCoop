#!/usr/bin/env python3
"""The evolution editors as the players' video showed them (BUG-1239, BUG-1240): what the OTHER player sees while one
is in an editor, and which creature each player plays afterwards. Real clicks (the mating-call button, the swim to the
mate, the vote's I'm ready, History's check, the editors' parts and done), and the screens are measured
(screen_look.py: a black picture is the symptom), not only game.mode.

  --part cell     Cell fixture, a guest without creations:
                    1. the host's mating call, the vote (the guest's I'm ready by a click, or --ready none: the vote
                       runs out), the host's editor: the guest's screen shows its Cell world all along (no black)
                    2. the guest's own mating call: its own editor, the host plays on: the host's screen shows its
                       world all along
  --part switch   Cell fixture, a guest without creations: History's check, the switch vote (I'm ready), both reach
                  Creature; the host adds legs in the first Creature editor, the guest gets its OWN first Creature
                  editor, adds legs and a second leg pair and saves it as new: afterwards each plays its own creature
                  (the avatar keys differ) and each game draws the partner with the partner's creature. While either
                  is in its editor the other's screen is never black for long (an editor or the world is drawn).

  --ready click|none   the guest's I'm ready in the votes (none: it never presses; the vote's 60 s run out)

Frames: key frames at every step (<run>/frames/evo_screens*), shots out/laneN/shots/evo_*.png.
"""
import os
import re
import sys
import threading
import time

from harness import field_dotall as field, mode, waiter
import screen_look as sl
import lane
from sporectl import Console
from twoinst import Checks, keyframe, shot, start

HEART = (721, 1018)    # the mating-call button of the cell HUD
READY_BTN = re.compile(r"ready (\d+) (\d+) (\d+) (\d+) 1 '(?:I'm ready|Я готов)'")
DONE = (1416, 1058)
CANCEL = (1383, 1062)
STAGE_BTN = (1396, 1063)
HISTORY_OK = (1415, 1055)
CAPTION_OK = (1410, 1048)
SAVE_NEW = (926, 604)  # the own editor's exit dialog after an edit: "Save New"
BLACK_LIMIT = 20.0     # seconds of a black picture in a row that a player would call "a black screen"

wait = waiter(step=1.0)
ARG = lambda k, d: sys.argv[sys.argv.index(k) + 1] if k in sys.argv else d


def pool(con):
    return con.call('coop.pool')['out']


def hud_ready(con):
    if 'Cell' not in mode(con) or 'cine=0' not in pool(con):
        return False
    m = re.search(r'input_lock=(-?\d+)', con.call('coop.uistate')['out'])
    return bool(m) and int(m.group(1)) <= 0


def mating_call(con, name, until):
    """the mating-call button, then swim to the mate (clicks at it) until until()"""
    presses = 0
    for _ in range(3):
        wait(lambda: hud_ready(con), 40)
        con.call('coop.pool mate button')
        time.sleep(1)
        con.call('click %d %d' % HEART)
        presses += 1
        time.sleep(3)
        if 'alive=1' in con.call('coop.pool mate')['out']:
            break
    for _ in range(30):
        if until():
            return True
        m = re.search(r'alive=1 .* at (-?\d+) (-?\d+)', con.call('coop.pool mate')['out'])
        if not m:
            if presses < 6 and hud_ready(con):
                con.call('coop.pool mate button')
                con.call('click %d %d' % HEART)
                presses += 1
            time.sleep(2)
            continue
        x, y = (min(max(int(v), 20), lim) for v, lim in ((m.group(1), 1420), (m.group(2), 1000)))
        con.call(f'click {x} {y}')
        time.sleep(2)
    print(f'  {name}: mate {con.call("coop.pool mate")["out"].strip()} ({presses} presses)')
    return bool(until())


def press_ready(con, secs=15):
    btn = wait(lambda: READY_BTN.search(con.call('coop.uistate')['out']), secs)
    if not btn:
        return False
    con.call('click %d %d' % ((int(btn.group(1)) + int(btn.group(3))) // 2, (int(btn.group(2)) + int(btn.group(4))) // 2))
    return True


def margins(path):
    """the brightness of the picture beside the middle (the left and right thirds below the notices): the co-op
    window of a vote covers the middle of the view, so a black game behind it looked drawn to screen_look's `world`
    region (it read the window). The brighter side counts; -1 without Pillow or a picture"""
    try:
        from PIL import Image, ImageStat
        with Image.open(path) as im:
            g = im.convert('L')
            w, h = g.size
            return max(ImageStat.Stat(g.crop((int(a * w), int(b * h), int(c * w), int(d * h)))).mean[0]
                       for a, b, c, d in ((0.0, 0.10, 0.20, 0.85), (0.80, 0.35, 1.0, 0.85)))
    except (OSError, ImportError, SyntaxError, ValueError):
        return -1


class Screens(threading.Thread):
    """a game's screen measured every `step` s in the background (its own console connection, so the test's clicks
    and drags do not stop it): (t, mode, world brightness); black runs are judged on these rows"""

    def __init__(self, con, label, step=2.0):
        super().__init__(daemon=True)
        self.port, self.label, self.step = con.port - 47799, label, step
        self.rows = []
        self.stop_ev = threading.Event()
        self.n = 0

    def run(self):
        c = Console(self.port, timeout=30)
        try:
            while not self.stop_ev.is_set():
                t = time.time()
                m = (c.call('game.mode').get('out') or '').strip()
                path = os.path.join(lane.out_dir('shots'), f'evo_{self.label}_{self.n % 400:03d}.png')
                self.n += 1
                ok = c.call('shot ' + path).get('ok')
                self.rows.append((t, m, margins(path) if ok else -1))
                self.stop_ev.wait(max(0.2, self.step - (time.time() - t)))
        finally:
            c.sock and c.sock.close()

    def stop(self):
        self.stop_ev.set()
        self.join(30)

    def since(self, t0):
        return [r for r in self.rows if r[0] >= t0]

    def longest_black(self, t0=0, t1=None):
        """the longest time the picture stayed black (from the first black shot to the next drawn one)"""
        rows = [r for r in self.rows if r[0] >= t0 and (t1 is None or r[0] <= t1)]
        best, since = 0.0, None
        for t, m, w in rows:
            if 0 <= w <= sl.WORLD_MIN:
                since = since or t
                best = max(best, t - since)
            elif since is not None:
                best = max(best, t - since)
                since = None
        return best

    def text(self, t0=0, t1=None):
        rows = [r for r in self.rows if r[0] >= t0 and (t1 is None or r[0] <= t1)]
        base = rows[0][0] if rows else 0
        out, last = [], None
        for t, m, w in rows:
            key = (m.split()[-1] if m else '?', 'black' if 0 <= w <= sl.WORLD_MIN else 'drawn' if w > 0 else '?')
            if key != last:
                out.append(f'{t - base:.0f}s:{key[0]}/{key[1]}({w:.0f})')
                last = key
        return ' '.join(out)


def avatar_key(con):
    return field(con.call('coop.avatar')['out'], r'key=0x([0-9A-F]{8})')


def shown_species(con, who):
    """the species this game draws for partner `who` (coop.status remote line)"""
    return field(con.call('coop.status')['out'], rf"remote \d+ '{who}'.*?species=([0-9A-F]{{8}})")


def check_black(ck, scr, who, what, t0, t1=None):
    lb = scr.longest_black(t0, t1)
    print(f'  {who} screen {what}: {scr.text(t0, t1)}', flush=True)
    ck(lb < BLACK_LIMIT, f"the {who}'s screen is not black {what} (longest black {lb:.0f} s)")
    return lb


def edit_cell(host, name):
    host.call('move 866 540')
    host.call('button 866 540 down')
    for x in (900, 940, 980):
        host.call(f'move {x} 552')
        time.sleep(0.2)
    host.call('button 980 552 up')
    time.sleep(1)
    host.call('click 720 1055')
    time.sleep(1)
    host.call('click 720 810')
    host.call('text ' + name)
    time.sleep(0.5)
    host.call('click %d %d' % DONE)


def part_cell(host, guest, ck, ready, scr):
    sh, sg = scr
    # 1. the host's mating call: the vote, the host's editor; the guest plays on and SEES its world
    host_vote = lambda: 'vote active' in host.call('coop.vote')['out'] or 'Editor' in mode(host)
    reached = mating_call(host, 'host', host_vote)
    ck(reached, 'the host reaches its mate: the evolution vote starts')
    t_vote = time.time()
    keyframe('host_vote', 'Cell: the host reached its mate, the vote runs (the guest sees its pool and the vote window)', settle=0)
    if ready == 'click':
        ck(press_ready(guest), "the guest's co-op window shows I'm ready (clicked)")
    ck(wait(lambda: 'Editor' in mode(host), 80), f"the host's editor opens after the vote ({mode(host).strip()})")
    t_ed = time.time()
    time.sleep(4)
    keyframe('host_editor', "Cell: the host in its cell editor; the guest's screen shows its pool (not black)", settle=0)
    time.sleep(20)
    edit_cell(host, 'Alicecell')
    ck(wait(lambda: 'Cell' in mode(host), 40), f"the host is back in its pool ({mode(host).strip()})")
    t_back = time.time()
    time.sleep(40)
    check_black(ck, sh, 'host', 'while its vote runs', t_vote, t_ed)
    check_black(ck, sg, 'guest', 'while the host\'s vote runs', t_vote, t_ed)
    check_black(ck, sg, 'guest', 'while the host is in its editor', t_ed, t_back)
    check_black(ck, sg, 'guest', 'after the host\'s editor', t_back)
    check_black(ck, sh, 'host', 'after its editor', t_back)
    keyframe('host_back', "Cell: the host back in its pool with its new cell, the guest plays on")

    # 2. the guest's own mating call: its own editor; the host plays on and sees its world
    if '--one' in sys.argv:  # creature.mode host: the guest's own evolution is refused; its screen must come back
        t_g = time.time()
        mating_call(guest, 'guest', lambda: 'Editor' in mode(guest) or time.time() - t_g > 45)
        time.sleep(25)
        check_black(ck, sg, 'guest', 'after its own mating call was refused (creature.mode host)', t_g)
        check_black(ck, sh, 'host', "after the guest's refused mating call", t_g)
        keyframe('guest_refused', 'Cell, creature.mode host: the guest\'s own mating call is refused; both games show the pool')
        return
    reached = mating_call(guest, 'guest', lambda: 'Editor' in mode(guest))
    ck(reached, "the guest reaches its mate: its own cell editor opens")
    t_ged = time.time()
    ck('vote none' in host.call('coop.vote')['out'], 'no vote for the guest\'s own evolution')
    time.sleep(4)
    keyframe('guest_editor', "Cell: the guest in its own cell editor; the host plays on in its pool", settle=0)
    time.sleep(20)
    edit_cell(guest, 'Bobcell')
    ck(wait(lambda: 'Cell' in mode(guest), 40), 'the guest is back in the pool')
    t_gback = time.time()
    time.sleep(30)
    check_black(ck, sh, 'host', "while the guest is in its own editor", t_ged, t_gback)
    check_black(ck, sh, 'host', "after the guest's editor", t_gback)
    check_black(ck, sg, 'guest', "after its own editor", t_gback)
    keyframe('guest_back', 'Cell: both in the pool after the guest\'s own editor')


def part_cell_both(host, guest, ck, ready, scr):
    """BUG-1239: the guest calls its mate first and sits in its own cell editor; then the host's mating call: the host's
    vote must not keep the host waiting on a black screen for the guest, who cannot answer from its editor"""
    sh, sg = scr
    reached = mating_call(guest, 'guest', lambda: 'Editor' in mode(guest))
    ck(reached, "the guest reaches its mate: its own cell editor opens")
    time.sleep(3)
    host_vote = lambda: 'vote active' in host.call('coop.vote')['out'] or 'Editor' in mode(host)
    reached = mating_call(host, 'host', host_vote)
    ck(reached, 'the host reaches its mate while the guest is in its own editor')
    t_vote = time.time()
    keyframe('host_vote_guest_editing', 'Cell: the host reached its mate while the guest edits its own cell; the host waits for no one in an editor', settle=0)
    opened = wait(lambda: 'Editor' in mode(host), 80)
    t_ed = time.time()
    print(f'  host editor {t_ed - t_vote:.0f} s after its mate (guest {mode(guest).strip()})', flush=True)
    ck(opened, f"the host's editor opens ({mode(host).strip()})")
    ck(t_ed - t_vote < BLACK_LIMIT, f"the host's editor opens without waiting for the guest in its editor ({t_ed - t_vote:.0f} s)")
    time.sleep(1)
    check_black(ck, sh, 'host', 'between its mating call and its editor (the guest in its own editor)', t_vote, t_ed)
    time.sleep(8)
    edit_cell(guest, 'Bobcell')
    edit_cell(host, 'Alicecell')
    ck(wait(lambda: 'Cell' in mode(host) and 'Cell' in mode(guest), 60), f'both back in the pool ({mode(host).strip()} / {mode(guest).strip()})')
    t_back = time.time()
    time.sleep(25)
    check_black(ck, sh, 'host', 'after both editors', t_back)
    check_black(ck, sg, 'guest', 'after both editors', t_back)
    keyframe('both_back', 'Cell: both back in the pool with their new cells')


def add_legs(con, y=560):
    con.call('move 275 300')
    time.sleep(0.3)
    con.call('button 275 300 down')
    time.sleep(0.3)
    for x in (400, 600, 800, 950, 1000):
        con.call(f'move {x} {y}')
        time.sleep(0.2)
    time.sleep(0.5)
    con.call(f'button 1000 {y + 10} up')
    time.sleep(1)


def editor_drawn(con, name):
    tree = con.call('coop.ui tree 4')['out']
    if tree.count('Window') <= 15:
        return False
    s = sl.look(con, name)
    return bool(s) and s['world'] > 25


def part_switch(host, guest, ck, ready, scr):
    host.call('coop.pool food 1000')
    t0 = time.time()
    while time.time() - t0 < 150 and 'vote active' not in host.call('coop.vote')['out'] and 'Creature' not in mode(host):
        if 'cine=1' in pool(host):
            host.call('click %d %d' % CAPTION_OK)
            time.sleep(3)
            continue
        if time.time() - t0 > 25:
            host.call('click %d %d' % STAGE_BTN)
            time.sleep(3)
            host.call('click %d %d' % HISTORY_OK)
        time.sleep(4)
    ck('vote active' in host.call('coop.vote')['out'], 'the switch to Creature waits for the vote')
    keyframe('switch_vote', 'Cell end: the switch vote runs on both games')
    if ready == 'click':
        ck(press_ready(guest), "the guest clicks I'm ready")
    sh, sg = scr
    t_go = time.time()
    t0 = time.time()
    host_ed = guest_ed = None
    host_done = guest_done = False
    k0 = None
    while time.time() - t0 < 420:
        mh, mg = mode(host), mode(guest)
        if 'Editor' in mh and not host_ed:
            host_ed = time.time()
            print(f'  host editor at {host_ed - t0:.0f} s (guest {mg.strip()})', flush=True)
            time.sleep(6)
            keyframe('host_editor', "Creature start: the host in its first Creature editor; the guest: its own editor or its world, never black", settle=0)
            time.sleep(10)
            add_legs(host)
            shot(host, 'evo_switch_host_legs.png')
            host.call('click %d %d' % DONE)
            host_done = True
        if 'Editor' in mg and not guest_ed:
            guest_ed = time.time()
            print(f'  guest editor at {guest_ed - t0:.0f} s (host {mh.strip()})', flush=True)
            ok = wait(lambda: editor_drawn(guest, 'evo_switch_guest_editor.png'), 90, 2)
            ck(ok, "the guest's own Creature editor draws its interface")
            time.sleep(3)
            keyframe('guest_editor', "Creature start: the guest in its OWN first Creature editor", settle=0)
            add_legs(guest)
            add_legs(guest, 640)
            shot(guest, 'evo_switch_guest_legs.png')
            guest.call('click %d %d' % DONE)
            time.sleep(3)
            if 'Editor' in mode(guest):
                shot(guest, 'evo_switch_guest_dialog.png')
                guest.call('click %d %d' % SAVE_NEW)
            guest_done = True
        if k0 is None and 'Creature' in mh and 'Editor' not in mh and not host_ed:
            k0 = avatar_key(host)
        if host_done and (guest_done or time.time() - host_ed > 150) and 'Creature' in mh and 'Creature' in mg:
            break
        time.sleep(1)
    time.sleep(20)
    ck(host_ed, "the host's first Creature editor opened")
    ck(guest_ed, "the guest's OWN first Creature editor opened (each player makes its own creature)")
    check_black(ck, sg, 'guest', 'from the switch to the end of both editors (outside its own editor)', t_go)
    check_black(ck, sh, 'host', 'from the switch to the end of both editors (outside its own editor)', t_go)

    def ident():
        hc, gc = host.call('coop.creations')['out'], guest.call('coop.creations')['out']
        hk, gk = avatar_key(host), avatar_key(guest)
        return dict(host=hk, guest=gk, host_shows_guest=shown_species(host, 'Bob'),
                    guest_shows_host=shown_species(guest, 'Alice'),
                    guest_creation_on_host=field(hc, r"player 1 creature: ([0-9A-F]{8})"),
                    host_creation_on_guest=field(gc, r"player 0 creature: ([0-9A-F]{8})"),
                    host_key_on_guest=(field(gc, rf'host species {hk} = ([0-9A-F]{{8}}) here') or hk) if hk else None,
                    host_mode=mode(host).strip(), guest_mode=mode(guest).strip())

    def confirm_captions():
        """a player confirms the stage's caption ("It is the beginning of a new day...") with its check"""
        for con in (host, guest):
            if re.search(r'cinematic ([0-9A-F]{8}) playing', con.call('coop.status')['out']):
                con.call('key enter')

    def good():
        confirm_captions()
        s = ident()
        if not all((s['host'], s['guest'], s['host_shows_guest'], s['guest_shows_host'])):
            return None
        own = s['host'] != s['guest'] and k0 not in (s['host'], s['guest'])
        # the host draws the guest with the guest's own creation (registered here under a content key), not with
        # the host's creature; the guest draws the host with the host's new one
        host_draws = s['host_shows_guest'] == s['guest_creation_on_host'] and s['host_shows_guest'] not in (k0, s['host'])
        guest_draws = s['guest_shows_host'] in (s['host_creation_on_guest'], s['host_key_on_guest'], s['host']) and \
            s['guest_shows_host'] != s['guest']
        return s if own and host_draws and guest_draws else None

    time.sleep(10)
    r = wait(good, 150, 3)
    print(f'  pre-editor {k0}; after: {r or ident()}')
    ck(r, 'each player plays its OWN new creature (two different species), the host draws the guest with the guest\'s')
    keyframe('own_creatures', 'Creature: each player plays its own new creature; each game draws the partner with the partner\'s creature', calm=5)
    shot(host, 'evo_switch_host_after.png')
    shot(guest, 'evo_switch_guest_after.png')
    # some time later (BUG-0508's 30 s check, a world share): still each its own
    time.sleep(45)
    r2 = good()
    print(f'  45 s later: {r2 or ident()}')
    ck(r2, 'each player still plays its own creature 45 s later (no world share took it back)')
    keyframe('own_creatures_later', 'Creature: 45 s later, still each player its own creature', calm=5)


def main():
    part = ARG('--part', 'cell')
    ready = ARG('--ready', 'click')
    host, guest = start('cell', guest_creations=False)
    ck = Checks()
    time.sleep(3)
    one = '--one' in sys.argv  # the session setting creature.mode host (one creature for everyone, BUG-0769)
    if one:
        import settings_lib
        import twoinst
        twoinst.forget_settings()
        ck(settings_lib.force(host, [guest], 'creature.mode', 'host'), 'the guest got creature.mode host')
    scr = (Screens(host, 'host'), Screens(guest, 'guest'))
    for x in scr:
        x.start()
    try:
        if part == 'cell':
            part_cell(host, guest, ck, ready, scr)
        elif part == 'cell-both':
            part_cell_both(host, guest, ck, ready, scr)
        else:
            part_switch(host, guest, ck, ready, scr)
    finally:
        for x in scr:
            x.stop()
        if one:
            settings_lib.reset([host], ('creature.mode',))
            twoinst.forget_settings()
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
