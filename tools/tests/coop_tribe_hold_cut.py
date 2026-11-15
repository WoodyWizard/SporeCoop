#!/usr/bin/env python3
"""Tribe: a hold of the guest's mirror's own ends 0.35 s after it began at a low frame rate too (BUG-0993).

BUG-0962 cuts a hold the mirror's own animation took (no hold of the host's member for it) after kHoldUnsent 0.35 s.
The cut was looked for with the rest of held_items_tick every 0.25 s: it came at the second tick after the tick that
saw the hold, 0.5-0.85 s after the hold began, and over 1.0 s at 26 fps with hitches (tribe_hold_guest, lab
20261112-222723 #3: 1.05 s).

1. Two games on the tribe fixture; the guest at ~25 fps (`frame.limit 40`, a weak PC).
2. ROUNDS times, each when the host's chief and its mirror are in their idle / walk (two samples 0.3 s apart; the
   chief's gestures take its staff and such a host hold binds the mirror's; a hold taken in the mirror's gesture ends
   with it: neither round is counted, BUG-1105) for at most --secs: the guest puts the staff into its mirror's hand of the host's chief (`coop.tribe hand <id> 17`, the
   game's Citizen_SetHandheldItem: a started kind 5 modifier the host never sent, as when the mirror's own gesture
   takes it), then samples the game's own hand state (`hold=`) for 1.6 s.
3. The guest's hold trace (`coop.tribe holdtrace <id>`, game seconds): the time from the start of the hold the cut
   ended (B, the guest's every-frame look at the hand for 3 s after the hand set H, whatever the switches) to the cut
   (C). (BUG-1105: from H the test counted the game's late start of the hold, HOC 0.836 s, and a hold that ended by
   itself plus the next one a relayed animation took, HBOROC 1.42 s, as the cut's delay.) A round with a host's hold of the chief in between (S / T), or in which the game took no hold or its animation ended
   the hold by itself within 0.55 s, is not counted; fewer than ROUNDS / 2 counted in --secs: SKIP.
Checked: every cut within 0.55 s of the hold's start (game clock); the game's `hold=` back to 0 within 1.0 s of the
first sample that showed it. SPORE_FIX_OFF=tribe.hold_cut_frame: 0.6-0.9 s (FAIL).

  SPORE_LANE=6 python3 tools/tests/coop_tribe_hold_cut.py [--rounds 10] [--fps 25] [--secs 300]
"""
import argparse
import re
import time

from twoinst import Checks, game_runs, start

LOCO = {'02481DE5', '04866D8E', '04866DAB', '04866DD4'}  # the chief's locomotion idle / walk (tribe_mirror_stale)


def anim(con, cid):
    m = re.search(r'cur ' + cid + r' anim=(\w+)', con.call('coop.tribe anims')['out'])
    return m.group(1) if m else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rounds', type=int, default=10)
    ap.add_argument('--fps', type=float, default=25)
    ap.add_argument('--secs', type=float, default=300, help='at most this long for the rounds')
    a = ap.parse_args()
    host, guest = start('tribe')
    ck = Checks()
    game_runs(guest, 90)
    time.sleep(5)
    lead = re.findall(r'#(\d+) \S+ owner=0 .*leader=1', host.call('coop.tribe')['out'])
    cid = lead[0] if lead else '1'
    print(f"  the host's chief #{cid}", flush=True)
    if a.fps:
        print('  guest: ' + guest.call(f'frame.limit {round(1000 / a.fps)}')['out'].strip(), flush=True)
    time.sleep(3)
    cuts, sampled, skipped, r, t_all = [], [], 0, -1, time.time()
    while len(cuts) < a.rounds and time.time() - t_all < a.secs:
        h1, g1 = anim(host, cid), anim(guest, cid)
        time.sleep(0.3)
        h2, g2 = anim(host, cid), anim(guest, cid)
        # (the host's chief in a gesture: its own hold may come; the mirror in a gesture: the hold ends with it)
        if not {h1, h2, g1, g2} <= LOCO:
            time.sleep(0.5)
            continue
        r += 1
        out = guest.call(f'coop.tribe hand {cid} 17')['out'].strip()
        if r == 0:
            print(f'  guest hand: {out}', flush=True)
        rows, t0 = [], time.time()
        while time.time() - t0 < 1.6:
            m = re.search(r'hold=(\d)', guest.call(f'coop.tribe hand {cid}')['out'])
            if m:
                rows.append((time.time(), int(m.group(1))))
            time.sleep(0.03)
        on = [t for t, h in rows if h]
        held = None  # the game's own hold in this round, s (None: never held)
        if on:
            off = next((t for t, h in rows if not h and t > on[0]), rows[-1][0] + 0.03)
            held = round(off - on[0], 2)
        ev = []
        for ln in guest.call(f'coop.tribe holdtrace {cid}')['out'].splitlines():
            p = ln.split()
            if p[:1] == ['hev']:
                ev.append((float(p[1]), p[3]))
        hs = [i for i, (_, w) in enumerate(ev) if w == 'H']
        if not hs:
            print(f'  round {r + 1}: no hand event in the trace', flush=True)
            continue
        tail = ev[hs[-1]:]
        print('    before ' + ' '.join(f'{w}{t - tail[0][0]:.3f}' for t, w in ev[:hs[-1]] if t > tail[0][0] - 4), flush=True)
        print('    trace ' + ' '.join(f'{w}+{t - tail[0][0]:.3f}' for t, w in tail), flush=True)
        if any(w in 'ST' for _, w in tail):
            skipped += 1
            print(f'  round {r + 1}: a host hold came in between ({"".join(w for _, w in tail)}), not counted', flush=True)
            continue
        if 'O' not in [w for _, w in tail] and not on:  # (the game took no hold: the mirror in a walk or a change)
            skipped += 1
            print(f'  round {r + 1}: no hold began ({"".join(w for _, w in tail)}), not counted', flush=True)
            continue
        if held is not None:
            sampled.append(held)
        end = next((t for t, w in tail if w in 'CM'), None)
        if end is None:
            if held is not None and held < 0.55:  # (its animation ended and took the hold with it: nothing to cut)
                print(f'  round {r + 1}: the hold ended by itself after {held} s ({"".join(w for _, w in tail)})', flush=True)
                continue
            print(f'  round {r + 1}: no cut ({"".join(w for _, w in tail)}); sampled hold {held} s', flush=True)
            cuts.append(9.9)
            continue
        # (BUG-1105: from the start of the hold the cut ended, the last B before it: the frame the game began that
        # hold modifier. The game can begin it a few tenths after the hand set (lab 20261114-022615: HOC 0.836 s, the
        # cut comes 0.35 s after O), and the hand set's hold can end by itself and a relayed animation take the item
        # again (lab 20261114-031120 #2: B+0.04, hold= 0 after 0.09 s, R+0.93, O+1.05, C+1.46: 1.42 s from the first B))
        began = max((t for t, w in tail if w == 'B' and t <= end), default=tail[0][0])
        cuts.append(round(end - began, 3))
        print(f'  round {r + 1}: cut {cuts[-1]} s after the hold began ({round(began - tail[0][0], 3)} s after the hand '
              f'set; {"".join(w for _, w in tail)}); sampled hold {held} s', flush=True)
        time.sleep(0.5)
    if a.fps:
        guest.call('frame.limit auto')
    print(f'  cuts {cuts}; sampled holds {sampled}; skipped {skipped}', flush=True)
    if len(cuts) < a.rounds // 2:  # (the host's chief held its staff itself in most rounds: the case did not come)
        print(f'RESULT: SKIP {len(cuts)} rounds counted (< {a.rounds // 2}): the host\'s holds came in between', flush=True)
        return 0
    ck(cuts and all(c <= 0.55 for c in cuts), f'every hold of the mirror\'s own is cut within 0.55 s ({cuts})')
    ck(sampled and all(s < 1.0 for s in sampled), f'the game\'s hold= is back to 0 within 1.0 s ({sampled})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
