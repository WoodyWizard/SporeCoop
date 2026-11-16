#!/usr/bin/env python3
"""The co-op jump into star systems nobody visited yet (docs/re/space.md 7.3, the known issue
"jumpto into another system arrives without planets"): from a system, from a planet and from the
galaxy view, into NPC empires' systems and into empty ones. After every jump the jumping game has
the new system's planets (coop.targets all), an active planet of that star, the empire's comm
(first contact through the comm screen, real clicks) and the partner sees it leave and arrive.

  SPORE_LANE=7 python3 tools/tests/coop_space_jump.py
"""
import re
import time

from harness import sp
from coop_ui import VK_F9, Ui
from twoinst import Checks, shot, start


def place(con):
    m = re.search(r'place ([0-9A-F]{8}) star (\d+) active ([0-9A-F]{8})', sp(con, 'star'))
    return (int(m.group(1), 16), int(m.group(2)), int(m.group(3), 16)) if m else (0, 0, 0)


def movie(con):
    return 'movie=1' in sp(con, 'hp')


def settle(con, who, t=60):
    """confirm the captions that wait for OK (the tutorial's galaxy caption, the first contact
    intercept) until no cinematic runs for 4 s"""
    t0, calm = time.time(), 0
    while time.time() - t0 < t:
        if movie(con):
            calm = 0
            con.call('key enter')
            time.sleep(3)
        else:
            calm += 1
            if calm >= 4:
                return True
            time.sleep(1)
    print(f'  ({who}: a cinematic still runs after {t} s)')
    return False


def here(con):
    """the 'here' line of coop.space empire near: (pol, contact flags, planet count)"""
    m = re.search(r'here ([0-9A-F]{8}) pol=([0-9A-F]{8}) contact=\d flags=([0-9A-F]+) visited=\d planets=(\d+)',
                  sp(con, 'empire', 'near', 0))
    return (int(m.group(2), 16), int(m.group(3), 16), int(m.group(4))) if m else (0, 0, 0)


def system_planets(con, star_id):
    """(PlanetIDs of that star loaded in the system, on screen or not; PlanetIDs among the click targets
    on the screen, any star)"""
    loaded = [int(x[1:], 16) for x in sp(con, 'planets').split() if len(x) == 9]
    shown = [int(x, 16) for x in re.findall(r'obj [PC]([0-9A-F]{8})', con.call('coop.targets all')['out'])]
    return [i for i in loaded if i & 0xFFFFFF == star_id], shown


def fresh(con, kind, n=6, skip=()):
    """unvisited stars of that kind (npc / free) near the active star, nearest first"""
    out = sp(con, 'empire', 'near', n)
    found = []
    for m in re.finditer(r'^(npc|free) ([0-9A-F]{8}) pol=\S+ d=\S+ contact=(\d) flags=\S+ visited=(\d)', out, re.M):
        sid = int(m.group(2), 16)
        if m.group(1) == kind and m.group(3) == '0' and m.group(4) == '0' and sid not in skip:
            found.append(sid)
    return found


def arrived(ck, con, who, star_id, what):
    """the jumping game is in star_id's system with its planets loaded"""
    for _ in range(20):
        if place(con)[1] == star_id:
            break
        time.sleep(1)
    settle(con, who)
    pl, st, act = place(con)
    ck(st == star_id and pl == (star_id | 0x80000000), f'{who} {what}: in the system of {star_id:08X} ({pl:08X})')
    ck(act and act & 0xFFFFFF == star_id, f'{who} {what}: the active planet belongs to that star ({act:08X})')
    for _ in range(10):  # (all planet objects of the records, polled)
        mine, shown = system_planets(con, star_id)
        pol, flags, n = here(con)
        if n and len(mine) == n:
            break
        time.sleep(1)
    ck(n > 0 and len(mine) == n, f'{who} {what}: the planets are loaded ({len(mine)} of {n} planet records)')
    ck(shown and all(i & 0xFFFFFF == star_id for i in shown),
       f'{who} {what}: the click targets are planets of that star ({len(shown)} on the screen)')
    return pol, flags


def comm_contact(ck, con, who, pol):
    """first contact the player's way: open the empire's comm panel (bottom left, next to the system
    name), answer, say goodbye; the contact flag (cPlayer +0x11D8 bit 1) is set"""
    for attempt in range(3):
        con.call('click 288 914')  # the comm tab above the empire's card
        time.sleep(4)
        shot(con, f'space_jump_comm_{who}{attempt}.png')
        for _ in range(3):
            con.call('click 720 725')  # the first answer ("I come in peace", "It is an honor")
            time.sleep(3)
        con.call('click 424 882')  # Goodbye
        time.sleep(3)
        if here(con)[1] & 2:
            break
    pol2, flags, _ = here(con)
    ck(pol2 == pol and flags & 2, f'{who}: first contact with the empire {pol:08X} through its comm (flags {flags:X})')
    ck(f'contact {pol:08X}' in sp(con, 'empire'), f'{who}: coop.space empire lists the contact')


def bob_row(con):
    """(index, button) of the guest's place row of the page (space_map.cpp: "Bob: at <star> | Fly there"), or None"""
    for m in re.finditer(r'^(\d+): Bob: [^|]*?(?: \| (.*))?$', sp(con, 'rows'), re.M):
        return int(m.group(1)), m.group(2) or ''
    return None


def partner_row(host, text, t=30):
    """text 'away': the guest's row offers "Fly there"; 'here': together, no button"""
    for _ in range(t):
        b = bob_row(host)
        if b and (b[1] == 'Fly there') == (text == 'away'):
            return True
        time.sleep(1)
    print('  host rows:', sp(host, 'rows').replace('\n', ' | '))
    return False


def same_planets(ck, host, guest, star_id):
    """did both games generate the same planets for the star (seeded generation, docs/re/space.md 7.3)"""
    hp, gp = sorted(system_planets(host, star_id)[0]), sorted(system_planets(guest, star_id)[0])
    # not a check yet: the same in most sessions, not in all (docs/known-issues.md)
    print(f'  {"same" if hp and hp == gp else "DIFFERENT"} planets of {star_id:08X} in both games: '
          f'host {[f"{i:08X}" for i in hp]} guest {[f"{i:08X}" for i in gp]}')


def page_jump(ck, ui, con, who):
    """F9, "Space >" unless the window reopens on the Space page, "Fly there", F9 (real clicks)"""
    if ui.state().get('open') != '1':
        con.call(f'key {VK_F9}')
    st = ui.wait(lambda s: s['open'] == '1' and (s['page'] == 'space' or s['space']['vis']), 20, 'co-op window')
    if st and st['page'] != 'space':
        ui.click('space')
    n = (bob_row(con) or (0,))[0]
    st = ui.wait(lambda s: s['page'] == 'space' and s[f'sbtn{n}']['vis'], 10, 'Space page')
    ck(st is not None and 'Fly' in st[f'sbtn{n}']['text'], f'{who}: the Space page offers the jump (row {n})')
    ui.click(f'sbtn{n}')
    time.sleep(2)
    if ui.state().get('open') == '1':
        con.call(f'key {VK_F9}')


def main():
    host, guest = start('space_empire')
    ck = Checks()
    time.sleep(5)
    home = place(host)[1]
    ck(home and place(guest)[1] == home, f'both games in the home system {home:08X}')
    npcs, frees = fresh(guest, 'npc'), fresh(guest, 'free')
    print('  unvisited NPC stars', [f'{s:08X}' for s in npcs], 'free', [f'{s:08X}' for s in frees])
    if not ck(len(npcs) >= 2 and len(frees) >= 2, 'the fixture has unvisited NPC and empty systems near home'):
        return ck.result()
    n1, n2, f1, f2 = npcs[0], npcs[1], frees[0], frees[1]

    # 1. the guest jumps from its home system into an NPC empire's system nobody visited
    r = sp(guest, 'jumpto', hex(n1))
    print('  guest jumpto', f'{n1:08X}:', r)
    pol, flags = arrived(ck, guest, 'guest', n1, 'system -> unvisited NPC system')
    ck(flags & 0x20, f'guest: the empire {pol:08X} knows of the player now (flags {flags:X})')
    shot(guest, 'space_jump_npc.png')
    ck(partner_row(host, 'away'), 'the host sees the guest leave')
    comm_contact(ck, guest, 'guest', pol)
    settle(guest, 'guest')

    # 2. the host follows with the Space page button (real clicks): system -> that system
    ui = Ui(host, 'host')
    page_jump(ck, ui, host, 'host (system)')
    arrived(ck, host, 'host', n1, 'Jump there (system)')
    ck(partner_row(host, 'here'), 'the host is together with the guest again')
    same_planets(ck, host, guest, n1)

    # 3. the guest lands, then jumps from the planet into an empty unvisited system
    print('  guest land:', sp(guest, 'land'))
    for _ in range(30):
        if place(guest)[0] & 0x40000000:
            break
        time.sleep(1)
    settle(guest, 'guest')
    ck(place(guest)[0] & 0x40000000, f'guest on a planet ({place(guest)[0]:08X})')
    print('  guest jumpto', f'{f1:08X}:', sp(guest, 'jumpto', hex(f1)))
    pol, _ = arrived(ck, guest, 'guest', f1, 'planet -> unvisited empty system')
    ck(pol == 0xFFFFFFFF, 'the empty system has no empire')
    shot(guest, 'space_jump_free.png')
    ck(partner_row(host, 'away'), 'the host sees the guest leave the planet')

    # 4. the host zooms out to the galaxy view and jumps to the guest from there (page button)
    for _ in range(4):
        host.call('wheel 720 540 -10')
        time.sleep(2)
    settle(host, 'host')
    ck(place(host)[0] == 0, f'host: in the galaxy view ({place(host)[0]:08X})')
    page_jump(ck, ui, host, 'host (galaxy view)')
    arrived(ck, host, 'host', f1, 'Jump there (galaxy view -> unvisited empty system)')
    ck(partner_row(host, 'here'), 'together in the empty system')
    same_planets(ck, host, guest, f1)

    # 5. the guest zooms out and jumps from the galaxy view into a second unvisited NPC system
    for _ in range(4):
        guest.call('wheel 720 540 -10')
        time.sleep(2)
    settle(guest, 'guest')
    ck(place(guest)[0] == 0, f'guest: in the galaxy view ({place(guest)[0]:08X})')
    print('  guest jumpto', f'{n2:08X}:', sp(guest, 'jumpto', hex(n2)))
    pol, flags = arrived(ck, guest, 'guest', n2, 'galaxy view -> unvisited NPC system')
    ck(flags & 0x20, f'guest: the second empire {pol:08X} knows of the player (flags {flags:X})')
    comm_contact(ck, guest, 'guest', pol)
    ck(partner_row(host, 'away'), 'the host sees the guest leave again')

    st = host.call('coop.status')['out']
    ck('together 1 1' in st or 'players=2' in st, 'host: the session still has both players')
    for who, con, n in (('guest', guest, 3), ('host', host, 2)):
        m = re.search(r'jumps=(\d+)', con.call('coop.space')['out'])
        ck(m and int(m.group(1)) == n, f'{who}: {n} jumps counted ({m and m.group(1)})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
