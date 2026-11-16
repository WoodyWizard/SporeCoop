#!/usr/bin/env python3
"""Tribe: the host's Tribal Outfitter reaches the guest, a guest's own outfitter is refused (E4, BUG-1156, BUG-1158).

The Tribal Outfitter (the hut's planner, second tab, the edit badge of the tribe's card; editor 0x156276D1) saves the
outfitted creature in the player's My Creations and reloads the tribe's species profile with it in place: every
member wears the outfit (docs/re/creations.md#creations-stage-designs). Since BUG-0508 a guest stays in place after the
host's editor, and nothing sent the new outfit: the guest's tribe kept the old one.

1. Both games on the `tribe` fixture. The host opens the outfitter the players' way (a click on its hut, the second
   tab, the card's edit badge); the vote of BUG-0151 asks the guest (its real "I'm ready" click); in the editor the
   host drags a piece of clothing onto the creature and clicks Done.
2. The host's tribe profile names a new creation (one of the host's My Creations). PASS: within 40 s the guest's
   tribe profile is the host's creation too (the host's key, or a key of ours holding the host's records), the model
   its members are built from has the host's model record (the bem hash of the profile's model key equals the
   host's), and every member of the guest's player tribe was rebuilt from it (its +0xB24 is the profile's generation).
3. The guest opens its own outfitter the same way. PASS: it is refused (the guest stays in Tribe, a line says why)
   and the guest's game goes on (no pause or input lock left behind).

SPORE_FIX_OFF=tribe.outfit_sync: step 2 FAILs (the guest's tribe keeps the old outfit);
SPORE_FIX_OFF=editors.guest_designs: step 3 FAILs (the guest's own editor opens; the test leaves it with the red X).

  SPORE_LANE=4 python3 tools/tests/coop_tribe_outfit.py
"""
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
from harness import mode_word as mode
import lane  # noqa: E402
import part_substitutes  # noqa: E402,F401
import twoinst  # noqa: E402
from coop_nest_species import read_pkg  # noqa: E402
from coop_ui import Ui  # noqa: E402
from twoinst import Checks, start  # noqa: E402

TRIBE, EDITOR = '0x01654C02', '0x00DBDBA1'
TAB2, BADGE, TAB3 = (204, 40), (44, 630), (260, 40)  # the planner's outfit tab, the card's edit badge, a clothes tab
ITEM, ONTO = (84, 216), (975, 440)  # the tab's second piece (a shoulder leaf) and the creature's chest
DONE, CANCEL = (1416, 1060), (1380, 1062)


def profile(con):
    """the player tribe's species profile: (key instance, model (inst, type, group), generation, [member generations])"""
    out = con.call('coop.designs members')['out']
    m = re.search(r'tribe profile \S+ key=(\w+)/\w+/\w+ model=(\w+)/(\w+)/(\w+) gen=(\d+)', out)
    gens = [int(g) for g in re.findall(r'member \S+ profile=\S+ own=1 gen=(\d+)/\d+', out)]
    if not m:
        return None, None, -1, gens
    return int(m.group(1), 16), tuple(int(m.group(i), 16) for i in (2, 3, 4)), int(m.group(5)), gens


def model_hash(con, key):
    out = con.call('coop.creations modelhash %08X %08X %08X' % key)['out']
    m = re.search(r'model \w+ (\w+)', out)
    return m.group(1) if m else '?'


def hut(con):
    for ln in con.call('coop.targets objs')['out'].splitlines():
        f = ln.split()
        if len(f) == 5 and f[0] == 'obj' and f[1] == 'hut0p':
            return int(f[3]), int(f[4])
    return None


def drag(con, a, b):
    con.call('move %d %d' % a)
    time.sleep(0.4)
    con.call('button %d %d down' % a)
    time.sleep(0.3)
    for i in range(1, 21):
        con.call('move %d %d' % (a[0] + (b[0] - a[0]) * i // 20, a[1] + (b[1] - a[1]) * i // 20))
        time.sleep(0.06)
    time.sleep(0.3)
    con.call('button %d %d up' % b)


def open_outfitter(con):
    """the hut's planner (a click on the hut; the first click after a while may only take the focus: up to three, until
    the planner's input lock is up), the outfit tab, the card's edit badge (real clicks); False when no planner opened"""
    h = hut(con)
    if not h:
        return False
    for _ in range(3):
        con.call('move %d %d' % (h[0], h[1] - 30))
        time.sleep(0.8)
        con.call('click %d %d' % (h[0], h[1] - 30))
        time.sleep(2.5)
        if 'input_lock' in busy(con):
            break
    else:
        return False
    con.call('click %d %d' % TAB2)
    time.sleep(2)
    con.call('click %d %d' % BADGE)
    return True


def close_planner(con):
    con.call('click %d %d' % DONE)  # the planner's "Save and Exit" check
    time.sleep(1.5)


def busy(con):
    m = re.search(r'^busy: (\S+)', con.call('coop.status')['out'], re.M)
    return m.group(1) if m else ''


def keys_in_save(d, inst):
    """the records of the profile's newest save that hold the key instance (big or little endian)"""
    import glob
    import part_substitutes as ps
    fs = sorted(glob.glob(os.path.join(d, 'Games', '*', '*.spo')), key=os.path.getmtime)
    if not fs:
        return ['no save']
    out = []
    for e in ps.entries(fs[-1]):
        data = ps.read(fs[-1], e)
        if inst.to_bytes(4, 'big') in data or inst.to_bytes(4, 'little') in data:
            out.append('%08X' % e['type'])
    return out


def my_creations(d):
    p = os.path.join(d, 'EditorSaves.package')
    return {r[2] for r in read_pkg(p)} if os.path.exists(p) else set()


def outfit_closeup(host, guest):
    """BUG-1220: the outfit seen up close: both cameras at the same place 10 m from an adult member of the player tribe
    (the host's chief first), so the new piece of clothing can be judged on both games (a frame 25 m away showed
    members of 20 px). A key frame of kind sameview; the game's own cameras after it."""
    import keyviews
    from coop_tribe_looks import members
    ms = [m for m in members(host).values() if m['player'] and m['adult'] and m['owner'] == 0]
    ms.sort(key=lambda m: not m['leader'])
    if not ms:
        print('  (outfit close-up: no adult member of the host\'s tribe found)', flush=True)
        return
    try:
        ok = keyviews.look_both((host, guest), ms[0]['pos'], dist=10, height=4, fov=40, look_up=4.0)
        print(f'  outfit close-up at {ms[0]["pos"]} (leader {ms[0]["leader"]}): views set {ok}', flush=True)
        twoinst.keyframe('outfit_close', 'same view 10 m from a member of the player tribe (the host\'s chief): the new '
                                         'piece of clothing (a leaf on the chest) on both games', kind='sameview',
                         settle=0)
    finally:
        keyviews.view_off(host, guest)


def main():
    host, guest = start('tribe')
    ck = Checks()
    gu = Ui(guest, 'guest')
    hk0, hm0, _, _ = profile(host)
    gk0, gm0, _, _ = profile(guest)
    print(f'  before: host profile {hk0 and f"{hk0:08X}"} guest {gk0 and f"{gk0:08X}"}', flush=True)
    if not ck(hk0 and gk0 and hk0 == gk0, 'both tribes play the same species before'):
        return ck.result()
    mine0 = my_creations(lane.HOST_DIR)

    # 1. the host's outfitter, the players' way
    ck(open_outfitter(host), 'the host\'s hut planner opened')
    if not gu.wait(lambda s: s['ready']['vis'], 15, 'ready button'):
        host.call('click %d %d' % BADGE)  # (once more: the tab took the click)
        gu.wait(lambda s: s['ready']['vis'], 15, 'ready button')
    gu.click('ready')
    t0 = time.time()
    while time.time() - t0 < 60 and mode(host) != EDITOR:
        time.sleep(1)
    if not ck(mode(host) == EDITOR, f'the host is in the Tribal Outfitter ({mode(host)})'):
        return ck.result()
    host.call('wait.frames 120 30000')
    host.call('click %d %d' % TAB3)
    time.sleep(1.5)
    drag(host, ITEM, ONTO)
    time.sleep(1)
    host.call('move %d %d' % DONE)
    time.sleep(0.5)
    host.call('click %d %d' % DONE)
    t0 = time.time()
    while time.time() - t0 < 60 and mode(host) != TRIBE:
        time.sleep(1)
    if not ck(mode(host) == TRIBE, f'the host is back in Tribe ({mode(host)})'):
        return ck.result()
    time.sleep(3)
    close_planner(host)
    hk, hm, hgen, hgens = profile(host)
    new = my_creations(lane.HOST_DIR) - mine0
    print(f'  host profile {hk:08X} model {"%08X/%08X/%08X" % hm} gen {hgen}, new in its My Creations: '
          f'{sorted("%08X" % n for n in new)}', flush=True)
    if not ck(hk != hk0 and hk in new, f'the host\'s tribe wears a new outfit of its My Creations ({hk:08X}, was {hk0:08X})'):
        return ck.result()
    hh = model_hash(host, (hk, 0x2B978C46, 0x40626200))
    st = host.call('coop.status')['out']
    m = re.search(r'evolve in place: kept (\d+)', st)
    print(f'  {m.group(0) if m else "evolve in place: ?"}', flush=True)

    # 2. the guest's tribe: the host's outfit
    gk, gm, ggen, ggens, gh = None, None, -1, [], '?'
    t0 = time.time()
    while time.time() - t0 < 40:
        gk, gm, ggen, ggens = profile(guest)
        if gm and gm[0] != gk0:
            gh = model_hash(guest, (gm[0], 0x2B978C46, 0x40626200))
            if gh == hh and ggens and all(g == ggen for g in ggens):
                break
        time.sleep(1)
    print(f'  guest profile {gk and f"{gk:08X}"} model {gm and "%08X/%08X/%08X" % gm} -> {gh} (host {hh}), '
          f'gen {ggen}, members {ggens}', flush=True)
    print('  guest:', guest.call('coop.designs')['out'].strip().splitlines()[0], flush=True)
    ck(gk == hk and gm and gm[0] != gk0, f'the guest\'s tribe takes the host\'s new outfit (species {gk and f"{gk:08X}"}, '
       f'model {gm and f"{gm[0]:08X}"}, was {gk0:08X}, host {hk:08X})')
    ck(gh == hh, f'the guest\'s tribe is drawn from the host\'s outfit model ({gh}, host {hh})')
    ck(ggens and all(g == ggen for g in ggens), f'every member of the guest\'s tribe was rebuilt from it ({ggens}, gen {ggen})')
    if gm and gm[0] != gk:  # a key of ours: never in a save (the guest saves its copy of the host's world)
        print('  guest:', guest.call('coop.save')['out'].strip(), flush=True)
        time.sleep(4)
        found = keys_in_save(lane.GUEST_DIR, gm[0])
        ck(not found, f'the guest\'s save names no key of ours ({gm[0]:08X} in {found or "no record"})')
    twoinst.keyframe('outfit', 'after the host\'s Tribal Outfitter: the tribe members on both games wear the new '
                               'piece of clothing (a leaf on the chest), both in Tribe play')
    outfit_closeup(host, guest)

    # 3. the guest's own outfitter
    ck(open_outfitter(guest), 'the guest\'s hut planner opened')
    t0 = time.time()
    while time.time() - t0 < 10 and mode(guest) != EDITOR:
        time.sleep(1)
    gm3 = mode(guest)
    log = guest.call('coop.designs')['out']
    m = re.search(r'refused=(\d+)', log)
    print(f'  guest mode after its outfitter click: {gm3}; {m.group(0) if m else "?"}', flush=True)
    refused = gm3 == TRIBE and m and int(m.group(1)) > 0
    ck(refused, 'the guest\'s own outfitter is refused (its tribe is the shared one)')
    if gm3 == EDITOR:  # the switch off: leave its editor
        guest.call('wait.frames 120 30000')
        guest.call('move %d %d' % CANCEL)
        time.sleep(0.5)
        guest.call('click %d %d' % CANCEL)
        t0 = time.time()
        while time.time() - t0 < 60 and mode(guest) != TRIBE:
            time.sleep(1)
    close_planner(guest)
    time.sleep(3)
    b = busy(guest)
    print(f'  guest busy after: {b or "-"}', flush=True)
    ck(not refused or 'input_lock' not in b, f'the refused outfitter left no input lock on the guest ({b or "-"})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
