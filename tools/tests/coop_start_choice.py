#!/usr/bin/env python3
"""Every player chooses their own start in a NEW Cell game (BUG-9273, docs/01-architecture.md#architecture-start-choice).

Two players who just bought the game (empty profiles): the host starts a new Cell game from its galaxy menu through
the game's own wizard (herbivore card, the planet's name), hosts, the guest joins. Then, as the players see it:
  1. the guest is asked: its co-op window opens on "Your own cell" (the page's widgets, not a mod counter);
  2. the guest clicks Carnivore, types its creature's name, picks the next free colour, a blue body and clicks Start;
  3. on the HOST's game: the guest's cell is its own copy of the carnivore cell painted blue (a 0x7C key of the
     creations pipeline, its model's carnivore mouth flag, GetCellModelData +0x1250; screenshots of both games), its name tag reads "Bob (<name>)" in the colour it picked (the colour table the tags are drawn with);
  4. the guest's own game eats meat it swims into and not plants (AvatarEat: its own points);
  5. its own game's Cell card (Cell_GetConsequenceTrait from its own meals) is carnivore, and the host has it as the
     guest's report for its seat's Cell card;
  6. the guest leaves and joins again: no question, its carnivore cell and its colour are back (the host's game);
  7. `--part off`: cell.start_choice off: a new guest is not asked (0.7.15).

  SPORE_LANE=N python3 tools/tests/coop_start_choice.py [--part main|off]
On a build without the feature `coop.start` is unknown: every part fails.
"""
import argparse
import os
import re
import subprocess
import sys
import time

from twoinst import Checks, HERE, fix_off_ok, shot, wait_guest_in, wait_up, watch  # (sets the path)
import lane  # noqa: E402
from lane import sh  # noqa: E402
from sporectl import Console  # noqa: E402

NAME = 'Glorbo'
CARD_CARNIVORE = 'CFB01B93'


def out(con, line):
    return con.call(line).get('out', '').strip()


def wait(f, secs, step=1.0):
    t = time.time()
    while time.time() - t < secs:
        try:
            if f():
                return True
        except (OSError, ValueError):
            pass
        time.sleep(step)
    return False


def wipe_profile(d):
    """a player who just bought the game: no saves, no creations, no co-op choices"""
    subprocess.run(['rm', '-rf', d + '/Games', d + '/Games-sporecoop-backup', d + '/EditorSaves.package',
                    d + '/SporeCoop/choices.ini', d + '/SporeCoop/worlds.ini'])


def widget(con, caption):
    """the centre of the co-op window's visible widget with this caption (screen pixels), None if not shown"""
    for l in out(con, 'coop.uistate widgets').splitlines():
        m = re.match(r"widget (\d+) (\d+) (\d+) (\d+) \w -?\d+ '(.*)'$", l)
        if m and m[5] == caption:
            return (int(m[1]) + int(m[3])) // 2, (int(m[2]) + int(m[4])) // 2
    return None


def click(con, caption):
    p = widget(con, caption)
    if not p:
        return False
    con.call(f'move {p[0]} {p[1]}')
    con.call('wait.frames 3 5000')
    con.call(f'click {p[0]} {p[1]}')
    time.sleep(1.0)
    return True


def click_last(con, caption, times=1):
    """the lowest visible widget with this caption (the body colour's ">" is below the player colour's)"""
    at = []
    for l in out(con, 'coop.uistate widgets').splitlines():
        m = re.match(r"widget (\d+) (\d+) (\d+) (\d+) \w -?\d+ '(.*)'$", l)
        if m and m[5] == caption:
            at.append(((int(m[1]) + int(m[3])) // 2, (int(m[2]) + int(m[4])) // 2))
    if not at:
        return False
    x, y = max(at, key=lambda p: p[1])
    for _ in range(times):
        con.call(f'move {x} {y}')
        con.call('wait.frames 3 5000')
        con.call(f'click {x} {y}')
        time.sleep(0.6)
    return True


def start_line(con, key):
    m = re.search(key + r'=(\S+)', out(con, 'coop.start'))
    return m[1] if m else ''


def partner_diet(con):
    """this game's model of the partner's cell and its mouths (GetCellModelData +0x1250 / +0x1251)"""
    m = re.search(r'diet partner model=(\w+) carnivore=(\d) herbivore=(\d)', out(con, 'coop.pool diet'))
    return (m[1], m[2] == '1', m[3] == '1') if m else ('', False, False)


def own_points(con):
    m = re.search(r'own_points=(-?\d+)', out(con, 'coop.pool'))
    return int(m[1]) if m else 0


def try_eat(con, kind, secs=25, any_size=True):
    """swim to the nearest `kind` on screen whose scale difference a mouth takes (docs/re/cell.md#cell-eat-gate):
    (key, own points before, after) or None"""
    t, food = time.time(), []
    while time.time() - t < secs and not food:
        for l in out(con, 'coop.pool see 80').splitlines():
            f = l.split()
            if l.startswith('see ') and f[2] == kind and (any_size or len(f) < 9 or int(f[8]) in ((0, 1) if kind == 'meat' else (1,))):
                food.append(f)
        if not food:
            time.sleep(1)
    if not food:
        return None
    key = food[0][1]
    p0 = own_points(con)
    if float(food[0][5]) > 8:
        con.call(f'coop.pool swimto {key} tp')
    for _ in range(24):
        time.sleep(0.25)
        con.call(f'coop.pool swimto {key}')
        if not out(con, f'coop.pool cell {key}').startswith('cell '):
            break
    time.sleep(1.5)
    return key, p0, own_points(con)


def color_of(con, player):
    m = re.search(r'color id=%d idx=(-?\d+) key=(\S+)' % player, out(con, 'coop.find colors'))
    return (int(m[1]), m[2]) if m else (-1, '-')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--part', default='main', choices=('main', 'off'))
    a = ap.parse_args()
    ck = Checks()
    sh('kill')
    time.sleep(2)
    sh('deploy')
    for d in (lane.HOST_DIR, lane.GUEST_DIR):
        wipe_profile(d)
    lane.run_guest()
    time.sleep(2)
    lane.run_host()
    for inst, run in ((lane.GUEST, lane.run_guest), (lane.HOST, lane.run_host)):
        if not wait_up(inst, 90):
            run()
            assert wait_up(inst), f'instance {inst} did not start'
    host, guest = Console(lane.HOST), Console(lane.GUEST)
    if 'coop.start' not in out(host, 'help coop.start'):
        print('RESULT: FAIL (coop.start is unknown: a build without the own start)', flush=True)
        return 1
    if a.part == 'off':
        out(host, 'coop.settings force cell.start_choice off')
    t0 = time.time()
    r = subprocess.run([sys.executable, os.path.join(HERE, 'new_stage_game.py'), 'cell', '-i', str(lane.HOST),
                        '--slot', '0', '--diet', 'herb', '--shots'], capture_output=True, text=True, timeout=900)
    if not ck(r.returncode == 0, f'the host started a new cell game with the game\'s wizard ({time.time() - t0:.0f} s)'):
        shot(host, 'start_choice_wizard.png')
        return ck.result()
    watch(lane.HOST, lane.GUEST)
    host.call(lane.HOST_CMD)
    wait(lambda: 'GalaxyGameEntry' in out(guest, 'game.mode'), 60)
    guest.call('wait.frames 200 60000')
    guest.call(lane.JOIN)
    if not ck(wait_guest_in(host, guest, 240), 'the guest is in the host\'s new Cell game'):
        return ck.result()
    asked = wait(lambda: widget(guest, 'Your own cell') is not None, 60)
    shot(guest, 'start_choice_page.png')
    if a.part == 'off':
        ck(not asked and start_line(guest, 'asks_rx') == '0',
           'cell.start_choice off: the guest is not asked (no "Your own cell" page in 60 s)')
        ck.faults(host=host, guest=guest)
        return ck.result()
    ck(asked, 'the guest is asked: its co-op window shows "Your own cell"')
    model0 = partner_diet(host)[0]
    # 2. the player's own choice, through the page
    ok = click(guest, 'Carnivore')
    name_at = widget(guest, 'Bobite')  # the name field starts with the player's name + "ite" (start_choice::default_name)
    if name_at:
        guest.call(f'click {name_at[0]} {name_at[1]}')
        time.sleep(0.5)
        guest.call('key end tap')
        for _ in range(30):
            guest.call('key backspace tap')
        guest.call(f'text {NAME}')
        time.sleep(0.5)
    c0 = color_of(host, 1)[0]
    ok &= click(guest, '>')
    ok &= click_last(guest, '>', 5)  # the body colour: Natural -> Red, Orange, Yellow, Green, Blue
    want = re.search(r"widget 32\d \d+ \d+ \d+ L \d+ '(\w+)'", out(guest, 'coop.uistate widgets'))
    shot(guest, 'start_choice_filled.png')
    ok &= click(guest, 'Start')
    ck(ok and name_at is not None, 'the guest picked Carnivore, typed its name, the next colour, a blue body and clicked Start')
    ck(wait(lambda: start_line(host, 'records') == '1', 20), 'the host recorded the guest\'s choice for this planet')
    # 3. what the host sees
    ck(wait(lambda: partner_diet(host)[0].startswith('7C'), 60),
       f'on the host the guest\'s cell is its own painted copy of the carnivore cell (model {model0} -> {partner_diet(host)[0]})')
    m = re.search(r'diet me model=(\w+)', out(guest, 'coop.pool diet'))
    ck(m is not None and m[1].startswith('7B') and 'paints=1' in out(guest, 'coop.start'),
       f'the guest plays the blue copy it made ({m[1] if m else "?"})')
    d = partner_diet(host)
    ck(d[1] and not d[2], f'its mouth on the host\'s game: carnivore={int(d[1])} herbivore={int(d[2])}')
    idx, key = color_of(host, 1)
    ck(idx >= 0 and idx != c0 and want and key == want[1].lower(),
       f'the host draws the guest in the colour it picked ({c0} -> {idx} {key}, picked {want[1] if want else "?"})')
    tag = ''
    for _ in range(20):
        tag = next((l for l in out(host, 'coop.find labels').splitlines() if 'kind=player id=1' in l), '')
        if tag:
            break
        time.sleep(1)  # (the guest's new cell hatches next to the host's: on its screen)
    ck(f'text=Bob ({NAME})' in tag, f'the host\'s name tag of the guest carries its creature\'s name ({tag[-60:]})')
    shot(host, 'start_choice_host.png')
    # 4. its own game eats meat, not plants
    time.sleep(8)  # (the own cell's rebuild and hatch)
    # (a single swim may miss its food: a mouth facing away, the food drifting; up to 4 meats, 2 plants)
    meats = []
    for _ in range(4):
        meats.append(try_eat(guest, 'meat'))
        if meats[-1] and meats[-1][2] > meats[-1][1]:
            break
    ck(any(m and m[2] > m[1] for m in meats), f'the carnivore guest eats meat ({meats})')
    plants = [try_eat(guest, 'plant') for _ in range(2)]
    ck(all(p is not None and p[2] == p[1] for p in plants), f'the carnivore guest does not eat plants it swims into ({plants})')
    # 5. its own Cell card
    out(guest, 'coop.pool meal 300')
    ck(wait(lambda: CARD_CARNIVORE in out(guest, 'coop.cards cell'), 20),
       f'the guest\'s own game decides a carnivore Cell card ({out(guest, "coop.cards cell")})')
    ck(wait(lambda: re.search(r'cell report \w+: ' + CARD_CARNIVORE, out(host, 'coop.cards')) is not None, 20),
       'the host has the guest\'s carnivore Cell card for its seat')
    # 6. a rejoin: no question, the same cell and colour
    asks = start_line(guest, 'asks_rx')
    guest.call('coop.leave')
    time.sleep(6)
    guest.call(lane.JOIN)
    ck(wait_guest_in(host, guest, 240), 'the guest is back in the host\'s world')
    time.sleep(30)
    ck(widget(guest, 'Your own cell') is None and start_line(guest, 'asks_rx') == asks,
       'the rejoin does not ask again')
    ck(wait(lambda: partner_diet(host)[0].startswith('7C') and partner_diet(host)[1], 60),
       'after the rejoin the host sees the painted carnivore cell again')
    ck(color_of(host, 1)[0] == idx, f'and the guest\'s colour ({color_of(host, 1)})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
