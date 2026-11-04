#!/usr/bin/env python3
"""A partner's colonies as an allied empire (space_allies.cpp, docs/re/space.md 15):

* the host founds a colony in a star system nobody owns (after the guest joined, so the guest's copy
  of the galaxy does not have it); the guest's copy shows that star owned by "Alice's empire"
  (its star record: empire, tech level 5, the planet's cCivData), allied;
* a galaxy view screenshot on the guest; the guest's F9 Space page row "Alice's empire" and its
  "Fly there" button (a real click) take the guest there, it lands: the planet has a city of
  "Alice's empire" and the city does not shoot;
* a raid on that colony in the host's game (the game's `raid -start` code): the guest on the planet
  gets a raid of its own on the mirror colony, beats it (the game's damage on the raid ships), and
  the host's raid is completed ("defended");
* the guest saves with the mirror in its galaxy, the host leaves (the guest's mirror goes), the
  guest loads its own save alone: no trace of the foreign colony or empire;
* the host hosts again, the guest joins again (it gets the host's world, with that colony).

  SPORE_LANE=9 python3 tools/tests/coop_space_allies.py
"""
import re
import time

import fixtures
import lane
from coop_ui import VK_F9, Ui
from twoinst import Checks, shot, start, wait_guest_in

VK_RETURN, VK_ESCAPE = 0x0D, 0x1B
W, H = 1440, 1080


def sp(con, *a):
    return con.call('coop.space ' + ' '.join(str(x) for x in a))['out'].strip()


def place(con):
    m = re.search(r'place ([0-9A-F]+) star (\d+) active ([0-9A-F]+)', sp(con, 'star'))
    return (int(m.group(1), 16), int(m.group(2)), m.group(3)) if m else (0, 0, '')


def context(con):
    p = place(con)[0]
    return 'planet' if p & 0x40000000 else 'system' if p & 0x80000000 else 'galaxy'


def wait(cond, secs, step=1.0):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return None


def no_movie(con, secs=40, esc=False):
    """confirm the captions that wait for OK (tutorial, badges); the intro of the fixture's system
    (the camera orbiting the star) only ends with Esc (esc=True: only there, an Esc during the
    landing left the planet view black)"""
    for i in range(int(secs / 2)):
        if 'movie=0' in sp(con, 'hp'):
            return True
        con.call(f'key {VK_ESCAPE if esc and i % 3 == 2 else VK_RETURN}')
        time.sleep(2)
    return 'movie=0' in sp(con, 'hp')


def star_info(con, star):
    """(empire id, owner name, tech, {PlanetID: (tech, [(civ pol, cities)])}) of a star record"""
    out = sp(con, 'allies star', star)
    m = re.search(r"emp=([0-9A-F]+) owner='(.*)'(?: \(mine\))? tech=(-?\d+)", out)
    planets = {}
    for pm in re.finditer(r"planet ([0-9A-F]+) '[^']*' tech=(-?\d+)((?: civ\(pol=[0-9A-F]+ cities=\d+\))*)", out):
        civ = re.findall(r'civ\(pol=([0-9A-F]+) cities=(\d+)\)', pm.group(3))
        planets[pm.group(1)] = (int(pm.group(2)), [(p, int(n)) for p, n in civ])
    if not m:
        return None
    return m.group(1), m.group(2), int(m.group(3)), planets


def travel(con, star, who):
    print(f'  {who} jumpto {star}:', sp(con, 'jumpto', star), flush=True)
    return wait(lambda: place(con)[1] == star and context(con) == 'system', 40)


def to_galaxy(con):
    for _ in range(16):
        con.call('wheel 720 540 -10')
        time.sleep(0.8)
        if context(con) == 'galaxy':
            return True
    return False


def cities(con):
    """[(distance, on screen, x, y)] of the cities of Alice's empire on this planet"""
    return [(float(m.group(1)), m.group(2) == '1', float(m.group(3)), float(m.group(4)))
            for m in re.finditer(r"owner='Alice's empire' dist (-?[\d.]+) screen (\d) (-?[\d.]+) (-?[\d.]+)",
                                 sp(con, 'allies cities'))]


def fly_to_city(con, secs=90):
    """planet view: steer over the nearest city of Alice's empire with the keys (W up, S down, A
    left, D right on the screen)"""
    t0 = time.time()
    best = 1e9
    while time.time() - t0 < secs:
        cs = cities(con)
        keys = ['0x57']
        if cs:
            d, on, x, y = min(cs)
            best = min(best, d)
            if d < 260:
                return d
            if on and d < 700:
                dx, dy = x - W // 2, y - H // 2
                keys = ([] if abs(dy) <= 30 else ['0x57' if dy < 0 else '0x53']) + \
                       ([] if abs(dx) <= 30 else ['0x44' if dx > 0 else '0x41'])
        keys = keys or ['0x57']
        for k in keys:
            con.call(f'key {k} down')
        time.sleep(1.2 if not cs or min(cs)[0] > 700 else 0.5)
        for k in keys:
            con.call(f'key {k} up')
    return best


def raids(con):
    """[(PlanetID, state, ships, mirror)]"""
    return [(m.group(1), int(m.group(2)), int(m.group(3)), bool(m.group(4)))
            for m in re.finditer(r'raid [0-9A-F]+ on ([0-9A-F]+) state (\d+) bombers \d+ ships (\d+)( \(mirror colony\))?',
                                 sp(con, 'allies raids'))]


def status(con):
    return dict((k, int(v)) for k, v in re.findall(r'(\w+)=(\d+)', sp(con, 'allies status')))


def main():
    host, guest = start('space')
    ck = Checks()
    for who, c in (('host', host), ('guest', guest)):
        ck('allies: mirrors' in sp(c, 'features'), f'{who}: the allies feature is on')
        no_movie(c, esc=True)
    home = place(host)[1]

    # ---- 1. the host founds a colony in a star system nobody owns ----
    free = [int(x) for x in re.findall(r'free (\d+)', sp(host, 'allies free'))]
    ck(bool(free), f'free stars near home: {free[:4]}')
    star = free[1] if len(free) > 1 else free[0]  # (the nearest is the tutorial's ruined colony)
    ck(travel(host, star, 'host') is not None, f'the host is in the system of star {star}')
    out = sp(host, 'allies colonize')
    m = re.search(r'colonized ([0-9A-F]+)', out)
    planet = m.group(1) if m else ''
    ck(bool(planet), f'the host colonized planet {planet}')
    hi = star_info(host, star)
    ck(hi and '(mine)' in sp(host, 'allies star', star) and hi[3].get(planet, (0,))[0] == 5,
       f'the host\'s own star record: its colony {hi}')
    ck(travel(host, home, 'host') is not None, 'the host flies home')

    # ---- 2. the guest's copy: the star belongs to "Alice's empire" ----
    gi = wait(lambda: (lambda i: i if i and i[1] == "Alice's empire" else None)(star_info(guest, star)), 30)
    print('  guest star:', gi, flush=True)
    ck(gi is not None, 'the guest\'s star record is owned by "Alice\'s empire"')
    if gi:
        pol = gi[0]
        pt = gi[3].get(planet)
        ck(gi[2] == 5 and pt and pt[0] == 5 and any(p == pol and n > 0 for p, n in pt[1]),
           f'tech level 5 and the planet\'s cCivData of that empire with cities: {pt}')
        emp = sp(guest, 'allies empire', pol)
        print('  guest empire:', emp, flush=True)
        ck(f"'Alice's empire' stars=1 home={star}" in emp and 'allies=1' in emp, 'the empire: name, its star, allied')
    else:
        pol = ''
    lst = sp(guest, 'allies list')
    ck(f'planet {planet}' in lst and 'skipped' in lst, 'the colonies both have since the join are not mirrored')

    # the galaxy view (the guest gets the interstellar drive)
    print('  drive:', sp(guest, 'tool add interstellardrive'), flush=True)
    no_movie(guest, esc=True)
    ck(to_galaxy(guest), 'the guest zoomed out to the galaxy view')
    no_movie(guest)
    time.sleep(2)
    shot(guest, 'allies_galaxy.png')
    # back into the home system for the trip (the Space page's usual place). A trip into a system
    # never visited before can arrive with the planets not loaded: the planet view stays black after
    # the landing and the cities have no position (docs/known-issues.md "jump into a not yet visited
    # system", fixed on fix/space-jump); the checks here read the game data, not the picture
    for _ in range(12):
        if context(guest) == 'system':
            break
        guest.call('wheel 720 540 10')
        time.sleep(1)
    ck(context(guest) == 'system', 'the guest is back in its home system')
    no_movie(guest)

    # ---- 3. the Space page: "Fly there" (a real click) ----
    rows = sp(guest, 'rows')
    print('  guest rows:', rows.replace('\n', ' | '), flush=True)
    idx = next((int(r.split(':')[0]) for r in rows.splitlines() if "Alice's empire: colonies in your galaxy: 1" in r), -1)
    ck(idx >= 0 and 'Fly there' in rows.splitlines()[idx], 'the Space page has the row of Alice\'s empire with "Fly there"')
    ui = Ui(guest, 'guest')
    if ui.state().get('open') != '1':
        guest.call(f'key {VK_F9}')
    ck(ui.wait(lambda s: s['open'] == '1' and s['space']['vis'], 20, 'lobby') is not None, 'F9: the lobby')
    ui.click('space')
    st = ui.wait(lambda s: s['page'] == 'space' and s[f'sbtn{idx}']['vis'], 10, 'Space page')
    ck(st is not None and st[f'sbtn{idx}']['text'] == 'Fly there', 'the Space page shows the button')
    shot(guest, 'allies_page.png')
    rows2 = sp(guest, 'rows')  # the page may have changed since (a row more shifts every button)
    if rows2 != rows:
        print('  guest rows now:', rows2.replace('\n', ' | '), flush=True)
    idx2 = next((int(r.split(':')[0]) for r in rows2.splitlines() if "Alice's empire: colonies in your galaxy: 1" in r), idx)
    ui.click(f'sbtn{idx2}')
    guest.call(f'key {VK_F9}')
    ck(wait(lambda: place(guest)[1] == star and context(guest) == 'system', 40), f'the button took the guest to star {star}')
    no_movie(guest)
    ck(place(guest)[2] == planet, f'the active planet is the colony ({place(guest)[2]})')

    # ---- 4. on the planet: a city of Alice's empire ----
    print('  land:', sp(guest, 'land'), flush=True)
    ck(wait(lambda: context(guest) == 'planet', 40), 'the guest landed on the colony')
    no_movie(guest)
    hp0 = re.search(r'hp ([\d.]+)', sp(guest, 'hp'))
    cities = wait(lambda: (lambda o: o if "owner='Alice's empire'" in o else None)(sp(guest, 'allies cities')), 20)
    ck(cities is not None, f'a city of Alice\'s empire on the planet: {cities}')
    d = fly_to_city(guest, 40)
    print(f'  guest near the city: {d:.0f} m', flush=True)
    time.sleep(2)
    shot(guest, 'allies_planet.png')
    hp1 = re.search(r'hp ([\d.]+)', sp(guest, 'hp'))
    ck(hp0 and hp1 and float(hp1.group(1)) >= float(hp0.group(1)), 'the allied city does not shoot at the guest')

    # ---- 5. a raid on the host's colony, fought in the guest's galaxy ----
    print('  host raid:', sp(host, 'allies raid', planet, 3), flush=True)
    hr = wait(lambda: [r for r in raids(host) if r[0] == planet and r[1] == 3], 10)
    ck(bool(hr), f'the host\'s colony is raided: {hr}')
    gr = wait(lambda: [r for r in raids(guest) if r[0] == planet and r[3] and r[2] > 0], 40)
    ck(bool(gr), f'the guest got a raid of its own on the mirror colony, with ships: {gr}')
    shot(guest, 'allies_raid.png')
    for _ in range(30):
        if not [r for r in raids(guest) if r[0] == planet]:
            break
        print('  fight:', sp(guest, 'allies fight 400'), flush=True)
        time.sleep(2)
    ck(wait(lambda: status(guest).get('raids_won', 0) >= 1, 20), f'the guest beat the raid: {sp(guest, "allies status")}')
    ck(wait(lambda: not [r for r in raids(host) if r[0] == planet], 20), 'the host\'s raid is over')
    ck(status(host).get('raids_completed', 0) >= 1, f'the host\'s raid was completed: {sp(host, "allies status")}')

    # ---- 6. the guest saves with the mirror in its galaxy ----
    ck(travel(guest, home, 'guest') is not None, 'the guest flies home (the game saves only off-planet)')
    no_movie(guest)
    saves0 = status(guest).get('saves', 0)
    print('  guest save:', guest.call('coop.save')['out'].strip(), flush=True)
    ck(wait(lambda: status(guest).get('saves', 0) > saves0, 20), 'the guest\'s save went through the hook (mirror out)')
    for _ in range(6):  # the "game saved" box
        guest.call(f'key {VK_RETURN}')
        time.sleep(1)
    ck(wait(lambda: status(guest).get('applied', 0) == 1 and (star_info(guest, star) or ('',))[0] == pol, 20),
       'after the save the mirror is back')
    slot = re.search(r'Games/(Game\d+)/', guest.call('coop.savedb')['out'])
    slot = slot.group(1) if slot else 'Game1'

    # ---- 7. the host leaves: the mirror goes ----
    print('  host leaves:', host.call('coop.leave')['out'].strip(), flush=True)
    gone = wait(lambda: (lambda i: i if i and i[0] == 'FFFFFFFF' else None)(star_info(guest, star)), 45)
    ck(gone is not None, f'the session ended: the star is nobody\'s again in the guest\'s galaxy ({gone})')
    ck(gone and gone[3].get(planet, (5,))[0] != 5, f'the planet is not a colony there any more: {gone and gone[3].get(planet)}')

    # ---- 8. the guest loads its own save alone: clean ----
    print('  guest menu:', guest.call('coop.menu')['out'].strip(), flush=True)
    ck(wait(lambda: 'GalaxyGameEntry' in guest.call('game.mode')['out'], 90), 'the guest is in the galaxy menu')
    guest.call('wait.frames 240 90000')
    print('  guest load:', guest.call(f'coop.load {slot} {fixtures.planet_of("space").split("/")[1]}')['out'].strip(), flush=True)
    ck(wait(lambda: 'Space' in guest.call('game.mode')['out'] and place(guest)[1] != 0, 180, 2),
       'the guest plays its own save again')
    no_movie(guest)
    time.sleep(3)
    ci = star_info(guest, star)
    print('  guest star after the load:', ci, flush=True)
    ck(ci and ci[0] == 'FFFFFFFF' and ci[1] == '-', 'the loaded save: the star belongs to nobody')
    ck(ci and ci[3].get(planet, (0,))[0] != 5 and not any(c for _, c in ci[3].values()),
       'the loaded save: no colony and no foreign cCivData on its planets')
    ck(pol and sp(guest, 'allies empire', pol).startswith('no empire'), f'the loaded save: no empire {pol}')

    # ---- 9. the host hosts again, the guest joins again ----
    host.call(lane.HOST_CMD)
    time.sleep(2)
    guest.call(lane.JOIN)
    wait_guest_in(host, guest)
    time.sleep(8)
    no_movie(guest)
    ri = wait(lambda: star_info(guest, star), 20)
    print('  guest star after the rejoin:', ri, flush=True)
    ck(ri and '(mine)' in sp(guest, 'allies star', star),
       'after the rejoin the guest plays the host\'s world: the colony is in it (shared, no mirror needed)')
    ck('skipped' in sp(guest, 'allies list') or status(guest).get('applied', 0) == 0, 'no mirror of a shared colony')

    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
