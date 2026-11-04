#!/usr/bin/env python3
"""Partners and their colonies on the galaxy map (space_map.cpp, docs/re/space.md 13):

* both games build the roster of their empire and get the partner's (home star, empire, colonies);
* the guest founds a colony of its own with the game's colony tool (Colony Incredi-Pak, fired at the
  ground of a planet of the home system): the host's roster of the guest has it at once;
* the guest flies to a neighbouring star, the host zooms out to the galaxy view: the host draws the
  guest's name tag at the guest's star (the screen point checked against the star's projected
  position) and a ring at the colony's star; screenshots;
* a real click on the tag takes the host to the guest's star (free, like the 0.6 jump);
* the Space page (F9, real clicks) lists the guest's colony with "Fly there": the host flies there
  and pays the game's own price for the trip;
* both in the galaxy view: the tag is on the guest's ship, a click on it teleports the host there
  (SpaceTeleportTo: free, instant);
* the guest leaves and joins again: both have each other's roster again, and the guest's own colony
  is back in its empire (the host kept it, BUG-0195; SPORE_FIX_OFF=space.keep_colonies: gone).

  SPORE_LANE=2 python3 tools/tests/coop_space_map.py
"""
import os
import re
import time

import fixtures
import lane
from coop_ui import VK_F9, Ui
from twoinst import Checks, shot, start, wait_guest_in

VK_RETURN = 0x0D
DRIVE = 'interstellardrive'


def sp(con, *a):
    return con.call('coop.space ' + ' '.join(str(x) for x in a))['out'].strip()


def place(con):
    m = re.search(r'place ([0-9A-F]+) star (\d+) active ([0-9A-F]+)', sp(con, 'star'))
    return (int(m.group(1), 16), int(m.group(2)), m.group(3)) if m else (0, 0, '')


def context(con):
    p = place(con)[0]
    return 'planet' if p & 0x40000000 else 'system' if p & 0x80000000 else 'galaxy'


def rosters(con):
    """{id: {'home': star, 'place': place, 'empire': name, 'colonies': [(planet hex, name, own)]}}"""
    out, cur = {}, None
    for line in sp(con, 'map roster').splitlines():
        m = re.match(r"roster (\d+) .* v(\d+) home (\d+)/([0-9A-F]+) place ([0-9A-F]+) .* empire '([^']*)'", line)
        if m:
            cur = out[int(m.group(1))] = dict(version=int(m.group(2)), home=int(m.group(3)), place=int(m.group(5), 16),
                                              empire=m.group(6), colonies=[])
            continue
        m = re.match(r"\s+colony ([0-9A-F]+) star (\d+) '([^']*)'.* (own|shared)$", line)
        if m and cur is not None:
            cur['colonies'].append((m.group(1), m.group(3), m.group(4) == 'own'))
    return out


def wait(cond, secs, step=1.0):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return None


def no_movie(con, secs=30):
    """confirm the captions that wait for OK (tutorial, badges) until no cinematic runs"""
    for i in range(int(secs / 2)):
        if 'movie=0' in sp(con, 'hp'):
            return True
        # Enter, and every other time the caption's green check button at the bottom right (on GOG
        # 2024 a caption once stayed through Enter for a minute)
        con.call(f'key {VK_RETURN}' if i % 2 == 0 else 'click 1410 1048')
        time.sleep(2)
    return 'movie=0' in sp(con, 'hp')


def targets(con):
    out = {}
    for line in con.call('coop.targets all')['out'].splitlines():
        p = line.split()
        if p[0] == 'obj' and len(p) == 5:
            out[p[1]] = (float(p[2]), int(p[3]), int(p[4]))
    return out


def land_on(con, planet_hex, who):
    """system view: click the planet until it is the active one, fly close, land (the respawn's
    landing, coop.space land)"""
    key = 'P' + planet_hex
    for attempt in range(8):
        t = targets(con)
        if key not in t:
            con.call('wheel 720 540 -4')  # zoom out to see the system
            time.sleep(1.5)
            continue
        _, x, y = t[key]
        con.call(f'click {x} {y}')
        time.sleep(1.5)
        if place(con)[2] == planet_hex:
            break
    else:
        print(f'  {who}: could not pick planet {planet_hex}', flush=True)
        return False
    wait(lambda: targets(con).get(key, (1e9,))[0] < 40, 30)
    print(f'  {who} land:', sp(con, 'land'), flush=True)
    return wait(lambda: context(con) == 'planet', 30) is not None


def to_galaxy(con):
    for _ in range(16):
        con.call('wheel 720 540 -10')
        time.sleep(0.8)
        if context(con) == 'galaxy':
            return True
    return False


def tags(con):
    out = []
    for line in sp(con, 'map tags').splitlines():
        m = re.match(r'tag (\d+) (colony|player) star (\d+) box (-?\d+) (-?\d+) (-?\d+) (-?\d+) at (-?\d+) (-?\d+)', line)
        if m:
            v = [int(m.group(i)) for i in (1, 3, 4, 5, 6, 7, 8, 9)]
            out.append(dict(id=v[0], kind=m.group(2), star=v[1], box=v[2:6], at=v[6:8]))
    return out


def projected(con, star):
    m = re.search(r'screen (\d) (-?\d+) (-?\d+)', sp(con, 'map project', star))
    return (int(m.group(2)), int(m.group(3))) if m and m.group(1) == '1' else None


def money(con):
    time.sleep(2)  # (the own roster is read every second)
    m = re.search(r'roster 0 .* money (-?\d+)', sp(con, 'map roster 0'))
    return int(m.group(1)) if m else None


def main():
    host, guest = start('space')
    ck = Checks()
    for c in (host, guest):
        print('  drive:', sp(c, 'tool add', DRIVE), flush=True)
        no_movie(c)
    home = int(fixtures.FIXTURES['space'].get('home', '0'), 16) & 0xFFFFFF or place(host)[1]

    # ---- 1. rosters on both sides ----
    r = wait(lambda: (lambda h, g: (h, g) if 1 in h and 0 in g and h[1]['colonies'] and g[0]['colonies'] else None)(
        rosters(host), rosters(guest)), 20)
    ck(r is not None, 'both games have the partner\'s roster with colonies')
    if r:
        h, g = r
        print('  host sees Bob:', h[1], flush=True)
        ck(h[1]['home'] == home and h[1]['empire'] != '', f'the roster has the home star {home} and the empire name')
        ck(all(not own for _, _, own in h[1]['colonies']), 'at the start every colony of the guest is shared (one empire)')
    print('  host map:', sp(host, 'map status'), flush=True)

    # ---- 2. the guest founds a colony of its own (the colony tool on a planet of the home system) ----
    target = None
    for line in sp(guest, 'map planets', home).splitlines():
        m = re.match(r"\s+planet ([0-9A-F]+) '([^']*)' type (\d+) tech (\d+)", line)
        if m and m.group(4) != '5' and m.group(3) in ('2', '3', '4'):
            target = (m.group(1), m.group(2))
            if m.group(3) == '3':
                break
    ck(target is not None, f'a planet to colonise in the home system: {target}')
    print('  colony tool:', sp(guest, 'tool add placecolony'), flush=True)
    landed = bool(target) and land_on(guest, target[0], 'guest')
    ck(landed, f'the guest landed on {target}')
    no_movie(guest)
    guest.call('move 720 680')  # the terrain cursor under the ship
    time.sleep(1.5)
    print('  colonize:', sp(guest, 'map colonize'), flush=True)
    def new_colony():  # (the drop plays a caption that waits for OK: the colony is founded after it)
        if 'movie=1' in sp(guest, 'hp'):
            guest.call(f'key {VK_RETURN}')
        return [c for c in rosters(host).get(1, {}).get('colonies', []) if c[2]]
    t0 = time.time()
    col = wait(new_colony, 60, 2)
    print(f'  the colony is in the host\'s roster of the guest after {time.time() - t0:.1f} s', flush=True)
    ck(bool(col) and col[0][0] == (target[0] if target else ''), f'the host\'s roster of the guest has the new colony: {col}')
    shot(guest, 'map_colony_guest.png')

    # ---- 3. the guest flies to a neighbouring star; the host looks at the galaxy view ----
    # the nearest other star (the galaxy view at the host's zoom shows the neighbours)
    near, hp, best = None, None, 1e9
    stars = []
    for line in sp(host, 'map planets').splitlines():
        m = re.match(r"star (\d+) '([^']*)' empire (\w+) type \d+ at (\S+) (\S+) (\S+)", line)
        if m:
            stars.append((int(m.group(1)), m.group(3), tuple(float(m.group(i)) for i in (4, 5, 6))))
    hp = next((p for s_, _, p in stars if s_ == home), None)
    for s_, emp, p in stars:
        d = sum((a - b) ** 2 for a, b in zip(p, hp)) ** 0.5 if hp else 0
        if s_ != home and emp == 'FFFFFFFF' and d < best:
            near, best = s_, d
    print(f'  nearest star {near} ({best:.2f} away)', flush=True)
    for _ in range(10):  # (the colony's captions wait for OK; no jump while a cinematic plays)
        no_movie(guest)
        r = sp(guest, 'jumpto', near)
        print('  guest to', near, r, flush=True)
        if r == 'jumping':
            break
        time.sleep(2)
    ck(wait(lambda: place(guest)[1] == near, 30) is not None, f'the guest is at star {near}')
    ck(to_galaxy(host), 'the host zoomed out to the galaxy view')
    no_movie(host)
    time.sleep(2)
    t = wait(lambda: [x for x in tags(host) if x['kind'] == 'player' and x['id'] == 1 and x['star'] == near and
                      x['at'][0] >= 0] or None, 20)
    pt = projected(host, near)
    print('  host tags:', tags(host), 'star on screen:', pt, flush=True)
    ck(bool(t) and pt is not None and abs(t[0]['at'][0] - pt[0]) <= 3 and abs(t[0]['at'][1] - pt[1]) <= 3,
       f'the host draws Bob\'s tag at Bob\'s star ({t[0]["at"] if t else None} ~ {pt})')
    if t:
        b = t[0]['box']
        ck(b[1] < pt[1] < b[3] + 40 and b[0] <= pt[0] <= b[2], 'the tag sits right above the star')
    ct = [x for x in tags(host) if x['kind'] == 'colony' and x['id'] == 1]
    ck(bool(ct) and ct[0]['star'] == home, f'the guest\'s colony star is marked on the host\'s map: {ct}')
    shot(host, 'map_galaxy_host.png')
    # under the pause menu the tags are hidden (coop.cpp hides the world labels, menu_up)
    host.call('key 27')  # Esc: the pause menu
    time.sleep(1.5)
    hidden = wait(lambda: not tags(host), 5) is not None
    shot(host, 'map_galaxy_menu.png')
    host.call('key 27')  # Esc again: back to the game
    back = wait(lambda: tags(host), 10) is not None
    if not back:  # (the menu stayed: "Resume Game")
        host.call('click 668 419')
        back = wait(lambda: tags(host), 10) is not None
    ck(hidden and back, f'no tags while the pause menu is up (hidden {hidden}, back {back})')

    # ---- 4. a real click on the tag: the host travels to the guest ----
    t = [x for x in tags(host) if x['kind'] == 'player' and x['id'] == 1]
    if t:
        b = t[0]['box']
        host.call(f'click {(b[0] + b[2]) // 2} {(b[1] + b[3]) // 2}')
    ck(wait(lambda: place(host)[1] == near, 30) is not None, f'a click on Bob\'s tag took the host to star {near}')
    print('  host map:', sp(host, 'map status'), flush=True)
    no_movie(host)

    # ---- 5. the Space page lists the guest's colony: "Fly there" (the game's price) ----
    rows = sp(host, 'rows')
    print('  host rows:', rows.replace('\n', ' | '), flush=True)
    ck(f"colony {target[1] if target else '?'} (" in rows and 'Fly there' in rows, 'the Space page lists Bob\'s colony')
    cost = re.search(r'cost (-?\d+) money (-?\d+)', sp(host, 'map cost', home))
    m0 = int(cost.group(2)) if cost else None
    ui = Ui(host, 'host')
    if ui.state().get('open') != '1':
        host.call(f'key {VK_F9}')
    st = ui.wait(lambda s: s['open'] == '1' and s['space']['vis'], 20, 'lobby with the Space button')
    ck(st is not None, 'F9: the lobby shows "Space >"')
    ui.click('space')
    lines = rows.splitlines()
    idx = next((i for i, l in enumerate(lines) if f"colony {target[1] if target else '?'} (" in l), -1)
    st = ui.wait(lambda s: s['page'] == 'space' and s.get(f'sbtn{idx}', {}).get('vis'), 10, 'Space page')
    ck(st is not None and idx >= 0, f'the colony row {idx} has its button')
    shot(host, 'map_space_page.png')
    if idx >= 0:
        ui.click(f'sbtn{idx}')
    ck(wait(lambda: place(host)[1] == home, 30) is not None, 'the button took the host to the colony\'s star')
    m1 = money(host)
    print(f'  trip cost {cost.group(1) if cost else "?"}: money {m0} -> {m1}', flush=True)
    # (the game's galaxy flight price is 0 in this game: TravelToStar charges 0xFFC390, 0 for every star)
    ck(cost is not None and m0 is not None and m1 is not None and m0 - m1 == int(cost.group(1)) >= 0,
       'the trip to the colony cost the game\'s price for the distance')
    ui.click('sback')
    host.call(f'key {VK_F9}')

    # ---- 6. a partner in the galaxy view: the tag is on its ship, a click teleports next to it ----
    ck(to_galaxy(guest) and to_galaxy(host), 'both in the galaxy view')
    no_movie(guest)
    no_movie(host)
    t = wait(lambda: [x for x in tags(host) if x['kind'] == 'player' and x['id'] == 1 and x['star'] == 0 and
                      x['at'][0] >= 0] or None, 15)
    print('  host tags:', tags(host), flush=True)
    ck(bool(t), 'the host draws Bob\'s tag on Bob\'s ship in the galaxy view')
    shot(host, 'map_galaxy_flying.png')
    if t:
        b = t[0]['box']
        host.call(f'click {(b[0] + b[2]) // 2} {(b[1] + b[3]) // 2}')

    def gpos(con, who):
        m = re.search(rf'roster {who} .* at (\S+) (\S+) (\S+) empire', sp(con, 'map roster', who))
        return tuple(float(m.group(i)) for i in (1, 2, 3)) if m else None

    def close():
        a, b = gpos(host, 0), gpos(host, 1)
        return a and b and sum((x - y) ** 2 for x, y in zip(a, b)) ** 0.5 < 0.3
    ck(wait(close, 15) is not None and context(host) == 'galaxy',
       f'a click on the tag put the host next to Bob in the galaxy view ({gpos(host, 0)} ~ {gpos(host, 1)})')

    # ---- 7. rejoin: the rosters come back ----
    # (the host back home first: the rejoined guest loads the host's world where the host is)
    for _ in range(5):
        no_movie(host)
        if sp(host, 'jumpto', home) == 'jumping':
            break
        time.sleep(2)
    wait(lambda: place(host)[1] == home, 30)
    guest.call('coop.leave')
    ck(wait(lambda: 1 not in rosters(host), 15) is not None, 'the guest left: the host dropped its roster')
    time.sleep(2)
    guest.call(lane.JOIN)
    ck(wait_guest_in(host, guest, timeout=120), 'the guest is back')
    no_movie(guest)
    r = wait(lambda: (lambda h, g: (h, g) if 1 in h and 0 in g else None)(rosters(host), rosters(guest)), 20)
    ck(r is not None, 'after the rejoin both games have the partner\'s roster again')
    if r:
        own = [c for c in r[0][1]['colonies'] if c[2]]
        if 'space.keep_colonies' in os.environ.get('SPORE_FIX_OFF', ''):  # 0.7.5: the host kept nothing
            ck(not own, 'the rejoined guest plays the host\'s world again: its own colony is gone from its roster')
        else:  # BUG-0195: the host gave it back
            ok = wait(lambda: [c for c in rosters(host).get(1, {}).get('colonies', []) if c[2] and col and c[0] == col[0][0]], 30)
            ck(ok is not None, f'the rejoined guest has its own colony again (the host\'s roster of it: {ok or own})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
