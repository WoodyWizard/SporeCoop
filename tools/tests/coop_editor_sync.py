#!/usr/bin/env python3
"""What one player changes in its editor reaches the other player's game (BUG-0457, BUG-0466).

Creature fixture. The host starts an evolution vote, the guest clicks "I'm ready": the host's
evolution editor and the guest's own editor open (real input, outside the frame like a player's
clicks). One of the two adds a part (a mouth: it costs DNA) and presses done, the other presses
done without a change.

  --case host   BUG-0457: the host edits. Checked on the guest's game: the creature it draws for the
                host (the puppet's species key, the game's creature) is the host's new species, the
                one the host's own avatar plays.
  --case guest  BUG-0466: the guest edits and saves it as a new creation. Checked on both games
                (the game's own DNA, GetEvolutionPoints): the DNA the guest spent is gone from the
                party's total on the host's game and stays gone on the guest's (it came back to
                the value before the editor with the next total of the host).

  --case spend  BUG-0466's class (G6): the guest's own game spends DNA (AddEvolutionPoints -25, no
                editor): the host's total loses it and the guest's game keeps it.

  coop_editor_sync.py --case host|guest|spend   (host: SPORE_FIX_OFF=creations.editor_species, guest: creations.own_editor_dna, spend: core.dna_spend fail)
"""
import re
import sys
import time

from coop_ui import Ui
from twoinst import Checks, shot, start

DONE = (1416, 1062)    # the creature editors' green check (1440x1080 test window)
CANCEL = (1383, 1062)  # the red cross
SAVE_NEW = (926, 604)  # the own editor's exit dialog after an edit: "Save New"
MOUTH = 25             # the mouth's price in the creature editors (docs/re/creations.md#creations-own-editor-dna)


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


def dna(con):
    v = field(con.call('coop.dna')['out'], r'dna=(-?[\d.]+)')
    return float(v) if v is not None else None


def avatar_key(con):
    return field(con.call('coop.avatar')['out'], r'key=0x([0-9A-F]{8})')


def puppet_species(con, rid):
    """the species key of the creature this game draws for player rid (the game's own creature)"""
    return field(con.call('coop.status')['out'], rf"remote {rid} '[^']*' puppet=\S+ state=\S+ [^\n]*\n\s+puppet pos=[^\n]*species=([0-9A-F]{{8}})")


def host_key_on_guest(guest, hk):
    """the key the guest's game uses for a creation of the host (its own creations keep their key)"""
    return field(guest.call('coop.creations')['out'], rf'host species {hk} = ([0-9A-F]{{8}}) here') or hk


def in_editor(con):
    return 'Editor' in mode(con)


def cinematic(con):
    return re.search(r'cinematic ([0-9A-F]{8}) playing', con.call('coop.status')['out'])


def confirm_captions(cons, secs):
    """Enter on the captions of the evolution's cinematic (as a player does) for secs"""
    t0 = time.time()
    while time.time() - t0 < secs:
        for con in cons:
            if cinematic(con):
                con.call('key enter')
        time.sleep(2)


def editor_ui(con):
    return con.call('coop.ui tree 4')['out'].count('Window') > 15


def add_part(con, y=170):
    """drag a part of the palette's first column (a mouth) onto the creature's body"""
    con.call(f'move 86 {y}')
    time.sleep(0.3)
    con.call(f'button 86 {y} down')
    time.sleep(0.3)
    for x, y in ((300, 250), (600, 400), (850, 500), (930, 560)):
        con.call(f'move {x} {y}')
        time.sleep(0.2)
    time.sleep(0.5)
    con.call('button 930 560 up')
    time.sleep(1)


def back(con, m0, secs=90):
    return wait(lambda: mode(con) == m0 and con.call('coop.avatar')['ok'], secs)


def main():
    case = sys.argv[sys.argv.index('--case') + 1] if '--case' in sys.argv else 'host'
    host, guest = start()
    ck = Checks()
    gu = Ui(guest, 'guest')
    time.sleep(3)
    m0 = mode(host)
    hk0 = avatar_key(host)
    d_h0, d_g0 = dna(host), dna(guest)
    print(f'  before: host species {hk0}, guest draws the host as {puppet_species(guest, 0)}; '
          f'DNA host {d_h0} guest {d_g0}', flush=True)
    if (d_h0 or 0) < 40:  # a mouth costs 25 DNA: enough for one
        host.call('coop.dna add 40')
        time.sleep(3)
        # (more DNA can grow the brain: its cinematic waits for the caption's OK, and the editor with it)
        confirm_captions((host, guest), 12)
        d_h0, d_g0 = dna(host), dna(guest)
        print(f'  DNA after +40 on the host: host {d_h0} guest {d_g0}', flush=True)

    if case == 'spend':  # the class (G6): a spend of the guest's own game, no editor
        print('  guest:', guest.call('coop.dna add -25')['out'].strip(), '(AddEvolutionPoints -25 in its game)', flush=True)
        samples = []
        for _ in range(8):
            time.sleep(2)
            samples.append((dna(host), dna(guest)))
        d_h1, d_g1 = samples[-1]
        print(f'  DNA after: host {d_h0} -> {d_h1}, guest {d_g0} -> {d_g1} (samples {samples})', flush=True)
        ck(d_h1 is not None and abs(d_h1 - (d_h0 - 25)) < 1, "the guest's spend left the party's total on the host's game")
        ck(d_g1 is not None and abs(d_g1 - (d_g0 - 25)) < 1, "the guest's game kept its spend (not given back by the host's next total)")
        ck.faults(host=host, guest=guest)
        return ck.result()
    rounds = 2 if case == 'host2' else 1  # host2: a second evolution of the species the first one made
    # host_late (BUG-0457, 0.7.11): the host leaves its editor first and plays its evolution's cinematic; the guest
    # stays in its own editor meanwhile (the host's new bundle reaches it there) and presses done 25 s later
    late = case == 'host_late'
    if case in ('host2', 'host_late'):
        case = 'host'
    for rnd in range(rounds):
        if rnd:  # the species of the first evolution is now the host's own creation (My Creations)
            hk0 = avatar_key(host)
            print(f'  second evolution: the host plays {hk0}, the guest draws it as {puppet_species(guest, 0)}', flush=True)
            shot(guest, 'editor_sync_host2_first_on_guest.png')
            host.call('coop.dna add 40')
            time.sleep(3)
            confirm_captions((host, guest), 12)
        d_stage = dna(host)  # BUG-0581: the stage's DNA when the host's request is made (and held by the vote)
        print(' ', host.call('coop.vote evolve')['out'].strip(), flush=True)  # the game's own evolution request
        time.sleep(2)
        gu.wait(lambda s: s['ready']['vis'], 10, 'ready button')
        gu.click('ready')
        ck(wait(lambda: in_editor(host), 60), 'the host is in its editor')
        ck(wait(lambda: in_editor(guest), 120), "the guest's own editor opened")
        ck(wait(lambda: editor_ui(host) and editor_ui(guest), 90, 2), 'both editors show their interface')
        host.call('wait.frames 200 60000')
        guest.call('wait.frames 200 60000')
        time.sleep(3)

        editor, other = (host, guest) if case == 'host' else (guest, host)
        d_in = dna(editor)
        # the guest's own editor locks the parts the species has not collected (BUG-0368): the unlocked
        # mouths of the fixture's guest are lower in the first column
        # (the host's evolution editor locks them too: at y 170 nothing was added, DNA 40 -> 40, lab 20261103-142443)
        add_part(editor, 780)
        if case == 'host' and dna(editor) == d_in:
            add_part(editor, 170)
        time.sleep(2)
        d_edit = dna(editor)
        shot(editor, f'editor_sync_{case}_edited.png')
        print(f'  the {case} added a part: DNA in its editor {d_in} -> {d_edit}', flush=True)
        # the other player leaves its editor first without a change (host_late: after the host)
        if not late:
            other.call('click %d %d' % DONE)
            if other is host:
                confirm_captions((host,), 20)
            ck(back(other, m0), f'the {"guest" if case == "host" else "host"} is back in the stage (done, no change)')
        editor.call('click %d %d' % DONE)
        if case == 'guest':  # the own editor asks: save as a new creation
            time.sleep(3)
            shot(editor, 'editor_sync_guest_dialog.png')
            if in_editor(editor):
                editor.call('click %d %d' % SAVE_NEW)
        # the lowest DNA the editing player's game shows on its way back (a host total may undo the
        # spend a moment later: that is what is checked below)
        low, t0, left = None, time.time(), False
        while time.time() - t0 < 90 and not left:
            v = dna(editor)
            if v is not None and editor.call('coop.avatar')['ok']:
                low = v if low is None else min(low, v)
            left = mode(editor) == m0 and editor.call('coop.avatar')['ok']
            time.sleep(0.2)
        for _ in range(10):
            v = dna(editor)
            low = v if low is None or (v is not None and v < low) else low
            time.sleep(0.2)
        ck(left, f'the {case} left its editor with done')
        d_exit = low
        if case == 'host' and d_stage is not None:
            # BUG-0581: the held request's editor got the stage's DNA as its budget. The game's DNA does not move
            # while the host's evolution editor is open (coop.dna 40 -> 40 with the mouth bought); on the way back
            # the stage sets the DNA to the result's remaining budget (0xD445AA): the stage's DNA minus the mouth's
            # price (25) only when the editor's budget was the stage's DNA (a lost budget message: the config's
            # start budget minus 25)
            print(f'  BUG-0581: stage DNA {d_stage} at the request, back in the stage {d_exit} (a mouth costs 25)', flush=True)
            ck(d_exit is not None and abs(d_exit - (d_stage - MOUTH)) < 1,
               f"the host's held evolution editor had the stage's DNA budget ({d_stage} - {MOUTH} = {d_exit})")
        if late:  # the guest still in its editor: the host's cinematic, its share, its bundle
            t1 = time.time()
            while time.time() - t1 < 25:
                confirm_captions((host,), 3)
            ck(in_editor(guest), "the guest is still in its own editor 25 s after the host's evolution")
            print(f'  the guest in its editor; the host plays {avatar_key(host)}, guest creations: '
                  f'{field(guest.call("coop.creations")["out"], r"(player 0 creature: [^ ]+)")}', flush=True)
            guest.call('click %d %d' % DONE)
            ck(back(guest, m0), 'the guest is back in the stage (done, no change) after the host')
        print(f'  the {case} back in the stage: DNA {d_exit}', flush=True)

        # the host's evolution: its new body, the cinematic (its captions), a world share to the guest
        confirm_captions((host, guest), 45)
        back(host, m0, 60)
        back(guest, m0, 120)
        time.sleep(10)  # several totals of the host (every 2 s), the bundles (every 2 s)

    shot(host, f'editor_sync_{case}_host_after.png')
    print('  host creations:', host.call('coop.creations')['out'].strip().splitlines()[:6], flush=True)
    if case == 'host':
        hk1 = wait(lambda: (lambda k: k if k and k != hk0 else None)(avatar_key(host)), 60)
        print(f'  host species {hk0} -> {hk1}', flush=True)
        ck(hk1, "the host's editor gave it a new species")
        # the guest draws the host from the host's bundle (its records under a content key, a new key for a
        # new version) or from the host's world package (the original key, the same records)
        bundle_key = lambda: field(guest.call('coop.creations')['out'], r'player 0 creature: ([0-9A-F]{8})/')
        world_key = host_key_on_guest(guest, hk1) if hk1 else None
        got = wait(lambda: (lambda p: p if p and p in (bundle_key(), world_key) else None)(puppet_species(guest, 0)), 60, 2)
        got = got or puppet_species(guest, 0)
        name_h = field(host.call('coop.creations')['out'], r"mine creature: [0-9A-F]{8} '([^']*)'")
        name_g = field(guest.call('coop.creations')['out'], r"player 0 creature: [0-9A-F/]+ '([^']*)'")
        print(f"  the guest's game draws the host as {got} (the host's bundle there {bundle_key()}, its world key {world_key}); "
              f"the host's creature '{name_h}', the guest's copy '{name_g}'", flush=True)
        print('  guest creations:', ' '.join(w for w in guest.call('coop.creations')['out'].split()
                                             if w.startswith(('bundles', 'swaps', 'registered', 'world_swaps'))), flush=True)
        # both cameras on the host's creature: the host walks next to the guest's
        gp = field(guest.call('coop.avatar')['out'], r'pos=\(([-\d.]+ [-\d.]+ [-\d.]+)\)')
        if gp:
            x, y, z = map(float, gp.split())
            host.call(f'coop.abilities goto {x + 3:.1f} {y:.1f} {z + 3:.1f}')
            time.sleep(4)
        shot(guest, 'editor_sync_host_on_guest.png')
        shot(host, 'editor_sync_host_self.png')
        ck(got and got in (bundle_key(), world_key), "the guest's game draws the host with its new species")
        ck(name_h and name_g == name_h, f"the creature the guest has for the host is the host's new one ('{name_g}' / '{name_h}')")
        # the body the guest's game builds for the puppet's species: the model record it finds for that key is the
        # host's new creature's (not a cached older version or another creation of the key)
        bem_h = field(host.call(f'coop.creations bem {hk1}')['out'], r'bem \S+ (\S+)') if hk1 else None
        bem_g = field(guest.call(f'coop.creations bem {got}')['out'], r'bem \S+ (\S+)') if got else None
        prof_g = guest.call(f'coop.creations speciesof {got}')['out'].strip() if got else ''
        print(f"  the body: the host's new creature bem {bem_h}, the guest's game for {got}: bem {bem_g} ({prof_g})", flush=True)
        ck(bem_h and bem_h != 'none' and bem_g == bem_h and f'bem {bem_h}' in prof_g,
           f"the guest's game builds the host's puppet from the host's new creature (bem {bem_g} / {bem_h}; {prof_g})")
    else:
        spent = (d_g0 or 0) - (d_exit or 0)
        print(f'  the guest spent {spent:.0f} DNA in its editor', flush=True)
        ck(spent >= 1, 'the part cost DNA in the guest\'s editor')
        t0 = time.time()
        samples = []
        while time.time() - t0 < 20:  # the host's totals keep coming (every 2 s)
            samples.append((dna(host), dna(guest)))
            time.sleep(2)
        d_h1, d_g1 = samples[-1]
        print(f'  DNA after: host {d_h0} -> {d_h1}, guest {d_g0} -> {d_g1} (samples {samples})', flush=True)
        ck(d_h1 is not None and d_h1 <= d_h0 - spent + 1, "the guest's spend left the party's total on the host's game")
        ck(d_g1 is not None and d_g1 <= d_g0 - spent + 1, "the guest's game kept its spend (not back to the DNA before the editor)")
        ck(d_h1 is not None and d_g1 is not None and abs(d_h1 - d_g1) < 1, 'both games show the same DNA')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
