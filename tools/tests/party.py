"""Three and four players in one test (BUG-0152; the base of the 0.8 Rivals tests, docs/11-rivals.md).

A Party is the host and the first guest of lane N (twoinst.start) plus one or two extra guests in
the same lane's bottle: instances 20+N and 30+N (profiles mp1 / mp2, empty: the host's world
arrives; coop_multi.start_extra). Every machine is a Player with a name, its console and its
instance. The helpers read each game's own state (stage, avatar, positions of the partners as that
machine's game shows them, shared resources) so a test can assert what the OTHER guest sees.

    from party import Party
    p = Party.start('creature', players=4)        # host Alice, guests Bob, Carol, Dave, all together
    p.check_together(ck, 'start')                 # every guest in the host's world, same stage, avatar
    p.check_partners(ck, 'start')                 # each guest's game shows every other player where it is
    p.check_res(ck, 'start')                      # shared food / money the same on every machine
    p.leave(p.byname('Dave')); p.join(p.byname('Dave'))
    p.vote_ready(p.byname('Carol'))               # "I'm ready" by a real click in the co-op window
    p.host_away(); p.host_back()                  # the host in the main menu and back (coop.menu / coop.load)
    p.stop()                                      # ends the lane's games (the extras share the bottle)

Tests that use it are cold (the extra games share the lane's bottle: quitting one Wine instance
ends the others; Party.start begins with a clean lane through twoinst.start).
"""
import math
import re
import subprocess
import time

import campaign_lib as cl
import lane
from coop_multi import join_cmd, my_key, sample, start_extra
from coop_ui import Ui
from twoinst import lane_state, start

VK_F9 = 0x78
# how far a partner may stand from where its own game has it, by stage (still players; Space
# puppets follow a moving ship with a delay)
NEAR = {'cell': 3.0, 'creature': 3.0, 'tribe': 3.0, 'civ': 6.0, 'space': 12.0}


class Player:
    def __init__(self, name, con, inst, join=None):
        self.name, self.con, self.inst, self.join = name, con, inst, join
        self.pid = 0 if join is None else None

    def call(self, line):
        try:
            return self.con.call(line).get('out', '')
        except OSError as e:
            return f'(no answer: {e})'

    def ok(self, line):
        try:
            return bool(self.con.call(line).get('ok'))
        except OSError:
            return False

    def __repr__(self):
        return f'{self.name}#{self.pid}'


class Party:
    def __init__(self, host, guests, fixture):
        self.host, self.guests, self.fixture = host, guests, fixture
        self.log = []

    @property
    def all(self):
        return [self.host] + self.guests

    @classmethod
    def start(cls, fixture, players=3, join_together=True, timeout=240):
        """host + first guest (twoinst.start, cold), then players-2 extra guests that join at the same
        moment (the world goes to two guests at once, #91) or one after the other"""
        assert 3 <= players <= 4
        h, g = start(fixture)
        lane_state(**{**lane_state(), 'build': ''})  # the extras share the bottle: no warm reuse of these games
        host = Player('Alice', h, lane.HOST)
        guests = [Player('Bob', g, lane.GUEST, lane.JOIN)]
        extra = start_extra(players - 2)
        for name, (con, inst) in extra.items():
            guests.append(Player(name, con, inst, join_cmd(name)))
        p = cls(host, guests, fixture)
        for gp in guests[1:]:
            gp.call(gp.join)
            if not join_together:
                p.wait_in(gp, timeout)
        p.wait_all(timeout)
        return p

    # ---------------- who is where ----------------
    def ids(self):
        got = {m.group(2): int(m.group(1)) for m in re.finditer(r'^\s*\[(\d+)\] (\S+)', self.host.call('coop.status'), re.M)}
        for gp in self.guests:
            gp.pid = got.get(gp.name, gp.pid)
        return got

    def together(self):
        return {int(m.group(1)) for m in re.finditer(r'^together (\d+) 1$', self.host.call('coop.status'), re.M)}

    def byname(self, name):
        return next(p for p in self.all if p.name == name)

    def stage(self, p):
        return cl.stage_of(p.call('game.mode'))

    def is_in(self, gp):
        """the guest plays the host's stage with an avatar and the host counts it as together (Space:
        in the session is enough, a guest may fly at another star)"""
        self.ids()
        hst = self.stage(self.host)
        return hst is not None and self.stage(gp) == hst and gp.ok('coop.avatar') and \
            (hst == 'space' or gp.pid in self.together())

    def wait_in(self, gp, timeout=240):
        """no Enter for the host's "game saved" box: the mod closes it (a press could hide a stuck
        box, BUG-0152)"""
        t0 = time.time()
        while time.time() - t0 < timeout:
            if self.is_in(gp):
                return True
            time.sleep(1)
        return False

    def wait_all(self, timeout=240):
        t0 = time.time()
        while time.time() - t0 < timeout:
            if all(self.is_in(gp) for gp in self.guests):
                return True
            time.sleep(1)
        return False

    # ---------------- checks of every machine's game ----------------
    def check_together(self, ck, label):
        bad = [gp.name for gp in self.guests if not self.is_in(gp)]
        ck(not bad, f'[{label}] every guest plays the host\'s world ({self.stage(self.host)}), not: {bad}')
        return not bad

    def own_pos(self, p):
        s = sample(p.con)
        k = my_key(p.con, s)
        return s.get(k) if k else None

    def partner_view(self, viewer, subject):
        """distance between where the subject's own game has its avatar and where the viewer's game
        shows the subject (the viewer's puppet / proxy of it, coop.sync key p<id>); None: not shown"""
        pid = 0 if subject is self.host else subject.pid
        a = self.own_pos(subject)
        b = sample(viewer.con).get(f'p{pid}')
        if a is None or b is None:
            return None
        return math.dist(a, b)

    def check_partners(self, ck, label, near=None, pairs=None):
        """every guest's game shows every other player (guests too) near where that player is"""
        self.ids()
        near = near or NEAR.get(self.stage(self.host), 6.0)
        pairs = pairs or [(v, s) for v in self.guests for s in self.all if s is not v]
        bad = []
        for v, s in pairs:
            d = None
            for _ in range(3):  # a player that moves between the two reads: read again
                d = self.partner_view(v, s)
                if d is not None and d <= near:
                    break
                time.sleep(1)
            if d is None or d > near:
                bad.append(f'{v.name} sees {s.name}: ' + ('not shown' if d is None else f'{d:.1f} m off'))
        ck(not bad, f'[{label}] each guest\'s game shows the other players where they are (<= {near} m): {bad}')
        return not bad

    def check_res(self, ck, label):
        hr = cl.res_of(self.host.call('coop.res'))
        bad = []
        for gp in self.guests:
            mm = cl.res_mismatch(hr, cl.res_of(gp.call('coop.res')))
            if mm:
                bad.append((gp.name, mm))
        ck(not bad, f'[{label}] the shared resources are the same on every machine ({hr}): {bad}')
        return not bad

    def faults(self, ck):
        for p in self.all:
            n = cl.faults_of(p.call('exc'))
            ck(n in (0, None), f'{p.name}: no caught fault ({n})')

    # ---------------- actions ----------------
    def leave(self, gp, timeout=20):
        gp.call('coop.leave')
        t0 = time.time()
        while time.time() - t0 < timeout and gp.pid in self.together():
            time.sleep(0.5)
        return gp.pid not in self.together()

    def join(self, gp, timeout=260):
        gp.call(gp.join)
        return self.wait_in(gp, timeout)

    def open_window(self, p):
        ui = Ui(p.con, p.name)
        st = ui.state()
        if st.get('open') != '1':
            p.call(f'key {VK_F9} down')
            time.sleep(0.3)
            p.call(f'key {VK_F9} up')
            st = ui.wait(lambda s: s.get('open') == '1', 6, 'the co-op window')
        return ui, st

    def vote_line(self, p):
        """the vote line of the player's own co-op window ('... - ready 2/4 - 51 s'), '' when none"""
        _, st = self.open_window(p)
        return (st or {}).get('world', '') if st and 'ready ' in (st or {}).get('world', '') else ''

    def vote_ready(self, p):
        """"I'm ready" by a real click in the player's co-op window"""
        ui, st = self.open_window(p)
        if not st or not st.get('ready', {}).get('vis'):
            return False
        ui.click('ready')
        time.sleep(1)
        return True

    def host_away(self, timeout=90):
        """the host goes to the main menu: every guest's game knows it (host_away=1)"""
        self.host.call('coop.menu')
        t0 = time.time()
        while time.time() - t0 < timeout:
            if all('host_away=1' in gp.call('coop.status') for gp in self.guests):
                return True
            time.sleep(1)
        return False

    def host_back(self, timeout=200):
        """the host loads its world again (the lane's current slot): every guest is back with it"""
        st = lane_state()
        slot, name = (st.get('planet') or 'Game0/x').split('/')
        if st.get('slot', 9) != 9:
            slot = f'Game{st["slot"]}'
        self.host.call(f'coop.load {slot} {name}')
        t0 = time.time()
        while time.time() - t0 < timeout:
            if all('host_away=0' in gp.call('coop.status') for gp in self.guests) and self.wait_all(5):
                return True
            time.sleep(2)
        return False

    def stop(self):
        """end the lane's games (all four share the bottle)"""
        subprocess.run([lane.SPORE, 'kill'], capture_output=True)
