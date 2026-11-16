#!/usr/bin/env python3
"""Co-op contracts (mods/coop/space_contracts.cpp, docs/09-coop-design.md 0.7 feature 7), played on
two games the way players do:

1. pirate armada on the host's homeworld: the host offers it (coop.space contracts offer armada
   here), both games show it on the F9 Space page, both players accept it with a real click on
   "Accept" and land with the page's "Land" button; the waves (the game's own pirate raids, with
   tougher ships) come because both ships are on the colony; both players shoot the raid ships
   (guest shots go through the host, coop.fleet shoot); the reward arrives on both games: the money
   once (shared budget, plus spacecoop's +2000 per raid defended together), the tool on each;
2. joint survey of 2 stars: accepted by clicks, both players fly to each star with the page's
   "Jump there" button (real clicks), the reward arrives on both;
3. failure: a survey with a 25 s limit that nobody flies: failed on both games, announced, no money.

  SPORE_LANE=3 python3 tools/tests/coop_space_contracts.py
"""
import re
import time

from harness import sp, waiter
from coop_ui import VK_F9, Ui
from twoinst import Checks, shot, start


def st(con):
    return dict(re.findall(r'(\w+)=(\S+)', sp(con, 'contracts')))


def money(con):
    m = re.search(r'money (-?\d+)', sp(con, 'contracts money'))
    return int(m.group(1)) if m else -1


def rows(con):
    out = []
    for line in sp(con, 'rows').splitlines():
        m = re.match(r'(\d+): (.*?)(?: \| (.*))?$', line)
        if m:
            out.append((int(m.group(1)), m.group(2), m.group(3) or ''))
    return out


def row_with(con, text, button):
    for n, t, b in rows(con):
        if text in t and b == button:
            return n
    return -1


wait = waiter(step=1.0)


def space_page(ui, con):
    """the co-op window open on its Space page"""
    s = ui.state()
    if s.get('open') != '1':
        con.call(f'key {VK_F9}')
        s = ui.wait(lambda s: s['open'] == '1', 20, 'the co-op window')
    if s and s.get('page') != 'space':
        ui.wait(lambda s: s['space']['vis'], 20, 'the Space button')
        ui.click('space')
    return ui.wait(lambda s: s['page'] == 'space', 10, 'the Space page') is not None


def captions(con, ui):
    """confirm the captions that wait for OK (landing tutorials: "Something is showing up on the
    ship's radar...", "You have discovered a wild planet..."), with the co-op window closed"""
    for _ in range(30):
        if 'movie=1' not in sp(con, 'hp') and 'cine=1' not in sp(con, 'hp'):
            return True
        if ui.state().get('open') == '1':
            con.call(f'key {VK_F9}')
            time.sleep(0.5)
        con.call('key enter')
        time.sleep(1.5)
    return False


def click_row(ui, con, text, button):
    """a real click on the button of the page row with this text (captions that wait for OK first:
    they take the clicks)"""
    captions(con, ui)
    if not space_page(ui, con):
        return False
    n = wait(lambda: row_with(con, text, button) + 1, 15)
    if not n:
        print(f'  {ui.who}: no row "{text}" with "{button}": {rows(con)}')
        return False
    s = ui.wait(lambda s: s[f'sbtn{n - 1}']['vis'] and button in s[f'sbtn{n - 1}']['text'], 10, f'sbtn{n - 1}')
    return s is not None and ui.click(f'sbtn{n - 1}')


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(5)
    for who, c in (('host', host), ('guest', guest)):
        ck('contracts: id=' in sp(c, 'features'), f'{who}: the contracts feature is on')
    sp(host, 'contracts period 0')  # no automatic offers during the test
    sp(host, 'contracts limit 0')
    hstar = re.search(r'star (\d+)', sp(host, 'star')).group(1)
    if re.search(r'star (\d+)', sp(guest, 'star')).group(1) != hstar:
        print('  guest to the host\'s star:', sp(guest, 'jumpto', hstar))
        time.sleep(12)
    uh, ug = Ui(host, 'host'), Ui(guest, 'guest')

    # ---- 1. the pirate armada ----
    m0h, m0g = money(host), money(guest)
    rb0 = int(re.search(r'raid_bonus=(\d+)', sp(host)).group(1))
    print('  ', sp(host, 'contracts offer armada here'))
    s = st(host)
    reward, tool = int(s['money']), s['tool']
    ok = wait(lambda: row_with(guest, 'pirate armada', 'Accept') >= 0, 15)
    print('  guest rows:', ' | '.join(t for _, t, _ in rows(guest)))
    ck(ok, 'the guest\'s Space page shows the armada offer with "Accept"')
    ck('New co-op contract' in sp(guest, 'contracts notices'), 'the guest got the offer as a notice')
    ck(click_row(uh, host, 'pirate armada', 'Accept'), 'host: a click on "Accept"')
    ck(click_row(ug, guest, 'pirate armada', 'Accept'), 'guest: a click on "Accept"')
    shot(guest, 'contracts_armada_page.png')
    ok = wait(lambda: st(host)['state'] == 'active' and st(guest)['state'] == 'active', 15)
    ck(ok, f'the contract is active on both games (crew {st(host)["crew"]})')
    ck(int(st(host)['ships_up']) == 0, 'no wave in the system view (the armada fights on a planet)')
    # both land on the system's active planet with the page's "Land" button
    for ui, c in ((uh, host), (ug, guest)):
        ck(click_row(ui, c, 'Armada at', 'Land'), f'{ui.who}: a click on "Land"')
    ok = wait(lambda: sp(host, 'star').startswith('place 4') and sp(guest, 'star').split()[1] == sp(host, 'star').split()[1], 30)
    ck(ok, f'both on one planet ({sp(host, "star")} / {sp(guest, "star")})')
    t0 = time.time()
    while time.time() - t0 < 40 and int(st(host)['ships_up']) == 0:
        captions(host, uh)
        captions(guest, ug)
        time.sleep(1)
    ck(int(st(host)['ships_up']) > 0, f'both ships on the colony: the first wave comes ({st(host)["ships_up"]} ships)')
    ok = wait(lambda: int(re.search(r'mirrors=(\d+)', guest.call('coop.fleet list')['out']).group(1)) > 0, 15)
    ck(ok, f'the guest sees the raid ships ({guest.call("coop.fleet list")["out"].splitlines()[0]})')
    shot(host, 'contracts_armada_wave.png')
    t0, waves_seen, guest_shots = time.time(), set(), 0
    while time.time() - t0 < 180 and st(host)['state'] == 'active':
        captions(host, uh)
        captions(guest, ug)
        s = st(host)
        waves_seen.add(s['wave'])
        ptrs = re.findall(r'armada (\S+)', sp(host, 'contracts ships'))
        fleet = host.call('coop.fleet list')['out']
        ids = [m.group(1) for p in ptrs for m in [re.search(r'ship #(\d+) ' + re.escape(p) + ' ', fleet)] if m]
        if not ids:
            time.sleep(1)
            continue
        gfleet = guest.call('coop.fleet list')['out']
        for k, i in enumerate(ids):  # half of the ships each: the guest's shots go through the host
            if k % 2 == 0 and f'ship #{i} ' in gfleet:
                guest.call(f'coop.fleet shoot {i} 600')
                guest_shots += 1
            else:
                host.call(f'coop.fleet shoot {i} 600')
        time.sleep(1.5)
    s = st(host)
    print('  host:', sp(host, 'contracts'))
    ck(s['state'] == 'done' and '2/2' in waves_seen, f'both waves destroyed: the contract is done (waves {sorted(waves_seen)})')
    ck(guest_shots > 0, f'the guest shot armada ships ({guest_shots} shots)')
    ok = wait(lambda: st(guest)['state'] == 'done' and st(guest)['rewards'] != '0', 10)
    ck(ok, 'the guest sees the contract done')
    bonus = 2000 * (int(re.search(r'raid_bonus=(\d+)', sp(host)).group(1)) - rb0)  # spacecoop: raids defended together
    ok = wait(lambda: money(host) == m0h + reward + bonus and money(guest) == m0g + reward + bonus, 15)
    ck(ok, f'the reward money arrived once in the shared budget: host {m0h} -> {money(host)}, guest {m0g} -> '
       f'{money(guest)} (+{reward} + raid bonus {bonus})')
    if tool != '00000000':
        for who, c in (('host', host), ('guest', guest)):
            ck('have' in sp(c, 'contracts tool', tool), f'{who}: the reward tool {sp(c, "contracts tool", tool)}')
    ck('Contract done' in sp(guest, 'contracts notices'), 'the guest was told')

    # ---- 2. the joint survey: "Jump there" by real clicks ----
    # the armada's end gives badge points: their panel ("2 badge points" + OK) takes the mouse, the
    # co-op window's buttons do not get clicks while it is up (docs/known-issues.md): Enter closes it
    for ui, c in ((uh, host), (ug, guest)):
        if ui.state().get('open') == '1':
            c.call(f'key {VK_F9}')
        time.sleep(0.5)
        c.call('key enter')
    time.sleep(2)
    m0h, m0g = money(host), money(guest)
    print('  ', sp(host, 'contracts offer survey 2'))
    targets = re.findall(r'target (\d+) \S+ (.+?) done', sp(host, 'contracts targets'))
    reward = int(st(host)['money'])
    ck(len(targets) == 2, f'the survey has 2 stars near the host: {targets}')
    ck(click_row(uh, host, 'joint survey', 'Accept'), 'host: a click on "Accept" (survey)')
    ck(click_row(ug, guest, 'joint survey', 'Accept'), 'guest: a click on "Accept" (survey)')
    ck(wait(lambda: st(host)['state'] == 'active', 10), 'the survey is active')
    for k, (sid, name) in enumerate(targets):
        if k == 0:
            shot(host, 'contracts_survey.png')
        for ui, c in ((uh, host), (ug, guest)):
            ck(click_row(ui, c, name, 'Jump there'), f'{ui.who}: a click on "Jump there" ({name})')
        ok = wait(lambda: int(st(host)['targets'].split('/')[0]) > k, 45)
        ck(ok, f'both at {name}: surveyed ({st(host)["targets"]})')
    ok = wait(lambda: st(host)['state'] == 'done' and st(guest)['state'] == 'done', 15)
    ck(ok, 'the survey is done on both games')
    ok = wait(lambda: money(host) == m0h + reward and money(guest) == m0g + reward, 15)
    ck(ok, f'the survey reward arrived once: {m0h} -> {money(host)} / {m0g} -> {money(guest)} (+{reward})')
    tool = st(host)['tool']
    if tool != '00000000':
        for who, c in (('host', host), ('guest', guest)):
            ck('have' in sp(c, 'contracts tool', tool), f'{who}: the survey tool ({tool})')

    # ---- 3. failure: the time is up ----
    m0h, m0g = money(host), money(guest)
    sp(host, 'contracts limit 25')
    print('  ', sp(host, 'contracts offer survey 1'))
    sp(host, 'contracts accept')
    sp(guest, 'contracts accept')
    ck(wait(lambda: st(host)['state'] == 'active', 10), 'the short survey is active')
    ok = wait(lambda: st(host)['state'] == 'failed' and st(guest)['state'] == 'failed', 45)
    ck(ok, f'nobody flew there: the contract failed on both games (reason {st(host).get("reason")})')
    ck(st(guest).get('reason') == '1', 'the reason is the timeout')
    ck('Contract failed' in sp(guest, 'contracts notices') and 'Contract failed' in sp(host, 'contracts notices'),
       'the failure is announced on both games')
    ck(any('failed' in t for _, t, _ in rows(guest)), 'the guest\'s page shows the failure')
    ck(money(host) == m0h and money(guest) == m0g, 'no reward for a failed contract')
    sp(host, 'contracts limit 0')
    host.call(f'key {VK_F9}')
    guest.call(f'key {VK_F9}')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
