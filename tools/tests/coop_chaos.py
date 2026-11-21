#!/usr/bin/env python3
"""Chaos bot ("monkey"): both players do random but valid things for a few minutes, mostly through
real input (clicks on units - the partner's ones too, boxes, orders, keys, the co-op window), plus
the stage's co-op actions and network churn (the guest leaves and joins again, the host saves).
The invariants of twoinst (freeze, crash) and the fault logger decide; on a failure the last
actions and the seed are printed: the same seed replays the same actions.

  coop_chaos.py [--fixture tribe] [--minutes 3] [--seed N] [--rate 3]
"""
import argparse
import random
import re
import sys
import time

import lane
from twoinst import Checks, Monitor, shot, start, wait_guest_in

W, H = 1440, 1080
WORLD = (120, 120, W - 120, H - 200)  # the world view, away from the HUD at the edges
KEYS = ['1', '2', '3', '4', '5', 'space', 'tab', 'w', 'a', 's', 'd', 'q', 'e', 'c', 'v', 'home']
STAGE_CMDS = {
    'tribe': ['coop.give', 'coop.ping'],
    'civ': ['coop.give', 'coop.role {r}', 'coop.ping'],
    'creature': ['coop.team point', 'coop.ping'],
    'cell': ['coop.pool bite'],
    'space': ['coop.ping'],
}


def key_arg(k):
    return f'0x{ord(k.upper()):02X}' if len(k) == 1 else k


class Bot:
    def __init__(self, host, guest, fixture, seed, rate, extra=None, joins=None, ids=None, back_in=None, churn=1):
        """extra: more players {name: console} (coop_multi.py); joins: {name: join command};
        ids: {name: player id}; back_in(name) -> True when a rejoined player is back in the world"""
        self.c = {'host': host, 'guest': guest, **(extra or {})}
        self.join_cmd = joins or {'guest': lane.JOIN}
        self.ids = ids or {'host': 0, 'guest': 1}
        self.back_in = back_in or (lambda name: wait_guest_in(self.c['host'], self.c[name], 90))
        # action weights; "pause" is off: random clicks into an open pause menu quit the game
        # (a tribe host shut down this way, 2026-10-23); play_creature.py drives that menu properly
        self.weights = dict(target=30, order=15, click=12, box=8, key=12, wheel=5, ui=4, pause=0, stage=8, churn=1,
                            save=1, outage=1)
        self.fixture, self.rng, self.rate = fixture, random.Random(seed), rate
        self.log = []
        self.joins = 0
        self.count = {}
        self.weights['churn'] = churn  # weight of rejoins among the actions (1 of 99 by default)

    def do(self, who, line):
        self.log.append(f'{time.strftime("%H:%M:%S")} {who}: {line}')
        r = self.c[who].call(line)
        return r.get('out', '')

    def pt(self):
        return self.rng.randint(WORLD[0], WORLD[2]), self.rng.randint(WORLD[1], WORLD[3])

    def targets(self, who):
        out = []
        for line in self.do(who, 'coop.targets').splitlines():
            m = re.match(r'(\w+) (-?\d+) (-?\d+) (-?\d+) (-?\d+)', line)
            if m and 0 < int(m.group(4)) < W and 0 < int(m.group(5)) < H - 150:
                out.append((m.group(1), int(m.group(3)), int(m.group(4)), int(m.group(5))))
        return out

    def leave_editor(self, who):
        """a random key or click can open an editor (Civ: a design editor, "Bob edits a design"); a guest that
        joins in it gets the host's world when it leaves the editor (by design: "the world of Alice is loaded
        when you leave the editor"), so it leaves like a player: "done", the dice's name when one is asked
        (chaos_civ_long, lab 20261101-134020: the guest waited in its editor). The host too: its own evolution
        editor opens at once (core.editor_now) after a mating the bot's clicks started, and no guest is in its world
        until it is done (multi3, lab 20261120-133703, BUG-1367). BUG-1374: since 0.7.16 a guest's palette "new
        design" opens its own design editor with an empty creation (lab 20261120-133703: `civedit: our design editor
        request ... starts from 00000000`), which "done" never leaves: from the second try three parts go in first,
        as coop_civ_guest_editors' driver does. The mode is read without a line in the log (this runs on every poll
        of coop_multi's waits)"""
        for i in range(6):
            if 'Editor' not in self.c[who].call('game.mode').get('out', ''):
                return
            # (here: coop_campaign imports coop_multi, which imports this module)
            from coop_campaign import EDITOR_DICE, EDITOR_DONE, TRIBE_EDITOR_PARTS, drag_slow, editor_click
            time.sleep(2)
            con = self.c[who]
            if i in (1, 4):  # an empty creation: three parts (City Hall / vehicle / building palettes)
                for (sx, sy), (tx, ty) in TRIBE_EDITOR_PARTS:
                    drag_slow(con, sx, sy, tx, ty, 1.0 + i // 3)
                self.log.append(f'{time.strftime("%H:%M:%S")} {who}: three parts into its editor')
            if i >= 2:
                editor_click(con, *EDITOR_DICE)  # the name panel's dice
            editor_click(con, *EDITOR_DONE)  # the editors' green "done"
            self.log.append(f'{time.strftime("%H:%M:%S")} {who}: leaves its editor ("done")')
            time.sleep(3)
        if 'Editor' in self.c[who].call('game.mode').get('out', ''):
            shot(self.c[who], f'chaos_{who}_editor_stuck.png')  # (what kept it there)

    def leave_editors(self):
        """every player that sits in an editor leaves it ("done"), the host first (BUG-1367)"""
        for who in self.c:
            self.leave_editor(who)

    def rejoin(self, g):
        """the guest g leaves and joins again; False when it is not back in the host's world"""
        self.do(g, 'coop.leave')
        time.sleep(self.rng.uniform(1, 4))
        self.do(g, self.join_cmd[g])
        self.joins += 1
        self.leave_editor(g)
        if not self.back_in(g):
            self.log.append(f'  !! {g} did not get back into the host world')
            return False
        return True

    def step(self):
        rng = self.rng
        who = rng.choice(list(self.c))
        kind = rng.choices(list(self.weights), weights=list(self.weights.values()))[0]
        self.count[kind] = self.count.get(kind, 0) + 1
        if kind == 'target':  # click a unit: own, the partner's, the partner itself
            t = self.targets(who)
            if t:
                k, owner, x, y = rng.choice(t)
                mine = owner == self.ids.get(who, -1)
                key = f'{k}:{"own" if mine else "partner"}'
                self.count[key] = self.count.get(key, 0) + 1
                self.do(who, f'click {x} {y}' + (' right' if rng.random() < 0.3 else ''))
        elif kind == 'order':
            x, y = self.pt()
            self.do(who, f'click {x} {y} right')
        elif kind == 'click':
            x, y = self.pt()
            self.do(who, f'click {x} {y}')
        elif kind == 'box':
            (x1, y1), (x2, y2) = self.pt(), self.pt()
            self.do(who, f'drag {x1} {y1} {x2} {y2}')
        elif kind == 'key':
            self.do(who, 'key ' + key_arg(rng.choice(KEYS)))
        elif kind == 'wheel':
            x, y = self.pt()
            self.do(who, f'wheel {x} {y} {rng.choice([-3, -1, 1, 3])}')
        elif kind == 'ui':  # the co-op window open, a look, closed again
            self.do(who, 'coop.uistate 1')
            time.sleep(rng.uniform(0.3, 1.5))
            self.do(who, 'coop.uistate 0')
        elif kind == 'pause':  # the pause menu, closed right away
            self.do(who, 'key esc')
            time.sleep(0.8)
            self.do(who, 'key esc')
        elif kind == 'stage':
            cmds = STAGE_CMDS.get(self.fixture, [])
            if cmds:
                self.do(who, rng.choice(cmds).format(r=rng.randint(0, 3)))
        elif kind == 'churn':  # a guest leaves and joins again
            if not self.rejoin(rng.choice([n for n in self.c if n != 'host'])):
                return False
        elif kind == 'outage':  # bad network only: the link drops for a few seconds
            import netem
            if netem.proxy():
                netem.proxy().outage(rng.uniform(2, 8))
                self.log.append(f'{time.strftime("%H:%M:%S")} net: outage')
        elif kind == 'save':
            self.do('host', 'coop.save')
            time.sleep(2)
            self.do('host', 'key enter')
        time.sleep(rng.expovariate(self.rate))
        return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--fixture', default='tribe')
    ap.add_argument('--minutes', type=float, default=3)
    ap.add_argument('--seed', type=int, default=None)
    ap.add_argument('--rate', type=float, default=3.0, help='actions per second (mean)')
    ap.add_argument('--churn', type=float, default=1, help='weight of rejoins among the actions (others: 98)')
    a = ap.parse_args()
    seed = a.seed if a.seed is not None else random.randrange(1 << 30)
    print(f'  chaos: fixture {a.fixture}, {a.minutes} min, seed {seed}', flush=True)
    host, guest = start(a.fixture)
    ck = Checks()
    bot = Bot(host, guest, a.fixture, seed, a.rate, churn=a.churn)
    t0, n, ok = time.time(), 0, True
    try:
        while time.time() - t0 < a.minutes * 60 and ok:
            ok = bot.step()
            n += 1
            if Monitor.events:
                ok = False
    except (OSError, ValueError) as e:
        bot.log.append(f'  !! console error: {e!r}')
        ok = False
    import netem
    if netem.proxy():
        print(' ', netem.proxy().describe(), flush=True)
    print(f'  {n} actions in {time.time() - t0:.0f} s, {bot.joins} rejoins:',
          ' '.join(f'{k}={v}' for k, v in sorted(bot.count.items())), flush=True)
    ck(ok, 'the games survived the chaos')
    if not ok or Monitor.events:
        print('  last actions:')
        for line in bot.log[-30:]:
            print('   ', line)
        print(f'  replay: SPORE_LANE={lane.LANE} python3 tools/tests/coop_chaos.py --fixture {a.fixture} '
              f'--minutes {a.minutes} --seed {seed} --churn {a.churn}')
    try:
        shot(host, f'chaos_{a.fixture}_host.png')
        shot(guest, f'chaos_{a.fixture}_guest.png')
        ck.faults(host=host, guest=guest)
    except (OSError, ValueError) as e:
        ck(False, f'the games answer after the chaos ({e!r})')
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
