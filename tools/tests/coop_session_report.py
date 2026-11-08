#!/usr/bin/env python3
"""One report for the session (0.7.12 F4, BUG-0767, docs/REPORTING.md#reporting-session).

Alice hosts, Bob, Carol and Dave are guests (tools/tests/party.py, four games in one lane). Every round is started by
a real click in the host's co-op window and answered by real clicks in each guest's window, which the guest's game
opens by itself with "<host> (host) asks for your report:  Send / Don't send".

  A. "Ask the players for their reports": every guest sends. The host's SporeCoop-session-report-*.zip holds exactly
     Guest-Bob.zip, Guest-Carol.zip, Guest-Dave.zip and SESSION.txt; each Guest-*.zip is byte for byte the zip that
     guest's own game wrote in mods\\logs (the same zip as its "Report a problem"), a zip with the report inside.
  B. "Report a problem" on the host: Bob says no, Carol's report is over 25 MB (test hook `big`), Dave's has a name
     that climbs out of its folder (`badname`): the session zip holds exactly Host-Alice.zip (the host's own report,
     byte for byte) and SESSION.txt; SESSION.txt lists Bob "not sent", Carol and Dave "refused".
  C. "Ask the players" again: Bob sends junk (`junk`), Carol sends more than she announced (`overrun`, stopped at
     the size), Dave sends: exactly Guest-Dave.zip and SESSION.txt.
  Every round: no .part file is left, SESSION.txt has no network address, each refused guest's own window says the
  host refused it, no fault in any game.
  D. The guests leave; the host's "Report a problem" alone still writes its own SporeCoop-report zip and no session
     zip.
SPORE_FIX_OFF=core.session_report must FAIL (no guest is asked, no session zip).

  SPORE_LANE=4 python3 tools/tests/coop_session_report.py [--fixture cell]
"""
import argparse
import glob
import io
import os
import re
import shutil
import time
import zipfile

import lane
from coop_ui import Ui
from party import Party
from twoinst import Checks, shot

WT_OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'out')


def logs_dir():
    return os.path.join(lane.game_dir(), 'SporebinEP1', 'mods', 'logs')


def newest(pattern, since):
    files = [f for f in glob.glob(os.path.join(logs_dir(), pattern)) if os.path.getmtime(f) >= since - 2]
    return max(files, key=os.path.getmtime) if files else None


def own_report(p, since):
    """the newest SporeCoop-report zip this player's game wrote since `since` (instance suffix -i<N>)"""
    files = [f for f in glob.glob(os.path.join(logs_dir(), 'SporeCoop-report-*.zip'))
             if os.path.getmtime(f) >= since - 2 and (f.endswith(f'-i{p.inst}.zip') if p.inst > 1 else
                                                       not re.search(r'-i\d+\.zip$', f))]
    return max(files, key=os.path.getmtime) if files else None


def sstatus(p):
    return p.call('coop.sreport status')


def keep_shot(p, name):
    try:
        path = shot(p.con, name)
        if os.path.exists(path):
            shutil.copy(path, os.path.join(WT_OUT, name))
            print(f'  shot {name}', flush=True)
    except OSError as e:
        print(f'  shot {name} failed: {e}', flush=True)


def run_round(ck, party, label, how, answers, hooks, expect_names, expect_states, shots=False):
    """how: 'ask' (askreports) or 'report' (the host's Report a problem); answers {guest: 'send'|'no'};
    hooks {guest: test hook}; expect_names: the session zip's entries (None: no zip); expect_states {guest: text}"""
    host = party.host
    for g in party.guests:
        print(f'  {g.name}: ' + g.call(f'coop.sreport test {hooks.get(g.name, "none")}').strip(), flush=True)
    t0 = time.time()
    hui, st = party.open_window(host)
    btn = 'askreports' if how == 'ask' else 'report'
    if not ck(st is not None and hui.click(btn), f'[{label}] the host clicks "{btn}" in its co-op window'):
        return
    # each guest's window opens by itself with the question; answered by a click
    asked = 0
    for g in party.guests:
        ui = Ui(g.con, g.name)
        s = ui.wait(lambda s: s.get('open') == '1' and s.get('rsend', {}).get('vis'), 30, 'the host\'s question')
        if not ck(s is not None, f'[{label}] {g.name}\'s game asks "{(s or {}).get("rask", {}).get("text", "")}" with Send / '
                  'Don\'t send, its window opened by itself'):
            continue
        asked += 1
        s = ui.wait(lambda s: 'Sent:' in s.get('world', ''), 6, 'what is sent') or s
        ck('asks for your report' in s['rask']['text'] and 'Sent:' in s.get('world', ''),
           f'[{label}] {g.name}: the question names the host, the status says what is sent ({s.get("world", "")!r})')
        if shots and g is party.guests[0]:
            keep_shot(g, 'session_report_guest_dialog.png')
        ck(ui.click('rsend' if answers[g.name] == 'send' else 'rnosend'),
           f'[{label}] {g.name} clicks {"Send" if answers[g.name] == "send" else "Don't send"}')
    if not asked:
        return
    # the round ends: the host's line lists every guest
    t1 = time.time()
    line = ''
    while time.time() - t1 < 150:
        out = sstatus(host)
        line = re.search(r"^hostline '(.*)'$", out, re.M).group(1) if "hostline '" in out else ''
        if 'round=none' in out and line and not line.startswith('Reports:') and not line.startswith('Asking'):
            break
        time.sleep(1)
    print(f'  [{label}] host: {line}', flush=True)
    for name, want in expect_states.items():
        ck(f'{name}: {want}' in line, f'[{label}] the host lists {name}: {want}')
    zpath = newest('SporeCoop-session-report-*.zip', t0)
    leftover = glob.glob(os.path.join(logs_dir(), '*.part'))
    ck(not leftover, f'[{label}] no partial file in mods\\logs ({[os.path.basename(f) for f in leftover]})')
    if expect_names is None:
        ck(zpath is None, f'[{label}] no session zip ({zpath and os.path.basename(zpath)})')
        return
    if not ck(zpath is not None, f'[{label}] the host wrote a SporeCoop-session-report-*.zip'):
        return
    ck(os.path.basename(zpath) in line, f'[{label}] the host\'s line names the file {os.path.basename(zpath)}')
    hst = Ui(host.con, host.name).wait(lambda s: os.path.basename(zpath) in s.get('world', ''), 8, 'the result line')
    ck(hst is not None, f'[{label}] the host\'s own window says "{(hst or {}).get("world", "")}"')
    with zipfile.ZipFile(zpath) as z:
        names = sorted(z.namelist())
        ck(names == sorted(expect_names), f'[{label}] the session zip holds exactly {sorted(expect_names)} ({names})')
        ck(z.testzip() is None, f'[{label}] the session zip reads back without a CRC error')
        text = z.read('SESSION.txt').decode('utf-8', 'replace') if 'SESSION.txt' in names else ''
        ck(not re.search(r'\b\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}\b', text), f'[{label}] SESSION.txt has no network address')
        for name, want in expect_states.items():
            ck(f'{name}: {want}' in text, f'[{label}] SESSION.txt lists {name}: {want}')
        for n in names:
            if not n.endswith('.zip'):
                continue
            who = n[n.index('-') + 1:-4]
            p = next((x for x in party.all if x.name == who), None)
            data = z.read(n)
            inner = zipfile.ZipFile(io.BytesIO(data))
            inames = inner.namelist()
            ck('README.txt' in inames and any(re.fullmatch(r'(report|crash)-.*\.txt', x) for x in inames),
               f'[{label}] {n} is a report zip ({len(inames)} files: README.txt, the report)')
            own = own_report(p, t0) if p else None
            ck(own is not None and open(own, 'rb').read() == data,
               f'[{label}] {n} is byte for byte {who}\'s own report {own and os.path.basename(own)}')
    for g in party.guests:
        if expect_states.get(g.name, '').startswith('refused'):
            out = sstatus(g)
            gl = re.search(r"^guestline \d '(.*)'$", out, re.M)
            ck(gl and 'refused' in gl.group(1), f'[{label}] {g.name}\'s own window says the host refused it '
               f'({gl and gl.group(1)!r})')
    if shots:
        party.open_window(host)
        time.sleep(1.5)
        keep_shot(host, 'session_report_host_result.png')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--fixture', default='cell')
    a = ap.parse_args()
    ck = Checks()
    os.makedirs(WT_OUT, exist_ok=True)
    t_start = time.time()
    p = Party.start(a.fixture, 4)
    try:
        if not p.check_together(ck, 'join'):
            return ck.result()
        p.ids()
        print('  ' + sstatus(p.host).splitlines()[0], flush=True)
        everyone = {'Bob': 'send', 'Carol': 'send', 'Dave': 'send'}
        run_round(ck, p, 'A', 'ask', everyone, {},
                  ['Guest-Bob.zip', 'Guest-Carol.zip', 'Guest-Dave.zip', 'SESSION.txt'],
                  {'Bob': 'sent', 'Carol': 'sent', 'Dave': 'sent'}, shots=True)
        run_round(ck, p, 'B', 'report', {'Bob': 'no', 'Carol': 'send', 'Dave': 'send'},
                  {'Carol': 'big', 'Dave': 'badname'}, ['Host-Alice.zip', 'SESSION.txt'],
                  {'Bob': 'not sent', 'Carol': 'refused (too big)', 'Dave': 'refused (not a SporeCoop report)'})
        run_round(ck, p, 'C', 'ask', everyone, {'Bob': 'junk', 'Carol': 'overrun'}, ['Guest-Dave.zip', 'SESSION.txt'],
                  {'Bob': 'refused (not a SporeCoop report)', 'Carol': 'refused (too big)', 'Dave': 'sent'})
        # D. the host alone: its own report still works, no session zip
        for g in p.guests:
            g.call('coop.leave')
        time.sleep(4)
        t0 = time.time()
        hui, st = p.open_window(p.host)
        ck(st is not None and hui.click('report'), '[D] the host alone clicks "Report a problem"')
        time.sleep(8)
        own = own_report(p.host, t0)
        ck(own is not None, f'[D] the host\'s own report is written ({own and os.path.basename(own)})')
        ck(newest('SporeCoop-session-report-*.zip', t0) is None, '[D] no session zip without guests')
        # (the test makes report-*.txt on purpose: suite reports=True; a crash or a dead game is checked here)
        crashes = [os.path.basename(f) for f in glob.glob(os.path.join(logs_dir(), 'crash-*'))
                   if os.path.getmtime(f) >= t_start - 2]
        ck(not crashes, f'no crash report of any game ({crashes})')
        for x in p.all:
            ck('fix=' in sstatus(x), f'{x.name}\'s game still answers')
        p.faults(ck)
        return ck.result()
    finally:
        p.stop()


if __name__ == '__main__':
    raise SystemExit(main())
