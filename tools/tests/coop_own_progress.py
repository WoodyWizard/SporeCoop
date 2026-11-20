#!/usr/bin/env python3
"""BUG-9265 (own progress, the owner's rule of 2026-11-20): in the Cell and Creature stages every player has their own
DNA and their own evolution bar, earned by what they do themselves; nobody evolves alone: the first full bar waits
(with a notice, the player plays on), the stage changes only when every player's own bar is full and everybody agreed
in the vote. The session setting all.own_progress (default on); off: the party's one shared total.

Every check reads what the game itself shows: the food bar of the Cell stage (cCellSerializableData +0x1C), the DNA
(GetEvolutionPoints), the evolution bar (cPlayer +0x10F0) and the brain level ([0x169E370]) of each game, through
`coop.progress` (its first line reads the game) and `coop.dna bar`; the stage each game plays (game.mode).

--part cell (fixture cell):
  1. the host's meal fills the host's bar only, the guest's meal the guest's only (each game's food);
  2. a rejoin keeps the guest's own food (the host's world it loads holds the host's);
  3. one player fills its bar first (--first guest|host): its notice says it waits; its click on History's check keeps
     both in Cell, no vote, and its screen is the game, not the white of the Cell end (BUG-9266: the click is taken
     before the game's end sequence; SPORE_FIX_OFF=cell.end_click: white, the check fails);
  4. the other fills its bar: every bar is full; guest first: the host's check starts the vote; host first: the host's
     kept click goes on by itself and the guest's click during the vote is its I'm ready (not white either); both
     games enter the Creature stage.
--part creature (fixture creature):
  1. a game reward on each game (AddEvolutionPoints: `coop.dna gain`) raises that game's DNA and bar only;
  2. the guest's kill of one of the host's animals pays the guest's DNA, not the host's;
  3. a rejoin and a reload of the planet (the host to the galaxy menu and back) give the guest its own DNA, bar and
     brain level back (the save holds the host's: <planet>.spo.duoprogress next to it holds the guest's);
  4. the host's brain is fully grown first: its evolve button (History's check) is refused, no vote, it stays in
     Creature; the guest's too: every bar full, the host's check starts the vote, I'm ready opens the host's editor.
--part off (fixture creature): `coop.settings force all.own_progress off`: the host's gain reaches the guest's DNA
  (the shared total of 0.7.15), the old behaviour.

A new feature: on main `coop.progress` is an unknown command and every part fails at its first check; --part off is
the switch's twin (the shared total is still there when the setting is off).
"""
import argparse
import re
import time

import lane
import settings_lib as sl
from coop_campaign import CAPTION_OK, HISTORY_BTN, HISTORY_OK
from coop_ui import Ui
from twoinst import Checks, shot, start, wait_guest_in

CELL_STAGE_BTN = (1396, 1063)  # the cell HUD button under the portrait: History, then the next stage
CELL_HISTORY_OK = (1415, 1055)


def wait(f, secs, step=1.0):
    t = time.time() + secs
    while time.time() < t:
        try:
            if f():
                return True
        except (OSError, ValueError, TypeError, KeyError):
            pass
        time.sleep(step)
    return False


def out(con, line):
    return con.call(line).get('out', '')


def prog(con):
    """the game's own values as coop.progress reads them (first line), or None"""
    s = out(con, 'coop.progress')
    m = re.search(r'active=(\d) .*?dna=([-\d.]+) bar=([-\d.]+) level=(-?\d+) food=(-?\d+) spent=(-?\d+) full=(\d) '
                  r'pct=(\d+) all_full=(\d)', s)
    if not m:
        return None
    k = ('active', 'dna', 'bar', 'level', 'food', 'spent', 'full', 'pct', 'all')
    v = dict(zip(k, [int(m[1]), float(m[2]), float(m[3]), int(m[4]), int(m[5]), int(m[6]), int(m[7]), int(m[8]), int(m[9])]))
    v['raw'] = s
    return v


def count(con, name):
    m = re.search(name + r'=(\d+)', out(con, 'coop.progress'))
    return int(m.group(1)) if m else -1


def white_share(path):
    """the share of near-white pixels of a screenshot (-1 without Pillow or a picture)"""
    try:
        from PIL import Image
        with Image.open(path) as im:
            px = list(im.convert('RGB').resize((256, 192)).getdata())
        return sum(1 for r, g, b in px if r > 235 and g > 235 and b > 235) / len(px)
    except (OSError, ImportError, SyntaxError, ValueError):
        return -1


def mode(con):
    return out(con, 'game.mode')


def vote_open(host):
    return out(host, 'coop.vote').startswith('vote active')


def click(con, x, y, settle=1.0):
    con.call(f'move {x} {y}')
    time.sleep(0.4)
    con.call(f'click {x} {y}')
    time.sleep(settle)


def cell_history_check(con):
    click(con, *CELL_STAGE_BTN, 3)
    click(con, *CELL_HISTORY_OK, 2)


def creature_history_check(con):
    click(con, *HISTORY_BTN, 3)
    click(con, *HISTORY_OK, 2)


def captions(con, secs=60):
    """the brain upgrades' / the Cell end's captions wait for their check, as a player reads them"""
    t = time.time() + secs
    while time.time() < t:
        st = out(con, 'coop.status')
        if 'movie=1' in st or 'cine=1' in out(con, 'coop.pool'):
            click(con, *CAPTION_OK, 3)
            continue
        if not re.search(r'cinematic \S+ playing', st):
            return
        time.sleep(2)


def ready(guest):
    gu = Ui(guest, 'guest')
    st = gu.state()
    if st.get('open') != '1':
        guest.call('key 120 down')  # F9
        time.sleep(0.3)
        guest.call('key 120 up')
        gu.wait(lambda s: s['ready']['vis'], 10, 'ready button')
    gu.click('ready')


def rejoin(host, guest, ck):
    guest.call('coop.leave')
    time.sleep(4)
    guest.call(lane.JOIN)
    ck(wait_guest_in(host, guest), 'the guest joined again')
    ck(wait(lambda: count(guest, 'applied') >= 0 and 'synced=1' in out(guest, 'coop.progress'), 240, 2),
       'the guest asked the host for its own values after the load')
    time.sleep(6)


def part_cell(host, guest, ck, first):
    ph, pg = prog(host), prog(guest)
    ck(ph and pg and ph['active'] == 1 and pg['active'] == 1, 'own progress is on in both games (coop.progress)')
    if not (ph and pg):
        return
    # 1. each meal fills the eater's bar only
    host.call('coop.pool meal 300')
    time.sleep(4)
    ph, pg2 = prog(host), prog(guest)
    ck(ph['food'] >= 300 and pg2['food'] == pg['food'], f"the host's meal: host food {ph['food']}, guest food stays {pg2['food']}")
    guest.call('coop.pool meal 150')
    time.sleep(4)
    ph2, pg = prog(host), prog(guest)
    ck(pg['food'] == pg2['food'] + 150 and ph2['food'] == ph['food'],
       f"the guest's meal: guest food {pg['food']}, host food stays {ph2['food']}")
    # 2. a rejoin
    before = pg['food']
    rejoin(host, guest, ck)
    pg = prog(guest)
    ck(pg and pg['food'] == before, f"after the rejoin the guest's own food is back ({pg and pg['food']}, before {before}; "
                                     f"the host's world holds {ph2['food']})")
    # 3. one player fills first and clicks History's check at once (`first`)
    early, late = (guest, host) if first == 'guest' else (host, guest)
    who, other = ('guest', 'host') if first == 'guest' else ('host', 'guest')
    early.call(f'coop.pool meal {1000 - prog(early)["food"]}')
    ck(wait(lambda: prog(early)['full'] == 1, 20), f"the {who}'s bar is full")
    ck(wait(lambda: count(early, 'full_notices') >= 1, 10), f'the {who} is told that it waits for the partner')
    ck(prog(late)['full'] == 0 and prog(late)['all'] == 0, f"the {other}'s bar is not full: nobody moves on")
    captions(early)
    cell_history_check(early)
    time.sleep(8)
    ck('Cell' in mode(guest) and 'Cell' in mode(host), f"the {who}'s check keeps both games in Cell")
    ck(not vote_open(host), f"no vote while the {other}'s bar is not full")
    # BUG-9266: the early click is taken before the game's end sequence: no white screen (SPORE_FIX_OFF=cell.end_click:
    # the sequence runs, the screen stays white)
    w = white_share(shot(early, f'own_progress_{who}_early_click.png'))
    ck(0 <= w < 0.5, f"the {who}'s screen after its early click is the game, not white ({w:.2f} of it white)")
    # 4. the other fills its bar: every bar is full
    late.call(f'coop.pool meal {1000 - prog(late)["food"]}')
    ck(wait(lambda: prog(host)['all'] == 1 and prog(guest)['all'] == 1, 20), 'every bar is full on both games')
    captions(late)
    if first == 'host':
        ck(wait(lambda: vote_open(host), 30), "the host's kept click goes on by itself: the vote starts")
        cell_history_check(guest)  # the guest's click on History's check during the vote is its I'm ready
        time.sleep(6)
        w = white_share(shot(guest, 'own_progress_guest_click_in_vote.png'))
        ck(0 <= w < 0.5, f"the guest's screen after its click in the vote is the game, not white ({w:.2f})")
    else:
        t0 = time.time()
        while time.time() - t0 < 90 and not vote_open(host) and 'Cell' in mode(host):
            cell_history_check(host)
            time.sleep(6)
        ck(vote_open(host), "the host's check starts the vote (%s)" % out(host, 'coop.vote').splitlines()[0])
        ready(guest)
    ck(wait(lambda: 'Creature' in mode(host), 120), "the host's game enters the Creature stage (%s)" % mode(host).strip())
    ck(wait(lambda: 'Creature' in mode(guest), 180), "the guest's game follows (%s)" % mode(guest).strip())


def part_creature(host, guest, ck):
    ph0, pg0 = prog(host), prog(guest)
    ck(ph0 and pg0 and ph0['active'] == 1 and pg0['active'] == 1, 'own progress is on in both games (coop.progress)')
    if not (ph0 and pg0):
        return
    # 1. each reward goes to the game that earned it
    host.call('coop.dna gain 100')
    time.sleep(4)
    guest.call('coop.dna gain 60')
    time.sleep(4)
    ph, pg = prog(host), prog(guest)
    ck(abs(ph['dna'] - ph0['dna'] - 100) < 1 and abs(ph['bar'] - ph0['bar'] - 100) < 1,
       f"the host's reward: its DNA {ph0['dna']} -> {ph['dna']}, bar {ph0['bar']} -> {ph['bar']}")
    ck(abs(pg['dna'] - pg0['dna'] - 60) < 1 and abs(pg['bar'] - pg0['bar'] - 60) < 1,
       f"the guest's reward: its DNA {pg0['dna']} -> {pg['dna']}, bar {pg0['bar']} -> {pg['bar']} (not the host's)")
    # 2. the guest's kill of a host animal
    fam = re.search(r'key=0x([0-9A-F]+)', out(host, 'coop.avatar'))
    fam = fam.group(1).zfill(8) if fam else ''
    mirrored = set(re.findall(r'#(\d+) .*spawned', out(guest, 'coop.npc list')))
    prey = [m.group(1) for m in re.finditer(r'#(\d+) (?:[0-9A-Fa-f]{8} )?species=([0-9A-F]+) hp=([\d.]+)',
                                             out(host, 'coop.npc list'))
            if m.group(1) in mirrored and m.group(2) != fam and float(m.group(3)) > 0]
    ph, pg = prog(host), prog(guest)
    paid = False
    for cand in prey[:4]:
        guest.call(f'coop.npc hit {cand} 100000')
        if wait(lambda: prog(guest)['dna'] > pg['dna'], 6, 0.5):
            paid = True
            break
    time.sleep(2)
    ph2 = prog(host)
    ck(paid, f"the guest's kill pays the guest's own DNA ({pg['dna']} -> {prog(guest)['dna']}, prey {prey[:4]})")
    ck(abs(ph2['dna'] - ph['dna']) < 0.5 and abs(ph2['bar'] - ph['bar']) < 0.5,
       f"the host's DNA and bar do not move for it ({ph['dna']} -> {ph2['dna']})")
    # 3. a rejoin and a reload keep the guest's own values
    host.call('coop.dna gain 400')  # the host's world (the save the guest loads) is far from the guest's
    time.sleep(4)
    pg = prog(guest)
    rejoin(host, guest, ck)
    pg2 = prog(guest)
    ck(pg2 and abs(pg2['dna'] - pg['dna']) < 1 and abs(pg2['bar'] - pg['bar']) < 1 and pg2['level'] == pg['level'],
       f"after the rejoin the guest's own DNA / bar / brain are back ({pg2 and (pg2['dna'], pg2['bar'], pg2['level'])}, "
       f"before {(pg['dna'], pg['bar'], pg['level'])})")
    planet = re.search(r"'([^']+)\.spo'", out(host, 'coop.status'))
    host.call('coop.save')
    time.sleep(6)
    host.call('key enter')
    host.call('coop.menu')
    ck(wait(lambda: 'GalaxyGameEntry' in mode(host), 120), 'the host is in the galaxy menu')
    host.call('wait.frames 240 90000')
    if planet:
        host.call(f'coop.load Game0 {planet.group(1)}')
    ck(wait(lambda: 'Creature' in mode(host), 300, 3), 'the host plays the planet again')
    ck(wait_guest_in(host, guest), 'the guest is in the host\'s world again')
    ck(wait(lambda: 'synced=1' in out(guest, 'coop.progress'), 240, 2), 'the guest asked for its values after the load')
    time.sleep(6)
    pg3 = prog(guest)
    ck(pg3 and abs(pg3['dna'] - pg['dna']) < 1 and abs(pg3['bar'] - pg['bar']) < 1,
       f"after the reload the guest's own DNA / bar are back from <planet>.spo.duoprogress "
       f"({pg3 and (pg3['dna'], pg3['bar'])}, before {(pg['dna'], pg['bar'])})")
    # 4. the host's brain is fully grown first
    host.call('coop.dna gain 3000')
    ck(wait(lambda: prog(host)['full'] == 1, 90, 2), f"the host's brain is fully grown (level {prog(host)['level']})")
    captions(host)
    ck(wait(lambda: count(host, 'full_notices') >= 1, 10), 'the host is told that it waits for the partner')
    holds = count(host, 'holds')
    creature_history_check(host)
    time.sleep(6)
    ck(count(host, 'holds') > holds, 'the host\'s evolve button is refused (every bar is not full)')
    ck('Creature' in mode(host) and not vote_open(host), f'no vote, the host stays in Creature ({mode(host).strip()})')
    host.call('key 27')  # Esc closes History
    guest.call('coop.dna gain 3000')
    ck(wait(lambda: prog(guest)['full'] == 1, 90, 2), "the guest's brain is fully grown")
    captions(guest)
    ck(wait(lambda: prog(host)['all'] == 1 and prog(guest)['all'] == 1, 30), 'every bar is full on both games')
    t0 = time.time()
    while time.time() - t0 < 120 and not vote_open(host) and 'Creature' in mode(host):
        captions(host, 10)
        creature_history_check(host)
        time.sleep(8)
    ck(vote_open(host), f'the host\'s evolve button starts the vote ({out(host, "coop.vote").splitlines()[0]})')
    ready(guest)
    ck(wait(lambda: 'Editor' in mode(host) or 'Tribe' in mode(host), 120), f'the host\'s game opens its editor ({mode(host).strip()})')


def part_off(host, guest, ck):
    ck(sl.force(host, [guest], 'all.own_progress', 'off'), 'the guest plays with all.own_progress off')
    try:
        p = prog(guest)
        ck(p and p['active'] == 0, 'own progress is off on the guest')
        g0 = prog(guest)['dna']
        host.call('coop.dna gain 100')
        ck(wait(lambda: prog(guest)['dna'] >= g0 + 99, 15), f"off: the host's gain reaches the guest's DNA (the shared total): "
                                                               f"{g0} -> {prog(guest)['dna']}")
    finally:
        sl.reset([host, guest], ('all.own_progress',))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--part', choices=('cell', 'creature', 'off'), default='creature')
    ap.add_argument('--first', choices=('guest', 'host'), default='guest', help='cell: who fills its bar first')
    a = ap.parse_args()
    host, guest = start('cell' if a.part == 'cell' else 'creature', own_progress=True)
    ck = Checks()
    if a.part == 'cell':
        part_cell(host, guest, ck, a.first)
    else:
        {'creature': part_creature, 'off': part_off}[a.part](host, guest, ck)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
