#!/usr/bin/env python3
"""Merge the logs of the players of one co-op session into one timeline (docs/SUPPORT.md).

Every game in a session writes the session time (the host's clock) into its log lines:
`12:34:56.789 [s 1234.567] [INF] coop: ...`. This tool puts the lines of several logs in the order
of that time, so the host's and the guests' views of one moment are next to each other.

    tools/merge_logs.py host.log guest.log            the last session found in each log
    tools/merge_logs.py report.zip other-report.zip   the logs inside report zips (logs/, previous/)
    tools/merge_logs.py --session 2 a.log b.log       the 2nd session of each log (see the list printed)
    tools/merge_logs.py --grep 'MARK|sync-check|eat:' --around 30 a.log b.log
                                                      only matching lines (and 30 s around each MARK)
Options: --all: also lines without a session time (placed by the nearest line with both clocks),
--from S / --to S: a window of session time, --list: only list the sessions of each log.

A log file name can be given a label: `Bob=guest.log`. Without one, the label is the file name.
"""
import argparse
import io
import os
import re
import sys
import zipfile

LINE = re.compile(r'^(\d\d):(\d\d):(\d\d)\.(\d{3}) (?:\[s (-?\d+\.\d+)\] )?(.*)$')


def local_seconds(m):
    return int(m.group(1)) * 3600 + int(m.group(2)) * 60 + int(m.group(3)) + int(m.group(4)) / 1000


class Log:
    def __init__(self, label, text):
        self.label = label
        self.lines = []  # (local seconds, session time or None, text, session index or None)
        self.sessions = []  # [first index, last index, s min, s max, local start]
        day = 0
        last_local = None
        last_s = None
        cur = None
        for raw in text.splitlines():
            m = LINE.match(raw)
            if not m:
                if self.lines:  # a continuation line: keeps the times of the line before
                    lt, s, _, k = self.lines[-1]
                    self.lines.append((lt, s, raw, k))
                continue
            lt = local_seconds(m) + day
            if last_local is not None and lt < last_local - 12 * 3600:  # past midnight
                day += 86400
                lt += 86400
            last_local = lt
            s = float(m.group(5)) if m.group(5) else None
            k = None
            if s is not None:
                # a new session: the clock started again (or the first line with a clock)
                if cur is None or last_s is None or s < last_s - 5 or lt - self.lines[self.sessions[cur][1]][0] > 600:
                    self.sessions.append([len(self.lines), len(self.lines), s, s, raw[:12]])
                    cur = len(self.sessions) - 1
                ses = self.sessions[cur]
                ses[1] = len(self.lines)
                ses[2] = min(ses[2], s)
                ses[3] = max(ses[3], s)
                last_s = s
                k = cur
            self.lines.append((lt, s, raw, k))

    def estimate(self, i, k):
        """the session time of line i (no clock of its own) from the nearest line of session k"""
        best = None
        for j in range(max(0, i - 400), min(len(self.lines), i + 400)):
            lt, s, _, kk = self.lines[j]
            if kk == k and s is not None and (best is None or abs(j - i) < abs(best[0] - i)):
                best = (j, s - lt)
        return None if best is None else self.lines[i][0] + best[1]


def read_inputs(paths):
    logs = []
    for p in paths:
        label = None
        if '=' in p and not os.path.exists(p):
            label, p = p.split('=', 1)
        if zipfile.is_zipfile(p):
            z = zipfile.ZipFile(p)
            for n in z.namelist():
                if n.endswith('.log') and (n.startswith('logs/') or n.startswith('previous/')):
                    text = io.TextIOWrapper(z.open(n), errors='replace').read()
                    logs.append(Log(f'{label or os.path.basename(p)}:{os.path.basename(n)}', text))
        else:
            with open(p, errors='replace') as f:
                logs.append(Log(label or os.path.basename(p), f.read()))
    return logs


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('logs', nargs='+')
    ap.add_argument('--session', type=int, default=0, help='the Nth session of each log (1 = first; default: the last)')
    ap.add_argument('--all', action='store_true', help='also lines without a session time')
    ap.add_argument('--from', dest='t_from', type=float)
    ap.add_argument('--to', dest='t_to', type=float)
    ap.add_argument('--grep', help='only lines matching this regular expression')
    ap.add_argument('--around', type=float, default=0, help='with --grep: also every line this many s around a MARK')
    ap.add_argument('--list', action='store_true', help='only list the sessions of each log')
    a = ap.parse_args(argv)
    logs = [lg for lg in read_inputs(a.logs) if lg.sessions or a.list]
    if not logs:
        print('no line with a session time ([s ...]) in these logs: the games were not in a session, or an older '
              'SporeCoop wrote them', file=sys.stderr)
        return 1
    width = max(len(lg.label) for lg in logs)
    for lg in logs:
        for n, (i0, i1, s0, s1, start) in enumerate(lg.sessions, 1):
            print(f'# {lg.label}: session {n}: s {s0:.1f}..{s1:.1f}, local {start}, lines {i0 + 1}..{i1 + 1}')
    if a.list:
        return 0
    rows = []
    for lg in logs:
        k = (a.session - 1) if a.session > 0 else len(lg.sessions) - 1
        if k < 0 or k >= len(lg.sessions):
            print(f'# {lg.label}: no session {a.session}', file=sys.stderr)
            continue
        i0, i1 = lg.sessions[k][0], lg.sessions[k][1]
        for i in range(i0, i1 + 1):
            lt, s, text, kk = lg.lines[i]
            est = False
            if s is None or kk != k:
                if not a.all:
                    continue
                s = lg.estimate(i, k)
                est = True
                if s is None:
                    continue
            rows.append((s, len(rows), lg.label, text, est))
    rows.sort()
    marks = [r[0] for r in rows if ' MARK ' in r[3]]
    rx = re.compile(a.grep) if a.grep else None
    for s, _, label, text, est in rows:
        if a.t_from is not None and s < a.t_from or a.t_to is not None and s > a.t_to:
            continue
        if rx and not rx.search(text) and not (a.around and any(abs(s - m) <= a.around for m in marks)):
            continue
        print(f'{s:10.3f}{"~" if est else " "} {label:<{width}}  {text}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
