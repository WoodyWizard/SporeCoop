#!/usr/bin/env python3
"""BUG-0271: the host finishes the Cell stage while the consoles keep asking about the players.

4-player campaign_short (lab 20261029-111744): the host faulted at coop.dll+0x88951 =
modes::cell_fill ~17 s after History's check, while the game went Cell -> Creature. On that way the
game frees the cell pool's memory while mode_id() still reads Cell, and our console commands run from
the game's message pump inside that teardown (between our frames): `coop.status` read the partner's
puppet cell (and `coop.avatar` / `coop.sync` the local cell) from memory that was already gone.

  1. Cell: host and guest play together (the guest's cell is a puppet on the host).
  2. Pollers ask both games all the time (`coop.status`, `coop.avatar`, `coop.sync`), as the
     campaign's loop and services do, while the host ends the Cell stage the players' way (food bar
     full, the captions, History, its check) and both get into the Creature stage.
  3. No game faults or dies; `coop.cellgone` shows how often the pool block was gone in the window.

  coop_cell_evolve_poll.py   (SPORE_FIX_OFF=core.stale_local must FAIL)
"""
import re
import threading
import time

import lane
from twoinst import Checks, Console, Monitor, alive, start

STAGE_BTN = (1396, 1063)  # the cell HUD button under the portrait: History, then the next stage
HISTORY_OK = (1415, 1055)
CAPTION_OK = (1410, 1048)
POLL = ('coop.status', 'coop.avatar', 'coop.sync')


def mode(con):
    return con.call('game.mode')['out']


def wait(cond, secs, step=1.0):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return None


def gone(con):
    out = con.call('coop.cellgone')['out']
    return {k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', out)}


class Poller(threading.Thread):
    """one console connection asking one game the same questions as fast as it answers"""

    def __init__(self, inst, name, cmds):
        super().__init__(daemon=True)
        self.inst, self.name, self.cmds = inst, name, cmds
        self.stop = False
        self.calls = 0
        self.errors = 0

    def run(self):
        con = None
        while not self.stop:
            try:
                con = con or Console(self.inst, timeout=30)
                for c in self.cmds:
                    con.call(c)
                    self.calls += 1
            except (OSError, ValueError):
                self.errors += 1
                con = None
                time.sleep(1)
            time.sleep(0.02)


def main():
    host, guest = start('cell')
    ck = Checks()
    ck(wait(lambda: 'Cell' in mode(host) and 'Cell' in mode(guest), 30), 'both play the Cell stage')
    time.sleep(5)
    s0 = {'host': gone(host), 'guest': gone(guest)}

    pollers = [Poller(lane.HOST, 'host', POLL[:1]), Poller(lane.HOST, 'host', POLL[1:]),
               Poller(lane.GUEST, 'guest', POLL[:1]), Poller(lane.GUEST, 'guest', POLL[1:])]
    for p in pollers:
        p.start()
    try:
        # the end of the Cell stage, the players' way (coop_cell_leave.py step 2)
        host.call('coop.pool food 1000')
        t0 = time.time()
        while time.time() - t0 < 150 and 'Creature' not in mode(host):
            if not alive(lane.HOST):
                break
            if 'cine=1' in host.call('coop.pool')['out']:
                host.call('click %d %d' % CAPTION_OK)
                time.sleep(3)
                continue
            if time.time() - t0 > 25:  # the cinematic is over: History, then its check = next stage
                host.call('click %d %d' % STAGE_BTN)
                time.sleep(3)
                host.call('click %d %d' % HISTORY_OK)
            time.sleep(4)
        ck('Creature' in mode(host), f'the host entered the Creature stage ({mode(host).strip()})')
        ck(wait(lambda: 'Creature' in mode(guest) and guest.call('coop.avatar')['ok'], 120),
           f'the guest follows into the Creature stage ({mode(guest).strip()})')
        time.sleep(5)
    finally:
        for p in pollers:
            p.stop = True
        for p in pollers:
            p.join(40)
    for p in pollers:
        print(f'  poller {p.name} {"/".join(p.cmds)}: {p.calls} calls, {p.errors} errors', flush=True)
    for name, con in (('host', host), ('guest', guest)):
        if alive(lane.HOST if name == 'host' else lane.GUEST):
            s1 = gone(con)
            d = {k: s1.get(k, 0) - s0[name].get(k, 0) for k in s1}
            print(f'  {name} cell pool: fix={s1.get("fix")} the block went away {d.get("windows")} time(s) while the '
                  f'mode read Cell ({d.get("frames_gone")} frames of ours), checks {d.get("checks")} '
                  f'({d.get("outside")} outside a frame), found gone {d.get("pool_gone")}, reads refused '
                  f'{d.get("refused")}', flush=True)
    up_h, up_g = alive(lane.HOST), alive(lane.GUEST)
    ck(up_h and up_g, f'both games run on (host {up_h}, guest {up_g})')
    if up_h and up_g:
        ck.faults(host=host, guest=guest)
    else:
        for e in Monitor.events:
            print('  (monitor)', e)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
