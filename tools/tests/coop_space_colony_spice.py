#!/usr/bin/env python3
"""The shared colonies' stored spice (space_planets.cpp, BUG-0537) in the cases its first version missed.

  --case away    (BUG-0822, fixture space) the host acts as outside Space (`coop.space away 1`: an editor or an
                 adventure entered from Space); the guest leaves the home system and comes back, its game collects
                 the home colony's spice (every arrival does). The host back in Space (`away 0`): 12 s later the
                 guest's colony cities must still be empty, and the host's too. Before the fix the guest's message
                 came while the host was away and was dropped: the host's next sync wrote the old amounts back into
                 the guest's game (the spice to collect again). SPORE_FIX_OFF=space.colony_spice_away fails.
  --case mirror  (BUG-0840, fixture space) the guest makes a colony of its own in the shared home system
                 (`coop.space allies colonize`): the host's game shows it as one of the host's empire (a mirror with
                 its own cities). The guest's colony stores 40 spice per city (`colonyspice set`): 30 s later its
                 cities must still hold it. Before the fix the host synced its mirror's spice (about 0) into the
                 guest's real colony. SPORE_FIX_OFF=space.colony_spice_mirror fails.
  --case cap     (BUG-0834, fixture space_empire: three colonies) the old code synced the first 48 colonies of the
                 list only. `coop.space colonyspice cap 1` makes that cap 1 on both games (it is used only with the
                 fix off), so the colonies after the first one stand for the 49th and later. The host's last colony
                 stores 30 spice per city: the guest's game must get it. SPORE_FIX_OFF=space.colony_spice_all fails.
"""
import argparse
import re
import time

from twoinst import Checks, start

HOME, HOME_STAR = '00460035', 4587573


def sp(con, *a):
    return con.call('coop.space ' + ' '.join(str(x) for x in a))['out'].strip()


def colonies(con):
    """{PlanetID: [stored spice of each city]} in this game's list order (dict keeps it)"""
    out = {}
    for m in re.finditer(r'colony ([0-9A-F]{8}) cities \d+:([-\d. ]*)', sp(con, 'colonyspice')):
        out[m.group(1)] = [float(v) for v in m.group(2).split()]
    return out


def spice_of(con, planet):
    return colonies(con).get(planet, [])


def wait(pred, timeout=20, step=0.5):
    t0 = time.time()
    while time.time() - t0 < timeout:
        v = pred()
        if v:
            return v
        time.sleep(step)
    return pred()


def case_away(host, guest, ck):
    h0, g0 = spice_of(host, HOME), spice_of(guest, HOME)
    print('  colony spice: host', h0, '| guest', g0, flush=True)
    ck(sum(h0) >= 5, f'the host\'s home colony stores spice ({sum(h0):.1f})')
    print('  host:', sp(host, 'away 1'))
    time.sleep(2)
    print('  guest away:', sp(guest, 'jumpto', HOME_STAR + 1))
    time.sleep(15)
    print('  guest back:', sp(guest, 'jump'))
    ok = wait(lambda: sum(spice_of(guest, HOME)) < sum(g0) - 3, 25, 1)
    g1 = spice_of(guest, HOME)
    print('  guest after its arrival:', g1, '|', sp(guest, 'colonyspice').splitlines()[0], flush=True)
    ck(ok, f'the guest\'s game collected the colony\'s spice on its arrival ({sum(g0):.1f} -> {sum(g1):.1f})')
    time.sleep(3)
    print('  host (away):', sp(host, 'colonyspice').splitlines()[0], '|', spice_of(host, HOME), flush=True)
    print('  host:', sp(host, 'away 0'))
    time.sleep(12)  # the host's sync runs on a change and every 10 s
    g2, h2 = spice_of(guest, HOME), spice_of(host, HOME)
    print('  12 s after the host is back: guest', g2, '| host', h2, flush=True)
    print('  host:', sp(host, 'colonyspice').splitlines()[0], '| guest:', sp(guest, 'colonyspice').splitlines()[0])
    ck(sum(g2) <= sum(g1) + 1.5, f'the guest\'s colony stays collected ({sum(g1):.1f} -> {sum(g2):.1f}): '
                                 f'no spice to collect twice')
    ck(sum(h2) <= sum(g1) + 1.5, f'the host\'s colony lost the collected spice too ({sum(h0):.1f} -> {sum(h2):.1f})')


def case_mirror(host, guest, ck):
    out = sp(guest, 'allies colonize')
    print('  guest:', out.splitlines()[0] if out else '-', flush=True)
    m = re.match(r'colonized ([0-9A-F]+)', out)
    ck(m is not None, 'the guest made a colony of its empire in the home system')
    if not m:
        return
    planet = m.group(1)
    ck(wait(lambda: planet in colonies(guest), 15), f'{planet} is in the guest\'s colony list')
    ok = wait(lambda: planet in colonies(host), 30, 1)
    print('  host colonies:', colonies(host), flush=True)
    ck(ok, f'the host\'s game shows the guest\'s colony {planet} in its empire (the mirror)')
    print('  guest:', sp(guest, 'colonyspice set', planet, 40).strip(), flush=True)
    g0 = spice_of(guest, planet)
    ck(g0 and min(g0) >= 39, f'the guest\'s colony stores 40 per city ({g0})')
    low = None
    t0 = time.time()
    while time.time() - t0 < 30:
        g = spice_of(guest, planet)
        if g and min(g) < 30:
            low = g
            break
        time.sleep(1)
    g1 = spice_of(guest, planet)
    print('  guest 30 s later:', g1, '| host mirror:', spice_of(host, planet), '|',
          sp(host, 'colonyspice').splitlines()[0], flush=True)
    ck(low is None and g1 and min(g1) >= 30, f'the guest\'s own colony keeps its spice ({g0} -> {low or g1})')


def case_cap(host, guest, ck):
    for c in (host, guest):
        print(' ', sp(c, 'colonyspice cap 1').strip())
    hc, gc = colonies(host), colonies(guest)
    print('  host colonies:', hc, '\n  guest colonies:', gc, flush=True)
    ids = list(hc)
    ck(len(ids) >= 2, f'the host has more than one colony ({ids})')
    if len(ids) < 2:
        return
    last = ids[-1]
    ck(last in gc and len(gc[last]) == len(hc[last]), f'the guest has colony {last} with the same cities')
    print('  host:', sp(host, 'colonyspice set', last, 30).strip(), flush=True)
    ok = wait(lambda: spice_of(guest, last) and min(spice_of(guest, last)) >= 29, 25, 1)
    print('  guest:', spice_of(guest, last), '|', sp(guest, 'colonyspice').splitlines()[0], flush=True)
    ck(ok, f'the guest\'s game has the host\'s spice in colony {last} (the {len(ids)}th of the list): '
           f'{spice_of(guest, last)}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--case', choices=('away', 'mirror', 'cap'), required=True)
    a = ap.parse_args()
    host, guest = start('space_empire' if a.case == 'cap' else 'space')
    ck = Checks()
    time.sleep(5)
    {'away': case_away, 'mirror': case_mirror, 'cap': case_cap}[a.case](host, guest, ck)
    sp(host, 'away 0')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
