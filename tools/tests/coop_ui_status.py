#!/usr/bin/env python3
"""The co-op window's connect flow and status line, two instances (creature fixture).

 1. BUG-0138: the guest types a host address with a wrong port: the window says the port is wrong
    at once (it used to say "Connecting to ..." and time out 10 s later). Then the address with a
    leading space, as pasted from a chat: the join works (it used to fail as "cannot resolve").
 2. BUG-0104: once the guest is in, the window's status no longer says "Connecting to ..." (the
    lobby showed it under the player list for the whole session).
 3. BUG-0183: six games on the network (`coop.lanfake`): the connect page lists three and "More >"
    shows the others (only the first three were ever listed).
 4. BUG-0085: the host goes to the main menu, the guest earns DNA alone, the host leaves the
    session: the guest is told the session ended, never "player0 (host) is back: your N DNA go to
    the pack", and the window shows the reason (BUG-0104).

  coop_ui_status.py [--without-fix]   (--without-fix: coop.corefix addr_check, ui_status,
                                       lan_pages and host_away_reset 0, the old behaviour: FAILS)"""
import re
import sys
import time

import lane
from coop_ui import Ui
from twoinst import Checks, start, wait_guest_in

SWITCHES = ('addr_check', 'ui_status', 'lan_pages', 'host_away_reset')


def connected(con):
    return 'client id=' in con.call('coop.status')['out'] and ' connected' in con.call('coop.status')['out']


def lan_page(st):
    """(first row number, number of games) from the header "Games on your network: 4-6 of 18" """
    m = re.search(r'(\d+)-\d+ of (\d+)', (st or {}).get('lan', ''))
    return (int(m.group(1)), int(m.group(2))) if m else None


def wait(pred, timeout):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if pred():
            return True
        time.sleep(0.5)
    return False


def main():
    without = '--without-fix' in sys.argv
    host, guest = start('creature')
    ck = Checks()
    for con in (host, guest):
        for sw in SWITCHES:
            con.call(f'coop.corefix {sw} {0 if without else 1}')
    ui = Ui(guest, 'guest')

    # ---- 1. address checks (BUG-0138) ----
    guest.call('coop.leave')
    time.sleep(1)
    guest.call('coop.uistate 1')
    st = ui.wait(lambda s: s['join']['vis'] and s['address']['vis'], 30, 'the connect page')
    ck(st is not None, 'the guest sees the connect page')
    ui.type_into('address', f'127.0.0.1:{lane.PORT + 60000}')
    ui.click('join')
    st = ui.wait(lambda s: s.get('status', '') != '', 10, 'a status line')
    status = st.get('status', '') if st else ''
    ck('port must be a number' in status, f'a wrong port is refused at once ({status!r})')
    if 'offline' not in guest.call('coop.status')['out']:
        guest.call('coop.leave')  # the old code tried the wrapped port: back to the connect page
        ui.wait(lambda s: s['address']['vis'], 20, 'the connect page again')
    typed = ui.type_into('address', f' 127.0.0.1:{lane.PORT}') and ui.click('join')
    st = ui.state()
    ck(typed and 'cannot resolve' not in st.get('status', ''), f'a leading space is no error ({st.get("status")!r})')
    joined = wait(lambda: connected(guest), 15)
    ck(joined, 'the guest is connected after joining with a leading space')
    if not joined:
        guest.call(lane.JOIN)  # go on with the other parts
    ok_in = wait_guest_in(host, guest, timeout=180)
    ck(ok_in, 'the guest is in the host world again')

    # ---- 2. the status after the join (BUG-0104) ----
    time.sleep(3)
    guest.call('coop.uistate 1')
    st = ui.wait(lambda s: 'world' in s and 'world' not in s['world'].lower(), 120, 'the lobby after the transfer')
    status = st.get('status', '') if st else ''
    world_line = st.get('world', '') if st else ''
    ck(not status.startswith('Connecting'), f'the status no longer says "Connecting" once in ({status!r})')
    ck(not world_line.startswith('Connecting'), f'the lobby world line is not "Connecting" ({world_line!r})')

    # ---- 3. more than three games on the network (BUG-0183) ----
    guest.call('coop.leave')
    time.sleep(1)
    guest.call('coop.uistate 1')
    print(' ', guest.call('coop.lanfake 6 120')['out'].strip())
    seen = set()
    st = ui.wait(lambda s: sum(1 for i in range(3) if s.get(f'lan{i}', {}).get('vis')) == 3, 20, 'three LAN rows')
    # Other lab lanes' hosts answer too (each twice on a machine with two network addresses), so the
    # list can hold 18 games and the made-up ones are on pages 4-6. The game drops clicks for a few
    # seconds after it reaches the menu (the guest just left a world): a click that did not turn the
    # page is clicked again, and the pages are turned until every game was on screen once.
    turned = missed = 0
    while st and turned < 16:
        print('  rows:', [st.get(f'lan{i}', {}).get('text') for i in range(3)], st.get('lan'))
        for i in range(3):
            w = st.get(f'lan{i}', {})
            if w.get('vis'):
                seen.update(re.findall(r'\bfake(\d)\b', w.get('text', '')))
        page = lan_page(st)
        if seen >= {str(i) for i in range(1, 7)} or not page or not st.get('lanmore', {}).get('vis'):
            break
        if turned >= (page[1] + 2) // 3 + 1:  # every page shown (and one more for a list that changed)
            break
        for _ in range(6):
            ui.click('lanmore')
            nxt = ui.wait(lambda s: lan_page(s) and lan_page(s)[0] != page[0], 3, 'the next LAN page')
            if nxt:
                break
            missed += 1
        st = nxt
        turned += 1
    if missed:
        print(f'  {missed} click(s) on "More >" did not turn the page (clicked again)')
    print(f'  fake games listed: {sorted(seen)}; header {st and st.get("lan")!r}')
    ck(seen == {str(i) for i in range(1, 7)}, f'all six made-up games can be listed ({len(seen)} of 6)')
    guest.call('coop.lanfake 0')
    guest.call('coop.uistate 0')

    # ---- 4. the host leaves while it is away (BUG-0085) ----
    guest.call(lane.JOIN)
    ck(wait_guest_in(host, guest, timeout=180), 'the guest is back in the host world')
    time.sleep(3)
    print(' ', host.call('coop.menu')['out'].strip())
    ck(wait(lambda: 'host_away=1' in guest.call('coop.status')['out'], 90), 'the guest knows the host is away')
    guest.call('coop.dna add 50')
    time.sleep(1)
    host.call('coop.leave')
    ck(wait(lambda: 'offline' in guest.call('coop.status')['out'], 40), 'the guest sees the session end')
    time.sleep(4)
    log = guest.call('log 200')['out']
    tail = log[log.rfind('disconnected:'):] if 'disconnected:' in log else log
    back = [ln for ln in tail.splitlines() if '(host) is back' in ln]
    ck(not back, f'no "host is back" after the session ended ({back[:1]})')
    ck('host_away=0' in guest.call('coop.status')['out'], 'the away state ended with the session')
    st = ui.state()
    ck(st.get('status', '').startswith('Disconnected'), f'the window shows why the session ended ({st.get("status")!r})')

    for con in (host, guest):
        for sw in SWITCHES:
            con.call(f'coop.corefix {sw} 1')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
