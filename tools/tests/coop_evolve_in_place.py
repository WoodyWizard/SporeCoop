#!/usr/bin/env python3
"""The host's editor does not reload a guest that is in the same world and stage (BUG-0508).

Players' top complaint (0.7.3.4 / 0.7.3.6): after every host editor (Cell and Creature) each guest got
the whole world again (a 3 MB save) and went Cell / Creature -> galaxy menu -> load, although the host's
new creature had already reached it. The guest here has no creations of its own (it plays the host
world's species, the case where the reload gave the guest's own avatar its new body).

  --case cell      cell fixture: the host's mating call, the vote, the guest's "I'm ready", the host moves
                   an eye and names the cell, done.
  --case creature  creature fixture: +40 DNA, `coop.vote evolve` (the game's evolution request), the
                   guest's "I'm ready", the host drags a mouth onto its creature (a real evolution: a new
                   species key), done; the guest leaves its own editor with the red cross.

Checked for 60 s after the host is back in its stage, on the guest's game:
  * it never left the stage for the galaxy menu / a load (every mode seen), and the host sent no world
    (`world saves ... for shares` of the host's coop.status);
  * the creature it draws for the host is the host's new creation (the puppet's model / species key is the
    key of the host's newest bundle there, with the host's creature's name);
  * its own avatar plays the host's new creation, as the reload gave it (Cell: the game's player cell model,
    Creature: the avatar's species key), and in Creature the world's player species of its game
    (cSpeciesManager +0xA4) is the host's new species.

  SPORE_FIX_OFF=world.evolve_in_place: the old reload: FAIL (the guest goes through the galaxy menu).
"""
import re
import sys
import time

from coop_ui import Ui
from twoinst import Checks, shot, start

HEART = (721, 1018)    # the mating-call button of the cell HUD
READY = (478, 747)     # "I'm ready" in the co-op window
DONE_CELL = (1416, 1058)
DONE = (1416, 1062)    # the creature editors' green check
CANCEL = (1383, 1062)  # the red cross


def mode(con):
    return con.call('game.mode')['out']


def field(text, pat):
    m = re.search(pat, text or '', re.S)
    return m.group(1) if m else None


def wait(cond, secs, step=1.0):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return None


def in_editor(con):
    return 'Editor' in mode(con)


def shares(host):
    v = field(host.call('coop.status')['out'], r'world saves posted \d+, for shares (\d+)')
    return int(v) if v else 0


def puppet_key(con, stage):
    st = con.call('coop.status')['out']
    if stage == 'cell':
        return field(st, r"remote 0 '[^']*'.*?species=([0-9A-F]{8})")
    return field(st, r"remote 0 '[^']*' puppet=\S+ state=\S+ [^\n]*\n\s+puppet pos=[^\n]*species=([0-9A-F]{8})")


def creations(con):
    return con.call('coop.creations')['out']


def host_bundle(guest, kind):
    """the key and name of the host's newest creation of a kind on the guest's game"""
    gc = creations(guest)
    m = re.search(rf"player 0 {kind}: ([0-9A-F]{{8}})/\S+ '([^']*)'", gc)
    return (m.group(1), m.group(2)) if m else (None, None)


def own_bundle(guest, kind):
    m = re.search(rf"player [1-9] {kind}: ([0-9A-F]{{8}})/\S+ '([^']*)'", creations(guest))
    return (m.group(1), m.group(2)) if m else (None, None)


def own_model(con, stage):
    if stage == 'cell':
        return field(creations(con), r'cell: player model ([0-9A-F]{8})')
    return field(con.call('coop.avatar')['out'], r'key=0x([0-9A-F]{8})')


def host_own(host, stage):
    if stage == 'cell':
        return field(creations(host), r'cell: player model ([0-9A-F]{8})')
    return field(host.call('coop.avatar')['out'], r'key=0x([0-9A-F]{8})')


def world_species(con):
    return field(creations(con), r'world player species ([0-9A-F]{8})')


# ---- the host's editor, per stage ----

def hud_ready(con):
    if 'Cell' not in mode(con) or 'cine=0' not in con.call('coop.pool')['out']:
        return False
    m = re.search(r'input_lock=(-?\d+)', con.call('coop.uistate')['out'])
    return bool(m) and int(m.group(1)) <= 0


def host_cell_editor(host, guest, ck):
    """coop_editor_desync.py parts 2-3: the mating call, the vote, the host's cell editor"""
    vote = lambda: 'vote active' in host.call('coop.vote')['out'] or in_editor(host)
    for _ in range(3):
        wait(lambda: hud_ready(host), 40)
        host.call('coop.pool mate button')
        time.sleep(1)
        host.call('click %d %d' % HEART)
        time.sleep(3)
        if 'alive=1' in host.call('coop.pool mate')['out']:
            break
    for _ in range(25):
        if vote():
            break
        m = re.search(r'alive=1 .* at (-?\d+) (-?\d+)', host.call('coop.pool mate')['out'])
        if m:
            x, y = (min(max(int(v), 20), lim) for v, lim in ((m.group(1), 1420), (m.group(2), 1000)))
            host.call(f'click {x} {y}')
        time.sleep(2)
    ck(vote(), 'the host reaches its mate: the evolution vote starts')
    wait(lambda: "'I'm ready'" in guest.call('coop.uistate')['out'], 10)
    guest.call('click %d %d' % READY)
    ck(wait(lambda: in_editor(host), 30), 'the host is in its cell editor')
    time.sleep(6)
    host.call('move 866 540')  # the second eye to the back
    time.sleep(0.3)
    host.call('button 866 540 down')
    time.sleep(0.3)
    for x in (900, 940, 980):
        host.call(f'move {x} 552')
        time.sleep(0.2)
    host.call('button 980 552 up')
    time.sleep(1)
    host.call('click 720 1055')  # the species name ("Name Your Species" on a first edit)
    time.sleep(1)
    host.call('click 720 810')
    host.call('text Inplacecell')
    time.sleep(0.5)
    host.call('click %d %d' % DONE_CELL)
    if wait(lambda: in_editor(guest), 8):  # the guest's own editor, if the vote opened one
        time.sleep(3)
        guest.call('click %d %d' % CANCEL)


def add_part(con, y):
    con.call(f'move 86 {y}')
    time.sleep(0.3)
    con.call(f'button 86 {y} down')
    time.sleep(0.3)
    for x, yy in ((300, 250), (600, 400), (850, 500), (930, 560)):
        con.call(f'move {x} {yy}')
        time.sleep(0.2)
    time.sleep(0.5)
    con.call('button 930 560 up')
    time.sleep(1)


def editor_ui(con):
    return con.call('coop.ui tree 4')['out'].count('Window') > 15


def cinematic(con):
    return re.search(r'cinematic ([0-9A-F]{8}) playing', con.call('coop.status')['out'])


def host_creature_editor(host, guest, ck):
    """coop_editor_sync.py --case host (fix/w40-cell-evo): a real evolution of the host's species"""
    dna = lambda c: float(field(c.call('coop.dna')['out'], r'dna=(-?[\d.]+)') or 0)
    if dna(host) < 40:
        host.call('coop.dna add 40')  # (not divided by the party's DNA pace, BUG-0510)
        time.sleep(3)
        for _ in range(6):
            for c in (host, guest):
                if cinematic(c):
                    c.call('key enter')
            time.sleep(2)
    gu = Ui(guest, 'guest')
    print(' ', host.call('coop.vote evolve')['out'].strip(), flush=True)
    time.sleep(2)
    gu.wait(lambda s: s['ready']['vis'], 10, 'ready button')
    gu.click('ready')
    ck(wait(lambda: in_editor(host), 60), 'the host is in its editor')
    ck(wait(lambda: in_editor(guest), 120), "the guest's own editor opened")
    ck(wait(lambda: editor_ui(host) and editor_ui(guest), 90, 2), 'both editors show their interface')
    host.call('wait.frames 200 60000')
    guest.call('wait.frames 200 60000')
    time.sleep(3)
    d_in = dna(host)
    add_part(host, 780)  # the unlocked mouth (the part at y 170 is locked in the evolution editor)
    if dna(host) == d_in:
        add_part(host, 170)
    time.sleep(2)
    print(f'  the host added a part: DNA in its editor {d_in} -> {dna(host)}', flush=True)
    guest.call('click %d %d' % CANCEL)  # the guest leaves its own editor unchanged
    time.sleep(2)
    host.call('click %d %d' % DONE)


def main():
    stage = sys.argv[sys.argv.index('--case') + 1] if '--case' in sys.argv else 'cell'
    kind = 'cell' if stage == 'cell' else 'creature'
    host, guest = start('cell' if stage == 'cell' else 'creature', guest_creations=False)
    ck = Checks()
    time.sleep(3)
    if '--nosave' in sys.argv:  # BUG-0517: no save of the host after its editor (as a host alone or without a share)
        print('  host:', host.call('coop.evokey nosave')['out'].strip(), flush=True)
    m_stage = 'Cell' if stage == 'cell' else 'Creature'
    h0 = host_own(host, stage)
    g0 = own_model(guest, stage)
    print(f'  before: host plays {h0}, the guest draws it as {puppet_key(guest, stage)}, the guest plays {g0}, '
          f'world player species host {world_species(host)} guest {world_species(guest)}', flush=True)
    sh0 = shares(host)

    (host_cell_editor if stage == 'cell' else host_creature_editor)(host, guest, ck)

    # from here: every mode of the guest's game, 0.5 s apart, while the host comes back and 60 s after it
    modes_seen = []
    back_at = None
    t0 = time.time()
    while time.time() - t0 < 150:
        gm, hm = mode(guest), mode(host)
        if not modes_seen or modes_seen[-1] != gm:
            modes_seen.append(gm)
            print(f'  {time.time() - t0:5.1f} s guest mode {gm}', flush=True)
        if back_at is None and m_stage in hm and host.call('coop.avatar')['ok']:
            back_at = time.time()
            print(f'  {time.time() - t0:5.1f} s the host is back in its stage', flush=True)
        for c in (host, guest):  # the evolution's captions, as a player confirms them
            if stage != 'cell' and cinematic(c):
                c.call('key enter')
        if back_at and time.time() - back_at > 60:
            break
        time.sleep(0.5)
    ck(back_at, 'the host is back in its stage')
    left = [m for m in modes_seen if m_stage not in m and 'Editor' not in m]
    sh1 = shares(host)
    print(f'  guest modes: {" -> ".join(modes_seen)}; host shares {sh0} -> {sh1}', flush=True)
    print('  host:', host.call('coop.status')['out'].count('evolve in place'),
          field(host.call('coop.status')['out'], r'(evolve in place: [^\n]*)'), flush=True)
    ck(not left, f"the guest's game stayed in the {m_stage} stage (no galaxy menu, no load): {left or 'none'}")
    ck(sh1 == sh0, f'the host sent no world after its editor ({sh0} -> {sh1})')

    h1 = host_own(host, stage)
    print(f'  host {kind} {h0} -> {h1}', flush=True)
    if stage != 'cell':
        print('  host:', host.call('coop.evokey')['out'].strip(), flush=True)
    ck(h1 and h1 != h0, f"the host's editor gave it a new {kind}")

    def seen():
        bk, bn = host_bundle(guest, kind)
        pk = puppet_key(guest, stage)
        ok, on = own_bundle(guest, kind)
        gm = own_model(guest, stage)
        return bk, bn, pk, ok, on, gm

    def good():
        bk, bn, pk, ok, on, gm = seen()
        puppet_ok = bk and pk in (bk, h1)
        own_ok = gm and gm != g0 and (gm == h1 or (gm == ok and on == bn))
        return puppet_ok and own_ok
    wait(good, 60, 2)
    bk, bn, pk, ok, on, gm = seen()
    hn = field(creations(host), rf"mine {kind}: [0-9A-F]{{8}} '([^']*)'")
    print(f"  guest: the host's bundle {bk} '{bn}' (host's name '{hn}'), puppet {pk}; own bundle {ok} '{on}', "
          f"own {kind} {g0} -> {gm}", flush=True)
    ck(bk and pk in (bk, h1) and bn == hn, f"the guest's game draws the host with its new {kind} ({pk}, '{bn}')")
    ck(gm and gm != g0 and (gm == h1 or (gm == ok and on == bn)),
       f"the guest's own {kind} is the host's new one, as the reload gave it ({g0} -> {gm})")
    if stage != 'cell':
        ws_h, ws_g = world_species(host), world_species(guest)
        print(f'  world player species: host {ws_h}, guest {ws_g}', flush=True)
        ck(ws_g and ws_g == ws_h == h1, f"the guest's world player species is the host's new species ({ws_g} / {ws_h})")
    shot(guest, f'evolve_in_place_{stage}_guest.png')
    shot(host, f'evolve_in_place_{stage}_host.png')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
