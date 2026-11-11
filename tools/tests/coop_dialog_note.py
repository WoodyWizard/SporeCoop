#!/usr/bin/env python3
"""BUG-0925: a dialog on the main thread that stops the frames is named in the log: its title, its text, the stack.

The 0.7.12 final check's long_creature host stopped with the C runtime's error box ('Wine C++ Runtime Library'): no
exception, no fault line, and the log had the box's title only (the keepalive's window list), not its text (R6025 pure
virtual function call / abnormal program termination / an assertion) and not the caller. SporeKit's watchdog now looks
for a visible dialog (#32770) of the main thread once the frames have stopped for 2 s and logs its title, the text of
its controls and the main thread's stack over every module; a runtime library box also writes a report.

Two games on the creature fixture. The host's main thread shows a message box with a runtime error's text for 6 s
(`kit.test.dialog 6000 report`: no frames, the thread pumps messages; the command closes the box). Checks: no frame
while the box was up, one `watchdog: ... shows a dialog` line with the title and the text, the main thread's call
chain (read by the thread itself) that names user32, a report file with the text, and frames afterwards.
DIALOG_NOTE_OFF=1 (`kit.fix dialognote 0`): the old SporeKit, the checks fail.

  SPORE_LANE=9 python3 tools/tests/coop_dialog_note.py
"""
import glob
import os
import re
import time

from twoinst import Checks, start
import lane


def main():
    host, guest = start('creature')
    ck = Checks()
    if os.environ.get('DIALOG_NOTE_OFF'):
        print('  ' + host.call('kit.fix dialognote 0')['out'].strip(), flush=True)
    path = lane.game_log(lane.HOST)
    logs = os.path.dirname(path)
    reports0 = set(glob.glob(os.path.join(logs, 'report-*.txt')))
    size0 = len(open(path, errors='replace').read())
    host.timeout = 40
    out = host.call('kit.test.dialog 6000 report')['out'].strip()
    print('  host:', out, flush=True)
    m = re.search(r'frames (\d+) before, (\d+) when it was up, (\d+) when it was closed', out)
    ck(bool(m) and int(m.group(3)) - int(m.group(2)) <= 1,
       'the host drew no frame while its main thread showed the box')
    time.sleep(3)
    new = open(path, errors='replace').read()[size0:]
    lines = [l for l in new.splitlines() if 'the main thread shows a dialog' in l]
    for l in lines:
        print('  ' + l[:400], flush=True)
    ck(len(lines) == 1, f'one dialog line in the host\'s log ({len(lines)})')
    ck(bool(lines) and "'SporeKit test dialog'" in lines[0] and 'R6025' in lines[0] and 'pure virtual' in lines[0],
       'it names the box\'s title and its text (R6025, pure virtual function call)')
    k = new.find('WATCHDOG (a dialog on the main thread')
    dump = new[k:] if k >= 0 else ''
    mods = sorted(set(x.lower() for x in re.findall(r'\] [0-9A-F]{8} ([\w.]+)\+0x', dump)))
    print('  stack dump modules:', mods, flush=True)
    ck('user32.dll' in mods and any(x.startswith('sporekit') for x in mods),
       'the main thread\'s stack is dumped over every module (user32 and the caller of the box)')
    reports = sorted(set(glob.glob(os.path.join(logs, 'report-*.txt'))) - reports0)
    text = open(reports[-1], errors='replace').read() if reports else ''
    print('  report:', os.path.basename(reports[-1]) if reports else None, text.splitlines()[:1], flush=True)
    ck('error dialog' in text and 'R6025' in text, 'a report names the dialog and its text')
    f0 = host.call('frame')['out']
    time.sleep(1)
    f1 = host.call('frame')['out']
    ck(f0 != f1, f'the host\'s frames go on after the box ({f0.strip()} -> {f1.strip()})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
