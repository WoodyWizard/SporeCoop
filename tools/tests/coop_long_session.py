#!/usr/bin/env python3
"""A long co-op session of one stage with the transitions players go through, the divergence detector
sampled all along (D0 of docs/plan-0.7.7.md; docs/06-testing.md#testing-long-session).

The play itself is the stage's play bot (play_creature.Player threads / play_tribe.Game: real input, two
friends playing), cut into segments; between two segments one transition of the guest:

  death     Creature: the guest's creature takes a lethal hit (`coop.team die`) and respawns;
            Tribe: the guest's chief leaves the tribe at once (`coop.tribe drop_chief 1` on the host: the
            game's RemoveMember, the way a killed chief goes; the game makes a new one later)
  editor    the host's evolution vote (`coop.vote editor`, the guest's "I'm ready" click), the editor left
            with the red X; the host shares its world again and the guest loads it
  menu      the guest leaves to the main menu through the pause menu (Exit to Main Menu, Don't Save x2)
            and the host's world comes back
  restart   the guest's game is killed (as a crash or a player closing it), started again, joins the
            host and loads its world
  rejoin    the guest leaves the session (`coop.leave`) and joins again

After every transition (20 s later) and every SPORE_DETECTOR seconds during the play (the monitor) the
detector compares the two games: the area around the guest, the world state and the whole planet
(worlddiff.py, `coop.wstate`). The test FAILS only on faults, crashes, freezes and a guest that does not
come back from a transition; every divergence is a DETECTOR row for tools/detector_report.py, no gate.

  SPORE_DETECTOR=20 SPORE_LANE=2 python3 tools/tests/coop_long_session.py --stage tribe [--minutes 40]
      [--events death,editor,menu,restart,rejoin] [--seed N]
"""
import argparse
import json
import random
import re
import sys
import time

import lane
import twoinst
import uifind
import worlddiff
from coop_creature_desync import kill_game
from twoinst import Checks, Console, Monitor, start, wait_guest_in, wait_up, watch

FIXTURE = {'creature': 'creature', 'tribe': 'tribe'}
STAGE_MODE = {'creature': 'Creature', 'tribe': 'Tribe'}
EVENTS = ['death', 'editor', 'menu', 'restart', 'rejoin']
CANCEL = (1380, 1062)  # the red X of the editor (1440x1080 test window), coop_tribe_editor_world.py


def mode(con):
    try:
        return con.call('game.mode').get('out', '')
    except (OSError, ValueError):
        return ''


def sample(host, guest, after):
    """one detector comparison now, printed like the monitor's (monitor.Watch.detect) with `after`"""
    try:
        hw, gw = worlddiff.pair_dump(host, guest, 250, state=True)
    except (OSError, ValueError) as e:
        print(f'  (detector after {after}: dump failed: {e!r})', flush=True)
        return []
    found = worlddiff.diff(hw, gw)
    skip = [f for f in found if f['sig'] in ('apart', 'nocentre')]
    real = [f for f in found if f['sig'] not in ('apart', 'nocentre')]
    print('DETECTOR-SAMPLE ' + json.dumps(dict(guest='guest', stage=hw.mode, gstage=gw.mode, host_rows=len(hw.rows),
                                               guest_rows=len(gw.rows), findings=len(real), after=after,
                                               skipped=skip[0]['sig'] if skip else '')), flush=True)
    for f in real:
        print('DETECTOR ' + json.dumps(dict(guest='guest', stage=f['stage'], sig=f['sig'], n=f['n'], text=f['text'],
                                            after=after)), flush=True)
    return real


def busy_ok(fn, *a):
    """BUG-0430 (long_creature_short, rel/0.7.8 lab 20261105-123757): a console call into a game that loads a world
    can time out (the restarted guest took 52 s to load the shared world and played on; the first TimeoutError ended
    the transition). A timeout or a closed console counts as "not yet" here; the caller's deadline decides."""
    try:
        return fn(*a)
    except (OSError, ValueError) as e:
        print(f'  (console busy: {e!r}; waiting on)', flush=True)
        return None


class Session:
    def __init__(self, stage, seed):
        self.stage, self.seed = stage, seed
        self.rng = random.Random(seed)
        self.host, self.guest = start(FIXTURE[stage])
        self.played = 0.0
        self.notes = []
        self.game = None
        if stage == 'tribe':
            print('  setup:', self.host.call('coop.tribe spawn 3')['out'].strip(), flush=True)
            time.sleep(10)

    def in_stage(self, con):
        return STAGE_MODE[self.stage] in mode(con)

    def together(self):
        """both play the stage in the same world, the host sees the guest"""
        if not (self.in_stage(self.host) and self.in_stage(self.guest)):
            return False
        if self.stage == 'creature' and not self.guest.call('coop.avatar')['ok']:
            return False
        m = re.search(r'client id=(\d+) connected', self.guest.call('coop.status').get('out', ''))
        return f'together {m.group(1) if m else 1} 1' in self.host.call('coop.status')['out']

    def wait_together(self, timeout, what):
        t0 = time.time()
        while time.time() - t0 < timeout:
            if busy_ok(self.together):
                print(f'  {what}: together again after {time.time() - t0:.0f} s', flush=True)
                return True
            f1 = busy_ok(twoinst.host_frame, self.host)
            time.sleep(1.5)
            if f1 is not None and busy_ok(twoinst.host_frame, self.host) == f1:
                busy_ok(self.host.call, 'key enter')  # the host's "game saved" box after a share
        print(f'  {what}: NOT together after {timeout} s (host {mode(self.host).strip()}, guest {mode(self.guest).strip()})',
              flush=True)
        return False

    # ---------------- play ----------------
    def play(self, minutes):
        t0 = time.time()
        if self.stage == 'creature':
            self.play_creature(minutes)
        else:
            self.play_tribe(minutes)
        self.played += time.time() - t0
        print(f'  played {(time.time() - t0) / 60:.1f} min (total {self.played / 60:.1f})', flush=True)

    def play_creature(self, minutes):
        import play_creature as pc
        world, sh = pc.World(lane.HOST), pc.Shared()
        sh.exit_result = None
        deadline = time.time() + minutes * 60
        s = self.rng.randrange(1 << 30)
        players = [pc.Player('host', lane.HOST, 1, world, sh, s, deadline),
                   pc.Player('guest', lane.GUEST, 0, world, sh, s + 1, deadline)]
        for p in players:
            p.start()
        for p in players:
            p.join(minutes * 60 + 120)
            if p.error:
                self.notes.append(f'{p.name} bot: {p.error}')
        print('  actions:', ' '.join(f'{k}={v}' for k, v in sorted(sh.c.items())), flush=True)

    def play_tribe(self, minutes):
        import play_tribe as pt
        if self.game is None or self.game.guest is not self.guest:
            self.game = pt.Game(self.host, self.guest, self.rng.randrange(1 << 30), 1.0)
            for p in self.game.p:
                p.calibrate()
        g, t0, n, last = self.game, time.time(), 0, 0
        try:
            while time.time() - t0 < minutes * 60 and not Monitor.events:
                g.step(g.p[n % 2])
                n += 1
                if time.time() - last > 10:
                    last = time.time()
                    for p in g.p:
                        g.check_orders(p)
                    g.measure()
        except (OSError, ValueError) as e:
            self.notes.append(f'tribe bot: console error {e!r}')
        print(f'  {n} actions; food {g.food[-1][1] if g.food else "?"}; orders '
              + ', '.join(f'{p.name} {p.orders}' for p in g.p), flush=True)

    def leave_editors(self):
        """an editor the play opened (the creature bots' own evolution vote ends in the host's editor when its time is
        up, lab 20261104-210208: the host stayed there and the next transition waited on it): the red X, both games"""
        # a vote the bots started in the last seconds of the play still ends in the host's editor (lab
        # 20261105-015350: it passed 1/1 when the guest left for the restart and the host stayed in the editor)
        t0 = time.time()
        while time.time() - t0 < 90 and 'vote active' in self.host.call('coop.vote')['out']:
            time.sleep(2)
        if time.time() - t0 > 1:
            time.sleep(5)
        left = False
        for _ in range(3):
            busy = [c for c in (self.host, self.guest) if 'Editor' in mode(c)]
            if not busy:
                break
            for c in busy:
                c.call('wait.frames 200 60000')
                c.call('move %d %d' % CANCEL)
                time.sleep(0.5)
                c.call('click %d %d' % CANCEL)
            left = True
            time.sleep(10)
        if left:
            print('  an editor of the play left with the red X', flush=True)
            self.wait_together(240, 'after the play\'s editor')

    # ---------------- transitions of the guest ----------------
    def death(self):
        if self.stage == 'creature':
            print('  guest:', self.guest.call('coop.team die')['out'].strip(), flush=True)
            t0 = time.time()
            while time.time() - t0 < 90:
                time.sleep(2)
                self.guest.call('key enter')  # the death caption's OK
                if self.guest.call('coop.avatar')['ok'] and time.time() - t0 > 8:
                    break
            return self.wait_together(120, 'death')
        print('  host:', self.host.call('coop.tribe drop_chief 1')['out'].strip(), flush=True)
        time.sleep(5)
        return self.wait_together(60, 'death')

    def editor(self):
        from coop_ui import Ui
        m0 = mode(self.host)
        print(' ', self.host.call('coop.vote editor')['out'].strip().splitlines()[0], flush=True)
        time.sleep(2)
        gu = Ui(self.guest, 'guest')
        if gu.wait(lambda s: s['ready']['vis'], 10, 'ready button'):
            gu.click('ready')
        t0 = time.time()
        while time.time() - t0 < 90 and mode(self.host) == m0:
            time.sleep(1)
        if mode(self.host) == m0:
            self.notes.append('editor: the host\'s editor did not open')
            print('  editor: the host\'s editor did not open; on with the play', flush=True)
            self.host.call('coop.vote cancel')
            return True
        print(f'  editor: the host is in {mode(self.host).strip()} ({time.time() - t0:.0f} s)', flush=True)
        time.sleep(5)
        self.host.call('wait.frames 200 60000')
        self.host.call('move %d %d' % CANCEL)
        time.sleep(0.5)
        self.host.call('click %d %d' % CANCEL)
        # the host back in the stage, then its world shared again: the guest loads it (BUG-0517 path)
        t0 = time.time()
        while time.time() - t0 < 90 and not self.in_stage(self.host):
            time.sleep(1)
        # Creature: the vote opened the guest's own editor too (lab 20261104-203017: the guest stayed in it):
        # the guest leaves it the same way
        for _ in range(3):
            if 'Editor' not in mode(self.guest):
                break
            print('  editor: the guest is in its editor: the red X there too', flush=True)
            self.guest.call('wait.frames 200 60000')
            self.guest.call('move %d %d' % CANCEL)
            time.sleep(0.5)
            self.guest.call('click %d %d' % CANCEL)
            time.sleep(10)
        t0, loaded = time.time(), False
        while time.time() - t0 < 90 and not loaded:
            loaded = not self.in_stage(self.guest)
            f1 = twoinst.host_frame(self.host)
            time.sleep(1)
            if twoinst.host_frame(self.host) == f1:
                self.host.call('key enter')  # the "game saved" box of the share
        print(f'  editor: the host is back; the guest loaded the shared world: {loaded} ({time.time() - t0:.0f} s)',
              flush=True)
        if not loaded:
            self.notes.append('editor: no world load of the guest seen within 90 s after the editor')
        return self.wait_together(240, 'editor')

    def menu(self):
        g = self.guest
        if 'open=1' in g.call('coop.uistate').get('out', ''):
            g.call('key f9')
        for _ in range(3):
            g.call('key esc')
            time.sleep(1.2)
            if uifind.shown(g, 'Resume Game'):
                break
        ok = uifind.press(g, 'Exit to Main Menu', 'Do you want to save your game?') \
            and uifind.press(g, "Don't Save", 'Are you sure?') and uifind.press(g, "Don't Save")
        if not ok:
            self.notes.append('menu: the pause menu path failed')
            print('  menu: the pause menu path failed; on with the play', flush=True)
            for _ in range(2):
                g.call('key esc')
                time.sleep(1)
            return True
        t0, seen = time.time(), False
        while time.time() - t0 < 60 and not seen:  # (mode() is '' on a console timeout: waits on)
            seen = 'GalaxyGameEntry' in mode(g)
            time.sleep(1)
        print(f'  menu: the guest in the main menu: {seen}', flush=True)
        return self.wait_together(240, 'menu')

    def restart(self):
        # a player closing its game: the session left first (a crash, the guest gone without a word, is
        # coop_creature_desync's; here the host's puppet rule would count the seconds until the timeout)
        self.guest.call('coop.leave')
        time.sleep(2)
        for m in twoinst._monitors:
            m.stop = True
        kill_game(lane.GUEST)
        time.sleep(3)
        lane.run_guest()
        if not wait_up(lane.GUEST, 90):
            lane.run_guest()
            if not wait_up(lane.GUEST):
                print('  restart: the guest did not start', flush=True)
                return False
        self.guest = Console(lane.GUEST)
        t0 = time.time()
        while time.time() - t0 < 120 and 'GalaxyGameEntry' not in mode(self.guest):
            time.sleep(2)
        busy_ok(self.guest.call, 'wait.frames 200 60000')
        if busy_ok(self.guest.call, lane.JOIN) is None:  # (a join the console did not answer: once more)
            busy_ok(self.guest.call, lane.JOIN)
        ok, t0 = False, time.time()
        while not ok and time.time() - t0 < 240:  # through console timeouts while the guest loads the world
            ok = busy_ok(wait_guest_in, self.host, self.guest, max(10, 240 - (time.time() - t0)))
        watch(lane.HOST, lane.GUEST)
        return bool(ok) and self.wait_together(120, 'restart')

    def rejoin(self):
        self.guest.call('coop.leave')
        time.sleep(4)
        r = busy_ok(self.guest.call, lane.JOIN)
        print('  rejoin:', (r or {}).get('out', '(no answer)').strip()[:60], flush=True)
        time.sleep(10)
        return self.wait_together(240, 'rejoin')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--stage', choices=sorted(FIXTURE), default='tribe')
    ap.add_argument('--minutes', type=float, default=40, help='minutes of play (the transitions come on top)')
    ap.add_argument('--events', default=','.join(EVENTS))
    ap.add_argument('--seed', type=int, default=None)
    a = ap.parse_args()
    events = [e for e in a.events.split(',') if e]
    bad = [e for e in events if e not in EVENTS]
    if bad:
        ap.error(f'unknown events {bad}')
    seed = a.seed if a.seed is not None else random.randrange(1 << 30)
    print(f'  long session: {a.stage}, {a.minutes} min of play, events {events}, seed {seed}', flush=True)
    t_start = time.time()
    s = Session(a.stage, seed)
    ck = Checks()
    seg = a.minutes / (len(events) + 1)
    sample(s.host, s.guest, 'start')
    ok = True
    for ev in events + [None]:
        s.play(seg)
        s.leave_editors()
        if Monitor.events:
            ok = False
            break
        if ev is None:
            break
        print(f'  --- {ev} ({(time.time() - t_start) / 60:.1f} min into the session)', flush=True)
        t0 = time.time()
        try:
            back = getattr(s, ev)()
        except (OSError, ValueError) as e:
            back = False
            print(f'  {ev}: console error {e!r}', flush=True)
        ck(back, f'{ev}: the guest plays the host\'s world again ({time.time() - t0:.0f} s)')
        if not back:
            ok = False
            break
        time.sleep(20)
        sample(s.host, s.guest, ev)
    sample(s.host, s.guest, 'end')
    for n in s.notes:
        print('  note:', n, flush=True)
    print(f'  session: {(time.time() - t_start) / 60:.1f} min, {s.played / 60:.1f} min of play', flush=True)
    ck(ok, 'the games survived the session')
    try:
        ck.faults(host=s.host, guest=s.guest)
    except (OSError, ValueError) as e:
        ck(False, f'the games answer after the session ({e!r})')
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
