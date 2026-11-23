#!/usr/bin/env python3
""""Report a problem" does not open Explorer while the game fills the screen (BUG-1528, the tearing report).

The report button opened Explorer with the archive selected (ShellExecute explorer.exe /select). In exclusive full
screen Explorer took the foreground and the game minimized itself: its own focus pause, which the report's log then
showed as a 20-30 s freeze (out/research/dc-report-tearing-20261116.md). Now, while the game window fills its monitor,
the folder is not opened and the window's status names where the report is.

The lab plays windowed, so the test tells the mod that the game fills the screen (`coop.uistate fullscreen 1`; the
real answer comes from the game window: no caption, the monitor's whole rectangle).

Creature fixture, two players. The host opens the co-op window (F9) and clicks "Report a problem" (a real click):
  PASS: the archive is written (`report archive:` in the host's log), the host's log says the folder is not opened
  (no "the folder opens in Explorer"), the status says "the folder is not opened while the game is in full screen";
  key frame of the host's window. Then `coop.uistate fullscreen 0` and a second click: the folder opens as before
  (windowed play keeps the old way).
  SPORE_FIX_OFF=core.report_no_explorer: Explorer is opened in full screen: FAIL.

  SPORE_LANE=2 python3 tools/tests/coop_report_fullscreen.py
"""
import os
import time

import lane
from coop_ui import VK_F9, Ui
from twoinst import Checks, keyframe, start

LOGS = os.path.join(lane.game_dir(), 'SporebinEP1', 'mods', 'logs')


def log_path(inst):
    return os.path.join(LOGS, 'sporekit.log' if inst == 1 else f'sporekit.{inst}.log')


def log_size(inst):
    try:
        return os.path.getsize(log_path(inst))
    except OSError:
        return 0


def log_from(inst, offset):
    try:
        with open(log_path(inst), errors='replace') as f:
            f.seek(offset)
            return f.read()
    except OSError:
        return ''


def click_report(host, ui):
    """a real click on Report a problem; (status, the host's log since the click)"""
    h0 = log_size(lane.HOST)
    time.sleep(1.1)  # a new archive name
    ui.click('report')
    st = ui.wait(lambda s: 'Report saved' in s.get('status', ''), 20, 'the report status')
    time.sleep(1)
    return (st or {}).get('status', ''), log_from(lane.HOST, h0)


def main():
    host, guest = start('creature')
    ck = Checks()
    try:
        time.sleep(3)
        print(' ', host.call('coop.uistate fullscreen 1')['out'].strip(), flush=True)
        ui = Ui(host, 'host')
        host.call(f'key {VK_F9}')
        st = ui.wait(lambda s: s['open'] == '1' and s['report']['vis'], 20, 'the co-op window')
        if not ck(st is not None, 'the host\'s co-op window is open with "Report a problem"'):
            return ck.result()
        status, log = click_report(host, ui)
        print('  full screen: status', status[:200], flush=True)
        ck('report archive:' in log, 'the report archive is written')
        ck('the folder is not opened' in log and 'the folder opens in Explorer' not in log,
           'in full screen the folder is not opened in Explorer (the game keeps its window)')
        ck('not opened while the game is in full screen' in status, 'the status says where the report is and why')
        keyframe('report_fullscreen', 'the host\'s co-op window: "Report saved in SporebinEP1\\mods\\logs: ... (the folder '
                 'is not opened while the game is in full screen)"; no Explorer window', players={'host': host})
        # windowed: as before
        host.call('coop.uistate fullscreen 0')
        time.sleep(2.5)
        status, log = click_report(host, ui)
        print('  windowed: status', status[:200], flush=True)
        ck('the folder opens in Explorer' in log, 'windowed, the folder opens in Explorer as before')
        host.call(f'key {VK_F9}')
    finally:
        host.call('coop.uistate fullscreen auto')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
