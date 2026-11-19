#!/usr/bin/env python3
"""WP-S2 of 0.7.16: the guest's own editors are robust (BUG-1297, BUG-1298, BUG-1299, BUG-1301;
docs/re/creations.md#creations-own-editors-robust).

Modes (one per ticket; each asserts the game's own fields on both games and takes key frames):

key   BUG-1297 (species.own_key_here). Tribe fixture. The guest's My Creations get one of its own creatures (another
      body) under the key of a creature of the host's My Creations (the host's "TigerHorse" 290229B9, as BUG-0278's
      "Save New" of an edited host creation leaves it, common after BUG-1255's landfall editor); the guest's game is
      started again with them and joins. The host plays that creature as its own (`coop.creations choose creature`):
      its bundle (with its records) registers the host's body under that key on the guest above My Creations (the
      shadow). (Not the tribe's species 067B3E4A: it is not in the host's My Creations, its bundle has no records and
      registers nothing, so no game shadows it: lab 20261119-104136, BUG-1297.) The guest plays its own creature of
      that key (the line's shared 0x7C...... copy on both games, its members of it). Then the players' route of its
      own Tribal Outfitter (hut, card badge). PASS: the outfitter was asked for a key whose model on the guest's game
      is the guest's own body (`coop.creations modelhash`), not the host's; key frame in the editor. Then a piece of
      clothing, Done: both games dress the guest's members in the new outfit (as tribe_own_outfitter).
      FAIL twin SPORE_FIX_OFF=species.own_key_here: the outfitter opens on the raw key: the host's body.
drop  BUG-1298 (designs.own_editor_offline), Tribe. The guest goes through its own proxy (SPORE_NET, set by the test
      when unset: no loss); it opens its own Tribal Outfitter (players' route); inside the editor the proxy drops
      every packet for 45 s (netem outage) until both games ended the session (30 s timeout). The guest puts a piece of
      clothing on and leaves with Done. PASS on the guest: the return was ours without a session (offline=1, the
      result kept), its tribe's species slot is the host's species as before (key and model), no member of its game
      wears the new outfit (key frame). The guest joins again: within 60 s both games have the guest's tribe line from
      the new creation and dress the guest's members in it, the host's members not (key frames); the host's row names
      it (acked); a host save names no key of the guest's.
      FAIL twin SPORE_FIX_OFF=designs.own_editor_offline: the native return reloads the slot with the outfit on the
      guest's game and the outfit never reaches the line.
civdrop BUG-1298, Civ (civ_npc). The guest buys an NPC city, opens its planner and its own house editor (the
      planner's message of the tile's button, as civ_guest_editors' fallback); inside the editor the link drops as in
      `drop`; the guest makes a house and leaves. PASS on the guest: the planner's apply was ours without a session
      (offline_returns >= 1), the guest's copy of the culture set keeps the house design it had, the civilization's
      species, the world's player species and the City Hall key unchanged. The guest joins again: within 90 s its line
      has the new house on both games (the same 0x7B key) and the guest's houses show it on both (key frame); the
      host's culture set unchanged.
      FAIL twin SPORE_FIX_OFF=designs.own_editor_offline: Civ_SetDesign writes the guest's house into its copy of the
      civilization and the line never gets it.
birth BUG-1299 (tribe.birth_species). Tribe fixture, the guest plays its own creature (members of it on both games).
      The guest buys a baby at its hut (real click). PASS: on both games the first time the new member is seen
      (`coop.tribe species`, polled as fast as the console answers) it is already of the guest's species (+0xB28);
      key frame right after. FAIL twin SPORE_FIX_OFF=tribe.birth_species: first seen in the tribe's species.
kid   BUG-1313 (tribe.kid_species). As birth; then the guest chooses another own creature while its kid lives: within
      40 s both games answer the new key, the guest's grown-up members are of it on both games, and 8 s later the kid
      (+0xB34 0) still has the old key on both games, no fault on either game; key frame. FAIL twin
      SPORE_FIX_OFF=tribe.kid_species: the pass changes the kid, SetSpecies starts the growth timer a citizen does not
      have: a fault on both games (lab 20261119-105127, tribe_birth_species_off).
stray BUG-1319 (tribe.stray_line_key). Tribe fixture, the guest plays its own creature; on the guest's game a grown-up
      member of the host gets the guest's line key by the game's SetSpecies (`coop.tribe setspecies <key> <id>`, as a
      mirror born in that species at a world's opening and forgotten by the world reset, lab 20261119-112300). PASS:
      within 15 s that member is of the tribe's species again on the guest's game, the mix right on both games; key
      frame. FAIL twin SPORE_FIX_OFF=tribe.stray_line_key: it keeps the guest's creature.
ship  BUG-1301 (creations.own_ship_key). Space fixture. When the guest's empire flies the host's ship key K (both
      games play the fixture's ship 0675F7B2, which the cold start puts into the host's My Creations as a base game
      model A: the host's bundle carries its records and registers A under K on the guest, checked), the guest's game
      is started again with another UFO model B of its own under K in its My Creations (a guest that edited its ship
      and saved it under the same id). PASS: after the join the guest's
      game answers its own model for K (what its own ship is drawn from), the host draws the guest's ship from that
      model too; key frame of the guest's ship on both screens.
      FAIL twin SPORE_FIX_OFF=creations.own_ship_key: the host's ship registered under K shadows the guest's own.

  SPORE_LANE=6 python3 tools/tests/coop_own_editors_robust.py key|drop|civdrop|birth|kid|stray|ship
"""
import os
import random
import re
import subprocess
import sys
import time

MODE = sys.argv[1] if len(sys.argv) > 1 else 'key'
if MODE in ('drop', 'civdrop'):
    # the guest's own proxy (tools/tests/netem.py) is how the test cuts its link; set before lane makes its JOIN
    os.environ.setdefault('SPORE_NET', 'delay=1')

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
from harness import fnv  # noqa: E402

import lane  # noqa: E402
import twoinst  # noqa: E402
from coop_creature_desync import kill_game  # noqa: E402
from coop_nest_species import HOST_ONLY, colliding_creations, fixtures_dir, read_pkg, write_pkg  # noqa: E402
from coop_tribe_outfit import (DONE, EDITOR, ITEM, ONTO, TAB3, TRIBE, busy, close_planner, drag, mode,  # noqa: E402
                               my_creations, open_outfitter, profile)
from coop_tribe_own_outfitter import designs, own_tribe, tribe_line  # noqa: E402
from coop_tribe_own_species import closeup, keys_in_slot, line_key, mix_ok, model_hash, species  # noqa: E402
from twoinst import Checks, Console, keyframe, start, wait_guest_in, wait_up, watch  # noqa: E402

SWITCH = {'key': 'species.own_key_here', 'drop': 'designs.own_editor_offline', 'civdrop': 'designs.own_editor_offline',
          'birth': 'tribe.birth_species', 'kid': 'tribe.kid_species', 'stray': 'tribe.stray_line_key', 'ship': 'creations.own_ship_key'}[MODE]
OUTAGE = 45.0  # s without a packet: both games end the session (net::Session timeout_ms 30000)
UBEM, UGROUP, UTYPE = 0x1A99B06B, 0x40656200, 0x476A98C7
SHIP = 0x0675F7B2  # the space fixture's ship (both players' empires fly it)


def off_twin():
    return SWITCH in twoinst.fix_off_names()


def role(con):
    m = re.search(r'role=(\w+)', con.call('coop.species').get('out') or '')
    return m.group(1) if m else '?'


def wait_for(cond, secs, step=1.0):
    t0 = time.time()
    while time.time() - t0 < secs:
        v = cond()
        if v:
            return v
        time.sleep(step)
    return cond()


def restart_guest(host, write_saves):
    """the guest's game again with My Creations written by write_saves(path) (its game reads them at start), joining
    the running host (coop_nest_species.restart_guest for any fixture)"""
    st = twoinst.lane_state()  # (the next test starts cold: these My Creations are not the fixture's)
    twoinst.lane_state(**{**st, 'knobs': st.get('knobs', []) + ['(own_editors_robust: the guest\'s My Creations)']})
    for m in twoinst._monitors:
        m.stop = True
    kill_game(lane.GUEST)
    time.sleep(3)
    write_saves(os.path.join(lane.GUEST_DIR, 'EditorSaves.package'))
    subprocess.run(['rm', '-f', os.path.join(lane.GUEST_DIR, 'SporeCoop', 'choices.ini')])
    lane.run_guest()
    if not wait_up(lane.GUEST, 90):
        lane.run_guest()
        assert wait_up(lane.GUEST), 'the guest did not start'
    guest = Console(lane.GUEST)
    twoinst.apply_fix_off(guest, 'guest')
    wait_for(lambda: 'GalaxyGameEntry' in guest.call('game.mode')['out'], 120)
    guest.call('wait.frames 200 60000')
    guest.call(lane.JOIN)
    ok = wait_guest_in(host, guest, 240)
    watch(lane.HOST, lane.GUEST)
    return guest, ok


def cut_link(host, guest, ck):
    """the guest's proxy drops everything for OUTAGE s; True once both games ended the session"""
    import netem
    px = netem.proxy()
    if not ck(px is not None, f'the guest plays through its own proxy (SPORE_NET={os.environ.get("SPORE_NET")})'):
        return False
    t_cut = time.time()
    px.outage(OUTAGE)
    print(f'  link cut for {OUTAGE:.0f} s (netem outage)', flush=True)
    ended = wait_for(lambda: role(guest) != 'guest' and role(host) == 'host' and
                     not re.search(r"remote \d+ 'Bob'", host.call('coop.status')['out']), OUTAGE - 2)
    print(f'  {time.time() - t_cut:.0f} s after the cut: guest role {role(guest)}, mode {mode(guest)}', flush=True)
    return ck(ended, 'both games ended the session while the guest was in its own editor')


def rejoin(host, guest, ck):
    import netem
    px = netem.proxy()
    if px:
        px.outage(0)
    time.sleep(2)
    print('  guest:', guest.call(lane.JOIN)['out'].strip()[:100], flush=True)
    ok = wait_guest_in(host, guest, 240)
    watch(lane.HOST, lane.GUEST)
    return ck(ok, 'the guest is in the host\'s world again')


def outfit_piece(guest, ck):
    """in the outfitter: the third tab, a body piece onto the chest, Done; True when back in Tribe"""
    guest.call('wait.frames 120 30000')
    guest.call('click %d %d' % TAB3)
    time.sleep(1.5)
    drag(guest, ITEM, ONTO)
    time.sleep(1)
    guest.call('move %d %d' % DONE)
    time.sleep(0.5)
    guest.call('click %d %d' % DONE)
    back = wait_for(lambda: mode(guest) == TRIBE, 60)
    return ck(back, f'the guest is back in Tribe ({mode(guest)})')


def open_own_outfitter(guest, ck):
    # BUG-1297 (lab fix/w57-tribe-keys 20261119-115724, -121054): right after a join from the galaxy menu the clicks on
    # the hut opened no planner: wait for the game's time (the stage entry's pause takes real input, BUG-0944) and,
    # when the planner still did not come, click the hut once more after 5 s (as a player does); both said in the log
    waited = twoinst.game_runs(guest)
    from coop_tribe_outfit import hut
    print(f'  guest: game time runs after {waited} s; hut on screen {hut(guest)}; busy {busy(guest) or "-"}', flush=True)
    opened = open_outfitter(guest)
    if not opened:
        print(f'  guest: no planner after the clicks (busy {busy(guest) or "-"}, hut {hut(guest)}): once more in 5 s',
              flush=True)
        time.sleep(5)
        opened = open_outfitter(guest)
    ck(opened, 'the guest\'s hut planner opened')
    t0 = time.time()
    while time.time() - t0 < 20 and mode(guest) != EDITOR:
        if time.time() - t0 > 8 and mode(guest) == TRIBE and 'input_lock' in busy(guest):
            from coop_tribe_outfit import BADGE
            guest.call('click %d %d' % BADGE)
            time.sleep(4)
        time.sleep(1)
    return ck(mode(guest) == EDITOR, f'the guest\'s own Tribal Outfitter opens ({mode(guest)})')


def own_creature(host, guest, ck, choose=None):
    """the guest plays a creature of its own (choose: that key); its line key on both games (0: none)"""
    slot, _, _ = species(host)
    if choose is None:
        own = [k for k in re.findall(r'^creature ([0-9A-F]{8}) ', guest.call('coop.creations list')['out'], re.M)
               if int(k, 16) != slot]
        if not ck(own, f'the guest has creatures of its own ({own[:4]})'):
            return 0
        choose = int(own[0], 16)
    print('  guest:', guest.call(f'coop.creations choose creature {choose:08X}')['out'].strip(), flush=True)
    gkey = wait_for(lambda: (k := line_key(guest, 1)) and line_key(host, 1) == k and k, 40)
    ck(gkey, f'both games have the guest\'s creature under one key ({gkey or 0:08X}, its own {choose:08X})')
    if gkey:
        res = wait_for(lambda: all(mix_ok(c, gkey)[0] for c in (host, guest)), 25)
        for r, c in (('host', host), ('guest', guest)):
            print(f'  {r}: {mix_ok(c, gkey)[1]}', flush=True)
        ck(res, f'the guest\'s members are of its creature {gkey:08X} on both games, the host\'s of the tribe\'s')
    return gkey or 0


def put_back():
    try:
        g = Console(lane.GUEST)
        print('  guest:', g.call('coop.creations unchoose creature')['out'].strip(), flush=True)
    except OSError:
        pass


# ---------------------------------------------------------------------------------------------------------- key
def run_key(ck):
    host, guest = start('tribe')
    time.sleep(4)
    print(' ', host.call('coop.tribe spawn 2')['out'].strip(), flush=True)
    time.sleep(4)
    slot, smodel, _ = species(host)
    own = [int(k, 16) for k in re.findall(r'^creature ([0-9A-F]{8}) ', guest.call('coop.creations list')['out'], re.M)
           if int(k, 16) != slot]
    if not ck(slot and own, f'the tribe\'s species {slot or 0:08X} and a creature of the guest\'s own ({own[:3]})'):
        return ck.result()
    src = own[0]
    # BUG-1297: a creature of the host's own My Creations (its bundle carries records: a world package on the guest);
    # the tribe's species of the fixture is not one (its bundle had no records and shadowed nothing)
    hosts = [int(k, 16) for k in re.findall(r'^creature ([0-9A-F]{8}) ', host.call('coop.creations list')['out'], re.M)]
    kk = HOST_ONLY if HOST_ONLY in hosts else next((k for k in hosts if k not in own and k != slot), 0)
    if not ck(kk, f'a creature of the host\'s own My Creations to collide with ({kk:08X}; {len(hosts)} of the host\'s)'):
        return ck.result()
    host_hash = model_hash(host, kk)
    guest, ok = restart_guest(host, lambda p: colliding_creations(p, kk, src))
    if not ck(ok, f'the guest with its own creature under the host\'s key {kk:08X} joined'):
        return ck.result()
    # the host plays that creature as its own: its bundle registers the host's body under that key on the guest
    print('  host:', host.call(f'coop.creations choose creature {kk:08X}')['out'].strip(), flush=True)
    shadow = wait_for(lambda: model_hash(guest, kk) == host_hash, 40)
    print(f'  key {kk:08X}: the host\'s body {host_hash}, on the guest {model_hash(guest, kk)}', flush=True)
    if not ck(shadow, f'the guest\'s game answers the host\'s body for {kk:08X} (the host\'s world package)'):
        return ck.result()
    gkey = own_creature(host, guest, ck, choose=kk)
    if not gkey:
        return ck.result()
    own_hash = model_hash(guest, gkey)
    ck(own_hash not in ('?', host_hash), f'the guest\'s line copy {gkey:08X} is its own body ({own_hash}, the host\'s '
                                         f'{host_hash})')
    own0 = own_tribe(guest)
    print(f'  the guest\'s own tribe line here: {own0:08X} (the raw key {kk:08X})', flush=True)
    gk0, gm0, _, _ = profile(guest)
    mine0 = my_creations(lane.GUEST_DIR)
    if not open_own_outfitter(guest, ck):
        close_planner(guest)
        ck.faults(host=host, guest=guest)
        return ck.result()
    d, _ = designs(guest)
    asked = int(d.get('asked', '0/').split('/')[0], 16)
    asked_hash = model_hash(guest, asked)
    print(f'  the outfitter was asked for {asked:08X}: on the guest {asked_hash} (its own {own_hash}, the host\'s '
          f'{host_hash})', flush=True)
    ck(asked_hash == own_hash and asked_hash != host_hash,
       f'the guest\'s own outfitter opens on the guest\'s own body ({asked:08X} -> {asked_hash}, its own {own_hash}, '
       f'the host\'s {host_hash})')
    keyframe('outfitter_body', 'the guest is in its own Tribal Outfitter: the creature on its screen is the guest\'s own '
                               'creature (the line\'s body), not the tribe\'s species the host\'s screen shows in its '
                               'village')
    if not outfit_piece(guest, ck):
        return ck.result()
    time.sleep(3)
    close_planner(guest)
    new = my_creations(lane.GUEST_DIR) - mine0
    d, _ = designs(guest)
    result = int(d.get('result', '0'), 16) or next(iter(new), 0)
    gk1, gm1, _, _ = profile(guest)
    ck(gk1 == gk0 and gm1 == gm0, f'the guest\'s tribe slot is still the host\'s species ({gk1 or 0:08X})')
    rhash = model_hash(guest, result) if result else '?'
    lines = wait_for(lambda: (h := tribe_line(host)) and h[0] and h[0] != gkey and tribe_line(guest)[0] == h[0] and
                     all(mix_ok(c, h[0])[0] for c in (host, guest)) and h, 40)
    okey = lines[0] if lines else 0
    print(f'  the guest\'s tribe line {okey:08X}: host {tribe_line(host)}, guest {tribe_line(guest)}; the outfit '
          f'{result:08X} -> {rhash}', flush=True)
    ck(okey and all(tribe_line(c)[1] == rhash for c in (host, guest)),
       f'both games dress the guest\'s members in its new outfit ({okey:08X}, {rhash})')
    _, _, ms = species(host)
    gm = sorted(i for i, x in ms.items() if x['owner'] == 1 and x['age'] == 1 and not x['dead'])
    if gm:
        closeup(host, guest, gm[0], 'guest_outfit', 'same view 12 m from a guest\'s member: the guest\'s own creature '
                                                    '(not the tribe\'s species) with the new piece of clothing, on '
                                                    'both screens')
    print('  host:', host.call('coop.creations unchoose creature')['out'].strip(), flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


# ---------------------------------------------------------------------------------------------------------- drop
def run_drop(ck):
    host, guest = start('tribe')
    time.sleep(4)
    print(' ', host.call('coop.tribe spawn 2')['out'].strip(), flush=True)
    time.sleep(4)
    gkey = own_creature(host, guest, ck)
    if not gkey:
        return ck.result()
    gk0, gm0, _, _ = profile(guest)
    hk0, hm0, _, _ = profile(host)
    mine0 = my_creations(lane.GUEST_DIR)
    if not open_own_outfitter(guest, ck):
        close_planner(guest)
        return ck.result()
    if not cut_link(host, guest, ck):
        return ck.result()
    if not ck(mode(guest) == EDITOR, f'the guest is still in its outfitter after the session ended ({mode(guest)})'):
        return ck.result()
    if not outfit_piece(guest, ck):
        return ck.result()
    time.sleep(3)
    close_planner(guest)
    new = my_creations(lane.GUEST_DIR) - mine0
    d, _ = designs(guest)
    result = int(d.get('result', '0'), 16) or next(iter(new), 0)
    print(f'  guest without a session: {d}; new creations {sorted("%08X" % n for n in new)}', flush=True)
    ck(new, 'the outfitter saved a new creation in the guest\'s My Creations')
    ck(int(d.get('offline', '0')) >= 1 and int(d.get('held', '0')) == 1,
       f'the return was ours without a session and the result is kept (offline={d.get("offline")}, held={d.get("held")})')
    gk1, gm1, _, _ = profile(guest)
    ck(gk1 == gk0 and gm1 == gm0, f'the guest\'s tribe slot is still the host\'s species ({gk1 or 0:08X} model {gm1}, '
                                  f'before {gk0 or 0:08X} model {gm0})')
    rhash = model_hash(guest, result) if result else '?'
    _, smodel, ms = species(guest)
    worn = [i for i, x in ms.items() if not x['dead'] and model_hash(guest, x['model']) == rhash]
    ck(rhash != '?' and not worn, f'no member on the guest\'s game wears the new outfit ({worn}, outfit {rhash})')
    keyframe('drop_alone', 'the session has ended inside the guest\'s outfitter; the guest is back in its village: every '
                           'member on the guest\'s screen is in the tribe\'s species without the new piece of clothing '
                           '(the host plays on alone)')
    if not rejoin(host, guest, ck):
        return ck.result()
    lines = wait_for(lambda: (h := tribe_line(host)) and h[0] and h[0] != gkey and h[1] == rhash and
                     tribe_line(guest)[0] == h[0] and h, 60)
    okey = lines[0] if lines else 0
    print(f'  after the join: the guest\'s tribe line host {tribe_line(host)}, guest {tribe_line(guest)} (outfit '
          f'{rhash})', flush=True)
    if not ck(okey, f'both games have the guest\'s new outfit as its tribe line ({okey:08X}, {rhash})'):
        ck.faults(host=host, guest=guest)
        return ck.result()
    gkey_now = line_key(host, 1)
    mixed = wait_for(lambda: all(mix_ok(c, okey)[0] for c in (host, guest)), 40)
    for r, c in (('host', host), ('guest', guest)):
        print(f'  {r}: {mix_ok(c, okey)[1]}', flush=True)
    ck(mixed, f'both games dress the guest\'s members in the outfit, the host\'s in the tribe\'s species ({gkey_now:08X})')
    d, _ = designs(guest)
    ck(wait_for(lambda: int(designs(guest)[0].get('acked', '0')) >= 1, 20),
       f'the host\'s row names the outfit (acked={designs(guest)[0].get("acked")})')
    ck(profile(host)[0] == hk0 and profile(host)[1] == hm0, 'the host\'s tribe slot is its species as before')
    keyframe('drop_rejoined', 'after the guest joined again: on both screens the guest\'s members wear the outfit made '
                              'while it was away (the leaf on the chest), the host\'s members the tribe\'s species')
    from coop_content_civ import save_slot, wait_saved
    slotname, folder, _ = save_slot(host, lane.HOST_DIR)
    since = time.time()
    print('  host:', host.call('coop.save')['out'].strip()[:80], f'(slot {slotname})', flush=True)
    saved = wait_saved(folder, since - 1, 90)
    time.sleep(3)
    own = set()  # (BUG-1326: instances that are also ids of the host's own resources of the slot, not counted)
    hits = keys_in_slot(folder, [okey, result, gkey] + sorted(new), own) if saved else ['not saved']
    print(f'  host save slot {folder}: {hits or "no key of the guest"}; the host\'s own ids among them: '
          f'{[f"{i:08X}" for i in sorted(own)]}', flush=True)
    ck(saved and not hits, f'the host\'s save names no key of the guest\'s ({hits})')
    ck.faults(host=host, guest=guest)
    return ck.result()


# ---------------------------------------------------------------------------------------------------------- civdrop
def run_civdrop(ck):
    from coop_civ import civ, dist
    from coop_civ_guest_editors import (BGROUP, BTYPE, CIV, HOUSE, civedit, close_planner as civ_close, culture,
                                        drive_editor, game_fields, in_editor, open_planner)
    from coop_civ_own_designs import mhash, own
    from play_civ import Player, frame_city
    host, guest = start('civ_npc')
    time.sleep(4)
    print('  host:', host.call('coop.res add money 30000')['out'].strip(), flush=True)
    hc, _ = civ(host)
    cap = [k for k, v in hc.items() if v['player']]
    npc = [k for k, v in hc.items() if not v['player']]
    if not ck(cap and npc, f'a player city {cap} and NPC cities {npc}'):
        return ck.result()
    npc.sort(key=lambda i: dist(hc[i]['pos'], hc[cap[0]]['pos']))
    gci, gpos = npc[0], hc[npc[0]]['pos']
    print(f'  guest buys city {gci}:', guest.call(f'coop.civ buycity {gci} 500')['out'].strip(), flush=True)
    if not ck(wait_for(lambda: all((c := civ(x)[0].get(gci)) and c['player'] and c['owner'] == 1 for x in (host, guest)),
                       30), f'city {gci} is the guest\'s on both games'):
        return ck.result()
    frame_city({'host': host, 'guest': guest}, gpos)
    before = game_fields(host, guest)
    p = Player('guest', guest, 1, random.Random(5), [])
    if not ck(open_planner(p, gpos), 'the guest\'s planner of its city is open (real clicks)'):
        return ck.result()
    print('  guest:', guest.call(f'coop.civedit new {HOUSE:08X}')['out'].strip(), flush=True)
    if not ck(in_editor(guest, 30), f'the guest\'s own house editor opened ({mode(guest)})'):
        return ck.result()
    if not cut_link(host, guest, ck):
        return ck.result()
    left = drive_editor(guest, 'house')
    ck(left, f'the guest left its editor with a creation, back in Civ ({mode(guest)})')
    time.sleep(4)
    civ_close(p)
    took, game, st = civedit(guest)
    m = re.search(r'offline_returns=(\d+) unacked=(\d+)', st)
    pend = re.search(r'pending=(\d+)', st)
    print('  guest without a session:', st.strip().splitlines()[0], flush=True)
    ck(m and int(m.group(1)) >= 1 and pend and int(pend.group(1)) >= 1,  # counter: the path (game fields below)
       f'the planner\'s apply was ours without a session and the design is kept ({m and m.groups()}, pending '
       f'{pend and pend.group(1)})')
    c0, g0 = before['guest']
    c1 = culture(guest)
    changed = {f'{k:08X}': (f'{v:08X}', f'{c1.get(k, 0):08X}') for k, v in c0.items() if v and c1.get(k) != v}
    ck(not changed, f'the guest\'s copy of the culture set keeps every design it had {changed or ""}')
    ck(g0 and game == g0, f'the civilization\'s species, the player species and the City Hall key are unchanged on the '
                          f'guest ({g0} -> {game})')
    keyframe('civdrop_alone', 'the session has ended inside the guest\'s house editor; back in Civ alone the guest\'s '
                              'cities show the civilization\'s houses (not the house it just made)')
    if not rejoin(host, guest, ck):
        return ck.result()

    def line_house(con):
        for ln in con.call('coop.species')['out'].splitlines():
            mm = re.match(r'player (\d+) .*design %08X=(\w+)' % HOUSE, ln)
            if mm and int(mm.group(2), 16) >> 24 == 0x7B:
                return int(mm.group(1)), int(mm.group(2), 16)
        return None
    got = wait_for(lambda: (a := line_house(host)) and line_house(guest) == a and a, 90)
    print(f'  after the join: the guest\'s house design host {line_house(host)}, guest {line_house(guest)}', flush=True)
    if not ck(got, 'both games have the house made while the guest was away in its line (one 0x7B key)'):
        ck.faults(host=host, guest=guest)
        return ck.result()
    pid = got[0]
    t = civedit(guest)[0].get(HOUSE)
    hb = mhash(guest, t['key'], t['type'], t['group']) if t else '?'
    # BUG-1307: the bought NPC city has no building of the house design id (lab 20261119-113002: none on both games):
    # the guest builds one (the game's own build), as civ_own_designs does
    print('  guest:', guest.call(f'coop.civ build {gci} house')['out'].strip(), flush=True)
    time.sleep(3)
    for con, who in ((host, 'host'), (guest, 'guest')):
        def houses(owner):
            return sorted({mhash(con, b['model'], BTYPE, BGROUP) for b in own(con)[1]
                           if b['owner'] == owner and b['did'] == HOUSE})
        ok = wait_for(lambda: houses(pid) == [hb], 30)
        print(f'  {who}: the guest\'s houses -> {houses(pid)}, the host\'s -> {houses(0)} (the new house {hb})', flush=True)
        ck(ok, f'{who}: the guest\'s houses show the house made while it was away ({houses(pid)}, {hb})')
    from coop_civ_guest_editors import guest_keys, host_culture_kept
    ck(*host_culture_kept(before['host'][0], culture(host), guest_keys(guest) | {got[1]}))  # BUG-1307 (the house build)
    frame_city({'host': host, 'guest': guest}, gpos)
    keyframe('civdrop_rejoined', 'after the guest joined again: the guest\'s city shows its own new house (made while it '
                                 'was away) on both screens; the capital keeps the culture\'s houses')
    ck(mode(guest) == CIV, 'the guest plays Civ')
    ck.faults(host=host, guest=guest)
    return ck.result()


# ---------------------------------------------------------------------------------------------------------- birth
def run_birth(ck):
    from coop_tribe_guest_baby import buy_click, food, hut, player
    host, guest = start('tribe')
    time.sleep(4)
    print(' ', host.call('coop.tribe spawn 2')['out'].strip(), flush=True)
    time.sleep(4)
    gkey = own_creature(host, guest, ck)
    if not gkey:
        return ck.result()
    slot, _, _ = species(host)
    print(' ', host.call('coop.tribe level 2')['out'].strip(), flush=True)
    _, pt = player(host)
    f = food(host) or 0
    host.call(f'call SporeCoop::cTribe_AddFood thiscall 0x{pt:X} {int(60 - f)}')
    time.sleep(4)
    ids0 = {r: set(species(c)[2]) for r, c in (('host', host), ('guest', guest))}
    h = hut(guest)
    if not ck(h is not None, f'the guest\'s own hut is on its screen ({h})'):
        return ck.result()
    print(f'  the guest\'s clicks on its hut until its game took one: {buy_click(guest, h)}', flush=True)
    first = {}
    t0 = time.time()
    framed = False
    while time.time() - t0 < 30 and len(first) < 2:
        for r, c in (('host', host), ('guest', guest)):
            if r in first:
                continue
            for i, x in species(c)[2].items():
                if i not in ids0[r] and not x['dead']:
                    first[r] = (i, x['key'], x['owner'], round(time.time() - t0, 2))
                    break
        if len(first) == 2 and not framed:
            framed = True
            keyframe('baby_born', 'right after the baby the guest bought appeared: the new small member is the guest\'s '
                                  'own creature on both screens (not the tribe\'s species)')
    print(f'  the new member as first seen (id, key, owner, s): {first} (the guest\'s {gkey:08X}, the slot\'s '
          f'{slot or 0:08X})', flush=True)
    for r in ('host', 'guest'):
        ck(r in first, f'{r}: the member the guest bought is there')
        if r in first:
            ck(first[r][1] == gkey, f'{r}: the new member is of the guest\'s species when it is first seen '
                                    f'({first[r][1]:08X}, the guest\'s {gkey:08X}, the tribe\'s {slot or 0:08X})')
    res = wait_for(lambda: all(mix_ok(c, gkey)[0] for c in (host, guest)), 10)
    ck(res, 'afterwards the guest\'s members are of its species, the host\'s of the tribe\'s, on both games')
    if 'host' in first:
        closeup(host, guest, first['host'][0], 'baby', 'same view 12 m from the new baby: the guest\'s own creature '
                                                       '(small) on both screens')
    if MODE == 'kid' and len(first) == 2:
        kid_line_change(host, guest, ck, gkey, first)
    ck.faults(host=host, guest=guest)
    return ck.result()


def kid_line_change(host, guest, ck, gkey, first):
    """BUG-1313: the guest's line changes while its kid lives: the pass changes the guest's adults on both games, the
    kid keeps its species until it is grown up (SetSpecies of a kid faulted on both games: the growth timer)"""
    slot, _, _ = species(host)
    own = [int(k, 16) for k in re.findall(r'^creature ([0-9A-F]{8}) ', guest.call('coop.creations list')['out'], re.M)
           if int(k, 16) != slot]
    cur = re.search(r'mine creature: ([0-9A-F]{8})', guest.call('coop.creations')['out'] or '')
    other = next((k for k in own if not cur or k != int(cur.group(1), 16)), 0)
    if not ck(other, f'the guest has another creature of its own ({other:08X})'):
        return
    print('  guest:', guest.call(f'coop.creations choose creature {other:08X}')['out'].strip(), f'{other:08X}', flush=True)
    k2 = wait_for(lambda: (k := line_key(guest, 1)) and k != gkey and line_key(host, 1) == k and k, 40)
    if not ck(k2, f'both games have the guest\'s new creature under one key ({k2 or 0:08X}, the old {gkey:08X})'):
        return
    kid = {r: first[r][0] for r in ('host', 'guest')}

    def adults_done():
        for c in (host, guest):
            _, _, ms = species(c)
            if any(m['owner'] == 1 and m['age'] == 1 and not m['dead'] and m['body'] and m['key'] != k2
                   for m in ms.values()):
                return False
        return True
    done = wait_for(adults_done, 30)
    time.sleep(8)  # (the pass runs every 0.15 s: a kid it would change is changed by now)
    for r, c in (('host', host), ('guest', guest)):
        _, _, ms = species(c)
        txt = ', '.join(f'#{i}:{m["owner"]}:{m["age"]}:{m["key"]:08X}' for i, m in sorted(ms.items()))
        print(f'  {r} after the line change: {txt}', flush=True)
        k = ms.get(kid[r])
        ck(k is not None and not k['dead'] and k['age'] == 0 and k['key'] == gkey,
           f'{r}: the kid keeps its species while it grows up ({k and k["key"] or 0:08X}, the old {gkey:08X}, '
           f'the new {k2:08X})')
    ck(done, f'the guest\'s grown-up members are of its new creature {k2:08X} on both games')
    keyframe('kid_kept', 'after the guest changed its creature: its grown-up members are the new creature on both '
                         'screens, its kid still the old one (small), both games running')


# ---------------------------------------------------------------------------------------------------------- stray
def run_stray(ck):
    host, guest = start('tribe')
    time.sleep(4)
    print(' ', host.call('coop.tribe spawn 2')['out'].strip(), flush=True)
    time.sleep(4)
    gkey = own_creature(host, guest, ck)
    if not gkey:
        return ck.result()
    slot, _, ms = species(guest)
    hm = sorted(i for i, m in ms.items() if m['owner'] == 0 and m['age'] == 1 and not m['dead'] and m['body'])
    if not ck(hm, f'a grown-up member of the host on the guest\'s game ({hm})'):
        return ck.result()
    out = guest.call(f'coop.tribe setspecies {gkey:08X} {hm[0]}')['out'].strip()
    print(f'  guest: host member #{hm[0]} given the guest\'s key {gkey:08X} by the game\'s SetSpecies: {out}', flush=True)
    if not ck(out.startswith('species set on 1'), f'the planted stray key took ({out})'):
        return ck.result()
    back = wait_for(lambda: (m := species(guest)[2].get(hm[0])) and m['key'] == slot, 15, 0.5)
    m = species(guest)[2].get(hm[0])
    print(f'  guest: host member #{hm[0]} now {m and m["key"] or 0:08X} (the slot\'s {slot:08X})', flush=True)
    ck(back, f'guest: the host\'s member wears the tribe\'s species again ({m and m["key"] or 0:08X}, the slot\'s '
             f'{slot:08X}, the stray {gkey:08X})')
    res = wait_for(lambda: all(mix_ok(c, gkey)[0] for c in (host, guest)), 10)
    for r, c in (('host', host), ('guest', guest)):
        print(f'  {r}: {mix_ok(c, gkey)[1]}', flush=True)
    ck(res, 'the guest\'s members are of its creature on both games, the host\'s of the tribe\'s')
    closeup(host, guest, hm[0], 'stray_back', 'same view 12 m from the host\'s member that wore the guest\'s creature: '
                                              'the tribe\'s species on both screens')
    ck.faults(host=host, guest=guest)
    return ck.result()


# ---------------------------------------------------------------------------------------------------------- ship
def ufo_hash(con, inst):
    m = re.search(r'model \w+ (\w+)', con.call(f'coop.creations modelhash {inst:08X} {UTYPE:08X} {UGROUP:08X}')['out'])
    return m.group(1) if m else '?'


def mine_ufo(con):
    m = re.search(r'mine ufo: (\w+)', con.call('coop.creations').get('out') or '')
    return int(m.group(1), 16) if m else 0


def run_ship(ck):
    from coop_space_host_ship_design import two_ufo_models
    # BUG-1301: the host's ship is a creation of its own My Creations (as a player's is: the ship editor saves it there),
    # so its bundle carries its records and registers on the guest under its key. The space fixture's ship is not in
    # the host's fixture My Creations: its bundle had no records, nothing was registered, nothing shadowed, and the twin
    # passed (lab 20261119-113824). The host's My Creations get a base game UFO model A under the fixture's ship key
    # at the cold start; the guest's game is started later with another model B under that key.
    ma, mb = two_ufo_models()
    ha, want = f'{fnv(ma):08X}', f'{fnv(mb):08X}'
    orig_restore = twoinst.restore_creations

    def restore_with_host_ship(guest=True):
        orig_restore(guest)
        p = os.path.join(lane.HOST_DIR, 'EditorSaves.package')
        base = os.path.join(fixtures_dir(), 'creations-host.package')
        recs = [r for r in read_pkg(p if os.path.exists(p) else base) if r[2] != SHIP]
        recs.append((UBEM, UGROUP, SHIP, ma))
        write_pkg(p, recs)
    twoinst.restore_creations = restore_with_host_ship
    host, guest = start('space')
    twoinst.restore_creations = orig_restore
    st = twoinst.lane_state()  # (the next test starts cold: these My Creations are not the fixture's)
    twoinst.lane_state(**{**st, 'knobs': st.get('knobs', []) + ['(own_editors_robust: the host\'s ship in My Creations)']})
    time.sleep(4)
    kh, kg = mine_ufo(host), mine_ufo(guest)
    print(f'  ships: host {kh:08X}, guest {kg:08X} (the host\'s My Creations: {SHIP:08X} -> {ha})', flush=True)
    if kh != SHIP or kh != kg:
        return ck.inconclusive(f'the players do not fly the fixture\'s ship {SHIP:08X} ({kh:08X}, {kg:08X})')
    reg = wait_for(lambda: ufo_hash(guest, kh) == ha, 30)
    ck(reg, f'the host\'s own ship (its records) answers for {kh:08X} on the guest\'s game ({ufo_hash(guest, kh)}, the '
            f'host\'s {ha})')

    def saves(p):
        src = p if os.path.exists(p) else os.path.join(fixtures_dir(), 'creations-guest.package')
        recs = [r for r in read_pkg(src) if r[2] != kh]
        recs.append((UBEM, UGROUP, kh, mb))
        write_pkg(p, recs)
    guest, ok = restart_guest(host, saves)
    if not ck(ok, 'the guest with its own ship under the host\'s ship key joined'):
        return ck.result()
    if not wait_for(lambda: 'Space' in guest.call('game.mode')['out'] or mine_ufo(guest), 120):
        return ck.inconclusive('the guest is not in Space after the join')
    time.sleep(10)  # (the host's bundles and the 2 s check)
    got = ufo_hash(guest, kh)
    st = re.search(r'own ship key: .*', guest.call('coop.creations')['out'])
    print(f'  {kh:08X}: the host\'s ship {ha}, the guest\'s own {want}; the guest\'s game answers {got}; '
          f'{st.group(0) if st else "-"}', flush=True)
    ck(got == want, f'the guest\'s own ship is drawn from its own model on its game ({got}, its own {want}, the '
                    f'host\'s {ha})')
    gk = wait_for(lambda: (m := re.search(r'player 1 ufo: (\w+)/', host.call('coop.creations')['out'])) and
                  int(m.group(1), 16), 30)
    hg = ufo_hash(host, gk) if gk else '?'
    print(f'  the host\'s copy of the guest\'s ship {gk or 0:08X} -> {hg}', flush=True)
    ck(hg == want, f'the host draws the guest\'s ship from the guest\'s own model ({hg}, {want})')
    keyframe('own_ship', 'the guest\'s own ship: on the guest\'s screen its own new ship model (not the host\'s ship), '
                         'on the host\'s screen the guest\'s ship the same')
    ck.faults(host=host, guest=guest)
    return ck.result()


def main():
    ck = Checks()
    print(f'  mode {MODE}, switch {SWITCH}{" (off: the FAIL twin)" if off_twin() else ""}', flush=True)
    try:
        return {'key': run_key, 'drop': run_drop, 'civdrop': run_civdrop, 'birth': run_birth, 'kid': run_birth, 'stray': run_stray,
                'ship': run_ship}[MODE](ck)
    finally:
        if MODE in ('key', 'drop', 'birth', 'kid', 'stray'):
            put_back()
        # (the guest's My Creations of `key` / `ship`: the next cold start restores the fixture's, restart_guest's knob)


if __name__ == '__main__':
    raise SystemExit(main())
