#!/usr/bin/env python3
"""Tribe: what the members hold and where the players' names stand, as each game draws them (BUG-0906, BUG-0907).

Both bugs were found by looking at the key frames of same_view_tribe and play_tribe (lab 20261110-201619); the games'
state the older tests read (the tool type, the leader) was the same on both games.

--part item (BUG-0906): the guest's copy of the host's chief had no staff, its copies of members with a weapon or an
  instrument were empty-handed. What a member holds is the game's mCurrentHandheldItem (+0xFFC, `item=` of
  `coop.tribe`) and the model the game attached to its hand (`model=`: the hand's attachment record has a model);
  `hold=` is 1 while the game still holds the item as in the middle of taking it (a timed modifier of the creature:
  on a mirror it never ended, the staff stuck out forward with the arm down), 0 in the resting grip.
  1. Tribe fixture. Every member of every tribe that both games have: the guest's item and model are the host's
     (the host read before and after the guest: a member that changes its item in between is read again).
  2. The host's player tribe has a leader that holds the staff (item 0x11) with a model in the host's game: the same
     on the guest, in the resting grip, and the staff's model has the host's size (`iscale=`, BUG-0912:
     `SPORE_FIX_OFF=tribe.item_scale` fails that). A key frame of kind `sameview` 14 m from that chief.
  3. The guest leaves and joins again: 1 and 2 again (the mirrors are made anew), another key frame. (There the
     host's chief may be away gathering with the staff put away in the host's game too: then its hand is compared
     as every member's.)
  SPORE_FIX_OFF=tribe.held_item: the mirrors' hands are empty: FAIL.

--part tags (BUG-0907): a partner's name hung over empty ground (the point under the partner's mouse).
  1. Tribe fixture. Each game's pointer is pinned on the ground 9 m from the chiefs (`coop.npc marker`: where a
     player's mouse rests), both cameras at the same place (`coop.view set`) with the chiefs and that point in view.
  2. Each game's name labels (`coop.find labels`: what the overlay drew in its last pass): every label that names
     the partner must stand over a member of that partner: within 60 px of the member's real position (the host's
     game's, the authority's) + the tag's height, projected by this game's camera (`coop.find project`). The
     partner's chief, on the screen, has a label. A key frame of kind `sameview`.
  SPORE_FIX_OFF=tribe.pointer_tag: the name at the pinned pointer is ~9 m from every member: FAIL.

  SPORE_LANE=6 python3 tools/tests/coop_tribe_looks.py --part item|tags
"""
import argparse
import math
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lane  # noqa: E402
import sameview as sv  # noqa: E402
from twoinst import Checks, keyframe, start  # noqa: E402

STAFF = 0x11
TAG_UP = 2.6  # m: coop.cpp draw_owner_tags
ROW = re.compile(r'\s+#(\d+) \S+ owner=(-?\d+) age=(\d+) sel=\d+ hp=([\d.]+) pos=\((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\).*'
                 r' tool=(-?\d+) leader=(\d) item=(-?\d+) model=(-?\d+) hold=(\d) iscale=([\d.]+) scale=([\d.]+)')


def members(con):
    """{member id: dict(tribe, player, owner, adult, hp, pos, tool, leader, item, model)} of every tribe"""
    out, ti, player = {}, None, False
    for line in (con.call('coop.tribe').get('out') or '').splitlines():
        m = re.match(r'tribe (\d+)', line)
        if m:
            ti, player = int(m.group(1)), '(player)' in line
            continue
        m = ROW.match(line)
        if m and m.group(1) != '0':
            out[int(m.group(1))] = dict(tribe=ti, player=player, owner=int(m.group(2)), adult=m.group(3) == '1',
                                        hp=float(m.group(4)), pos=tuple(float(m.group(i)) for i in (5, 6, 7)),
                                        tool=int(m.group(8)), leader=m.group(9) == '1', item=int(m.group(10)),
                                        model=int(m.group(11)), hold=int(m.group(12)), iscale=float(m.group(13)),
                                        scale=float(m.group(14)))
    return out


def hands(host, guest):
    """one sample: {id: (host item, host model, guest item, guest model)} of the members both games have and whose
    item the host did not change while the guest was read"""
    h1 = members(host)
    g = members(guest)
    h2 = members(host)
    out = {}
    for i, a in h1.items():
        b = h2.get(i)
        if i in g and b and (a['item'], a['model']) == (b['item'], b['model']):
            out[i] = (a['item'], a['model'], g[i]['item'], g[i]['model'])
    return out, h2, g


def same_hands(host, guest, secs=25):
    """poll until every member's hand is the same on both games (or secs passed): the last sample and the members
    whose hands differed in it"""
    t0 = time.time()
    while True:
        hs, hm, gm = hands(host, guest)
        bad = sorted(i for i, (hi, hmod, gi, gmod) in hs.items() if (hi, hmod) != (gi, gmod))
        if (hs and not bad and len(hs) >= len(hm) - 1) or time.time() - t0 > secs:
            return hs, hm, gm, bad, time.time() - t0
        time.sleep(1.5)


def view_both(host, guest, place, dist, height, k=0):
    eye, target = sv.camera_at(place, k, 4, dist, height)
    ah = host.call(sv.view_cmd(eye, target, 50)).get('out') or ''
    ag = guest.call(sv.view_cmd(eye, target, 50)).get('out') or ''
    time.sleep(1.5)
    return 'now=1' in ah and 'now=1' in ag


def check_item(ck, host, guest, tag, strict=True):
    hs, hm, gm, bad, took = same_hands(host, guest)
    print(f'  {tag}: {len(hs)} members compared after {took:.0f} s (host {len(hm)}, guest {len(gm)})', flush=True)
    for i in sorted(hs):
        hi, hmod, gi, gmod = hs[i]
        mark = '' if (hi, hmod) == (gi, gmod) else '   <-- differs'
        print(f'    #{i} tribe {hm[i]["tribe"]} tool {hm[i]["tool"]}: host item {hi} model {hmod}, guest item {gi} '
              f'model {gmod}{mark}', flush=True)
    ck(len(hs) >= 3, f'{tag}: both games have the members ({len(hs)} compared)')
    ck(not bad, f'{tag}: every member\'s hand on the guest\'s screen holds what it holds in the host\'s game '
                f'(differ: {["#%d" % i for i in bad]})')
    sizes = {i: (hm[i]['iscale'], gm[i]['iscale']) for i, (hi, hmod, gi, gmod) in hs.items()
             if hmod == 1 and gmod == 1 and hi == gi and i in gm}
    off = sorted(i for i, (a, b) in sizes.items() if abs(a - b) > 0.05 * a)
    print(f'  {tag}: item sizes (host, guest): ' + ', '.join(f'#{i} {a:.2f} {b:.2f}' for i, (a, b) in sorted(sizes.items())),
          flush=True)
    ck(not off, f'{tag}: what the members hold has the host\'s size on the guest\'s screen (differ: {["#%d" % i for i in off]})')
    armed = [i for i, (hi, hmod, _, _) in hs.items() if hi and hmod == 1]
    ck(len(armed) >= 1, f'{tag}: the host\'s game has members with something in the hand ({len(armed)})')
    lead = next((i for i, m in sorted(hm.items()) if m['player'] and m['leader']), None)
    ck(lead is not None, f'{tag}: the host\'s player tribe has a leader (#{lead})')
    if lead is None:
        return
    ok = held = False
    t0 = time.time()
    while time.time() - t0 < 30 and not ok:  # (the chief puts the staff away while it gathers or eats)
        h, g = members(host).get(lead), members(guest).get(lead)
        held = bool(h and h['item'] == STAFF and h['model'] == 1)
        ok = bool(held and g and g['item'] == STAFF and g['model'] == 1 and g['hold'] == 0)
        if not ok:
            time.sleep(1.5)
    if held or strict:
        ck(held, f'{tag}: the host\'s game draws its chief #{lead} with the staff (item {h and h["item"]}, model '
                 f'{h and h["model"]})')
        ck(ok, f'{tag}: the guest\'s game draws the copy of the host\'s chief #{lead} with the staff in its resting grip '
               f'(item {g and g["item"]}, model {g and g["model"]}, still being taken {g and g["hold"]}; the game\'s tool '
               f'{g and g["tool"]}, leader {g and g["leader"]})')
        # BUG-0912: the staff has the size it has in the host's game (the game sizes an item's model at the attach by
        # the member's size then; the host's chief was made 1.3x after it, its copy before)
        hs = gs = 0.0
        t1 = time.time()
        while time.time() - t1 < 10:
            h2, g2 = members(host).get(lead), members(guest).get(lead)
            if h2 and g2 and h2['model'] == 1 and g2['model'] == 1:
                hs, gs = h2['iscale'], g2['iscale']
                if abs(gs - hs) <= 0.03 * hs:
                    break
            time.sleep(1.0)
        ck(hs > 0 and abs(gs - hs) <= 0.03 * hs,
           f'{tag}: the staff of the chief\'s copy has the size of the host\'s (the model\'s scale: guest {gs:.3f}, host '
           f'{hs:.3f}; the chief itself guest {g2 and g2["scale"]}, host {h2 and h2["scale"]})')
        # the grip must last: a mirror's "taking it" state never ended by itself (the staff stuck out forward)
        stuck, t1 = 0, time.time()
        while time.time() - t1 < 8:
            g2 = members(guest).get(lead)
            stuck = stuck + 1 if g2 and g2['item'] and g2['hold'] else 0
            if stuck >= 4:
                break
            time.sleep(1.0)
        ck(stuck < 4, f'{tag}: the copy keeps the staff in the grip (not held as in the middle of taking it for 4 s)')
    else:  # after the rejoin the chief may be away gathering, the staff put away in the host's game too
        print(f'  ({tag}: the host\'s chief #{lead} held no staff in the host\'s game for 30 s (item {h and h["item"]}): '
              f'its hand is compared as every member\'s)', flush=True)
        ck(bool(h and g and (h['item'], h['model']) == (g['item'], g['model'])),
           f'{tag}: the copy of the host\'s chief #{lead} holds what the host\'s chief holds (host item '
           f'{h and h["item"]} model {h and h["model"]}, guest item {g and g["item"]} model {g and g["model"]})')
    if h and ck(view_both(host, guest, h['pos'], 14.0, 5.0), f'{tag}: both cameras 14 m from the chief'):
        keyframe(f'chief_{tag}', f'the same camera on both games 14 m from the host\'s chief (member #{lead}): it holds '
                 f'the chief\'s staff (a stick with a skull) on both screens; every member holds the same thing on both',
                 kind='sameview', extra=[f'check: host item {h["item"]} model {h["model"]}, guest item '
                                         f'{g and g["item"]} model {g and g["model"]}'])


def rejoin(ck, host, guest):
    guest.call('coop.leave')
    time.sleep(4)
    print('  rejoin:', (guest.call(lane.JOIN).get('out') or '').strip()[:60], flush=True)
    t0, left = time.time(), False
    while time.time() - t0 < 60 and not left:  # the host's world comes: the old copy goes
        m = guest.call('game.mode').get('out') or ''
        left = 'Load' in m or 'GalaxyGameEntry' in m or 'receiving world' in (guest.call('coop.status').get('out') or '')
        time.sleep(0.3)
    t1, back = time.time(), False
    while time.time() - t1 < 120 and not back:
        try:
            back = (guest.call('game.mode').get('out') or '').split()[-1:] == ['Tribe']
        except OSError:
            back = False
        time.sleep(0.5)
    ck(left and back, f'the guest left ({left}) and plays the host\'s Tribe again ({back})')
    return back


def part_item(ck, host, guest):
    time.sleep(8)
    try:
        check_item(ck, host, guest, 'start')
        for c in (host, guest):
            c.call('coop.view off')
        if rejoin(ck, host, guest):
            time.sleep(20)
            check_item(ck, host, guest, 'rejoin', strict=False)
    finally:
        for c in (host, guest):
            c.call('coop.view off')


def my_id(con):
    m = re.search(r'color id=(\d+) .*\(me\)', con.call('coop.find colors').get('out') or '')
    return int(m.group(1)) if m else None


def labels(con):
    out = []
    for line in (con.call('coop.find labels').get('out') or '').splitlines():
        m = re.match(r'label kind=(\S+) id=(\d+) sx=(-?[\d.]+) sy=(-?[\d.]+) pos=(-?[\d.]+),(-?[\d.]+),(-?[\d.]+) text=(.*)', line)
        if m:
            out.append(dict(kind=m.group(1), id=int(m.group(2)), sx=float(m.group(3)), sy=float(m.group(4)),
                            pos=tuple(float(m.group(i)) for i in (5, 6, 7)), text=m.group(8)))
    return out


def project(con, p):
    """(on screen, x, y) of a world point on this game's screen"""
    out = con.call('coop.find project %.2f %.2f %.2f' % tuple(p)).get('out') or ''
    m = re.search(r'on=(\d) filled=(\d) sx=(-?[\d.]+) sy=(-?[\d.]+)', out)
    return (m.group(1) == '1', float(m.group(3)), float(m.group(4))) if m else (False, 0.0, 0.0)


def raised(p, h):
    r = math.sqrt(sum(x * x for x in p)) or 1.0
    return tuple(x + x / r * h for x in p)


def part_tags(ck, host, guest):
    time.sleep(8)
    hm = members(host)
    ids = {'host': my_id(host), 'guest': my_id(guest)}
    ck(ids['host'] is not None and ids['guest'] is not None and ids['host'] != ids['guest'], f'the players\' ids ({ids})')
    own = {o: [m for m in hm.values() if m['player'] and m['owner'] == o and m['hp'] > 0] for o in ids.values()}
    ck(all(own.get(o) for o in ids.values()), 'each player has members of the player tribe in the host\'s game ('
       + ', '.join(f'{o}: {len(own.get(o) or [])}' for o in ids.values()) + ')')
    allm = [m for v in own.values() for m in v]
    if not allm:
        return
    mid = [sum(m['pos'][i] for m in allm) / len(allm) for i in range(3)]
    e1, e2, _ = sv.tangent_basis(mid)
    r_mid = math.sqrt(sum(x * x for x in mid))
    spot = None
    for k in range(8):  # a point on the ground 9 m from the middle with no member within 6 m: a resting mouse
        a = 2 * math.pi * k / 8
        q = [mid[i] + 9.0 * (math.cos(a) * e1[i] + math.sin(a) * e2[i]) for i in range(3)]
        rq = math.sqrt(sum(x * x for x in q))
        q = [x * r_mid / rq for x in q]
        if min(math.dist(q, m['pos']) for m in allm) > 6.0:
            spot = q
            break
    ck(spot is not None, 'a point on the ground 9 m from the members, none within 6 m of it')
    if spot is None:
        return
    try:
        for c in (host, guest):
            print('  pointer:', (c.call('coop.npc marker %.2f %.2f %.2f' % tuple(spot)).get('out') or '').strip()[:80],
                  flush=True)
        ck(view_both(host, guest, mid, 24.0, 9.0), 'both cameras 24 m from the members')
        time.sleep(3)
        seen = {}
        for name, con, partner in (('host', host, ids['guest']), ('guest', guest, ids['host'])):
            hm = members(host)  # the authority's positions now
            theirs = [m for m in hm.values() if m['player'] and m['owner'] == partner and m['hp'] > 0]
            ls = labels(con)
            mine = [l for l in ls if l['id'] == partner]
            print(f'  {name}: {len(ls)} labels, {len(mine)} name player {partner}', flush=True)
            spots = [(m, project(con, raised(m['pos'], TAG_UP))) for m in theirs]
            worst, bad = 0.0, []
            for l in mine:
                d = min((math.hypot(l['sx'] - x, l['sy'] - y) for _, (on, x, y) in spots), default=9e9)
                worst = max(worst, d)
                print(f'    label {l["kind"]} "{l["text"]}" at ({l["sx"]:.0f} {l["sy"]:.0f}): {d:.0f} px from the '
                      f'partner\'s nearest member', flush=True)
                if d > 60:
                    bad.append(f'{l["kind"]} "{l["text"]}" {d:.0f} px')
            ck(not bad, f'{name}: every label naming the partner stands over one of the partner\'s members (worst '
                        f'{worst:.0f} px; over nothing: {bad})')
            chief = min(theirs, key=lambda m: 0 if m['adult'] else 1, default=None)
            on_screen = [m for m, (on, x, y) in spots if on]
            ck(not on_screen or any(l['kind'] == 'chief' for l in mine),
               f'{name}: the partner\'s chief has its label ({len(on_screen)} of the partner\'s members on the screen, '
               f'labels {[l["kind"] for l in mine]})')
            on, px, py = project(con, raised(spot, 0.5))
            seen[name] = f'{name}: {len(mine)} labels for the partner, the pinned pointer at ({px:.0f} {py:.0f}) on={int(on)}'
            ck(chief is not None, f'{name}: the partner has members')
        keyframe('tags', 'the same camera on both games 24 m from the players\' members, each player\'s pointer pinned on '
                 'the empty ground 9 m from them: every name stands over a member (the chiefs with a star), no name '
                 'over empty ground', kind='sameview', extra=['check: ' + '; '.join(seen.values())])
    finally:
        for c in (host, guest):
            c.call('coop.npc marker off')
            c.call('coop.view off')


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--part', default='item', choices=('item', 'tags'))
    a = ap.parse_args()
    host, guest = start('tribe')
    ck = Checks()
    (part_item if a.part == 'item' else part_tags)(ck, host, guest)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
