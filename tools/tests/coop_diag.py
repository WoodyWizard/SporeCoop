#!/usr/bin/env python3
"""Support diagnostics of a co-op session (docs/SUPPORT.md), on host + guest of one stage:

1. the session clock: both games agree on the session time (the guest's estimate of the host's
   clock is off by less than 50 ms), every log line of the session carries `[s ...]`
2. a bug mark: `/bug <text>` typed into the guest's co-op chat writes a MARK line with the text, the
   player and the session time into the log of BOTH games; the guest's chat says "marked for the report"
3. the logs line up: tools/merge_logs.py over both logs gives one timeline in session-time order, the
   guest's MARK before the host's copy of it, less than 0.5 s apart
4. the sync self-check: OK with the guest in this stage on both games (`sync-check: OK`), and a
   difference forced on the guest (`coop.diag sync force shift`: its own creature 100 units off in its
   digest) is reported as a MISMATCH on both games after the 3 s recheck
5. its cost: the check's CPU time on the host (build + compare) and on the guest (build) under 1 ms,
   the diagnostics' average cost per frame under 0.05 ms (printed for docs/SUPPORT.md)
6. the environment: `env` lists the extra packages and the game files' size check

  coop_diag.py [--fixture cell|creature]
"""
import os
import re
import subprocess
import sys
import time

import lane
from twoinst import Checks, start

LOGS = os.path.join(lane.game_dir(), 'SporebinEP1', 'mods', 'logs')


def log_path(inst):
    return os.path.join(LOGS, 'sporekit.log' if inst == 1 else f'sporekit.{inst}.log')


def log_from(inst, offset):
    try:
        with open(log_path(inst), errors='replace') as f:
            f.seek(offset)
            return f.read()
    except OSError:
        return ''


def log_size(inst):
    try:
        return os.path.getsize(log_path(inst))
    except OSError:
        return 0


def wait(cond, secs, step=0.5):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return None


def session_time(con):
    m = re.search(r'clock: on, session time (-?[\d.]+)', con.call('coop.diag')['out'])
    return float(m.group(1)) if m else None


def num(rx, text, default=None):
    m = re.search(rx, text)
    return float(m.group(1)) if m else default


def main():
    fixture = sys.argv[sys.argv.index('--fixture') + 1] if '--fixture' in sys.argv else 'cell'
    stage = {'cell': 'Cell', 'creature': 'Creature'}[fixture]
    host, guest = start(fixture)
    ck = Checks()
    h0, g0 = log_size(lane.HOST), log_size(lane.GUEST)
    try:
        # 1. the session clock
        ck(wait(lambda: session_time(guest) is not None, 20), 'the guest has the session clock of the host')
        diffs = []
        for _ in range(5):
            t1 = time.time()
            th = session_time(host)
            tg = session_time(guest)
            t2 = time.time()
            if th is not None and tg is not None:
                diffs.append(abs(tg - th) - (t2 - t1))
            time.sleep(0.2)
        print(f'  clock: guest - host beyond the time between the reads: {[round(d * 1000, 1) for d in diffs]} ms')
        ck(diffs and min(diffs) < 0.05, f'host and guest agree on the session time (best {min(diffs) * 1000 if diffs else -1:.1f} ms)')

        # 4a. the sync-check OK in this stage
        host.call('coop.diag sync now')
        okh = wait(lambda: re.search(r'sync-check: OK with player \d+ .*\(' + stage, log_from(lane.HOST, h0)), 25)
        okg = wait(lambda: re.search(r'sync-check: OK with the host \(' + stage, log_from(lane.GUEST, g0)), 10)
        ck(okh, f'the host logs a sync-check OK in {stage}' + (f': {okh.group(0)}' if okh else ''))
        ck(okg, f'the guest logs a sync-check OK in {stage}')
        if not okh:
            print('  host coop.diag:', host.call('coop.diag')['out'].strip().replace('\n', ' | ')[:900])
            print('  host log sync lines:', re.findall(r'sync-check.*', log_from(lane.HOST, h0))[-4:])

        # 2. a bug mark typed into the guest's chat
        text = f'only one of us could eat {int(time.time()) % 10000}'
        said = guest.call(f'coop.say /bug {text}')['out']
        ck('marked for the report' in said, f'the guest is told "marked for the report" ({said.strip()})')
        rx = re.compile(r"MARK by 'Bob[^']*' \(player \d+\) at s ([\d.]+): " + re.escape(text))
        mh = wait(lambda: rx.search(log_from(lane.HOST, h0)), 10)
        mg = rx.search(log_from(lane.GUEST, g0))
        ck(mg, 'the MARK is in the guest\'s log with the player and the session time')
        ck(mh, 'the MARK reached the host\'s log')
        chat = guest.call('coop.status')['out']
        print('  guest status tail:', chat.strip().splitlines()[-1:] if chat else '')

        # 3. the logs line up (merge_logs)
        hp, gp = os.path.join(LOGS, 'merge-h.tmp.log'), os.path.join(LOGS, 'merge-g.tmp.log')
        open(hp, 'w').write(log_from(lane.HOST, h0))
        open(gp, 'w').write(log_from(lane.GUEST, g0))
        out = subprocess.run([sys.executable, os.path.join(lane.ROOT, 'tools', 'merge_logs.py'), f'host={hp}', f'guest={gp}'],
                             capture_output=True, text=True).stdout
        os.remove(hp)
        os.remove(gp)
        rows = [ln for ln in out.splitlines() if ln and not ln.startswith('#')]
        times = [float(ln.split()[0].rstrip('~')) for ln in rows]
        labels = {ln.split()[1] for ln in rows}
        ck(rows and times == sorted(times) and labels == {'host', 'guest'},
           f'merge_logs gives one timeline of both games in order ({len(rows)} lines)')
        marks = [(float(ln.split()[0]), ln.split()[1]) for ln in rows if 'MARK by' in ln and text in ln]
        print('  merged MARK lines:', marks)
        ck(len(marks) == 2 and marks[0][1] == 'guest' and marks[1][0] - marks[0][0] < 0.5,
           'in the merged timeline the guest\'s MARK comes first, the host\'s copy less than 0.5 s later')

        # 4b. a forced difference is reported as a MISMATCH on both games
        guest.call('coop.diag sync force shift')
        h1, g1 = log_size(lane.HOST), log_size(lane.GUEST)
        host.call('coop.diag sync now')
        mm = wait(lambda: re.search(r'sync-check: MISMATCH with player \d+ .*', log_from(lane.HOST, h1)), 30)
        ck(mm and re.search(r'p\d+ [\d.]+', mm.group(0)), 'the host reports the forced MISMATCH with the player apart' +
           (f': {mm.group(0)[:300]}' if mm else ''))
        mg2 = wait(lambda: re.search(r'sync-check: MISMATCH with the host.*', log_from(lane.GUEST, g1)), 10)
        ck(mg2, 'the guest\'s log has the MISMATCH too')
        guest.call('coop.diag sync force off')

        # 5. the cost
        dh, dg = host.call('coop.diag')['out'], guest.call('coop.diag')['out']
        hb = num(r'host build\+compare avg ([\d.]+) ms', dh, -1)
        hm = num(r'host build\+compare avg [\d.]+ ms max ([\d.]+) ms', dh, -1)
        gb = num(r'guest build avg ([\d.]+) ms', dg, -1)
        gm = num(r'guest build avg [\d.]+ ms max ([\d.]+) ms', dg, -1)
        hf = num(r'diagnostics cost: avg ([\d.]+) ms/frame', dh, -1)
        gf = num(r'diagnostics cost: avg ([\d.]+) ms/frame', dg, -1)
        hfm = num(r'diagnostics cost: avg [\d.]+ ms/frame over \d+ frames, max ([\d.]+) ms', dh, -1)
        byt = num(r'(\d+) bytes of digests', dh, -1)
        print(f'  COST {stage}: host check avg {hb} ms max {hm} ms; guest digest avg {gb} ms max {gm} ms; '
              f'per frame host {hf} ms (max {hfm}), guest {gf} ms; {byt:.0f} bytes of digests on the host')
        print('  host:', dh.strip().replace('\n', ' | ')[:700])
        ck(0 <= hm < 1.0 and 0 <= gm < 1.0, f'one check costs under 1 ms (host {hm} ms, guest {gm} ms)')
        ck(0 <= hf < 0.05 and 0 <= gf < 0.05, f'the diagnostics cost under 0.05 ms per frame on average (host {hf}, guest {gf})')

        # 6. the environment
        env = host.call('env')['out']
        ck('extra packages in Data' in env and 'game files' in env, 'env lists extra packages and the game files check')
        print('  env files:', [ln for ln in env.splitlines() if 'packages' in ln or 'game files' in ln][:4])
        summ = guest.call('coop.diag summary')['out']
        print('  guest summary:', summ.strip()[:400])
        ck('session as guest' in summ and 'sync-check' in summ, 'the session summary of the guest')
    finally:
        guest.call('coop.diag sync force off')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
