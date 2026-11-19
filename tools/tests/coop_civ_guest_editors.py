#!/usr/bin/env python3
"""Civilization: a guest's own building and vehicle editors make its own designs; the shared civilization stays the
host's (WP-C3 of docs/plan-0.7.16.md, BUG-1275, docs/re/civ.md#civ-own-editors).

Before 0.7.16 a guest's design editor in the host's Civ world was refused ("the civilization's designs are shared",
BUG-1158). In own mode (creature.mode own, the default) `civ.guest_editors` lets it open; its result goes to the
guest's species line (species::set_design), never into the game's civilization: the planner's apply (0xD095A0:
Civ_SetDesign, cPlayer +0x1150) is skipped for it. Every game then shows the guest's city and vehicles in the guest's
designs (civ_own_designs.cpp, WP-C2).

Set-up (cold start, civ_npc): the guest buys the NPC city nearest the capital (`coop.civ buycity`).

1. The guest opens its city's planner (real clicks: the city, the planner button), hovers the house tile and clicks
   its "new design" button (the palette item's command 0x4A1CB6B / 0x4A1CB71, `coop.ui cmd`); when no such button is
   shown it posts the same message the button posts (`coop.civedit new 4E3F7777`: the planner's 0x4A314E6) and says
   so (ROUTE). PASS: the building editor opens on the guest (FAIL twin: refused, the guest stays in Civ).
2. In the editor (real clicks, the route of the end of Tribe's City Hall editor, docs/re/tribe.md): three parts
   dragged onto the plot, done, the name panel's dice, done; the planner comes back, closed with its green check.
3. PASS on both games: the guest's line has the house design (`coop.civedit` took ok, `coop.species` design
   4E3F7777 = a shared key 0x7B......), every house of the guest's city is drawn from the new creation (the bem hash
   of its model key is the creation's, `coop.creations modelhash`), the host's houses not; the host's culture set
   (`coop.designs culture`), the civilization's species, the world's player species and the City Hall key in cPlayer
   are what they were before (on the guest: every entry that had a design before).
4. The guest opens its planner again, the vehicle editor for its city's land purchase (the planner's message, as 1's
   fallback: the vehicle tab's buttons are not known yet) and leaves it the same way; then it buys a land vehicle
   in its city (its city menu's land button, real click; `coop.civ buy` when not shown): on both games the vehicle
   shows the guest's new vehicle design (bem hash), the host's own vehicles not; the culture set as in 3.
5. Key frames: the guest's city with its houses and new vehicle on both screens.
6. The host goes to the galaxy menu (the game saves): no file of the save holds a key of the guest's new designs
   (its own creation instances or the line's shared keys), little or big endian.

SPORE_FIX_OFF=civ.guest_editors: step 1 FAILs (the BUG-1158 refusal of 0.7.15: the editor does not open).
Needs the species line's designs in civ_own_designs.cpp (feat/w54-shims: species::design).

  SPORE_LANE=6 python3 tools/tests/coop_civ_guest_editors.py
"""
import os
import random
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
from harness import waiter  # noqa: E402

import fixtures  # noqa: E402
import lane  # noqa: E402
from coop_campaign import EDITOR_DICE, EDITOR_DONE, TRIBE_EDITOR_PARTS, drag_slow, editor_click  # noqa: E402
from coop_civ import civ, dist  # noqa: E402
from coop_civ_own_designs import keys_in, mhash, own, save_files  # noqa: E402
from play_civ import BTN_LAND, BTN_PLANNER, PLANNER_DONE, PLANNER_NAME, Player, frame_city  # noqa: E402
from twoinst import Checks, keyframe, shot, start  # noqa: E402

BGROUP, BTYPE = 0x40636200, 0x2399BE55
VGROUP, VTYPE = 0x40646200, 0x24682294
HOUSE = 0x4E3F7777
HOUSE_TILE = (123, 385)                 # the planner's house tile (1440x1080, docs/re/civ.md#civ-planner-palette-runtime)
NEW_CMDS = ('04A1CB6B', '04A1CB71')     # a palette item's "new design" buttons (0x5F1750 -> message 0x4A314E6)
CIV, EDITOR = '0x01654C04', '0x00DBDBA1'
wait = waiter(step=1.0, final=True)


def mode(con, whole=False):
    """the mode id of `game.mode` (its first word; whole: the answer with the mode's name). BUG-1307: the answer is
    "0x00DBDBA1 Editor", compared whole with the bare id no editor (nor Civ) was ever seen"""
    try:
        out = con.call('game.mode')['out'].strip()
    except OSError:
        return ''
    return out if whole else (out.split() or [''])[0]


def civedit(con):
    out = con.call('coop.civedit')['out']
    took = {}
    for ln in out.splitlines():
        m = re.match(r'\s+took design design=(\w+) key=(\w+)/(\w+)/(\w+) ok=(\d) line=(\w+)', ln)
        if m:
            took[int(m.group(1), 16)] = dict(key=int(m.group(2), 16), type=int(m.group(3), 16), group=int(m.group(4), 16),
                                             ok=m.group(5) == '1', line=int(m.group(6), 16))
    g = re.search(r'civ_species=(\w+)/\w+ world_species=(\w+)/\w+ city_hall=(\w+)/', out)
    game = tuple(int(x, 16) for x in g.groups()) if g else None
    return took, game, out


def culture(con):
    out = {}
    for ln in con.call('coop.designs culture')['out'].splitlines():
        m = re.match(r'id=(\w+) key=(\w+)/', ln)
        if m:
            out[int(m.group(1), 16)] = int(m.group(2), 16)
    return out


def line_design(con, pid, did):
    """the key of player pid's own design `did` this game answers (coop.species), 0 = none"""
    for ln in con.call('coop.species')['out'].splitlines():
        if ln.startswith(f'player {pid} '):
            m = re.search(rf'design {did:08X}=(\w+)', ln)
            return int(m.group(1), 16) if m else 0
    return 0


def new_button(con, near):
    """the centre of a shown "new design" button of the palette item at `near` (None: none shown there)"""
    best = None
    out = con.call('coop.ui cmd ' + ' '.join(NEW_CMDS))['out']
    for ln in out.splitlines():
        f = ln.split()
        if len(f) >= 6 and f[5] == 'enabled=1':
            x0, y0, x1, y1 = (float(v) for v in f[1:5])
            c = ((x0 + x1) / 2, (y0 + y1) / 2)
            d = dist((c[0], c[1], 0), (near[0], near[1], 0))
            if d < 110 and (best is None or d < best[0]):
                best = (d, c)
    print(f'  "new design" buttons shown: {out.strip()[:300] or "none"}', flush=True)
    return best[1] if best else None


def open_planner(p, pos):
    for _ in range(3):
        if p.open_city(pos) and p.click_button(BTN_PLANNER) and p.wait_ui(PLANNER_NAME, True, 8):
            time.sleep(2)
            return True
        time.sleep(1)
    return False


def close_planner(p):
    for _ in range(4):
        if not p.wait_ui(PLANNER_NAME, True, 1):
            return True
        p.do(f'move {PLANNER_DONE[0]} {PLANNER_DONE[1]}')
        time.sleep(0.4)
        p.do(f'click {PLANNER_DONE[0]} {PLANNER_DONE[1]}')
        if p.wait_ui(PLANNER_NAME, False, 5):
            return True
    return False


def drive_editor(con, who, tries=12):
    """three parts, done, the dice's name, done (docs/re/tribe.md: the City Hall editor; coop_campaign's driver)"""
    for n in range(tries):
        if mode(con) != EDITOR:
            return True
        pace = 1.0 + n // 3
        if n % 3 == 0:
            for (sx, sy), (tx, ty) in TRIBE_EDITOR_PARTS:
                drag_slow(con, sx, sy, tx, ty, pace)
        editor_click(con, *EDITOR_DONE)
        time.sleep(1.5)
        if mode(con) != EDITOR:
            break
        editor_click(con, *EDITOR_DICE)
        editor_click(con, *EDITOR_DONE, settle=0)
        t0 = time.time()
        while time.time() - t0 < 8 and mode(con) == EDITOR:
            time.sleep(0.5)
        if n == 1:
            shot(con, f'civedit_{who}_editor.png')
    return wait(lambda: mode(con) == CIV, 60, 1)


def in_editor(con, secs):
    return wait(lambda: mode(con) == EDITOR, secs, 0.5)


def game_fields(host, guest):
    return {who: (culture(c), civedit(c)[1]) for who, c in (('host', host), ('guest', guest))}


def guest_keys(guest):
    """the guest's own design keys this test made (its creations and their 0x7B line keys)"""
    keys = set()
    for t in civedit(guest)[0].values():
        keys |= {t['key'], t['line']}
    return {k for k in keys if k}


def host_culture_kept(c0, c1, gkeys, what=''):
    """(ok, text): every culture entry the host had is unchanged, and an entry the game made meanwhile is the
    culture's own design. BUG-1307: the house build (the game's own, on the host for the guest's city) makes the
    missing house entry 4E3F7777 with the culture's design (lab 20261119-122505: 00000000 -> 0675F7B5); that is not a
    guest design in the host's culture, which this check is about: a new entry fails only when it is one of the guest's
    keys or a line key (0x7B......)"""
    changed = {f'{k:08X}': (f'{v:08X}', f'{c1.get(k, 0):08X}') for k, v in c0.items() if v and c1.get(k) != v}
    added = {k: v for k, v in c1.items() if not c0.get(k) and v}
    bad = {f'{k:08X}': f'{v:08X}' for k, v in added.items() if v in gkeys or v >> 24 == 0x7B}
    ok = not changed and not bad and len([k for k, v in c0.items() if v]) <= len(c1)
    new = {f'{k:08X}': f'{v:08X}' for k, v in added.items()}
    return ok, (f'{what}the host\'s culture set is unchanged on the host ({len(c0)} entries; changed {changed or "none"}, '
                f'made by the game {new or "none"}, of the guest {bad or "none"})')


def same_civilization(ck, before, host, guest, what):
    for who, con in (('host', host), ('guest', guest)):
        c0, g0 = before[who]
        c1, g1 = culture(con), civedit(con)[1]
        if who == 'host':
            ck(*host_culture_kept(c0, c1, guest_keys(guest), f'{what}: '))
        else:
            changed = {f'{k:08X}': (f'{v:08X}', f'{c1.get(k, 0):08X}') for k, v in c0.items() if v and c1.get(k) != v}
            ck(not changed, f'{what}: the guest\'s copy of the culture set keeps every design it had {changed or ""}')
        ck(g0 and g1 == g0, f'{what}: {who}: the civilization\'s species, the world\'s player species and the City Hall '
                            f'key are unchanged ({g0} -> {g1})')


def make_design(ck, p, guest, gpos, did, what, real_click):
    """the guest's editor for design `did`; True when it opened and was left with a creation"""
    if not ck(open_planner(p, gpos), f'{what}: the guest\'s planner of its city is open (real clicks)'):
        return False
    route = None
    if real_click:
        guest.call(f'move {HOUSE_TILE[0] - 30} {HOUSE_TILE[1]}')
        time.sleep(0.4)
        guest.call(f'move {HOUSE_TILE[0]} {HOUSE_TILE[1]}')
        time.sleep(1.0)
        b = new_button(guest, HOUSE_TILE)
        if b:
            guest.call(f'move {int(b[0])} {int(b[1])}')
            time.sleep(0.4)
            guest.call(f'click {int(b[0])} {int(b[1])}')
            route = f'the tile\'s "new design" button at ({int(b[0])}, {int(b[1])})'
            if not in_editor(guest, 15):
                route = None
    if not route:
        print('  guest:', guest.call(f'coop.civedit new {did:08X}')['out'].strip(), flush=True)
        route = 'the planner\'s own message of the tile\'s button (coop.civedit new)'
    print(f'  ROUTE {what}: {route}{"" if real_click else " (the vehicle tab is not known yet)"}', flush=True)
    opened = in_editor(guest, 30)
    print('  guest:', civedit(guest)[2].strip().splitlines()[2], flush=True)
    if not ck(opened, f'{what}: the guest\'s own editor opened (mode {mode(guest)}; BUG-1158 refused it in 0.7.15)'):
        shot(guest, 'civedit_no_editor.png')
        close_planner(p)
        return False
    shot(guest, f'civedit_{what.split()[0]}_open.png')
    left = drive_editor(guest, what.split()[0])
    ck(left, f'{what}: the guest left its editor with a creation, back in Civ ({mode(guest)})')
    time.sleep(3)
    close_planner(p)
    return left


def main():
    host, guest = start('civ_npc')
    ck = Checks()
    time.sleep(4)
    print('  host:', host.call('coop.res add money 30000')['out'].strip(), flush=True)

    # the guest's city
    hc, _ = civ(host)
    cap = [k for k, v in hc.items() if v['player']]
    npc = [k for k, v in hc.items() if not v['player']]
    if not ck(cap and npc, f'a player city {cap} and NPC cities {npc}'):
        return ck.result()
    npc.sort(key=lambda i: dist(hc[i]['pos'], hc[cap[0]]['pos']))
    gci, gpos = npc[0], hc[npc[0]]['pos']
    print(f'  guest buys city {gci}:', guest.call(f'coop.civ buycity {gci} 500')['out'].strip(), flush=True)
    got = wait(lambda: all((c := civ(x)[0].get(gci)) and c['player'] and c['owner'] == 1 for x in (host, guest)), 30)
    if not ck(got, f'city {gci} is the player civilization\'s and the guest\'s on both games'):
        return ck.result()
    frame_city({'host': host, 'guest': guest}, gpos)
    time.sleep(2)
    before = game_fields(host, guest)
    print(f'  before: host culture {len(before["host"][0])} entries, game fields host {before["host"][1]} '
          f'guest {before["guest"][1]}', flush=True)
    log = []
    p = Player('guest', guest, 1, random.Random(5), log)

    # 1.-3. the house
    if not make_design(ck, p, guest, gpos, HOUSE, 'house design', True):
        ck.faults(host=host, guest=guest)
        return ck.result()
    ok = wait(lambda: (t := civedit(guest)[0].get(HOUSE)) and t['ok'], 30)
    took, _, st = civedit(guest)
    t = took.get(HOUSE)
    print('  guest:', st.strip().splitlines()[0], flush=True)
    if not ck(ok and t, f'the guest\'s new house design is in its species line ({t})'):
        ck.faults(host=host, guest=guest)
        return ck.result()
    hb = mhash(guest, t['key'], t['type'], t['group'])
    print(f'  the guest\'s new house {t["key"]:08X}: bem {hb}', flush=True)
    shared = {}
    for con, who in ((host, 'host'), (guest, 'guest')):
        ok = wait(lambda: line_design(con, 1, HOUSE) >> 24 == 0x7B, 30)
        shared[who] = line_design(con, 1, HOUSE)
        ck(ok, f'{who}: the guest\'s line names its house design ({shared[who]:08X})')
    ck(shared['host'] == shared['guest'], 'the same key of the guest\'s house design on both games')
    # BUG-1307: the bought NPC city has no building of the house design id (its houses are the NPC culture's: lab
    # 20261119-111652 found none, on both games): the guest builds one, as civ_own_designs does (the game's own build)
    print('  guest:', guest.call(f'coop.civ build {gci} house')['out'].strip(), flush=True)
    time.sleep(3)
    for con, who in ((host, 'host'), (guest, 'guest')):
        def houses(owner):
            return sorted({mhash(con, b['model'], BTYPE, BGROUP) for b in own(con)[1] if b['owner'] == owner and b['did'] == HOUSE})
        got = wait(lambda: houses(1) == [hb], 30)
        print(f'  {who}: the guest\'s houses -> {houses(1)}, the host\'s -> {houses(0)}', flush=True)
        ck(got, f'{who}: every house of the guest\'s city shows the guest\'s new design ({houses(1)}, want {hb})')
        ck(hb not in houses(0), f'{who}: the host\'s houses keep the culture\'s design ({houses(0)})')
    same_civilization(ck, before, host, guest, 'after the guest\'s house editor')
    print('  guest:', guest.call('coop.designs')['out'].strip().splitlines()[0], flush=True)

    # 4. the vehicle
    m = re.search(r'did (\w+) purpose=(\d+)', host.call(f'coop.civ owndesign did {gci} 0')['out'])
    vdid = int(m.group(1), 16) if m else None
    if not ck(vdid, f'the land vehicle design id of a purchase in city {gci} ({vdid and f"{vdid:08X}"})'):
        ck.faults(host=host, guest=guest)
        return ck.result()
    vt = None
    if make_design(ck, p, guest, gpos, vdid, f'vehicle design {vdid:08X}', False):
        ok = wait(lambda: (x := civedit(guest)[0].get(vdid)) and x['ok'], 30)
        vt = civedit(guest)[0].get(vdid)
        ck(ok and vt, f'the guest\'s new vehicle design is in its species line ({vt})')
    if vt:
        vb = mhash(guest, vt['key'], vt['type'], vt['group'])
        ck(wait(lambda: line_design(host, 1, vdid) >> 24 == 0x7B, 30), 'host: the guest\'s line names its vehicle design')
        before_v = set(own(host)[0])
        bought = False
        if p.open_city(gpos) and p.click_button(BTN_LAND):
            time.sleep(3)
            bought = any(i not in before_v and v['owner'] == 1 for i, v in own(host)[0].items())
            print(f'  ROUTE purchase: the city menu\'s land button -> {"a vehicle" if bought else "nothing yet"}', flush=True)
        if not bought:
            print('  guest:', guest.call(f'coop.civ buy {gci} 0')['out'].strip(), flush=True)
        vid = None
        t0 = time.time()
        while time.time() - t0 < 20 and vid is None:
            new = [i for i, v in own(host)[0].items() if i not in before_v and v['owner'] == 1]
            vid = new[0] if new else None
            time.sleep(0.5)
        if ck(vid is not None, f'the guest\'s purchase made a vehicle on the host ({vid})'):
            for con, who in ((host, 'host'), (guest, 'guest')):
                def vh():
                    v = own(con)[0].get(vid)
                    return mhash(con, v['model'], VTYPE, VGROUP) if v else None
                got = wait(lambda: vh() == vb, 20)
                print(f'  {who}: the guest\'s vehicle #{vid} -> {vh()} (its design {vb})', flush=True)
                ck(got, f'{who}: the guest\'s new vehicle shows the guest\'s own vehicle design ({vh()}, want {vb})')
                others = {mhash(con, x['model'], VTYPE, VGROUP) for x in own(con)[0].values() if x['owner'] == 0}
                ck(vb not in others, f'{who}: the host\'s vehicles keep the culture\'s designs ({sorted(others)})')
        same_civilization(ck, before, host, guest, 'after the guest\'s vehicle editor and purchase')

    # 5. key frames
    frame_city({'host': host, 'guest': guest}, gpos)
    keyframe('guest_designs', 'the guest\'s city: its houses (and its new land vehicle) are the guest\'s own new designs '
                              'made in its editors, the same models on both screens; the capital\'s houses are other '
                              'models (the culture\'s)')
    frame_city({'host': host, 'guest': guest}, hc[cap[0]]['pos'])
    keyframe('host_city', 'the host\'s capital: its houses and vehicles are the culture\'s designs on both screens, not '
                          'the guest\'s new house or vehicle')

    # 6. the host's save
    keys = [t['key'], shared['host']] + ([vt['key'], line_design(host, 1, vdid)] if vt else [])
    keys = [k for k in keys if k]
    tables = host.call('coop.civ tables')['out']
    m = re.search(r'key=(\S+)', tables)
    slot = re.search(r'\\(game\d+)\\[^\\]+$', m.group(1), re.I) if m else None
    if ck(slot, f'the host\'s save slot is known ({m.group(1) if m else None})'):
        slot = slot.group(1).capitalize()
        t_save = time.time() - 1
        print('  host menu:', host.call('coop.menu')['out'].strip(), flush=True)
        ck(wait(lambda: 'GalaxyGameEntry' in mode(host, True), 90), 'the host is in the galaxy menu (its game saved)')
        host.call('wait.frames 240 90000')
        files = save_files(os.path.join(lane.HOST_DIR, 'Games', slot), t_save)
        found = keys_in(files, keys)
        print(f'  save: {len(files)} files; ' + ', '.join(f'{k:08X}: {found[k][:4]}' for k in keys), flush=True)
        ck(files and not any(found[k] for k in keys), 'the host\'s save holds no key of the guest\'s new designs')
    print(f'  planet {fixtures.planet_of("civ_npc")}', flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
