#!/usr/bin/env python3
"""Rivals mode (space_rivalry.cpp, docs/09-coop-design.md Space 0.7 #10): two games in space in the
same system.

* the host switches to rivals mode with a real click on the F9 Space page; both games are told;
  the shared money is split (host + guest = the total before);
* the budgets are separate: money the guest earns and the host spends stays with each of them;
* neutral players cannot hurt each other (a hit of the host's ship on the guest's puppet is dropped);
* the guest declares war with a real click; the guest's puppet on the host belongs to no empire;
  the host's shots (real input: palette "combat", key 1, clicks on the guest's ship; if no real
  shot lands, the debug hit through the same hooked TakeDamage) damage the guest's ship on the
  guest's game;
* the guest leaves and joins again: it gets the mode and the war back;
* the host proposes peace (real click), the guest accepts (real click): neutral again;
* the host switches back to allies (real click): the budgets are merged (one total = the sum on
  both games) and PvP stops.

  SPORE_LANE=6 python3 tools/tests/coop_space_rivalry.py
"""
import re
import time

import lane
from coop_ui import VK_F9, Ui
from fixtures import FIXTURES
from play_space import Player
from twoinst import Checks, shot, start, wait_guest_in


def sp(con, *a):
    return con.call('coop.space ' + ' '.join(str(x) for x in a))['out'].strip()


def riv(con):
    """the rivalry status as a dict: rivals, money, rel {id: relation}, hits_sent, ..."""
    out = sp(con, 'rivalry')
    d = {k: float(v) for k, v in re.findall(r'(\w+)=(-?[\d.]+)\b', out)}
    d['rel'] = {int(i): r for i, r in re.findall(r' (\d+)=(ally|neutral|war)\b', out)}
    d['text'] = out
    return d


def money(con):
    m = re.search(r'money=(-?\d+)', sp(con, 'rivalry'))
    return int(m.group(1)) if m else None


def hp(con):
    m = re.search(r'hp ([\d.]+) max ([\d.]+).*state=(\d)', sp(con, 'hp'))
    return (float(m.group(1)), float(m.group(2)), int(m.group(3))) if m else (0, 0, -1)


def wait(pred, t=20, step=0.5):
    t0 = time.time()
    while time.time() - t0 < t:
        v = pred()
        if v:
            return v
        time.sleep(step)
    return pred()


def row_of(con, button):
    """index of the Space page row whose button starts with this caption (coop.space rows)"""
    for line in sp(con, 'rows').splitlines():
        m = re.match(r'(\d+): (.*?) \| (.*)$', line)
        if m and m.group(3).startswith(button):
            return int(m.group(1))
    return None


def open_space_page(ui, con):
    if ui.state().get('open') != '1':
        con.call(f'key {VK_F9}')
    st = ui.wait(lambda s: s['open'] == '1' and (s['page'] == 'space' or s['space']['vis']), 20, 'the lobby')
    if st and st['page'] != 'space':
        ui.click('space')
        st = ui.wait(lambda s: s['page'] == 'space', 10, 'the Space page')
    return st is not None


def press(ui, con, button, ck, who):
    """a real click on the Space page button with this caption"""
    if not open_space_page(ui, con):
        return ck(False, f'{who}: the Space page opens')
    n = wait(lambda: row_of(con, button), 10)
    if n is None:
        return ck(False, f'{who}: the Space page has "{button}" ({sp(con, "rows")!r})')
    st = ui.wait(lambda s: s[f'sbtn{n}']['vis'] and s[f'sbtn{n}']['text'].startswith(button), 10, f'button {button}')
    if not st:
        return ck(False, f'{who}: the button "{button}" is visible')
    ok = ui.click(f'sbtn{n}')
    return ck(ok, f'{who}: clicked "{button}" (row {n})')


def close_page(ui, con):
    if ui.state().get('open') == '1':
        con.call(f'key {VK_F9}')
        con.call('wait.frames 10 5000')


def real_fire(ph, host, guest, secs=25):
    """the host shoots the guest's ship like a player: flies close, combat palette, key 1, clicks on it"""
    ph.regroup()  # (the keys, toward the partner's point on the screen, until < 80 m)
    print('  ', sp(host, 'dist'))
    ph.open_palette('combat')
    host.call('key 1')
    h0 = riv(guest).get('hits_got', 0)
    t0 = time.time()
    while time.time() - t0 < secs:
        me, players, _ = ph.targets()
        p = players.get(1)
        if p:
            host.call(f'click {p[0]} {p[1]}')
        time.sleep(0.6)
        if riv(guest).get('hits_got', 0) > h0:
            break
    for _ in range(int(secs / 0.6)):  # a few more shots
        me, players, _ = ph.targets()
        p = players.get(1)
        if p:
            host.call(f'click {p[0]} {p[1]}')
        time.sleep(0.6)
    host.call('key 0x1B')  # Esc: no tool
    ph.palette = None
    return riv(guest).get('hits_got', 0) - h0


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(5)
    ph = Player('host', lane.HOST, 0, 1, time.time() + 1800, {})
    pg = Player('guest', lane.GUEST, 1, 2, time.time() + 1800, {})
    ph.partner, pg.partner = pg, ph
    home = FIXTURES['space']['home']
    for who, c in (('host', host), ('guest', guest)):
        f = sp(c, 'features')
        ck('rivalry: rivals=0' in f, f'{who}: the rivalry feature is up, allies mode ({f.splitlines()[-1] if f else ""})')
    ck(re.search(r'place 8', sp(guest, 'star')) and sp(host, 'star').split()[1] == sp(guest, 'star').split()[1],
       f'both in the same place ({sp(host, "star")} / {sp(guest, "star")})')
    t0 = wait(lambda: money(host) == money(guest) and money(host), 15)
    total = money(host)
    ck(total and money(guest) == total, f'shared money before: host {money(host)} guest {money(guest)}')
    # both on the home planet (fights happen on planets: the weapons are for the planet view)
    # BUG-0262: one player's landing can take longer than one land_retry right after the load (1 of 4
    # runs: the check ran with one ship still in the system and the host had no puppet of the guest):
    # land whoever is still in the system again, then wait until both are there and the host's game
    # shows the guest's puppet
    def together():
        return ph.place()[0] == pg.place()[0] and ph.ctx() == 'planet' and pg.ctx() == 'planet'
    for rnd in range(2):
        for p in (ph, pg):
            if p.ctx() == 'system':
                p.land_retry(home)
        if wait(together, 30, 1):
            break
        print(f'  not together yet (round {rnd + 1}): host {ph.place()} {ph.ctx()} / guest {pg.place()} {pg.ctx()}')
    ck(together(), f'both on the home planet ({sp(guest, "dist")}; host {ph.place()} {ph.ctx()}, '
       f'guest {pg.place()} {pg.ctx()})')
    ck(wait(lambda: 'puppet 1 ' in sp(host, 'rivalry pol'), 30),
       f'the host\'s game has the guest\'s puppet here ({sp(host, "rivalry pol")})')
    # the host needs some money to split
    if total < 10000:
        host.call('coop.res add money 20000')
        time.sleep(2)
        total = money(host)
    print('  total', total)

    # ---- rivals mode by a real click
    uh, ug = Ui(host, 'host'), Ui(guest, 'guest')
    press(uh, host, 'Switch to rivals', ck, 'host')
    ok = wait(lambda: riv(host).get('rivals') == 1 and riv(guest).get('rivals') == 1, 15)
    ck(ok, 'rivals mode on both games')
    shot(host, 'space_rivalry_mode.png')
    close_page(uh, host)
    time.sleep(2)
    mh, mg = money(host), money(guest)
    ck(mh is not None and mg is not None and abs(mh + mg - total) <= 2 and abs(mh - mg) <= 2,
       f'the money is split fairly: host {mh} + guest {mg} = {total}')
    # separate budgets: the guest earns, the host spends
    guest.call('coop.res add money 5000')
    host.call('coop.res add money -1000')
    time.sleep(4)
    ck(money(guest) == mg + 5000 and money(host) == mh - 1000,
       f'own budgets: host {mh} -> {money(host)} (-1000), guest {mg} -> {money(guest)} (+5000)')
    bh, bg = riv(host)['text'], riv(guest)['text']
    ck(f'1:{mg + 5000}' in bh and f'0:{mh - 1000}' in bg, 'each game knows the other\'s budget (K_MONEY)')
    print('  host:', riv(host)['text'])
    print('  guest:', riv(guest)['text'])

    # neutral: no PvP
    hp0 = hp(guest)
    out = sp(host, 'rivalry shoot 1 20')
    time.sleep(2)
    ck(hp(guest)[0] == hp0[0] and riv(guest).get('hits_got', 0) == 0, f'neutral players cannot hurt each other ({out})')

    # ---- war by the guest's real click
    press(ug, guest, 'Declare war', ck, 'guest')
    chat = ' / '.join(ug.state()['chat'])
    ck('rivals mode' in chat, f'the guest was told about the mode ({chat})')
    ok = wait(lambda: riv(host)['rel'].get(1) == 'war' and riv(guest)['rel'].get(0) == 'war', 15)
    ck(ok, f'war on both games ({riv(host)["rel"]} / {riv(guest)["rel"]})')
    close_page(ug, guest)
    time.sleep(1)
    pol = sp(host, 'rivalry pol')
    ck('puppet 1 ' in pol, f'the host still has the guest\'s puppet ({pol})')
    ck('puppet 1 FFFFFFFF' in pol, f'the guest\'s puppet on the host belongs to no empire ({pol})')
    print('  guest pol:', sp(guest, 'rivalry pol'))

    # ---- the host's shots damage the guest's ship on the guest's game
    hp0 = hp(guest)
    n = real_fire(ph, host, guest)
    ck(True, f'real laser shots of the host that reached the guest: {n}')
    if n == 0:
        print('  (no real shot landed: the debug hit through the hooked TakeDamage)')
        for _ in range(3):
            print('  ', sp(host, 'rivalry shoot 1 15'))
            time.sleep(0.7)
    time.sleep(2)
    hp1 = hp(guest)
    g = riv(guest)
    ck(g.get('hits_got', 0) > 0 and (hp1[0] < hp0[0] or hp1[2] == 2),
       f'the host\'s hits damage the guest\'s ship: hp {hp0[0]:.0f} -> {hp1[0]:.0f} (hits_got={g.get("hits_got")})')
    shot(guest, 'space_rivalry_hit.png')
    # a lethal hit: the native death (no rescue by an enemy), OK on the caption, the respawn
    print('  ', sp(host, 'rivalry shoot 1 5000'))
    st = wait(lambda: hp(guest)[2] == 2, 10)
    ck(st and 'down=0' in sp(guest, 'hp') and riv(guest).get('pvp_deaths') == 1,
       f'a lethal PvP hit destroys the guest\'s ship, the enemy does not rescue it ({sp(guest, "hp")})')
    t0 = time.time()
    while time.time() - t0 < 90 and (hp(guest)[2] == 2 or 'movie=1' in sp(guest, 'hp')):
        guest.call('key enter')
        time.sleep(3)
    h = hp(guest)
    ck(h[2] == 0 and h[0] == h[1], f'the guest\'s ship is respawned natively ({sp(guest, "hp")})')

    # ---- a rejoin gets the mode, the war and the own budget back
    while hp(guest)[2] == 2 or 'movie=1' in sp(guest, 'hp'):  # (a lethal hit: the captions)
        guest.call('key enter')
        time.sleep(2)
    mg_before = money(guest)
    guest.call('coop.leave')
    time.sleep(3)
    guest.call(lane.JOIN)
    wait_guest_in(host, guest, 180)
    ok = wait(lambda: riv(guest).get('rivals') == 1 and riv(guest)['rel'].get(0) == 'war', 60, 1)
    ck(ok, f'after a rejoin the guest has rivals mode and the war ({riv(guest)["text"]})')
    # (the rejoin may reload the host's world: wait until the guest flies again)
    wait(lambda: hp(guest)[2] == 0 and 'place 8' in sp(guest, 'star'), 120, 2)
    while hp(guest)[2] == 2 or 'movie=1' in sp(guest, 'hp'):
        guest.call('key enter')
        time.sleep(2)
    ok = wait(lambda: money(guest) == mg_before, 30, 1)
    ck(ok, f'after the rejoin the guest has its own budget: {mg_before} -> {money(guest)} ({riv(guest)["text"]})')
    # a world load on the guest (the host sends its save again: its money is the host's, not the
    # guest's budget): the guest gets its budget back from the host
    r0 = riv(guest).get('restored', 0)
    host.call('coop.share')
    left = wait(lambda: 'Space' not in guest.call('game.mode')['out'], 60, 0.5)
    ck(left, 'the guest loads the host\'s world again')
    wait(lambda: 'Space' in guest.call('game.mode')['out'] and hp(guest)[1] > 0, 180, 2)
    wait_guest_in(host, guest, 120)
    ok = wait(lambda: money(guest) == mg_before and riv(guest).get('restored', 0) > r0, 40, 1)
    ck(ok, f'after the world load the guest has its own budget again: {money(guest)} == {mg_before} '
           f'({riv(guest)["text"]})')
    while hp(guest)[2] == 2 or 'movie=1' in sp(guest, 'hp'):
        guest.call('key enter')
        time.sleep(2)

    # ---- peace: proposed by the host, accepted by the guest (real clicks)
    press(uh, host, 'Propose peace', ck, 'host')
    ok = wait(lambda: '0>1peace' in riv(guest)['text'], 15)
    ck(ok, 'the guest sees the peace proposal')
    close_page(uh, host)
    press(ug, guest, 'Accept', ck, 'guest')
    ok = wait(lambda: riv(host)['rel'].get(1) == 'neutral' and riv(guest)['rel'].get(0) == 'neutral', 15)
    ck(ok, f'peace: neutral on both games ({riv(host)["rel"]} / {riv(guest)["rel"]})')
    close_page(ug, guest)
    time.sleep(1)
    ck('FFFFFFFF' not in sp(host, 'rivalry pol'), f'the puppet is of our empire again ({sp(host, "rivalry pol")})')

    # ---- back to allies: the budgets are merged, PvP stops
    time.sleep(2)  # the last budget reports
    mh, mg = money(host), money(guest)
    press(uh, host, 'Switch to allies', ck, 'host')
    ok = wait(lambda: riv(host).get('rivals') == 0 and riv(guest).get('rivals') == 0, 15)
    ck(ok, 'allies mode on both games')
    close_page(uh, host)
    ok = wait(lambda: money(host) == money(guest) == mh + mg, 20)
    ck(ok, f'merged: host {mh} + guest {mg} = {mh + mg}: host {money(host)} guest {money(guest)}')
    g0 = riv(guest).get('hits_got', 0)
    sp(host, 'rivalry shoot 1 20')
    time.sleep(2)
    ck(riv(guest).get('hits_got', 0) == g0, 'no PvP in allies mode')
    # shared again: the guest's earnings reach the host
    guest.call('coop.res add money 300')
    ok = wait(lambda: money(host) == money(guest) == mh + mg + 300, 15)
    ck(ok, f'the money is shared again: host {money(host)} guest {money(guest)}')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
