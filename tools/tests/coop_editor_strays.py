#!/usr/bin/env python3
"""Creature stage: the partner's puppet and its herd after an evolution (host crash 0xB88BEA,
2026-10-25, docs/re/creature.md 10).

Before the fix two things went wrong when the players came back from the editor:
* the old puppet stayed in the world as a creature of its own (an NPC with AI, in the nestless herd
  made for the puppet), next to the new puppet: "these guys look like our species";
* a puppet that appeared after the editor was blended from the partner's last snapshot out of the
  world (position 0,0,0): the herd made for it got its home between the planet's centre and the
  surface. An NPC of such a herd searched land around that home and read past the planet's cube
  map (the host's crash). Both are checked here with `coop.npc herds`.

The checks wait until the host's evolution cinematic is over and the world after the evolution is settled
(BUG-0018 holds the removal of the old puppet until the cinematic is over): checked earlier, the guest may
be in its galaxy menu and the old puppet still waits (BUG-0212). Since BUG-0508 (0.7.4) the host sends its
world after an evolution only when it needs to (a migration started, the guest left the world, or its avatar
still plays the host's pre-editor species 30 s later): either the guest loaded the world the host sent after
the cinematic (BUG-0019 holds that share), or the host kept the guest in place and its 30 s check is over
without a world (`evolve in place: ... checking 0` in coop.status). A guest that plays its own creature in
this world (worlds.ini, BUG-0345) is always kept in place; the test waited for a load in that case (BUG-1170).
core.strays + npc.strays off failed (core.strays retired in 0.7.15, BUG-1138: FAIL run 20261028-052840-fix-rel-strays:editor_strays) (left-over puppets).

  coop_editor_strays.py [--rounds N]
"""
import re
import sys
import time

import lane
from coop_ui import Ui
from twoinst import Checks, shot, start

DONE = (1416, 1062)  # the green "done" check of the creature editor (1440x1080 test window)


def mode(con):
    return con.call('game.mode')['out']


def herds(con):
    """(planet radius, our herds [(home radius, members)], strays [creature line])"""
    out = con.call('coop.npc herds')['out']
    m = re.search(r'avatar \|pos\|=([\d.]+)', out)
    radius = float(m.group(1)) if m else 0
    ours, ours_ptr = [], set()
    for h in re.finditer(r'herd (\w+) ours=1 .*?members=(\d+) .*?\|home\|=([\d.]+)', out):
        ours.append((float(h.group(3)), int(h.group(2))))
        ours_ptr.add(h.group(1))
    strays = [c.group(0) for c in re.finditer(r'creature \w+ herd=(\w+) puppet=0 avatar=0 dead=\d .*', out)
              if c.group(1) in ours_ptr]
    return radius, ours, strays


def check_world(ck, con, who, when):
    radius, ours, strays = herds(con)
    low = [r for r, _ in ours if r < radius * 0.9]
    ck(radius > 100 and not low, f'{who} {when}: the homes of our herds are on the surface '
       f'(planet radius {radius:.0f}, homes {[round(r) for r, _ in ours]})')
    ck(not strays, f'{who} {when}: no puppet left over as a creature of its own {strays}')


def wait_together(host, guest, t=90):
    t0 = time.time()
    while time.time() - t0 < t:
        hs, gs = host.call('coop.status')['out'], guest.call('coop.status')['out']
        if 'together 1 1' in hs and re.search(r"remote 1 'Bob' puppet=(?!00000000)[0-9A-F]{8}", hs) and \
                re.search(r"remote 0 '\w+' puppet=(?!00000000)[0-9A-F]{8}", gs):
            return True
        time.sleep(2)
    return False


def host_shares(host):
    m = re.search(r'for shares (\d+)', host.call('coop.status')['out'])
    return int(m.group(1)) if m else 0


def guest_loads():
    try:
        with open(lane.game_log(lane.GUEST), 'rb') as f:
            return len(re.findall(r'world: loading \S+ from ', f.read().decode('utf-8', 'replace')))
    except OSError:
        return 0


def in_place(host):
    """the host's evolve-in-place counters (BUG-0508): (kept, sent after all, checks still running)"""
    m = re.search(r'evolve in place: kept (\d+), sent after all (\d+), saves \d+, checking (\d+)',
                  host.call('coop.status')['out'])
    return tuple(int(g) for g in m.groups()) if m else (0, 0, 1)


def wait_settled(host, guest, shares0, loads0, kept0, t=180):
    """After an evolution the host's game plays its cinematic (CRG_SpeciesMigrate): creatures we
    remove meanwhile go when it is over (BUG-0018). Then the host either sends its world (BUG-0019 holds
    the share until the cinematic is over) and the guest loads it, or keeps the guest in place (BUG-0508)
    and its 30 s species check ends without a world. The checks are about the world after all that.
    Returns how it settled: 'shared', 'in place', or None."""
    t0 = time.time()
    while time.time() - t0 < t:
        hs = host.call('coop.status')['out']
        kept, sent, checking = in_place(host)
        shared = host_shares(host) > shares0 and guest_loads() > loads0
        # kept in place, and no world after all (a world sent after all must arrive and load: `shared`)
        kept_here = kept > kept0[0] and sent == kept0[1] and not checking
        if (shared or kept_here) and not re.search(r'cinematic \w+ playing', hs) and \
                not re.search(r'destroys during cinematics: .*pending=[1-9]', hs) and 'sending world to' not in hs and \
                'Creature' in mode(guest) and 'receiving world' not in guest.call('coop.status')['out']:
            return 'shared' if shared else 'in place'
        time.sleep(2)
    return None


def evolve(host, guest, ck, gu, order):
    m0 = mode(host)
    print(' ', host.call('coop.vote editor')['out'].strip().splitlines()[0])
    time.sleep(2)
    gu.wait(lambda s: s['ready']['vis'], 10, 'ready button')
    gu.click('ready')
    t0 = time.time()
    while time.time() - t0 < 60 and (mode(host) == m0 or mode(guest) == m0):
        time.sleep(1)
    ck(mode(host) != m0 and mode(guest) != m0, 'both are in their editors')
    host.call('wait.frames 200 60000')
    guest.call('wait.frames 200 60000')
    for c in order:  # one leaves its editor first, the other a few seconds later
        con = host if c == 'h' else guest
        con.call('click %d %d' % DONE)
        t0 = time.time()
        while time.time() - t0 < 60 and mode(con) != m0:
            time.sleep(1)
        ck(mode(con) == m0, f'{"host" if c == "h" else "guest"} is back in the stage')
        time.sleep(4)


def main():
    rounds = int(sys.argv[sys.argv.index('--rounds') + 1]) if '--rounds' in sys.argv else 2
    host, guest = start('creature')
    ck = Checks()
    gu = Ui(guest, 'guest')
    ck(wait_together(host, guest), 'together, each sees the other')
    check_world(ck, host, 'host', 'after the join')
    check_world(ck, guest, 'guest', 'after the join')
    for i in range(rounds):
        order = 'hg' if i % 2 == 0 else 'gh'
        print(f'  round {i + 1}: evolution, {"host" if order[0] == "h" else "guest"} back first')
        shares0, loads0, kept0 = host_shares(host), guest_loads(), in_place(host)
        evolve(host, guest, ck, gu, order)
        how = wait_settled(host, guest, shares0, loads0, kept0)
        ck(how, f'the evolution cinematic is over, the guest has the host\'s world after it ({how or "neither a load nor kept in place"})')
        ck(wait_together(host, guest), 'together again, each sees the other')
        time.sleep(5)  # (strays are removed on the first frame back; give the AI time to run too)
        shot(host, f'strays_host_{i + 1}.png')
        check_world(ck, host, 'host', f'after evolution {i + 1}')
        check_world(ck, guest, 'guest', f'after evolution {i + 1}')
        print('  host', host.call('coop.status')['out'].split('strays: ')[1].splitlines()[0] if 'strays: ' in
              host.call('coop.status')['out'] else '(no strays line)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
