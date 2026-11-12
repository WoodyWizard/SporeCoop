#!/usr/bin/env python3
"""Tribe: a guest's mirror holds its item in the hand only while the host's member does (BUG-0962).

same_view_tribe's stills (lab 20261112-102606) showed the host's chief holding its staff forward in the hand on one
game and upright in the resting grip on the other. Measured (lab lane 5, the chief's `coop.tribe anims` on both games
at ~20 Hz for 120 s): the holds the host's member takes reach the mirror 0.0-0.07 s later and end within 0.02 s, but
in a gesture 0x04C6CB02 the mirror's own animation event took the staff while the host's member did not, for 1.66 s
(BUG-0906's 1.5 s settle).

Two games on the tribe fixture. Both games' `coop.tribe anims` rows of the host's chief are sampled together for up
to WINDOWS x 120 s (until at least one hold of the mirror's own came, bounded). A mirror hold is "the mirror's own"
when no hold of the host's member overlaps it (within 0.3 s). Checked on the games' own state (the hand's started
modifier, `hold=`): every hold of the mirror's own is shorter than 1.0 s; the holds of the host's member are held by
the mirror too (start within 0.5 s). Without the fix (SPORE_FIX_OFF=tribe.mirror_hold_only) a hold of the mirror's own
lasts ~1.5 s.

  SPORE_LANE=5 python3 tools/tests/coop_tribe_hold_only.py [--windows 3]
"""
import argparse
import re
import threading
import time

from twoinst import Checks, game_runs, start  # (first: it puts tools/ on the path)
import lane
from sporectl import Console


def sample(inst, cid, secs, rows):
    c = Console(inst)
    t0 = time.time()
    while time.time() - t0 < secs:
        m = re.search(r'cur ' + cid + r' anim=(\w+) bhv=\w+ .*hold=(\d)', c.call('coop.tribe anims')['out'])
        if m:
            rows.append((time.time(), m.group(1), int(m.group(2))))
        time.sleep(0.04)


def holds(rows, open_end=False):
    """(start, end, anim) of each hold; open_end: also one still held at the last sample (ending there)"""
    out, st = [], None
    for t, a, h in rows:
        if h and st is None:
            st = (t, a)
        elif not h and st is not None:
            out.append((st[0], t, st[1]))
            st = None
    if open_end and st is not None and rows:
        out.append((st[0], rows[-1][0], st[1]))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--windows', type=int, default=3)
    ap.add_argument('--secs', type=float, default=120)
    ap.add_argument('--who', choices=('host', 'guest'), default='host',
                    help="whose chief: the host's, or the guest's (simulated by the host too; BUG-0968)")
    ap.add_argument('--changes', type=int, default=0,
                    help='BUG-0971: sample until this many holds of the host\'s member in the change animation 0x02C39200')
    a = ap.parse_args()
    host, guest = start('tribe')
    ck = Checks()
    game_runs(guest, 90)
    time.sleep(5)
    owner = r'owner=0' if a.who == 'host' else r'owner=[1-9]\d*'
    lead = re.findall(r'#(\d+) \S+ ' + owner + r' .*leader=1', host.call('coop.tribe')['out'])
    cid = lead[0] if lead else '1'
    print(f'  the {a.who}\'s chief #{cid}', flush=True)
    own, shared, missed, t0, changes = [], 0, [], None, 0
    for w in range(a.windows):
        rows = {'host': [], 'guest': []}
        ths = [threading.Thread(target=sample, args=(i, cid, a.secs, rows[r]))
               for r, i in (('host', lane.HOST), ('guest', lane.GUEST))]
        for t in ths:
            t.start()
        for t in ths:
            t.join()
        t0 = t0 or (rows['host'][0][0] if rows['host'] else time.time())
        hh, gh = holds(rows['host']), holds(rows['guest'])
        hh_all = holds(rows['host'], open_end=True)  # (a host hold still open at the window's end overlaps too)
        for s, e, an in gh:
            if not any(hs - 0.3 < e and s < he + 0.3 for hs, he, _ in hh_all):
                own.append((round(s - t0, 1), round(e - s, 2), an))
        w0 = rows['host'][0][0] if rows['host'] else 0
        for s, e, an in hh:
            if s - w0 < 0.2:  # (open at the window's start: not comparable)
                continue
            changes += an == '02C39200'
            if any(gs < e + 0.3 and s < ge + 0.3 for gs, ge, _ in gh):
                shared += 1
            elif e - s > 0.3:
                missed.append((round(s - t0, 1), round(e - s, 2), an))
                g_near = [(round(t - t0, 2), x, h) for t, x, h in rows['guest'] if s - 1.5 < t < e + 0.5][::2]
                h_near = [(round(t - t0, 2), x, h) for t, x, h in rows['host'] if s - 1.5 < t < e + 0.5][::2]
                print(f'    the mirror around the host\'s hold at {s - t0:.1f} s: {g_near[:20]}', flush=True)
                print(f'    the host around it: {h_near[:40]}', flush=True)
        new_miss = [m for m in missed if m[0] + t0 >= w0 - 1]
        if new_miss:  # the guest's own hold events of the chief around each miss (start / end received, binding, cuts)
            tr = Console(lane.GUEST).call(f'coop.tribe holdtrace {cid}')['out']
            m_now = re.search(r'now ([\d.]+)', tr)
            if m_now:  # (game seconds: shown relative to this moment, the wall clock of the call)
                gnow, wnow = float(m_now.group(1)), time.time() - t0
                for ms, md, _ in new_miss:
                    print(f'    guest events around the miss at {ms} s:', flush=True)
                    for ln in tr.splitlines():
                        p = ln.split()
                        if p[:1] == ['hev'] and ms - 2 < float(p[1]) - gnow + wnow < ms + md + 1:
                            print(f'      {round(float(p[1]) - gnow + wnow, 2)} {" ".join(p[3:])}', flush=True)
        print(f'  window {w + 1}: {len(rows["host"])} / {len(rows["guest"])} samples; host holds '
              f'{[(round(s - t0, 1), round(e - s, 2), x) for s, e, x in hh]}; the mirror\'s own holds so far {own}', flush=True)
        if a.changes:
            print(f'  holds in the change animation so far: {changes}', flush=True)
            if changes >= a.changes:
                break
        elif own and a.who == 'host':
            break
    st = next((ln.strip() for ln in guest.call('coop.status')['out'].splitlines() if 'unsent holds' in ln), '')
    print(f'  guest: {st[-60:]}', flush=True)
    if a.changes:
        ck(changes >= a.changes, f'the host\'s member took its item in the change animation {changes} times (>= {a.changes})')
    elif a.who == 'host':
        ck(own, f'the mirror took its item by its own animation at least once ({len(own)})')
    # (the cut comes kHoldUnsent 0.35 s after the guest's 0.25 s tick first saw the hold: up to 0.85 s by design; lab
    # 20261112-163629 #2 measured 0.8 s; without the fix 1.5-1.8 s)
    ck(all(d < 1.0 for _, d, _ in own), f'every hold of the mirror\'s own ends within 1.0 s ({own})')
    ck(not missed, f'the host member\'s holds are the mirror\'s too ({shared} shared, missed {missed})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
