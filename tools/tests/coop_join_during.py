#!/usr/bin/env python3
"""A guest joins while the host is busy: what the joining player's game shows in its first seconds.

Players join whenever they like: in the middle of the host's fight, while the host is in its editor.
The join tests start from a quiet host; these start the join in the busy moment and then read the
joining guest's game (not the mod's counters) every second until it agrees with the host's game.

--case combat   the host fights an NPC with real input (coop.social goto, the combat stance, a click on
                the target, key 1 bites) and the guest joins 4 s into the fight.
--case editor   the host opens its creature editor alone (`coop.vote editor` with nobody to wait for),
                the guest joins while it edits, the host leaves the editor with "done" 25 s later. While the
                host edits, the guest's own creatures must move in its game (BUG-0205: the host sends no NPC
                states then; frozen proxies are the bug; SPORE_FIX_OFF=core.away_editor).

Consistent (all at once, read on the joining guest's game against the host's game):
  * the guest plays the host's stage with an avatar and the host has it in its world;
  * the guest's game draws the host (coop.sync p0, its puppet) within 4 m of where the host's own game
    has the host's avatar, as the species the host plays (the census key of the creature drawn there);
  * the host's NPCs near the guest are there on the guest, alive / dead as on the host, within 10 m
    (campaign_lib.census_problems over coop.census of both);
  * the guest's game draws exactly one player creature (the host's puppet: no stray left by the host's editor);
  * combat: the NPC the host fights is on the guest, with the host's dead flag and its health within
    25 % of the creature's (the host's bites change it between the reads).
The first consistent read must come within --limit s (10) of the guest being in the world (combat) or
of the host being back in its stage with the guest in the world (editor); it must hold for 3 reads.
Combat: when the NPC dies on the host it is dead in the guest's game within 10 s too.

  SPORE_LANE=9 python3 tools/tests/coop_join_during.py --case combat|editor
  JOIN_PLANT=leave: the guest drops out of the session right after the join: must FAIL (JOIN_PLANT=lift, the
  host drawn 5 m up, did not: the puppet of a standing host stays on the ground)
"""
import argparse
import math
import os
import re
import threading
import time

import campaign_lib as cl
import census
import lane
from coop_creature_kills import npcs, family
from coop_multi import sample
from twoinst import Checks, shot, start, wait_guest_in

W, H = 1440, 1080
DONE = (1416, 1062)  # the green "done" of the creature editor (1440x1080)
NEAR_HOST = 4.0


def mode(con):
    return con.call('game.mode')['out']


def own_pos(con):
    m = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', con.call('coop.avatar')['out'])
    return tuple(map(float, m.groups())) if m else None


def together(host):
    return {int(m.group(1)) for m in re.finditer(r'^together (\d+) 1$', host.call('coop.status')['out'], re.M)}


class Fight(threading.Thread):
    """the host bites NPC nid with real input until it is dead or the time is up"""

    def __init__(self, host, nid, secs):
        super().__init__(daemon=True)
        self.host, self.nid, self.secs, self.bites, self.stop = host, nid, secs, 0, False

    def run(self):
        h = self.host
        h.call(f'coop.social goto {self.nid}')
        time.sleep(1)
        h.call(f'click {W // 2 + 43} {H - 82}')  # the combat stance
        t0 = time.time()
        while not self.stop and time.time() - t0 < self.secs:
            n = npcs(h).get(self.nid)
            if not n or n[4]:
                break
            for line in h.call('coop.targets all')['out'].splitlines():
                f = line.split()
                if len(f) == 5 and f[0] == 'obj' and f[1] == 'n' + self.nid and 20 < int(f[3]) < W - 20 \
                        and 90 < int(f[4]) < H - 170:
                    h.call(f'click {f[3]} {f[4]}')
            time.sleep(0.3)
            h.call('key 1')
            self.bites += 1
            time.sleep(1.2)


def consistent(host, guest, nid=None):
    """[] when the guest's game agrees with the host's, else what differs"""
    bad = []
    hst, gst = cl.stage_of(mode(host)), cl.stage_of(mode(guest))
    if hst is None or gst != hst:
        return [f'stage host {hst} guest {gst}']
    if not guest.call('coop.avatar')['ok']:
        return ['the guest has no avatar']
    hc, gc = census.dump(host), census.dump(guest)
    hp_ = own_pos(host)
    at = sample(guest).get('p0')
    if hp_ is None:
        bad.append('the host has no avatar')
    elif at is None:
        bad.append('the guest does not show the host')
    else:
        d = math.dist(hp_, at)
        if d > NEAR_HOST:
            bad.append(f'the guest shows the host {d:.1f} m off')
        hav = next((r for r in hc.rows if r['own'] == 'avatar'), None)
        pup = min((r for r in gc.rows if r['own'] == 'puppet'), key=lambda r: math.dist(r['pos'], at), default=None)
        if pup is None or math.dist(pup['pos'], at) > NEAR_HOST:
            bad.append('no creature drawn where the guest shows the host')
        elif hav and pup['key'] != hav['key']:
            bad.append(f'the guest draws the host as {pup["key"]}, the host plays {hav["key"]}')
    pups = [r for r in gc.rows if r['own'] == 'puppet']
    if len(pups) != 1:  # one partner: one puppet (a puppet left over after the host's editor, core.strays)
        bad.append(f'{len(pups)} player creatures drawn on the guest, 1 partner: '
                   + ', '.join(f'{r["key"]} at {[round(x) for x in r["pos"]]}' for r in pups[:4]))
    bad += cl.census_problems(hc, gc)
    if nid is not None:
        h = hc.by_id('c', ('npc',)).get(int(nid))
        g = gc.by_id('c', ('proxy',)).get(int(nid))
        if h and not g:
            near = gc.rows and next((r for r in gc.rows if r['own'] == 'avatar'), None)
            far = math.dist(h['pos'], near['pos']) if near else -1
            bad.append(f'the fought NPC #{nid} is not on the guest ({far:.0f} m from it)')
        elif h and g:
            if h['alive'] != g['alive']:
                bad.append(f'the fought NPC: alive={h["alive"]} on the host, {g["alive"]} on the guest')
            top = max(h['hp'], g['hp'], 1.0)
            if h['alive'] and abs(h['hp'] - g['hp']) > 0.25 * top:
                bad.append(f'the fought NPC: hp {h["hp"]:g} on the host, {g["hp"]:g} on the guest')
    return bad


def settle_check(host, guest, ck, label, limit, nid=None, t_in=None):
    """reads every second until 3 consistent reads in a row; the first of them within limit s"""
    t_in = t_in or time.time()
    if os.environ.get('JOIN_PLANT') == 'lift':  # a planted problem: the guest draws its partner 5 m up
        print('  PLANTED: guest: ' + guest.call('coop.track lift 5')['out'].strip()[:100], flush=True)
    if os.environ.get('JOIN_PLANT') == 'leave':  # a planted problem: the join breaks right after it (the guest
        # drops out of the session and keeps playing its copy: no host, the host's NPCs gone)
        print('  PLANTED: guest: ' + guest.call('coop.leave')['out'].strip()[:100], flush=True)
    first, run, last = None, 0, []
    while time.time() - t_in < limit + 30:
        bad = consistent(host, guest, nid)
        t = time.time() - t_in
        if not bad:
            run += 1
            first = first if first is not None else t
            if run >= 3:
                break
        else:
            run, first, last = 0, None, bad
            print(f'    {t:4.1f} s: {"; ".join(bad[:4])}' + (f' (+{len(bad) - 4})' if len(bad) > 4 else ''), flush=True)
        time.sleep(1)
    ck(first is not None and run >= 3 and first <= limit,
       f'[{label}] the joining guest\'s game agrees with the host\'s within {limit:.0f} s of the join '
       f'(first of 3 consistent reads at {first if first is None else round(first, 1)} s; last difference: {last[:3]})')


def in_after_reload(host, guest, t_join, timeout=200):
    """the guest is in the host's world: a guest that joins again first plays its old copy ("together" at once),
    then loads the world the host sends: wait for that load too; the time it is in the reloaded world"""
    from coop_rejoin_net import log_times
    if not wait_guest_in(host, guest, timeout=timeout):
        return None
    t0 = time.time()
    while time.time() - t0 < 90:
        p = log_times(lane.HOST, t_join, {'share': r'world: sharing'})
        p.update(log_times(lane.GUEST, t_join, {'loading': r'world: loading'}))
        if 'share' not in p and time.time() - t_join > 25:
            break  # no world sent
        if 'loading' in p and wait_guest_in(host, guest, timeout=2):
            break
        time.sleep(1)
    return time.time()


def case_combat(host, guest, ck, a):
    fam = family(host)
    hp_ = own_pos(host)
    cand = sorted((n[3], i) for i, n in npcs(host).items() if not n[4] and n[2] > 0 and n[1] != fam)
    ck(bool(cand), f'host NPCs to fight ({len(cand)}, host at {hp_})')
    if not cand:
        return
    nid = cand[0][1]
    print(f'  the host fights NPC #{nid} ({npcs(host)[nid][1]}, {cand[0][0]:.0f} m away)', flush=True)
    f = Fight(host, nid, 60)
    f.start()
    time.sleep(a.lead)
    n = npcs(host).get(nid)
    print(f'  {a.lead:.0f} s into the fight: NPC hp {n[2] if n else None}, dead {n[4] if n else None}, '
          f'{f.bites} bites; the guest joins', flush=True)
    t_join = time.time()
    guest.call(lane.JOIN)
    t_in = in_after_reload(host, guest, t_join)
    ck(t_in is not None, f'the guest got into the host\'s world during the fight '
       f'({"-" if t_in is None else "%.0f s" % (t_in - t_join)} after the join, its world loaded)')
    t_in = t_in or time.time()
    n = npcs(host).get(nid)
    print(f'  guest in after the join; NPC hp {n[2] if n else None}, dead {n[4] if n else None}', flush=True)
    settle_check(host, guest, ck, 'combat', a.limit, nid, t_in)
    shot(guest, 'join_during_combat_guest')
    f.join(70)
    n = npcs(host).get(nid)
    if n and n[4]:  # the host's kill: dead in the guest's game too
        t0 = time.time()
        g = None
        while time.time() - t0 < 10:
            g = census.dump(guest).by_id('c', ('proxy',)).get(int(nid))
            if g and not g['alive']:
                break
            time.sleep(1)
        ck(g is not None and not g['alive'], f'[combat] the host\'s kill of #{nid} is dead in the guest\'s game '
           f'within 10 s ({"not there" if g is None else "alive=%d" % g["alive"]})')
    else:
        print(f'  (the NPC was not dead after {f.bites} bites: hp {n[2] if n else None})', flush=True)


def case_editor(host, guest, ck, a):
    m0 = mode(host)
    print('  host: ' + host.call('coop.vote editor')['out'].strip().splitlines()[0], flush=True)
    t0 = time.time()
    while time.time() - t0 < 30 and mode(host) == m0:
        time.sleep(1)
    ck(mode(host) != m0, f'the host is in its editor ({mode(host).strip()})')
    host.call('wait.frames 120 60000')
    guest.call(lane.JOIN)
    t_join = time.time()
    seen = []
    first = None
    moving = None
    while time.time() - t_join < a.edit:
        # BUG-0205: while the host edits, the joined guest's game plays its creatures itself (the host sends no
        # NPC states): they move in the guest's game (not proxies standing frozen). Two census dumps 8 s apart
        if moving is None and time.time() - t_join > 10 and guest.call('coop.avatar')['ok']:
            first = census.dump(guest)
            time.sleep(8)
            ds = [d for _, d in census.moved(first, census.dump(guest), own=('local', 'proxy'))]
            moving = (len([d for d in ds if d > 1.0]), len(ds))
            print(f'    the guest\'s creatures that moved > 1 m in 8 s of the host\'s editor: {moving[0]} of {moving[1]}',
                  flush=True)
        st = guest.call('coop.status')['out']
        line = f'{time.time() - t_join:4.0f} s: guest {mode(guest).strip()}, avatar {guest.call("coop.avatar")["ok"]}, ' \
               f'host_away={re.search(r"host_away=(\d)", st).group(1) if re.search(r"host_away=(\d)", st) else "?"}'
        if not seen or line.split(': ', 1)[1] != seen[-1].split(': ', 1)[1]:
            seen.append(line)
            print('   ', line, flush=True)
        time.sleep(2)
    if moving == (0, 0) and a.allow_empty:  # the known open bug: printed with its ticket, never silent
        print(f'  allowed [{a.allow_empty}]: while the host edits, the joined guest\'s planet has no creatures', flush=True)
    else:
        ck(moving is not None and moving[0] >= 1, f'[editor] while the host edits, the joined guest\'s creatures move '
           f'in its game ({moving})')
    shot(guest, 'join_during_editor_guest')
    host.call(f'move {DONE[0]} {DONE[1]}')
    time.sleep(0.4)
    host.call(f'click {DONE[0]} {DONE[1]}')
    t0 = time.time()
    while time.time() - t0 < 60 and cl.stage_of(mode(host)) is None:
        time.sleep(1)
    ck(cl.stage_of(mode(host)) == 'creature', f'the host left its editor with "done" ({mode(host).strip()})')
    t_back = time.time()
    t_in = in_after_reload(host, guest, t_back)
    ck(t_in is not None, 'the guest is in the host\'s world after the host\'s editor')
    t_in = t_in or time.time()
    settle_check(host, guest, ck, 'editor', a.limit, None, t_in)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--case', choices=('combat', 'editor'), required=True)
    ap.add_argument('--limit', type=float, default=10, help='s after the join to be consistent')
    ap.add_argument('--lead', type=float, default=4, help='combat: s of fight before the guest joins')
    ap.add_argument('--edit', type=float, default=25, help='editor: s the host stays in its editor after the join')
    ap.add_argument('--allow-empty', default='', help='BUG-NNNN: a guest planet without creatures while the host edits '
                    'is that known open bug (printed, not failed)')
    a = ap.parse_args()
    host, guest = start('creature')
    ck = Checks()
    time.sleep(3)
    guest.call('coop.leave')
    t0 = time.time()
    while time.time() - t0 < 20 and together(host):
        time.sleep(0.5)
    ck(not together(host), 'the guest left (the join comes later)')
    time.sleep(3)
    (case_combat if a.case == 'combat' else case_editor)(host, guest, ck, a)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
