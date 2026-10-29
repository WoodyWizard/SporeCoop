#!/usr/bin/env python3
"""BUG-0258: after the live Creature -> Tribe change the guest is together with the host.

The players' way from the `creature` fixture: the host's evolution bar full (AddEvolutionPoints,
the only shortcut), History and its check (the next stage), the evolution vote with the guest's
real "I'm ready", "done" in the editors, the captions of the fire cinematic. The host enters the
Tribe stage and sends its world; the guest loads it.

After the game's own stage change the game's save name (CurrentGameName) reads "snapshot.spo"
until the next save. The place id of a player was the crc32 of that name, so the host was in
"snapshot.spo", the guest (who loaded the planet's file) in "<planet>.spo": `together 1 0`, no
puppets, for good. Checked: the host's game name was "snapshot.spo" at some time after the change
(else the run proves nothing and fails), and then, within 120 s of the guest's load:
  * the host's place equals the guest's, `together 1 1`;
  * the host has a puppet of the guest (`remote 1 ... puppet=<not null>`), the guest's log says
    "Alice appeared in the world" after its load.

  SPORE_LANE=3 python3 tools/tests/coop_tribe_snapshot.py [--nofix]
  (--nofix: `coop.fix world.snapshot_name off` on both games: must FAIL)
"""
import argparse
import re
import time

from coop_ui import Ui
from twoinst import Checks, shot, start

CAPTION_OK = (1410, 1048)   # the green check of a caption that waits for the player
HISTORY_BTN = (1400, 1062)  # the HUD button under the portrait: History
HISTORY_OK = (1415, 1055)   # its check: starts the next stage when the bar is full
EDITOR_DONE = (1416, 1062)  # the green "done" of the editors (1440x1080)
VK_F9 = '0x78'


def out(con, line):
    try:
        r = con.call(line)
        return r.get('out', '') if isinstance(r, dict) else str(r)
    except (OSError, ValueError):
        return ''


def click(con, xy):
    con.call(f'move {xy[0]} {xy[1]}')
    time.sleep(0.4)
    con.call(f'click {xy[0]} {xy[1]}')


def avatar(con):
    a = out(con, 'coop.avatar')
    w = re.search(r"place=0x([0-9A-F]{8}) world '([^']*)'", a)
    g = re.search(r"game_name '([^']*)'", a)
    return (w.group(1) if w else None, w.group(2) if w else None, g.group(1) if g else None)


class Player:
    def __init__(self, con, name):
        self.con, self.name = con, name
        self.cap = None  # (since, last click)

    def service(self, host_con):
        """captions, the vote, the editor: what a player answers"""
        con = self.con
        mode = out(con, 'game.mode')
        if 'Editor' in mode:
            time.sleep(3)
            click(con, EDITOR_DONE)
            return 'editor done'
        if self.name != 'host' and out(host_con, 'coop.vote').startswith('vote active'):
            ui = Ui(con, self.name)
            st = ui.state()
            if st.get('open') != '1':  # F9 opens the co-op window (a player's way to "I'm ready")
                con.call(f'key {VK_F9} down')
                time.sleep(0.3)
                con.call(f'key {VK_F9} up')
                st = ui.wait(lambda s: s.get('open') == '1', 6, 'the co-op window') or {}
            if st.get('ready', {}).get('vis'):
                ui.click('ready')
                time.sleep(1)
                return 'ready'
        if 'movie=1' in out(con, 'coop.status'):
            t = time.time()
            since, last = self.cap or (t, 0.0)
            self.cap = (since, last)
            if t - since >= 6 and t - last >= 10:
                self.cap = (since, t)
                click(con, CAPTION_OK)
                return 'caption ok'
            return None
        self.cap = None
        return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nofix', action='store_true')
    a = ap.parse_args()
    host, guest = start('creature')
    ck = Checks()
    if a.nofix:
        for con in (host, guest):
            print('  ', out(con, 'coop.fix world.snapshot_name off').strip())
    players = [Player(host, 'host'), Player(guest, 'guest')]
    t0 = time.time()
    host.call('call Simulator::cCreatureGameData::AddEvolutionPoints cdecl f:3000')
    last_kick = time.time()
    snapshot_seen = False
    tribe_at = None
    while time.time() - t0 < 600:
        h_mode = out(host, 'game.mode')
        if 'Tribe' in h_mode:
            tribe_at = tribe_at or time.time()
            break
        busy = False
        for p in players:
            did = p.service(host)
            if did:
                print(f'   {time.time() - t0:5.0f}s {p.name}: {did}', flush=True)
                busy = True
        busy |= 'movie=1' in out(host, 'coop.status') or out(host, 'coop.vote').startswith('vote active')
        if not busy and 'Creature' in h_mode and time.time() - last_kick > 25:
            last_kick = time.time()
            click(host, HISTORY_BTN)
            time.sleep(3)
            click(host, HISTORY_OK)
            print(f'   {time.time() - t0:5.0f}s host: History and its check', flush=True)
        time.sleep(2)
    if not ck(tribe_at, f'the host entered the Tribe stage ({time.time() - t0:.0f} s)'):
        return ck.result()
    # the Tribe intro: both play on, answer captions; the guest loads the host's world
    together_since = None
    g_log0 = None
    t1 = time.time()
    last_print = 0
    while time.time() - t1 < 360:
        for p in players:
            p.service(host)
        hp, hw, hg = avatar(host)
        gp, gw, gg = avatar(guest)
        snapshot_seen |= hg == 'snapshot.spo'
        st = out(host, 'coop.status')
        tog = re.search(r'^together 1 1$', st, re.M) is not None
        g_tribe = 'Tribe' in out(guest, 'game.mode') and gp is not None
        if time.time() - last_print > 15:
            last_print = time.time()
            print(f'   +{time.time() - t1:4.0f}s host place {hp} {hw!r} (game {hg!r}); guest place {gp} {gw!r}; '
                  f'together={int(tog)}', flush=True)
        if g_tribe and g_log0 is None:
            g_log0 = time.time()
        # done when the host's name was snapshot.spo and the guest played Tribe for 120 s (or together)
        if snapshot_seen and g_log0 and (tog and together_since and time.time() - together_since > 20):
            break
        if snapshot_seen and g_log0 and time.time() - g_log0 > 120:
            break
        together_since = (together_since or time.time()) if tog else None
        time.sleep(3)
    hp, hw, hg = avatar(host)
    gp, gw, gg = avatar(guest)
    print(f'   host: place {hp} world {hw!r} game name {hg!r}; guest: place {gp} world {gw!r} game name {gg!r}')
    ck(snapshot_seen, 'the host\'s game name read "snapshot.spo" after the stage change (the condition of BUG-0258)')
    ck(g_log0, 'the guest plays the Tribe stage in the host\'s world')
    ck(hp and hp == gp, f'the host\'s place is the guest\'s ({hp} / {gp})')
    st = out(host, 'coop.status')
    ck(re.search(r'^together 1 1$', st, re.M), 'the host is together with the guest')
    pup = re.search(r"^remote 1 '[^']*' puppet=([0-9A-Fa-f]+)", st, re.M)
    ck(pup and int(pup.group(1), 16) != 0, f'the host has a puppet of the guest ({pup.group(1) if pup else None})')
    glog = out(guest, 'log 400')
    tail = glog[glog.rfind('world: loading '):] if 'world: loading ' in glog else ''
    ck('Alice appeared in the world' in tail or 'host appeared in the world' in tail.lower(),
       'the guest sees the host appear after its load')
    shot(host, 'tribe_snapshot_host.png')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
