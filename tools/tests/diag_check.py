#!/usr/bin/env python3
"""Diagnostics for bug reports (SporeKit 0.3.4, docs/02-modkit.md 2.7), on one game of the lane:

  1. the log of the previous run is kept (mods/logs/previous/sporekit.N-<time>.log)
  2. the log starts with the environment: Windows / Wine, memory and the large-address flag, the game
     build, SporeCoop + SporeKit versions; after a few frames the GPU and other hooks
  3. `env` and `report` work: one archive (report, logs, previous logs, crashes, settings with the
     last joined address hidden), the report has every section
  4. the main menu shows "SporeCoop <version>" under the Co-op item, the co-op window shows the
     version line, and its "Report a problem" button (a real click) writes a report
  5. --crash: a call to address 1 kills the game: crash-<time>.txt with the exception, the call
     chain and the log, and a minidump next to it

  --crash also: the crash packs the archive by itself, the next start tells the player

  SPORE_LANE=2 python3 tools/tests/diag_check.py [--crash]
"""
import glob
import os
import re
import sys
import time
import zipfile

import lane
from coop_ui import VK_F9, Ui
from sporectl import Console
from twoinst import Checks, wait_up

GAME = lane.game_dir()
LOGDIR = os.path.join(GAME, 'SporebinEP1', 'mods', 'logs')


def start_host(ck):
    lane.sh('kill')
    time.sleep(2)
    lane.sh('deploy')
    lane.run_host()
    ck(wait_up(lane.HOST), 'the game starts')
    c = Console(lane.HOST)
    for _ in range(120):
        if 'GalaxyGameEntry' in c.call('game.mode')['out']:
            break
        time.sleep(1)
    c.call('wait.frames 120 60000')
    return c


def logname():
    return os.path.join(LOGDIR, f'sporekit.{lane.HOST}.log' if lane.HOST > 1 else 'sporekit.log')


def main():
    ck = Checks()
    crash = '--crash' in sys.argv
    c = start_host(ck)          # a first run: makes a log to keep
    before = set(glob.glob(os.path.join(LOGDIR, 'previous', '*')))
    c = start_host(ck)          # the second run keeps the first one's log
    after = set(glob.glob(os.path.join(LOGDIR, 'previous', '*')))
    stem = os.path.basename(logname())[:-4]
    new = [p for p in after - before if os.path.basename(p).startswith(stem + '-')]
    ck(new, f'the previous log is kept ({[os.path.basename(p) for p in new]})')

    log = open(logname(), errors='replace').read()
    for what, rx in (('Windows / Wine', r'(Wine \S+ on|Windows \d+\.\d+ build)'),
                     ('memory and the large-address flag', r'game address space \d+ of \d+ MB used, large-address flag'),
                     ('the game build', r'game build: (GOG|Steam|the GOG|unknown)'),
                     ('the versions', r'SporeCoop \d+(\.\d+)+ on SporeKit \d+(\.\d+)+, game '),
                     ('the GPU and other hooks', r'GPU .*other hooks: ')):
        ck(re.search(rx, log), f'the log has {what}')

    env = c.call('env')['out']
    print('  env:', env.strip().replace('\n', ' | ')[:400])
    ck('SporeKit' in env and 'mods: ' in env and 'coop' in env, 'env names SporeKit, the game build and the mods')
    rep = c.call('report test')['out'].strip()
    path = os.path.join(LOGDIR, os.path.basename(rep.replace('\\', '/')))
    ck(path.endswith('.zip') and os.path.exists(path), f'report writes one archive {os.path.basename(path)}')
    if os.path.exists(path) and path.endswith('.zip'):
        z = zipfile.ZipFile(path)
        names = z.namelist()
        print('  archive:', ', '.join(names)[:300], f'({os.path.getsize(path) >> 10} KB)')
        ck(z.testzip() is None, 'the archive is a valid zip')
        ck('README.txt' in names and any(n.startswith('report-') for n in names), 'it has README and the report')
        ck(any(n.startswith('logs/sporekit') for n in names), 'it has the current log')
        ck(any(n.startswith('previous/') for n in names), 'it has the logs of the previous runs')
        ini = [n for n in names if n.startswith('settings/coop.ini')]
        ini_txt = z.read(ini[0]) if ini else b''
        ck(ini and (b'last_address=(hidden in reports)' in ini_txt or b'last_address=' not in ini_txt) and
           re.search(rb'last_address=(?!\(hidden)', ini_txt) is None, 'coop.ini is in it, a joined address hidden')
        txt = z.read(next(n for n in names if n.startswith('report-'))).decode('utf-8', 'replace')
        for sec in ('== Environment', '== coop', '== Faults caught before', '== Modules', '== The last log lines'):
            ck(sec in txt, f'the report has "{sec}"')
    # the menu: the version under the Co-op item; the window: the version line and the report button
    found = c.call('coop.ui find "SporeCoop 0."')['out'] + c.call('coop.ui find SporeCoop')['out']
    ck(re.search(r'SporeCoop \d+\.\d+\.\d+', found), f'the main menu shows the version ({found.strip()[:80]})')
    ui = Ui(c, 'host')
    c.call(f'key {VK_F9}')
    st = ui.wait(lambda s: s['open'] == '1' and s['report']['vis'], 20, 'the co-op window')
    ck(st is not None, 'the co-op window has "Report a problem"')
    z0 = set(glob.glob(os.path.join(LOGDIR, 'SporeCoop-report-*.zip')))
    time.sleep(1.1)  # a new file name (seconds in it)
    ui.click('report')
    time.sleep(2)
    z1 = set(glob.glob(os.path.join(LOGDIR, 'SporeCoop-report-*.zip')))
    # a new name (only the newest 3 archives stay since BUG-0137: the count need not grow)
    ck(z1 - z0, f'"Report a problem" writes the archive ({[os.path.basename(p) for p in z1 - z0]})')
    st = ui.state()
    print('  status after the click:', st.get('status', ''))
    c.call(f'key {VK_F9}')

    if crash:
        # a fault in the frame, three outcomes: it reaches the unhandled filter (a crash report); the game's own
        # handler swallows it and the game stops drawing (a hang report after 30 s without frames); or the handler
        # swallows it and SporeKit finds the frame cut off and lets the frames go on (BUG-0667, SporeKit 0.3.11):
        # the game lives on, no report (seen in most runs of the 0.7.15 final check)
        n0 = set(glob.glob(os.path.join(LOGDIR, 'report-*.txt')) + glob.glob(os.path.join(LOGDIR, 'crash-*.txt')))
        try:
            log0 = os.path.getsize(logname())
        except OSError:
            log0 = 0
        try:
            c.call('call 0x1 cdecl')
        except (OSError, ValueError):
            pass
        got = []
        for _ in range(50):
            time.sleep(1)
            got = [p for p in set(glob.glob(os.path.join(LOGDIR, 'report-*.txt')) +
                                  glob.glob(os.path.join(LOGDIR, 'crash-*.txt'))) - n0
                   if re.search(r'hang|crash', open(p, errors='replace').readline())]
            if got:
                break
            # the third way: the game's handler caught the fault and SporeKit found the frame cut off (BUG-0667,
            # BUG-0966: "frames go on"); the game lives on and draws, so neither report comes (it did not crash)
            try:
                with open(logname(), errors='replace') as f:
                    f.seek(log0)
                    tail = f.read()
            except OSError:
                tail = ''
            m = re.search(r'frames: the frame \d+ was cut off by a fault the game\'s handler caught.*frames go on', tail)
            if m:
                try:  # it lives: a new console answers and frames go on
                    r = Console(lane.HOST).call('wait.frames 30 10000')
                    alive = (r.get('out') or '').startswith('frame=')
                except (OSError, ValueError):
                    alive = False
                if alive:
                    got = ['survived: ' + m.group(0)[:80]]
                    break
        ck(got, f'a fault in the frame: a hang or a crash report, or the game lives on (BUG-0667) ({[os.path.basename(p) for p in got]})')
        # a fault nobody handles (a thread of its own): the crash report and the minidump
        c = start_host(ck)
        n0 = set(glob.glob(os.path.join(LOGDIR, 'crash-*')))
        try:
            c.call('crash.test thread')
        except (OSError, ValueError):
            pass
        new = set()
        for _ in range(30):
            time.sleep(1)
            new = set(glob.glob(os.path.join(LOGDIR, 'crash-*'))) - n0
            if any(p.endswith('.dmp') for p in new):
                break
        txts = [p for p in new if p.endswith('.txt')]
        ck(txts, f'an unhandled fault: a crash report ({[os.path.basename(p) for p in new]})')
        if txts:
            t = open(txts[0], errors='replace').read()
            ck(re.search(r'== Exception\n0xC0000005 access violation at 0x[0-9A-F]{8} sporekit\.dll\+0x', t)
               and 'write of address 0x00000010' in t, 'it names the exception, the module and the address')
            ck('== Call chain' in t and '== The last log lines' in t and '== coop' in t,
               'it has the call chain, the co-op state and the log')
            print('  ' + '\n  '.join(t.split('== Exception')[1].splitlines()[:4]))
        for _ in range(20):  # the dump file exists before it is written in full
            if any(p.endswith('.dmp') and os.path.exists(p) and os.path.getsize(p) > 10000 for p in new):
                break
            time.sleep(1)
        ck(any(p.endswith('.dmp') and os.path.exists(p) and os.path.getsize(p) > 10000 for p in new),
           'a minidump is written next to it')
        # the archive is made by itself, with that crash in it; the next start tells the player
        zips = sorted(glob.glob(os.path.join(LOGDIR, 'SporeCoop-report-*.zip')), key=os.path.getmtime)
        crash_name = os.path.basename(txts[0]) if txts else '?'
        names = zipfile.ZipFile(zips[-1]).namelist() if zips else []
        ck(crash_name in names, f'the crash made the archive by itself, the crash report in it ({os.path.basename(zips[-1]) if zips else None})')
        ck(os.path.exists(os.path.join(LOGDIR, 'last-problem.txt')), 'the crash is marked for the next start')
        c = start_host(ck)
        time.sleep(10)
        chat = c.call('coop.status')['out']
        told = re.search(r'the last game crashed: mods.logs.SporeCoop-report-\S+\.zip', chat)
        ck(told, 'the next start tells the player where the archive is')
        ck(not os.path.exists(os.path.join(LOGDIR, 'last-problem.txt')), 'and only once (the mark is gone)')
        lane.sh('kill')
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
