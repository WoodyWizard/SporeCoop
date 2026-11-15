#!/usr/bin/env python3
"""The session settings (BUG-0762, F3 of 0.7.12; docs/07-modes.md#modes-session-settings).

Cell fixture, two players:
  1. the joining guest gets the host's table (its `coop.settings` reads the host's values);
  2. a session setting is refused once the host plays its world (all.dna_pace), and a guest cannot set any;
  3. regeneration near a partner, the game's own health of each cell (S+avatar hp, `coop.pool`): with cell.regen on
     (the default) a hurt cell next to its partner heals on both games; the host turns it off in play (a live
     setting): the running guest gets the change, and no cell heals any more on either game;
  4. the host's world restarts (both games end, start again, the host loads the same world): the host plays with
     the values its world stored (cell.regen off, and creature.pack_rescue off set in the galaxy menu before the
     start), and the guest joining the new session gets them.
SPORE_FIX_OFF=settings.cell_regen: the regeneration does not read the setting (0.7.11): step 3 must FAIL.

  coop_settings.py
"""
import re
import sys
import time

import fixtures
import lane
import twoinst
from twoinst import Checks, start, wait_up, wait_guest_in, host_up

HURT = 2       # the cells' health for the measurement (a cell has 6)
MEASURE = 10.0  # s: the regeneration gives +1 every 3 s


def wait(cond, secs, step=0.5):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return None


def get(con, key):
    m = re.search(re.escape(key) + r'=(-?\d+)', con.call(f'coop.settings get {key}')['out'])
    return int(m.group(1)) if m else None


def hp(con):
    m = re.search(r'avatar hp=(-?\d+)', con.call('coop.pool')['out'])
    return int(m.group(1)) if m else None


def partner_dist(con):
    m = re.search(r'partner_dist=(-?[\d.]+)', con.call('coop.pool')['out'])
    return float(m.group(1)) if m else None


def heal_measure(host, guest, what):
    """both cells hurt next to each other (invulnerable: no NPC bite counts), then their health MEASURE s later"""
    guest.call('coop.pool tp 3 0')
    time.sleep(1.0)
    for con in (host, guest):
        con.call('coop.pool invuln 1')
        con.call(f'coop.pool hp {HURT}')
    h0, g0 = hp(host), hp(guest)
    d = partner_dist(guest)
    time.sleep(MEASURE)
    h1, g1 = hp(host), hp(guest)
    for con in (host, guest):
        con.call('coop.pool invuln 0')
    print(f'  {what}: partner {d} m; host hp {h0} -> {h1}, guest hp {g0} -> {g1}', flush=True)
    return h0, h1, g0, g1, d


def restart(planet):
    """both games end and start again; the host loads its world, hosts, the guest joins (as twoinst._start, without
    the fixture restore and without forgetting the stored settings)"""
    seen = list(twoinst.Monitor.events)  # what the monitors saw so far stays a failure; the kill itself is none
    for m in twoinst._monitors:
        m.stop = True
    time.sleep(2)
    twoinst.sh('kill')
    time.sleep(2)
    lane.run_guest()
    time.sleep(2)
    lane.run_host()
    assert wait_up(lane.GUEST, 120) and wait_up(lane.HOST, 120), 'the games did not start again'
    host, guest = twoinst.Console(lane.HOST), twoinst.Console(lane.GUEST)
    wait(lambda: 'GalaxyGameEntry' in host.call('game.mode')['out'], 180, 1)
    host.call('wait.frames 240 90000')
    slot, name = planet.split('/')
    print('  host load:', host.call(f'coop.load {slot} {name}')['out'].strip(), flush=True)
    wait(lambda: host.call('coop.avatar')['ok'], 240, 2)
    time.sleep(4)
    return host, guest, seen


def main():
    host, guest = start('cell')
    ck = Checks()
    planet = fixtures.planet_of('cell')
    try:
        wait(lambda: hp(host) is not None and hp(guest) is not None, 30)
        # 1. the joining guest plays with the host's table
        gl = guest.call('coop.settings')['out']
        print('  guest:', gl.splitlines()[0] if gl else '?', flush=True)
        ck("the host's table" in gl and get(guest, 'cell.regen') == 1, "the joining guest got the host's settings table")
        # 2. refusals
        r = host.call('coop.settings set all.dna_pace off')['out'].strip()
        print('  host, session setting in play:', r, flush=True)
        ck('refused' in r and get(host, 'all.dna_pace') == 1 and get(guest, 'all.dna_pace') == 1,
           'a session setting is refused once the host plays its world (both games keep it on)')
        r = guest.call('coop.settings set cell.regen off')['out'].strip()
        print('  guest:', r, flush=True)
        ck('refused' in r and get(guest, 'cell.regen') == 1, 'a guest cannot change a setting')
        # 3. regeneration on (the default): the game heals both cells next to the partner
        h0, h1, g0, g1, d = heal_measure(host, guest, 'cell.regen on')
        ck(d is not None and d < 12, f'the cells are next to each other ({d} m)')
        ck(h0 == HURT and h1 is not None and h1 >= HURT + 2, f"regeneration on: the host's cell healed ({h0} -> {h1})")
        ck(g0 == HURT and g1 is not None and g1 >= HURT + 2, f"regeneration on: the guest's cell healed ({g0} -> {g1})")
        # the host turns it off in play (live): the running guest gets it, nobody heals
        r = host.call('coop.settings set cell.regen off')['out'].strip()
        print('  host:', r, flush=True)
        ck(r.startswith('cell.regen=0'), 'a live setting is accepted in play')
        ck(wait(lambda: get(guest, 'cell.regen') == 0, 5), 'the running guest got the change')
        h0, h1, g0, g1, d = heal_measure(host, guest, 'cell.regen off')
        ck(h0 == HURT and h1 == HURT, f"regeneration off: the host's cell did not heal ({h0} -> {h1})")
        ck(g0 == HURT and g1 == HURT, f"regeneration off: the guest's cell did not heal ({g0} -> {g1})")
        for con in (host, guest):
            con.call('coop.pool hp 6')
        print('  host stored:', host.call('coop.settings stored')['out'].strip(), flush=True)
        # 4. a session setting before the start: the host leaves its world for the galaxy menu
        for con in (guest, host):
            con.call('coop.leave')
        print('  host menu:', host.call('coop.menu')['out'].strip(), flush=True)
        ck(wait(lambda: 'GalaxyGameEntry' in host.call('game.mode')['out'], 90), 'the host is in the galaxy menu')
        r = host.call('coop.settings set creature.pack_rescue off')['out'].strip()
        print('  host in the menu:', r, flush=True)
        ck(r.startswith('creature.pack_rescue=0'), 'a session setting is accepted before the start')
        host.call('wait.frames 240 90000')
        slot, name = planet.split('/')
        host.call(f'coop.load {slot} {name}')
        ck(wait(lambda: host.call('coop.avatar')['ok'], 240, 2), 'the host plays its world again')
        print('  host stored:', host.call('coop.settings stored')['out'].strip(), flush=True)
        # the restart of the host's world: new processes, nothing kept in memory
        host, guest, seen = restart(planet)
        ck(get(host, 'cell.regen') == 0 and get(host, 'creature.pack_rescue') == 0,
           f"after the restart the host's world plays with its stored settings (cell.regen={get(host, 'cell.regen')}, "
           f"creature.pack_rescue={get(host, 'creature.pack_rescue')})")
        host_up(host)
        wait(lambda: 'GalaxyGameEntry' in guest.call('game.mode')['out'], 120, 1)
        guest.call('wait.frames 200 60000')
        guest.call(lane.JOIN)
        ck(wait_guest_in(host, guest), 'the guest joined the new session')
        ck(wait(lambda: get(guest, 'cell.regen') == 0 and get(guest, 'creature.pack_rescue') == 0, 10),
           'the guest of the new session plays with the stored settings')
        twoinst.watch(lane.HOST, lane.GUEST)
        twoinst.Monitor.events.extend(seen)
    finally:
        for con in (host, guest):
            try:
                con.call('coop.settings force cell.regen on')
                con.call('coop.settings force creature.pack_rescue on')
            except OSError:
                pass
        twoinst.forget_settings()
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
