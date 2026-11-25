#!/usr/bin/env python3
"""Optional session password (CAP_JOIN_PROOF, LAN lock flag, log masking) and recent servers list (BUG-0867).

Two instances:
 1. Host sets password "secret42" and hosts on the lane's UDP port.
 2. Guest opens the Join step: LAN discovery lists the host with [Locked].
 3. Guest joins with empty password -> rejected with "Incorrect or missing server password."
 4. Guest joins with wrong password ("wrong99") -> rejected with "Incorrect or missing server password."
 5. Rejected joins do not populate the recent servers list.
 6. Guest joins with right password ("secret42") -> joins cleanly (2 players on host).
 7. Recent servers list contains the joined address, persisted in mods/coop.ini under [recent].
 8. After reloading coop.dll (fresh read from coop.ini), the recent server still appears in coop.recent
    and in the Join step UI (rec0); clicking rec0 fills the address field.
 9. Security: raw password "secret42" never appears in sporekit*.log, coop.ini, coop.status, or coop.uistate.

Twin: password_recent_off (SPORE_FIX_OFF=session.password,ui.recent_servers) must FAIL.
"""
import glob
import os
import time

from coop_ui import Ui
import lane
from twoinst import Checks, shot, start

PASSWORD = 'secret42'
WRONG_PW = 'wrong99'
BAD_PW_TEXT = 'Incorrect or missing server password.'


def read_text_file(path):
    if not os.path.exists(path):
        return ''
    raw = open(path, 'rb').read()
    if raw[:2] == b'\xff\xfe':
        return raw[2:].decode('utf-16-le', errors='replace')
    if raw[:2] == b'\xfe\xff':
        return raw[2:].decode('utf-16-be', errors='replace')
    return raw.decode('utf-8', errors='replace')


def reapply_fix_off(con):
    names = [n.strip() for n in os.environ.get('SPORE_FIX_OFF', '').split(',') if n.strip()]
    for name in names:
        if not name.startswith('kit.'):
            con.call(f'coop.fix {name} off')


def wait_offline(con, timeout=15):
    t0 = time.time()
    while time.time() - t0 < timeout:
        if con.call('coop.status')['out'].splitlines()[:1] == ['offline']:
            return True
        time.sleep(0.25)
    return False


def wait_join_outcome(guest, gui, timeout=15):
    """Wait until guest either connects or gets rejected/offline with status text."""
    t0 = time.time()
    while time.time() - t0 < timeout:
        st_txt = guest.call('coop.status')['out']
        if 'connected' in st_txt and 'client id=' in st_txt:
            return 'connected', ''
        ui_st = gui.state()
        status_line = ui_st.get('status', '')
        if BAD_PW_TEXT in status_line:
            return 'rejected', status_line
        if st_txt.splitlines()[:1] == ['offline'] and time.time() - t0 > 2.0:
            return 'offline', status_line
        time.sleep(0.25)
    return 'timeout', gui.state().get('status', '')


def main():
    host, guest = start('creature')
    ck = Checks()
    gui = Ui(guest, 'guest')
    target_addr = f'127.0.0.1:{lane.PORT}'

    # Clean initial state: leave the fixture session and clear recent servers
    guest.call('coop.leave')
    host.call('coop.leave')
    wait_offline(guest)
    wait_offline(host)
    guest.call('coop.recent clear')
    guest.call('coop.password clear')
    host.call('coop.password clear')

    # ---- 1. Host with password "secret42" ----
    pw_out = host.call(f'coop.password set {PASSWORD}')['out']
    ck(PASSWORD not in pw_out, f'host: coop.password output does not echo raw password ({pw_out.strip()!r})')
    host.call(lane.HOST_CMD)
    time.sleep(0.5)
    hst = host.call('coop.status')['out']
    ck('password=yes' in hst, f'host: coop.status shows password=yes ({hst.splitlines()[0]!r})')
    ck(PASSWORD not in hst, 'host: coop.status never leaks raw password')

    # ---- 2. Guest Join step: LAN discovery shows [Locked] ----
    guest.call('coop.uistate 1')
    st = gui.wait(lambda s: s.get('open') == '1' and s.get('page') == 'connect', 15, 'guest connect page')
    if st and st.get('step') == 'choose' and st.get('gojoin', {}).get('vis'):
        gui.click('gojoin')
    st = gui.wait(lambda s: s.get('step') == 'join', 15, 'guest Join step')
    lan_text = ''
    t0 = time.time()
    while time.time() - t0 < 25:
        st = gui.state()
        for r in ('lan0', 'lan1', 'lan2'):
            w = st.get(r)
            if w and w['vis'] and f':{lane.PORT}' in w['text']:
                lan_text = w['text']
                break
        if lan_text:
            break
        if st.get('lanmore', {}).get('vis'):
            gui.click('lanmore')
        time.sleep(0.5)
    print(f'  guest LAN row: {lan_text!r}', flush=True)
    ck(bool(lan_text) and '[Locked]' in lan_text,
       f'guest: LAN discovery shows [Locked] for password-protected host ({lan_text!r})')
    shot(guest, 'password_lan_locked.png')

    # ---- 3. Guest join with empty password -> rejected ----
    guest.call('coop.password clear')
    guest.call(lane.JOIN)
    outcome, status_line = wait_join_outcome(guest, gui, timeout=12)
    print(f'  empty pw join: outcome={outcome} status={status_line!r}', flush=True)
    ck(outcome == 'rejected' and BAD_PW_TEXT in status_line,
       f'guest: empty password rejected with readable text ({outcome}, {status_line!r})')
    wait_offline(guest, timeout=8)

    # ---- 4. Guest join with wrong password -> rejected ----
    guest.call(f'coop.password set {WRONG_PW}')
    guest.call(lane.JOIN)
    outcome, status_line = wait_join_outcome(guest, gui, timeout=12)
    print(f'  wrong pw join: outcome={outcome} status={status_line!r}', flush=True)
    ck(outcome == 'rejected' and BAD_PW_TEXT in status_line,
       f'guest: wrong password rejected with readable text ({outcome}, {status_line!r})')
    wait_offline(guest, timeout=8)

    # ---- 5. Rejected joins do not populate recent servers ----
    rec_before = guest.call('coop.recent')['out']
    ck('count=0' in rec_before, f'guest: rejected joins do not add to recent servers ({rec_before.strip()!r})')

    # ---- 6. Guest join with right password -> accepted ----
    guest.call(f'coop.password set {PASSWORD}')
    guest.call(lane.JOIN)
    outcome, status_line = wait_join_outcome(guest, gui, timeout=15)
    print(f'  right pw join: outcome={outcome}', flush=True)
    ck(outcome == 'connected', f'guest: right password joins cleanly (outcome={outcome}, status={status_line!r})')
    t0 = time.time()
    host_two = False
    while time.time() - t0 < 10:
        if 'players=2/' in host.call('coop.status')['out']:
            host_two = True
            break
        time.sleep(0.25)
    ck(host_two, 'host: sees 2 players after guest joins with right password')

    # ---- 7. Recent server recorded and persisted in coop.ini ----
    rec_after = guest.call('coop.recent')['out']
    print(f'  guest coop.recent: {rec_after.strip()!r}', flush=True)
    ck(target_addr in rec_after, f'guest: recent list contains {target_addr} ({rec_after.strip()!r})')
    mods_dir = os.path.join(lane.game_dir(), 'SporebinEP1', 'mods')
    ini_path = os.path.join(mods_dir, 'coop.ini')
    ini_text = read_text_file(ini_path)
    ck('[recent]' in ini_text and f'server0={target_addr}' in ini_text,
       f'guest: coop.ini persists [recent] server0={target_addr}')
    ck(PASSWORD not in ini_text, 'coop.ini never stores raw password')

    # ---- 8. Persistence across restart (mod.reload coop re-reads coop.ini from scratch) ----
    guest.call('coop.leave')
    wait_offline(guest, timeout=10)
    guest.call('mod.reload coop')
    time.sleep(1.5)
    reapply_fix_off(guest)
    rec_reload = guest.call('coop.recent')['out']
    print(f'  guest coop.recent after reload: {rec_reload.strip()!r}', flush=True)
    ck(target_addr in rec_reload, f'guest: recent server persists after reload ({rec_reload.strip()!r})')

    guest.call('coop.uistate 1')
    st = gui.wait(lambda s: s.get('open') == '1' and s.get('page') == 'connect', 15, 'guest connect after reload')
    if st and st.get('step') == 'choose' and st.get('gojoin', {}).get('vis'):
        gui.click('gojoin')
    st = gui.wait(lambda s: s.get('step') == 'join', 15, 'guest Join step after reload')
    rec0 = st.get('rec0') if st else None
    ck(rec0 and rec0['vis'] and rec0['text'] == target_addr,
       f'guest: Join step shows persisted recent server rec0 ({rec0!r})')
    if rec0 and rec0['vis']:
        # Clear address field via UI or verify click fills it
        gui.click('rec0')
        time.sleep(0.5)
        st2 = gui.state()
        ck(st2.get('address', {}).get('text') == target_addr,
           f'guest: clicking rec0 fills address field ({st2.get("address")!r})')
    shot(guest, 'password_recent_join.png')

    # ---- 9. Security: raw password never appears in sporekit logs or UI state ----
    ui_dump = guest.call('coop.ui state')['out'] + '\n' + host.call('coop.ui state')['out']
    ck(PASSWORD not in ui_dump, 'coop.ui state never leaks raw password')
    log_dir = os.path.join(mods_dir, 'logs')
    leaked_logs = []
    for lp in glob.glob(os.path.join(log_dir, 'sporekit*.log')):
        if PASSWORD in read_text_file(lp):
            leaked_logs.append(os.path.basename(lp))
    ck(not leaked_logs, f'raw password never appears in sporekit.log ({leaked_logs})')

    # Cleanup
    guest.call('coop.uistate 0')
    host.call('coop.uistate 0')
    host.call('coop.password clear')
    guest.call('coop.password clear')
    guest.call('coop.recent clear')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
