#!/usr/bin/env python3
"""BUG-0471: effects are relayed only between players in the same place.

The relay used to send every effect of the local player to every partner, and the receiver created
it in whatever world its game had at that moment: a guest in the main menu, an editor or a loading
screen got the host's creature effects created there (play_creature's guest leaves to the main menu
and comes back while the host fights). Now the sender sends only while some partner plays in its
place, and the receiver plays a start only while its owner plays in the receiver's place.

Here the guest goes to the main menu (pause menu, Exit to Main Menu, Don't Save, like play_creature) while the host makes world effects at its creature
(`coop.effects load`): the guest's game must not be asked to create any of them (its relay's game
calls: played + failed). When the guest is back in the host's world, the host's effects reach it
again (the guest's game creates them, active).

  coop_effects_here.py   (SPORE_FIX_OFF=effects.here fails)
"""
import re
import time

import uifind
from twoinst import Checks, start


def busy(con):
    for line in con.call('coop.status')['out'].splitlines():
        if line.startswith('busy:'):
            return line.split(':', 1)[1].strip()
    return '?'


def free_games(h, g, seconds=30):
    t0 = time.time()
    while time.time() - t0 < seconds:
        if busy(h) == '-' and busy(g) == '-':
            return True
        for c in (h, g):
            c.call('key enter')
            c.call('click 1418 1044')
        time.sleep(1)
    return busy(h) == '-' and busy(g) == '-'


def nums(con):
    out = con.call('coop.effects')['out']
    m = re.search(r'effects: fix=(\d) sent=(\d+) received=(\d+) played=(\d+) stopped=\d+ skipped=\d+ failed=(\d+)', out)
    e = re.search(r'effects elsewhere: not sent=(\d+) not played=(\d+)', out)
    if not m:
        return {}
    d = dict(fix=int(m.group(1)), sent=int(m.group(2)), received=int(m.group(3)), played=int(m.group(4)),
             failed=int(m.group(5)))
    if e:
        d.update(not_sent=int(e.group(1)), not_played=int(e.group(2)))
    return d


def calls(con):
    n = nums(con)
    return n.get('played', 0) + n.get('failed', 0)


def main():
    h, g = start('creature')
    ck = Checks()
    ck(g.call('coop.avatar')['ok'], 'the guest is in the host\'s world')
    ck(free_games(h, g), f'both games take input (busy: {busy(h)} / {busy(g)})')
    # the relay works while both play here (the base line)
    c0 = calls(g)
    h.call('coop.effects load 2 1000 E1B24DDB')
    time.sleep(5)
    h.call('coop.effects load 0')
    ck(calls(g) > c0, f'in the same world the host\'s effects reach the guest\'s game ({calls(g) - c0} created)')
    # the guest in the main menu
    for _ in range(3):
        g.call('key esc')
        time.sleep(1.2)
        if uifind.shown(g, 'Resume Game'):
            break
    ok = uifind.press(g, 'Exit to Main Menu', 'Do you want to save your game?') \
        and uifind.press(g, "Don't Save", 'Are you sure?') and uifind.press(g, "Don't Save")
    ck(ok, 'the guest leaves through the pause menu')
    t0 = time.time()
    while time.time() - t0 < 60 and 'GalaxyGameEntry' not in g.call('game.mode')['out']:
        time.sleep(1)
    mode = g.call('game.mode')['out'].strip()
    ck('GalaxyGameEntry' in mode, f'the guest is in the main menu ({mode})')
    time.sleep(2)
    hb, gb = nums(h), nums(g)
    h.call('coop.effects load 2 1000 E1B24DDB')
    t0 = time.time()
    while time.time() - t0 < 10 and 'GalaxyGameEntry' in g.call('game.mode')['out']:
        time.sleep(0.5)
    h.call('coop.effects load 0')
    ha, ga = nums(h), nums(g)
    made = (ga.get('played', 0) + ga.get('failed', 0)) - (gb.get('played', 0) + gb.get('failed', 0))
    print(f'  menu: host {hb} -> {ha}', flush=True)
    print(f'  menu: guest {gb} -> {ga}', flush=True)
    ck(made == 0, f'the guest\'s game in the main menu is not asked to create the host\'s effects ({made})')
    # back in the host's world: the relay works again
    t0 = time.time()
    while time.time() - t0 < 150 and not ('Creature' in g.call('game.mode')['out'] and g.call('coop.avatar')['ok']
                                          and 'receiving world' not in g.call('coop.status')['out']):
        time.sleep(1)
    ck(g.call('coop.avatar')['ok'], 'the guest is back in the host\'s world')
    free_games(h, g, 30)
    time.sleep(5)
    c1 = calls(g)
    h.call('coop.effects load 2 1000 E1B24DDB')
    t0 = time.time()
    while time.time() - t0 < 15 and calls(g) <= c1:
        time.sleep(0.5)
    h.call('coop.effects load 0')
    ck(calls(g) > c1, f'back in the world the host\'s effects reach the guest\'s game again ({calls(g) - c1} created)')
    ck.faults(host=h, guest=g)
    raise SystemExit(ck.result())


if __name__ == '__main__':
    main()
