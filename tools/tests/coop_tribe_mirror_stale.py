#!/usr/bin/env python3
"""Tribe: a guest's mirror leaves a gesture that came back after another animation when the host's member stands
(BUG-0972).

same_view_tribe's stills showed the host's chief standing at the fire on one game and in another pose (a step, a
crouch, a gesture) on the other. Probe (lane 6, both games' `coop.tribe anims` at 5 Hz for 150 s): the host's chief
and its mirror played the same animation in 59 % of the samples; 257 of the others were the host's chief in its idle
0x02481DE5 and the mirror in the gesture 0x04C6CB02, for 37 s in one stretch. The relayed restart of the gesture had
waited behind the running one (a pending track, BUG-0992), the next relayed start (the change 0x02C39200) played,
and after it the waiting gesture came back; the idle fix (kind 10, BUG-0936) took it for the mirror's own idle or
walk because it was not the last relayed animation, and left it.

1. Two games on the tribe fixture; the host's chief (owner 0, the leader) and the guest's mirror of it.
2. ROUNDS times, when the host's chief stands or walks in its own idle (two samples in a row in its locomotion's
   idle / walk): the guest's mirror plays the gesture 0x04C6CB02 as such a returned start (`coop.tribe mplay`: the
   game's CreaturePlayAnim, the last relayed animation left as it is).
3. Both games' animation of the chief at 5 Hz for 9 s (the game's current main animation). A round counts while the
   host's chief stays in its idle / walk.
Fewer than 2 counted rounds in --secs: SKIP. Checked: in every counted round the mirror is back in its idle / walk
within 4.5 s (the host's idle news comes once a second; the fix waits 1.5-2 s after a relayed start).
SPORE_FIX_OFF=tribe.mirror_stale_anim: the mirror stays in the gesture (FAIL).
BUG-1071: every second round the host first forgets the chief's last behaviour start (`coop.tribe bhvzero`: bhv 0, as
right after the save's load, when the first round of a session failed); SPORE_FIX_OFF=tribe.mirror_idle_loco: no idle
news for it, the mirror stays in the gesture until it ends (FAIL). Key frame of kind sameview: the chief
on both games after the last round.

  SPORE_LANE=6 python3 tools/tests/coop_tribe_mirror_stale.py [--rounds 4]
"""
import argparse
import re
import time

from coop_tribe_looks import view_both
from twoinst import Checks, game_runs, keyframe, start

LOCO = {'02481DE5', '04866D8E', '04866DAB', '04866DD4'}
GESTURE = '04C6CB02'


def row(con, cid):
    m = re.search(r'cur ' + cid + r' anim=(\w+) bhv=(\w+) pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', con.call('coop.tribe anims')['out'])
    return (m.group(1), m.group(2), tuple(float(m.group(i)) for i in (3, 4, 5))) if m else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rounds', type=int, default=4)
    ap.add_argument('--secs', type=float, default=300, help='at most this long for the rounds')
    ap.add_argument('--zero-bhv', type=int, default=1, help='BUG-1071: every second round the host forgets the chief\'s '
                    'last behaviour start (`coop.tribe bhvzero`), as after the load; 0: never')
    a = ap.parse_args()
    host, guest = start('tribe')
    ck = Checks()
    game_runs(guest, 90)
    time.sleep(5)
    lead = re.findall(r'#(\d+) \S+ owner=0 .*leader=1', host.call('coop.tribe')['out'])
    cid = lead[0] if lead else '1'
    print(f"  the host's chief #{cid}", flush=True)
    done, backs, t0, zeros = 0, [], time.time(), 0
    while done < a.rounds and time.time() - t0 < a.secs:
        h1 = row(host, cid)
        time.sleep(0.3)
        h2 = row(host, cid)
        if not h1 or not h2 or h1[0] not in LOCO or h2[0] not in LOCO:
            time.sleep(1.0)
            continue
        zero = a.zero_bhv and done % 2 == 1  # BUG-1071: every second round as right after the load (no behaviour start)
        if zero:
            host.call(f'coop.tribe bhvzero {cid}')
            h2 = row(host, cid) or h2
        out = guest.call(f'coop.tribe mplay {cid} {GESTURE}')['out'].strip() + f' host={h2[0]} bhv={h2[1]}'
        ts, host_ok, back = time.time(), True, None
        seq = []
        while time.time() - ts < 9.0:
            h, g = row(host, cid), row(guest, cid)
            if h and h[0] not in LOCO:
                host_ok = False
            if g:
                if not seq or seq[-1][1] != g[0]:
                    seq.append((round(time.time() - ts, 1), g[0]))
                if back is None and g[0] in LOCO and time.time() - ts > 0.3:
                    back = round(time.time() - ts, 1)
            time.sleep(0.2)
        if not host_ok:
            print(f'  ({out}; the host\'s chief left its idle / walk meanwhile: not counted; mirror {seq})', flush=True)
            continue
        done += 1
        zeros += zero and h2[1] == '00000000'
        backs.append(back if back is not None else 99.0)
        if back is None:  # BUG-1071: what the guest's idle fix did meanwhile (I / Z: the host's idle news acted on)
            ev = [ln for ln in guest.call(f'coop.tribe holdtrace {cid}')['out'].splitlines() if ln.startswith('hev')]
            print('  (holdtrace of the round: ' + '; '.join(ev[-12:]) + ')', flush=True)
        print(f'  round {done}: {out}; the mirror back in its idle / walk after {back} s ({seq})', flush=True)
        time.sleep(2.0)
    print('  ' + next((ln.strip() for ln in guest.call('coop.tribe anims')['out'].splitlines() if ln.startswith('idle fix')), ''),
          flush=True)
    h = row(host, cid)
    if h and view_both(host, guest, h[2], 10.0, 3.0):
        keyframe('chief', f'the same camera on both games 10 m from the host\'s chief #{cid}: it stands or walks the '
                          f'same way on both screens (not in a gesture with its staff on the guest)', kind='sameview')
    if done < 2:  # (the host's chief gestured the whole time: the case never came)
        print(f'RESULT: SKIP {done} rounds with the host\'s chief in its idle / walk in {a.secs:.0f} s (< 2)', flush=True)
        return 0
    if a.zero_bhv:
        ck(zeros >= 1, f'at least one counted round with the host\'s chief without a behaviour start ({zeros})')
    ck(backs and all(b <= 4.5 for b in backs), f'the guest\'s mirror leaves the returned gesture within 4.5 s in every round ({backs})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
