#!/usr/bin/env python3
"""The evolution editors as the players' video showed them (BUG-1239, BUG-1240): what the OTHER player sees while one
is in an editor, and which creature each player plays afterwards. Real clicks (the mating-call button, the swim to the
mate, the vote's I'm ready, History's check, the editors' parts and done), and the screens are measured
(screen_look.py: a black picture is the symptom), not only game.mode.

  --part cell     Cell fixture, a guest without creations:
                    1. the host's mating call, the host's editor at once (BUG-1263, cell.editor_now: no vote, the
                       guest gets a notice; with --vote the vote: the guest's I'm ready by a click, or --ready none:
                       the vote runs out): neither screen is black for long (the host's was black behind the vote)
                    2. the guest's own mating call: its own editor, the host plays on: the host's screen shows its
                       world all along
  --part switch   Cell fixture, a guest without creations: History's check, the switch vote (I'm ready), both reach
                  Creature; the host adds legs in the first Creature editor, the guest gets its OWN first Creature
                  editor, adds legs and a second leg pair and saves it as new: afterwards each plays its own creature
                  (the avatar keys differ) and each game draws the partner with the partner's creature. While either
                  is in its editor the other's screen is never black for long (an editor or the world is drawn).
                  BUG-1269: while the switch vote runs the host's picture is not mostly white (the share of near-white
                  pixels beside the co-op window, in the key frame and in the shots of the vote; the guest's printed
                  beside it). `SPORE_FIX_OFF=core.switch_vote_world` (held at the switch, after the game's white end
                  sequence) must FAIL that check.

  --part cell-eggs  Cell fixture (BUG-1245): the host's mating call, the guest answers the vote late (--ready click:
                  after 45 s) or never: the host's game plays the mating scene and the egg laying once, not again
                  every ~10 s while the vote holds its editor; the co-op window with the vote stays on its screen

  --part cell-mate Cell fixture (BUG-1246): the guest calls its mate, the host swims next to the guest: the host's game
                  draws the guest's mate where the guest's game has it (no second copy on the guest); when the
                  guest meets it (its own editor) the host's copy goes
  --part cell-dna  Cell fixture (BUG-1249): the guest buys a part in its own cell editor: back in the pool its DNA
                  keeps the price off (the host's count put the old DNA back). Own DNA (WP-D, BUG-1291, the default):
                  the host's spent count and DNA do not take the guest's price, and after the host's own editor the
                  guest's spent count is still its own (the host's price not in it); SPORE_FIX_OFF=creature.own_dna
                  FAILs. --shared (all.shared_dna on, the old pool): the host's DNA has the guest's price off too, also
                  after the host's own editor, the same DNA and spent count on both

  --ready click|none   the guest's I'm ready in the votes (none: it never presses; the vote's 60 s run out)
  --vote    the session setting core.editor_now off: the host's Cell evolution goes through the vote (0.7.15; the
            parts cell-eggs and cell-both test what the vote does meanwhile, BUG-1245, BUG-1239)

Frames: key frames at every step (<run>/frames/evo_screens*), shots out/laneN/shots/evo_*.png.
"""
import os
import re
import sys
import threading
import time

from harness import field_dotall as field, mode, pool, waiter
import screen_look as sl
import lane
from sporectl import Console
from twoinst import Checks, keyframe, shot, start
from coop_sync import game_paused, sample as sync_sample

HEART = (721, 1018)    # the mating-call button of the cell HUD
READY_BTN = re.compile(r"ready (\d+) (\d+) (\d+) (\d+) 1 '(?:I'm ready|Я готов)'")
DONE = (1416, 1058)
CANCEL = (1383, 1062)
STAGE_BTN = (1396, 1063)
HISTORY_OK = (1415, 1055)
CAPTION_OK = (1410, 1048)
SAVE_NEW = (926, 604)  # the own editor's exit dialog after an edit: "Save New"
BLACK_LIMIT = 20.0     # seconds of a black picture in a row that a player would call "a black screen"
# BUG-1378: the guest's game's own load and stage-entry pause after it loads the host's world (BUG-0313, the bake wait;
# not the mod's, BUG-1239's root cause: "not changed") is measured apart from BLACK_LIMIT: 4-14 s at 60 fps and 12-22 s
# at 10 fps in 11 runs; a stage entry that never ends (or one the mod makes much longer) still fails at this limit
ENTRY_LIMIT = 45.0
LOAD_MODES = ('Loading', 'LoadGame', 'GalaxyGameEntry')  # game.mode names of the load (the guest's way to a new world)
VOTE = '--vote' in sys.argv  # the session setting core.editor_now off: the host's Cell evolution through the vote (BUG-1263)

wait = waiter(step=1.0)
ARG = lambda k, d: sys.argv[sys.argv.index(k) + 1] if k in sys.argv else d


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


WHITE_PIXEL = 240  # BUG-1269: a near-white pixel (grey level)
WHITE_LIMIT = 0.5  # the share of near-white pixels beside the co-op window over which a picture is "a white screen"


def white_share(path):
    """BUG-1269: the share of near-white pixels in the left and right thirds below the notices (margins' regions: the
    co-op window covers the middle and the letterbox bars the top and bottom); -1 without Pillow or a picture"""
    try:
        from PIL import Image
        with Image.open(path) as im:
            g = im.convert('L')
            w, h = g.size
            hi = n = 0
            for a, b, c, d in ((0.0, 0.10, 0.20, 0.85), (0.80, 0.35, 1.0, 0.85)):
                hist = g.crop((int(a * w), int(b * h), int(c * w), int(d * h))).histogram()
                hi += sum(hist[WHITE_PIXEL:])
                n += sum(hist)
            return hi / n if n else -1
    except (OSError, ImportError, SyntaxError, ValueError):
        return -1


class Screens(threading.Thread):
    """a game's screen measured every `step` s in the background (its own console connection, so the test's clicks
    and drags do not stop it): (t, mode, world brightness); black runs are judged on these rows"""

    def __init__(self, con, label, step=2.0, entry=False):
        super().__init__(daemon=True)
        self.port, self.label, self.step = con.port - 47799, label, step
        self.rows = []
        # BUG-1378: entry=True also reads per shot whether this game's time stands by itself (coop.status: busy has
        # `paused`, the host-busy wait of the mod `wait=none`): {t: (paused, mod_wait)}; see entry_rows()
        self.entry = entry
        self.pause = {}
        self.whites = []  # (t, white_share) per shot (BUG-1269)
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
                if self.entry:
                    st = c.call('coop.status').get('out') or ''
                    busy = re.search(r'^busy: ([^\n]*)', st, re.M)
                    self.pause[t] = (bool(busy) and 'paused' in busy.group(1).split(','),
                                     not re.search(r'\bwait=none\b', st))
                self.rows.append((t, m, margins(path) if ok else -1))
                self.whites.append((t, white_share(path) if ok else -1))
                self.stop_ev.wait(max(0.2, self.step - (time.time() - t)))
        finally:
            c.sock and c.sock.close()

    def stop(self):
        self.stop_ev.set()
        self.join(30)

    def white_median(self, t0, t1):
        """the median near-white share of the shots between t0 and t1 (None: no shot measured)"""
        v = sorted(w for t, w in self.whites if t0 <= t <= t1 and w >= 0)
        return v[len(v) // 2] if v else None

    def since(self, t0):
        return [r for r in self.rows if r[0] >= t0]

    def entry_rows(self, rows):
        """BUG-1378: the times of the black shots that are this game's own load and stage-entry pause (BUG-0313: after a
        load the game's time stands with its screen black until every creature model is baked; 4-14 s at 60 fps, 12-22 s
        at 10 fps in 11 runs of evo_screens_switch_lowfps, lab 20261118-100543 .. 20261120-133703): a black run that
        starts in a load mode (or right after one) and goes on while the game's time stands by itself (not the mod's
        host-busy wait). Only with entry=True; such shots are measured on their own (longest_entry), not as the black
        screen of BUG-1239 / BUG-1240 (the player waiting while the other edits)."""
        out = set()
        if not self.entry:
            return out
        on = after_load = False
        for t, m, w in rows:
            black = 0 <= w <= sl.WORLD_MIN
            paused, waits = self.pause.get(t, (False, False))
            if any(k in m for k in LOAD_MODES):
                on, after_load = black, True  # the loading screen itself
            else:
                on = (on or after_load) and black and paused and not waits
                after_load = False
            if on:
                out.add(t)
        return out

    def longest_entry(self, t0=0, t1=None):
        """the longest of this game's own load + stage-entry pauses (entry_rows), from its first shot to the next one"""
        rows = [r for r in self.rows if r[0] >= t0 and (t1 is None or r[0] <= t1)]
        ent = self.entry_rows(rows)
        best, since = 0.0, None
        for t, m, w in rows:
            if t in ent:
                since = since or t
            elif since is not None:
                best = max(best, t - since)
                since = None
        return best

    def longest_black(self, t0=0, t1=None):
        """the longest time the picture stayed black (from the first black shot to the next drawn one); with entry=True
        the game's own load and stage-entry pause (entry_rows) is not counted (BUG-1378)"""
        rows = [r for r in self.rows if r[0] >= t0 and (t1 is None or r[0] <= t1)]
        ent = self.entry_rows(rows)
        best, since = 0.0, None
        for t, m, w in rows:
            if 0 <= w <= sl.WORLD_MIN and t not in ent:
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
        ent = self.entry_rows(rows)
        for t, m, w in rows:
            key = (m.split()[-1] if m else '?', 'entry' if t in ent else 'black' if 0 <= w <= sl.WORLD_MIN else 'drawn' if w > 0 else '?')
            if key != last:
                out.append(f'{t - base:.0f}s:{key[0]}/{key[1]}({w:.0f})')
                last = key
        return ' '.join(out)


NPC_APART = 5.0  # m: a copy over this far from the host's animal in 3 reads 1 s apart (trail p90 0.6 m; BUG-1376 10-15 m)


def npcs_apart(host, guest, near=40.0):
    """[(key, m)]: the host's animals within `near` m of its avatar and how far the guest's copy stands from them (the
    host read before and after the guest: a walking animal counts against the nearer of its two places)"""
    h0, g, h1 = sync_sample(host), sync_sample(guest), sync_sample(host)
    me = h0.get('p0')
    d = lambda a, b: sum((x - y) ** 2 for x, y in zip(a, b)) ** 0.5  # noqa: E731
    out = []
    for k, (hp, _) in h0.items():
        if not k.startswith('n') or k not in g or k not in h1 or (me and d(hp, me[0]) > near):
            continue
        out.append((k, min(d(g[k][0], hp), d(g[k][0], h1[k][0]))))
    return out


def avatar_key(con):
    return field(con.call('coop.avatar')['out'], r'key=0x([0-9A-F]{8})')


def shown_species(con, who):
    """the species this game draws for partner `who` (coop.status remote line)"""
    return field(con.call('coop.status')['out'], rf"remote \d+ '{who}'.*?species=([0-9A-F]{{8}})")


def check_black(ck, scr, who, what, t0, t1=None):
    lb = scr.longest_black(t0, t1)
    print(f'  {who} screen {what}: {scr.text(t0, t1)}', flush=True)
    ck(lb < BLACK_LIMIT, f"the {who}'s screen is not black {what} (longest black {lb:.0f} s)")
    if scr.entry:  # BUG-1378: the game's own load + stage-entry pause, measured on its own (a hang there still fails)
        le = scr.longest_entry(t0, t1)
        ck(le < ENTRY_LIMIT, f"the {who}'s game's own load and stage-entry pause {what} ends (longest {le:.0f} s, "
                             f"limit {ENTRY_LIMIT:.0f} s)")
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
    # 1. the host's mating call: its editor (at once since BUG-1263, cell.editor_now; through the vote with --vote or
    # the switch off); the guest plays on and SEES its world, the host's picture is not black for long either
    g_off = log_size(guest)
    t_call = time.time()  # the host's black comes with the egg laying, before its editor (or its vote) starts
    host_vote = lambda: 'vote active' in host.call('coop.vote')['out'] or 'Editor' in mode(host)
    reached = mating_call(host, 'host', host_vote)
    voted = 'vote active' in host.call('coop.vote')['out']
    ck(reached, 'the host reaches its mate: its editor opens (or the evolution vote starts)')
    t_vote = time.time()
    print(f"  host after its mate: {mode(host).strip()}, {'a vote runs' if voted else 'no vote'}", flush=True)
    keyframe('host_vote', 'Cell: the host reached its mate: its editor opens at once (the guest sees its pool and a notice)'
             if not voted else 'Cell: the host reached its mate, the vote runs (the guest sees its pool and the vote window)', settle=0)
    if ready == 'click' and voted:
        ck(press_ready(guest), "the guest's co-op window shows I'm ready (clicked)")
    ck(wait(lambda: 'Editor' in mode(host), 80), f"the host's editor opens ({mode(host).strip()})")
    t_ed = time.time()
    print(f'  host editor {t_ed - t_vote:.0f} s after its mate', flush=True)
    if not VOTE:
        ck(t_ed - t_vote < BLACK_LIMIT, f"the host's editor opens without waiting for the guest ({t_ed - t_vote:.0f} s, cell.editor_now)")
        note = [l for l in log_since(guest, g_off) if 'evolves its cell' in l or 'evolves the creature of everybody' in l]
        print(f"  guest's notice: {note[-1].strip()[-90:] if note else None}", flush=True)
        ck(bool(note), "the guest is told that the host evolves its cell (a notice, no vote window)")
    time.sleep(4)
    keyframe('host_editor', "Cell: the host in its cell editor; the guest's screen shows its pool (not black)", settle=0)
    time.sleep(20)
    edit_cell(host, 'Alicecell')
    ck(wait(lambda: 'Cell' in mode(host), 40), f"the host is back in its pool ({mode(host).strip()})")
    t_back = time.time()
    time.sleep(40)
    # BUG-1263: a guest that never answers kept the host's picture black behind the vote for its 60 s (the game had
    # faded out after the egg laying, docs/re/cell.md#cell-vote-black-screen); with cell.editor_now there is no vote:
    # the host's editor follows the egg laying as in a solo game (SPORE_FIX_OFF=cell.editor_now, --ready none: FAILS)
    check_black(ck, sh, 'host', 'from its mating call to its editor', t_call, t_ed)
    check_black(ck, sg, 'guest', 'while the host\'s vote runs', t_vote, t_ed)
    check_black(ck, sg, 'guest', 'while the host is in its editor', t_ed, t_back)
    check_black(ck, sg, 'guest', 'after the host\'s editor', t_back)
    check_black(ck, sh, 'host', 'after its editor', t_back)
    keyframe('host_back', "Cell: the host back in its pool with its new cell, the guest plays on")

    # 2. the guest's own mating call: its own editor; the host plays on and sees its world
    if '--one' in sys.argv:  # creature.mode host: the guest's own evolution is refused; its screen must come back
        # BUG-1265: the guest's mating call in that mode brings no mating round: its mate is forgotten at once (no
        # mating scene, no egg laying, no fade); before, the refused request's round and its black fade came every ~10 s
        g_off2 = log_size(guest)
        m0, e0 = mate_counts(guest)
        t_g = time.time()
        mating_call(guest, 'guest', lambda: 'Editor' in mode(guest) or time.time() - t_g > 30)
        time.sleep(35)
        m1, e1 = mate_counts(guest)
        lines = log_since(guest, g_off2)
        n = sum('request is refused' in l for l in lines)
        told = any('the host evolves the cell for everybody' in l for l in lines)
        print(f'  guest after its mating call: {m1 - m0} matings, {e1 - e0} egg layings, {n} refused requests, told {told}', flush=True)
        ck(m1 - m0 == 0 and e1 - e0 == 0 and n == 0,
           f"the guest's own mating call starts no mating round in creature.mode host ({m1 - m0} matings, {e1 - e0} egg layings, {n} refused requests)")
        ck(told, 'the guest is told that the host evolves the cell for everybody')
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


def save_dialog_up(con):
    """the game's save dialog of an edit-a-creation editor is up ("Save <name> as a new creation or replace <name>"):
    the game is still in the editor and its golden Replace / Save New buttons are drawn (the mode alone also stays
    Editor during the editor's exit)"""
    if 'Editor' not in mode(con):
        return False
    path = os.path.join(lane.out_dir('shots'), 'evo_switch_dialog_probe.png')
    if not con.call('shot ' + path).get('ok'):
        return False
    try:
        from PIL import Image
        with Image.open(path) as im:
            im = im.convert('RGB')
            w, h = im.size
            n = gold = 0
            for x in range(int(w * 0.545), int(w * 0.68), 2):
                for y in range(int(h * 0.548), int(h * 0.572)):
                    r, g, b = im.getpixel((x, y))
                    n += 1
                    gold += r > 140 and g > 110 and b < 90 and r > b + 60
    except (OSError, ImportError):
        return 'Editor' in mode(con)
    print(f'  save dialog probe: {gold / max(n, 1):.2f} of the buttons\' area golden', flush=True)
    return gold / max(n, 1) > 0.3


def log_size(con):
    try:
        return os.path.getsize(lane.game_log(con.port - 47799))
    except OSError:
        return 0


def log_since(con, off):
    try:
        with open(lane.game_log(con.port - 47799), 'rb') as f:
            f.seek(off)
            return f.read().decode('utf-8', 'replace').splitlines()
    except OSError:
        return []


def mate_counts(con):
    """the mating scenes and egg layings the game's own avatar played (its animation entering 0x42 / 0x29)"""
    out = con.call('coop.pool mate')['out']
    m, e = re.search(r'matings=(\d+)', out), re.search(r'egg_lays=(\d+)', out)
    return (int(m.group(1)) if m else -1), (int(e.group(1)) if e else -1)


def window_shown(con):
    """the co-op window is open and drawn (its frame visible) in this game"""
    m = re.search(r'^window (-?\d+) (-?\d+) (-?\d+) (-?\d+) (\d)', con.call('coop.uistate')['out'], re.M)
    return bool(m) and m.group(5) == '1' and int(m.group(3)) > int(m.group(1))


def part_cell_eggs(host, guest, ck, ready, scr):
    """BUG-1245: the host's mating call and a vote the guest answers late (or never): while the vote holds the host's
    editor, the host's game must not play the mating scene and the egg laying again (the players' video: 6 eggs on a
    flickering black screen), and the co-op window with the vote stays on the host's screen"""
    sh, sg = scr
    host_vote = lambda: 'vote active' in host.call('coop.vote')['out'] or 'Editor' in mode(host)
    reached = mating_call(host, 'host', host_vote)
    ck(reached and 'vote active' in host.call('coop.vote')['out'], 'the host reaches its mate: the evolution vote starts')
    t_vote = time.time()
    m0, e0 = mate_counts(host)
    print(f'  host at the vote: matings={m0} egg_lays={e0} {host.call("coop.pool mate")["out"].strip()}', flush=True)
    hold = 45 if ready == 'click' else 70
    shown, rows = [], []
    k = 0
    m1, e1 = m0, e0
    while time.time() - t_vote < hold and 'Editor' not in mode(host) and 'vote active' in host.call('coop.vote')['out']:
        m, e = mate_counts(host)
        if m < 0 or 'Cell' not in mode(host):
            break  # the vote ran out: the editor comes (the counts are read in Cell only)
        look = window_shown(host)
        # BUG-1387: the vote ran out between the loop's check and this look (lab 20261120-133703 lowfps: "time is up
        # after 59.1 s", the 59 s look read no window): a look after the vote's end says nothing of the window
        if look or 'vote active' in host.call('coop.vote')['out']:
            shown.append(look)
        m1, e1 = m, e
        rows.append(f'{time.time() - t_vote:.0f}s:{m - m0}/{e - e0}')
        if k in (0, 3) and time.time() - t_vote > (12 if k == 0 else 33):
            keyframe('host_vote_%d' % (k // 3 + 1), 'Cell: %.0f s into the host\'s vote: one mating scene, no new eggs; the vote window on the host\'s screen' % (time.time() - t_vote), settle=0)
            k += 3
        time.sleep(3)
    t_end = time.time()
    print(f'  host during the vote (matings/egg layings since its start): {" ".join(rows)}', flush=True)
    ck(m1 - m0 == 0 and e1 - e0 == 0, f"the host's game plays the mating scene and the egg laying once, not again while the vote holds its editor ({m1 - m0} more matings, {e1 - e0} more egg layings in {t_end - t_vote:.0f} s)")
    ck(shown and all(shown), f"the host's co-op window with the vote stays on its screen ({sum(shown)} of {len(shown)} looks)")
    # what the host sees: each repeated mating scene drew the pool again for a moment (the black flickered)
    rows_v = [r for r in sh.since(t_vote + 8) if r[0] <= t_end and 'Cell' in r[1]]
    flashes, was = 0, False
    for _, _, w in rows_v:
        drawn = w > sl.WORLD_MIN
        flashes += drawn and not was
        was = drawn
    print(f'  host screen during the vote: {sh.text(t_vote)}', flush=True)
    ck(flashes == 0, f"the host's picture behind the vote does not flash back to the pool ({flashes} flashes in {len(rows_v)} shots)")
    if ready == 'click':
        ck(press_ready(guest), "the guest's co-op window shows I'm ready (clicked late)")
    ck(wait(lambda: 'Editor' in mode(host), 60), f"the host's editor opens after the vote ({mode(host).strip()})")
    t_ed = time.time()
    time.sleep(4)
    keyframe('host_editor', "Cell: the host in its cell editor after the late vote", settle=0)
    time.sleep(8)
    edit_cell(host, 'Alicecell')
    ck(wait(lambda: 'Cell' in mode(host), 40), f"the host is back in its pool ({mode(host).strip()})")
    t_back = time.time()
    time.sleep(25)
    m2, e2 = mate_counts(host)
    ck(m2 - m1 == 0, f'no mating scene after the editor ({m2 - m1})')
    check_black(ck, sg, 'guest', "while the host's vote runs", t_vote, t_ed)
    check_black(ck, sh, 'host', 'after its editor', t_back)
    check_black(ck, sg, 'guest', "after the host's editor", t_back)
    keyframe('host_back', 'Cell: the host back in its pool with its new cell, no egg loop')


def cell_dna(con):
    """the Cell DNA this game shows (DNAForFood(food) - spent, the game's own fields) and its spent count"""
    out = con.call('coop.pool spend')['out']
    d, sp = re.search(r'dna=(-?\d+)', out), re.search(r' spent=(-?\d+)', out)
    return (int(d.group(1)) if d else None), (int(sp.group(1)) if sp else None)


def buy_part(con, frm=(105, 195), to=(880, 560)):
    """drag a part of the cell editor's palette (the cilia, top left) onto the cell"""
    con.call(f'move {frm[0]} {frm[1]}')
    time.sleep(0.3)
    con.call(f'button {frm[0]} {frm[1]} down')
    time.sleep(0.3)
    for k in range(1, 6):
        con.call(f'move {frm[0] + (to[0] - frm[0]) * k // 5} {frm[1] + (to[1] - frm[1]) * k // 5}')
        time.sleep(0.2)
    time.sleep(0.4)
    con.call(f'button {to[0]} {to[1]} up')
    time.sleep(1)


def part_cell_dna(host, guest, ck, ready, scr):
    """BUG-1249: the guest buys a part in its own cell editor (its mating call): back in the pool its DNA shows the price
    taken off and keeps it (the host's spent count put the old DNA back), and the party's DNA (the host's) has it off
    too; also after the host's own editor"""
    sh, sg = scr
    shared = '--shared' in sys.argv
    host.call('coop.pool food 400')
    time.sleep(8)
    h0, g0 = cell_dna(host), cell_dna(guest)
    print(f'  DNA before: host {h0} guest {g0}', flush=True)
    ck(g0[0] and g0[0] > 15 and h0[0] == g0[0], f'both games show the same Cell DNA before ({h0[0]} / {g0[0]})')
    reached = mating_call(guest, 'guest', lambda: 'Editor' in mode(guest))
    ck(reached, "the guest reaches its mate: its own cell editor opens")
    time.sleep(8)
    buy_part(guest)
    time.sleep(2)
    keyframe('guest_editor_part', "Cell: the guest's own cell editor with a bought part (its DNA counter lower)", settle=0)
    shot(guest, 'evo_dna_guest_editor.png')
    edit_cell(guest, 'Bobcell')
    ck(wait(lambda: 'Cell' in mode(guest) and cell_dna(guest)[0] is not None, 40), 'the guest is back in the pool')
    t_back = time.time()
    seen = []
    while time.time() - t_back < 25:
        seen.append((round(time.time() - t_back), cell_dna(guest)[0], cell_dna(host)[0]))
        time.sleep(2.5)
    print(f'  after the guest\'s editor (s, guest DNA, host DNA): {seen}', flush=True)
    print(f'  guest {guest.call("coop.pool spend")["out"].strip()}', flush=True)
    print(f'  host  {host.call("coop.pool spend")["out"].strip()}', flush=True)
    paid = g0[0] - seen[0][1] if seen and seen[0][1] is not None else 0
    ck(paid > 0, f"the guest's own editor took the part's price ({g0[0]} -> {seen[0][1] if seen else None})")
    ck(paid > 0 and all(g == g0[0] - paid for _, g, _ in seen),
       f"the guest's DNA keeps the price off while the host plays on ({[g for _, g, _ in seen]}, expected {g0[0] - paid})")
    hs = cell_dna(host)[1]
    if shared:
        ck(paid > 0 and seen[-1][2] == g0[0] - paid, f"the party's DNA (the host's) has the guest's price off too ({seen[-1][2] if seen else None})")
    else:  # WP-D: own DNA; the host's DNA is judged by its spent count (either player may eat meanwhile)
        ck(paid > 0 and hs == h0[1], f"own DNA: the host's spent count does not take the guest's price ({h0[1]} -> {hs})")
    keyframe('guest_back_dna', "Cell: the guest back in the pool: its DNA counter shows the part's price taken off" +
             ("" if shared else "; the host's counter does not (own DNA)"), calm=3)
    # the host's own editor afterwards (the players' video: the guest's DNA changed only when the host left its editor)
    sp1 = cell_dna(host)[1]  # the game's spent count with the guest's price in it
    host_vote = lambda: 'vote active' in host.call('coop.vote')['out'] or 'Editor' in mode(host)
    if mating_call(host, 'host', host_vote):
        if 'vote active' in host.call('coop.vote')['out']:  # the Cell vote (core.editor_now off, BUG-1263)
            press_ready(guest)
        if wait(lambda: 'Editor' in mode(host), 70):
            time.sleep(8)
            edit_cell(host, 'Alicecell')
            ck(wait(lambda: 'Cell' in mode(host), 40), 'the host is back in its pool')
            time.sleep(15)
            h2, g2 = cell_dna(host), cell_dna(guest)
            print(f'  after the host\'s editor: host {h2} guest {g2}', flush=True)
            # the price is judged by the game's spent count (S+0x2C), not by the DNA: either player may eat after the
            # editor (food 400 -> 409 gave DNA 13 -> 14 with the spend kept, lab 20261118-135513)
            if shared:
                ck(h2[0] == g2[0] and g2[0] is not None and h2[1] is not None and g2[1] == h2[1] and sp1 is not None and
                   h2[1] >= sp1 >= paid,
                   f"after the host's editor both games show the same DNA and keep the guest's price in the spent count "
                   f"(DNA {h2[0]} / {g2[0]}, spent {h2[1]} / {g2[1]}, before the host's editor {sp1}, the price {paid})")
            else:
                gs = cell_dna(guest)[1]
                ck(g2[1] is not None and gs == g0[1] + paid and h2[1] is not None and h2[1] >= (sp1 or 0),
                   f"own DNA: after the host's editor the guest's spent count is still its own ({g0[1]} + {paid} = {gs}; "
                   f"the host's {sp1} -> {h2[1]})")
    keyframe('host_back_dna', 'Cell: both back in the pool' + (', the same DNA on both' if shared else
             ': each DNA counter its own (the guest\'s without the host\'s price, the host\'s without the guest\'s)'))


def mate_seen(con, x, y, model, rad=5.0):
    """the cells of this game drawn within rad m of (x, y) with the mate's model (not a player's cell)"""
    out = con.call(f'coop.pool near {x:.1f} {y:.1f} {rad}')['out']
    return [l for l in out.splitlines() if f'model={model}' in l and 'puppet=0' in l and 'avatar=0' in l]


def part_cell_mate(host, guest, ck, ready, scr):
    """BUG-1246: the guest calls its mate; the host swims next to the guest and must see that mate in its own game (a
    drawn cell of the guest's model where the guest's mate is), the guest no second copy of it; when the guest meets
    its mate (its own editor) the host's copy goes"""
    sh, sg = scr
    gmate = lambda: re.search(r'alive=1 .*pos=\((-?[\d.]+) (-?[\d.]+)\) model=([0-9A-F]{8})', guest.call('coop.pool mate')['out'])
    t0 = time.time()
    mating_call(guest, 'guest', lambda: bool(gmate()) or time.time() - t0 > 60)
    m = gmate()
    ck(m, f"the guest's mating call brought its mate ({guest.call('coop.pool mate')['out'].strip()[:160]})")
    if not m:
        return
    host.call('coop.pool tp 3 3')
    time.sleep(4)
    looks, hits = 0, 0
    for _ in range(6):
        m = gmate()
        if not m:
            break
        x, y, model = float(m.group(1)), float(m.group(2)), m.group(3)
        on_host = mate_seen(host, x, y, model)
        on_guest = mate_seen(guest, x, y, model, 3.0)
        looks += 1
        hits += bool(on_host)
        print(f'  guest mate at ({x:.1f} {y:.1f}) {model}: host {on_host[:1]} guest {len(on_guest)}', flush=True)
        ck(len(on_guest) <= 1, f"the guest's game has its mate once, no copy of it from the host ({len(on_guest)})")
        time.sleep(1.5)
    print(f'  host: {host.call("coop.pool mate")["out"].strip()}', flush=True)
    ck(looks and hits == looks, f"the host's game draws the guest's mate where the guest's game has it ({hits} of {looks} looks)")
    keyframe('host_sees_guest_mate', "Cell: the host next to the guest: the guest's mate (the guest's species) is in the host's pool too", calm=2)
    reached = mating_call(guest, 'guest', lambda: 'Editor' in mode(guest))
    ck(reached, "the guest meets its mate: its own cell editor opens")
    gone = wait(lambda: 'copies=0' in host.call('coop.pool mate')['out'], 10)
    ck(gone, f"the host's copy of the guest's mate goes with it ({host.call('coop.pool mate')['out'].strip()[-160:]})")
    time.sleep(6)
    edit_cell(guest, 'Bobcell')
    ck(wait(lambda: 'Cell' in mode(guest), 40), 'the guest is back in the pool')
    time.sleep(10)
    check_black(ck, sh, 'host', "while the guest called and met its mate", t0)
    keyframe('guest_back', 'Cell: the guest back with its new cell, no mate left in either game')


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


def check_white(ck, host, guest, e, scr, t_vote):
    """BUG-1269: the host's picture during the switch vote is not mostly white (the game's Cell-end sequence held at its
    white end): the key frame's and the vote's shots' share of near-white pixels beside the co-op window"""
    import frames
    d = frames.frames_dir(lane.out_dir())
    kf = {r: white_share(os.path.join(d, g['file'])) if g.get('file') else -1
          for r, g in ((e or {}).get('games') or {}).items()}
    sh, sg = scr
    t1 = time.time()
    hm, gm = sh.white_median(t_vote, t1), sg.white_median(t_vote, t1)
    print(f"  white share during the vote (near-white pixels beside the window): key frame host {kf.get('host', -1):.2f} "
          f"guest {kf.get('guest', -1):.2f}; shots' median host {hm} guest {gm}; "
          f"{host.call('coop.vote')['out'].splitlines()[-1]}", flush=True)
    vals = [v for v in (kf.get('host', -1), hm) if v is not None and v >= 0]
    ck(vals and max(vals) < WHITE_LIMIT,
       f"the host's picture is not a white screen during the switch vote (near-white share {vals}, limit {WHITE_LIMIT}; "
       f"the guest's {kf.get('guest', -1):.2f})")


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
    t_vote = time.time()
    e = keyframe('switch_vote', "Cell end: the switch vote runs on both games; the host's game shows its Cell world (or "
                 "the cinematic's last picture) behind the co-op window, not a plain white screen; the guest its "
                 "world or its own Cell-end cinematic")
    check_white(ck, host, guest, e, scr, t_vote)
    if ready == 'click':
        ck(press_ready(guest), "the guest clicks I'm ready")
    sh, sg = scr
    t_go = time.time()
    t0 = time.time()
    g_log0 = log_size(guest)
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
            dialog = save_dialog_up(guest)
            if dialog:
                shot(guest, 'evo_switch_guest_dialog.png')
                keyframe('guest_save_dialog', "Creature start: the guest's done brought the game's save dialog (Save New / Replace the world's species)", settle=0)
                guest.call('click %d %d' % SAVE_NEW)
            # BUG-1255: the guest's first own creature is a new creation: no dialog that offers to replace the host's
            # species (the world's, which the editor started from)
            ck(not dialog, "the guest's done saves its first own creature without the dialog to replace the host's species")
            guest_done = True
        if k0 is None and 'Creature' in mh and 'Editor' not in mh and not host_ed:
            k0 = avatar_key(host)
        if host_done and (guest_done or time.time() - host_ed > 150) and 'Creature' in mh and 'Creature' in mg:
            break
        time.sleep(1)
    time.sleep(20)
    # BUG-1258: what the guest is told while it makes its own creature (each player its own: no "new body for all")
    told = [l for l in log_since(guest, g_log0) if 'everybody gets the new body' in l]
    print(f'  the guest was told: {told[:2]}', flush=True)
    ck(not told, f"the guest making its own creature is not told that everybody gets the host's new body ({len(told)})")
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
    # BUG-1376: the host's game stood ~70 s under its evolution caption ("It is the beginning of a new day", confirmed by
    # good() above); when it goes on its game puts its family members 10-15 m away. The guest's copies must stand there
    # within seconds, not cross the gap's line over a minute (the lab's sync-check caught it only when its 30 s slot fell
    # into the few seconds before the test ended: "n: 3 apart (n15 14.6 n21 11.5 n17 10.6)", lab 20261120-133703).
    def host_runs():
        confirm_captions()
        return game_paused(host) is False
    ran = wait(host_runs, 60, 2)
    time.sleep(8)
    rounds = []
    for _ in range(3):
        rounds.append(dict(npcs_apart(host, guest)))
        time.sleep(1)
    off = sorted({k for r in rounds for k in r if all(r2.get(k, 0) > NPC_APART for r2 in rounds)})
    print(f'  the host\'s animals near it 8 s after its game went on (ran={ran}): {len(rounds[0])} compared, apart on the '
          f'guest: ' + (', '.join(f'{k} {rounds[-1][k]:.1f} m' for k in off) or 'none'), flush=True)
    ck(ran and rounds[0] and not off, f"the guest's copies of the host's animals stand where the host's game has them "
                                      f"after its caption (none over {NPC_APART:.0f} m in 3 reads)")


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
    if VOTE:  # BUG-1263: the host's Cell editor through the vote (as 0.7.15, or a host who chose "everybody votes first")
        import settings_lib
        import twoinst
        twoinst.forget_settings()
        ck(settings_lib.force(host, [guest], 'core.editor_now', 'off'), 'the guest got core.editor_now off (the Cell vote)')
    shared = '--shared' in sys.argv  # WP-D (BUG-1291): the old shared pool (all.shared_dna on)
    if shared:
        import settings_lib
        import twoinst
        twoinst.forget_settings()
        ck(settings_lib.force(host, [guest], 'all.shared_dna', 'on'), 'the guest got all.shared_dna on (one DNA pool)')
    if '--own-keep' in sys.argv:  # exploration (BUG-1255): the own editor's request bytes kept at the constructor's
        guest.call('coop.fix creations.own_editor_cancel off')
        guest.call('coop.editreq keep ' + ARG('--own-keep', '7'))
    scr = (Screens(host, 'host'), Screens(guest, 'guest', entry=True))  # BUG-1378: the guest's own load measured apart
    for x in scr:
        x.start()
    try:
        if part == 'cell':
            part_cell(host, guest, ck, ready, scr)
        elif part == 'cell-both':
            part_cell_both(host, guest, ck, ready, scr)
        elif part == 'cell-mate':
            part_cell_mate(host, guest, ck, ready, scr)
        elif part == 'cell-dna':
            part_cell_dna(host, guest, ck, ready, scr)
        elif part == 'cell-eggs':
            part_cell_eggs(host, guest, ck, ready, scr)
        else:
            part_switch(host, guest, ck, ready, scr)
    finally:
        for x in scr:
            x.stop()
        if one:
            settings_lib.reset([host], ('creature.mode',))
            twoinst.forget_settings()
        if VOTE:
            settings_lib.reset([host], ('core.editor_now',))
            twoinst.forget_settings()
        if shared:
            settings_lib.reset([host], ('all.shared_dna',))
            twoinst.forget_settings()
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
