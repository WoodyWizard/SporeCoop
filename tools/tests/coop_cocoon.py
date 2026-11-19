#!/usr/bin/env python3
"""WP-K (BUG-1303): a partner in an editor of its own is shown in the game's cocoon where it stood, and comes out of it
with its new body.

Creature fixture (the two creatures a few metres apart). Before 0.7.16 the partner's creature disappeared from the
other screen while the partner edited and came back later with its new body. Now, on the OTHER game (the observer):

  --who host (default): the host's evolution (the mating editor's request, `coop.vote evolve`: its editor at once,
      core.editor_now; the guest takes no offer and watches). The host drags a mouth onto its creature (a new species)
      and leaves with done. The guest is the observer.
  --who guest: the guest's own evolution (its mating editor's request, `coop.vote evolve` on the guest: its own editor
      at once). The guest leaves it with the red cross (no new design). The host is the observer.

Asserted on the observer's game (its own state, `coop.cocoon`: the puppet's current animation as the game's
AnimatedCreature reports it, the glow effect's IsActive):
  1. while the partner edits, the partner's creature stays shown (a puppet) and goes into the cocoon: its animation is
     gen_evolve_cocoon_hold 4E0D3CBF and the cocoon's glow crg_cocoon_hold is an active effect in most samples, the
     body shrunk inside (scale < 0.3 of its size); a key frame of the observer's screen;
  2. the partner is back: the creature comes out where the cocoon stood: the game's animation gen_evolve_emerge
     D4F48A35 (read back from the game) and a key frame of it;
  3. after it: the puppet is driven again at its size, and (--who host) it wears the host's new species (the body it
     came out with differs from the one it went in with and is the host's new creature).
  SPORE_FIX_OFF=creature.cocoon: the partner's creature is hidden while it edits (0.7.15): step 1 FAILS.

  coop_cocoon.py [--who host|guest]
"""
import re
import sys
import time

from harness import field_dotall as field, mode, waiter
from twoinst import Checks, keyframe, start

DONE = (1416, 1062)    # the creature editors' green check (1440x1080 test window)
CANCEL = (1383, 1062)  # the red cross
W, H = 1440, 1080
HOLD, EMERGE, ENTER = '4E0D3CBF', 'D4F48A35', '9C26F705'

wait = waiter(step=1.0)

ROW = re.compile(r'partner (\d+) phase=(\S+) for=\S+ puppet=(\S+) anim=([0-9A-F]{8}) held=(\d) glow=(\d) scale=([\d.]+) '
                 r'size=([\d.]+) species=([0-9A-F]{8}) body0=([0-9A-F]{8}) seen_hold=([0-9A-F]{8}) '
                 r'seen_emerge=([0-9A-F]{8}) emerged_body=([0-9A-F]{8}) spot=\(([^)]*)\) screen=(off )?(-?\d+) (-?\d+)')
LAST = re.compile(r'last (\d+) body0=([0-9A-F]{8}) seen_hold=([0-9A-F]{8}) seen_emerge=([0-9A-F]{8}) '
                  r'emerged_body=([0-9A-F]{8})')


def cocoon(con, pid):
    out = con.call(f'coop.cocoon {pid}')['out']
    m = ROW.search(out)
    row = None
    if m:
        row = dict(phase=m.group(2), puppet=m.group(3), anim=m.group(4), held=m.group(5) == '1', glow=m.group(6) == '1',
                   scale=float(m.group(7)), size=float(m.group(8)), species=m.group(9), body0=m.group(10),
                   seen_hold=m.group(11), seen_emerge=m.group(12), emerged_body=m.group(13), on_screen=not m.group(15),
                   sx=int(m.group(16)), sy=int(m.group(17)))
    lm = LAST.search(out)
    last = dict(body0=lm.group(2), seen_hold=lm.group(3), seen_emerge=lm.group(4), emerged_body=lm.group(5)) if lm else None
    return row, last, out


def in_editor(con):
    return 'Editor' in mode(con)


def editor_ui(con):
    return con.call('coop.ui tree 4')['out'].count('Window') > 15


def cinematic(con):
    return re.search(r'cinematic ([0-9A-F]{8}) playing', con.call('coop.status')['out'])


def avatar_key(con):
    return field(con.call('coop.avatar')['out'], r'key=0x([0-9A-F]{8})')


def add_part(con, y):
    """drag a part of the palette onto the creature's body (coop_evolve_in_place.py)"""
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


def dna(con):
    return float(field(con.call('coop.dna')['out'], r'dna=(-?[\d.]+)') or 0)


def face_spot(obs, pid):
    """turn the observer's camera until the cocoon is on its screen (a drag with the right button, as a player)"""
    for _ in range(8):
        row, _, _ = cocoon(obs, pid)
        if row and row['on_screen'] and 40 < row['sx'] < W - 40 and 60 < row['sy'] < H - 160:
            return True
        obs.call(f'drag {W // 2} {H // 2} {W // 2 + 300} {H // 2} right')
        time.sleep(0.6)
    return False


def main():
    who = sys.argv[sys.argv.index('--who') + 1] if '--who' in sys.argv else 'host'
    host, guest = start('creature')
    ck = Checks()
    editor, obs = (host, guest) if who == 'host' else (guest, host)
    pid = 0 if who == 'host' else 1
    obs_role = 'guest' if who == 'host' else 'host'
    time.sleep(3)
    print('  observer:', obs.call('coop.cocoon')['out'].strip().splitlines()[0], flush=True)
    if who == 'host' and dna(host) < 40:
        host.call('coop.dna add 40')  # (an evolution with a new part needs DNA; not divided by the party's pace)
        time.sleep(3)
        for _ in range(6):
            for c in (host, guest):
                if cinematic(c):
                    c.call('key enter')
            time.sleep(2)
    k0 = avatar_key(editor)

    # the partner goes into its editor: the game's own evolution request
    print(f'  {who}:', editor.call('coop.vote evolve')['out'].strip(), flush=True)
    ck(wait(lambda: in_editor(editor), 90), f"the {who}'s own editor opened ({mode(editor).strip()})")
    ck(not in_editor(obs), f'the observer plays on in the Creature stage ({mode(obs).strip()})')

    # 1. the cocoon on the observer's screen
    got = wait(lambda: cocoon(obs, pid)[0], 15, 0.5)
    row = cocoon(obs, pid)[0]
    print(f'  observer: {row}', flush=True)
    ck(got and row and row['puppet'] not in ('00000000', '(nil)', '0'),
       f"the observer's game still shows the {who}'s creature while it edits and put it into the cocoon ({row and row['phase']})")
    time.sleep(3.5)  # (the curl in: 2.4 s)
    face_spot(obs, pid)
    samples, held, glowing, small = 0, 0, 0, 0
    for _ in range(16):
        r = cocoon(obs, pid)[0]
        if r and r['phase'] == 'hold':
            samples += 1
            held += r['anim'] == HOLD
            glowing += r['glow']
            small += r['size'] > 0 and r['scale'] < 0.3 * r['size']
        time.sleep(0.5)
    print(f'  hold samples {samples}: animation {HOLD} in {held}, glow active in {glowing}, shrunk in {small}', flush=True)
    ck(samples >= 12 and held >= 0.8 * samples,
       f"the observer's game plays the cocoon's hold animation {HOLD} on the creature ({held}/{samples})")
    ck(samples >= 12 and glowing >= 0.7 * samples, f"the cocoon's glow is an active effect there ({glowing}/{samples})")
    ck(samples >= 12 and small >= 0.8 * samples, f'the creature is shrunk inside the shell ({small}/{samples})')
    keyframe('cocoon_hold', f"{obs_role}'s screen: where the {who}'s creature stood, a grey woven cocoon with a teal glow "
             f"(the creature curled up inside it, nothing of it showing through), while the {who} is in its editor",
             players={obs_role: obs}, settle=0)

    # 2. the partner is back
    if who == 'host':
        ck(wait(lambda: editor_ui(host), 90, 2), "the host's editor shows its interface")
        host.call('wait.frames 200 60000')
        time.sleep(3)
        d_in = dna(host)
        add_part(host, 780)  # the unlocked mouth
        if dna(host) == d_in:
            add_part(host, 170)
        print(f'  the host added a part: DNA in its editor {d_in} -> {dna(host)}', flush=True)
        host.call('click %d %d' % DONE)
    else:
        wait(lambda: editor_ui(guest), 60, 2)
        time.sleep(5)
        guest.call('click %d %d' % CANCEL)
    t_back = None
    emerge_live = False
    phases = []
    shot_emerge = False
    t0 = time.time()
    while time.time() - t0 < 90:
        if t_back is None and not in_editor(editor):
            t_back = time.time()
        r, last, _ = cocoon(obs, pid)
        if r and (not phases or phases[-1] != r['phase']):
            phases.append(r['phase'])
            print(f"  {time.time() - t0:5.1f} s observer: phase {r['phase']} anim {r['anim']} species {r['species']}", flush=True)
        if r and r['phase'] == 'emerge':
            emerge_live = emerge_live or r['anim'] == EMERGE
            if not shot_emerge:
                shot_emerge = True
                keyframe('cocoon_emerge', f"{obs_role}'s screen: the {who}'s creature comes out of the cocoon where it "
                         "stood (the shell bursts, the creature grows back to its size)", players={obs_role: obs}, settle=0)
        if t_back and not r and last:
            break
        for c in (host, guest):  # the evolution's captions, as a player confirms them
            if cinematic(c):
                c.call('key enter')
        time.sleep(0.25)
    r, last, out = cocoon(obs, pid)
    print('  observer:', out.strip().replace('\n', ' | '), flush=True)
    ck(t_back, f'the {who} is back from its editor')
    ck('wait' in phases or 'emerge' in phases, f'the creature stayed in the cocoon after the {who} was back ({phases})')
    ck(last and (emerge_live or last['seen_emerge'] == EMERGE),
       f"the observer's game plays the emerge animation {EMERGE} on it ({'seen live' if emerge_live else last and last['seen_emerge']})")
    time.sleep(3)
    st = obs.call('coop.status')['out']
    pk = field(st, rf"remote {pid} '[^']*' puppet=\S+ state=\S+ [^\n]*\n\s+puppet pos=[^\n]*species=([0-9A-F]{{8}})")
    k1 = avatar_key(editor)
    print(f"  {who} plays {k0} -> {k1}; the observer draws it as {pk}; cocoon {last}", flush=True)
    ck(r is None and pk, f'the creature is driven again after it came out (puppet species {pk})')
    if who == 'host':
        ck(k1 and k1 != k0, f"the host's editor gave it a new creature ({k0} -> {k1})")
        ck(last and last['emerged_body'] != last['body0'],
           f"it came out with its new body ({last and last['body0']} -> {last and last['emerged_body']})")
    keyframe('cocoon_after', f"{obs_role}'s screen: the {who}'s creature stands again at its normal size"
             + (' with the new part (a second mouth)' if who == 'host' else ''), players={obs_role: obs}, settle=0)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
