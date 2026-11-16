#!/usr/bin/env python3
"""The host's editor visit is not a world change: the animals on the guest's screen stay (BUG-1174, owner decision 2 of
docs/plan-0.7.15.md).

Creature fixture. Until 0.7.15 the host's evolution editor ended every NPC copy on the guest twice: 4 s into the host's
editor the guest counted the host away and its own game took the planet over (the copies destroyed, its own NPCs made
from them), and 2 s after the editor the host retired all its NPCs (no host pass ran meanwhile) and sent them again
under new ids: the guest's animals vanished, came back as others and were made a third time.

1. Both games on the creature fixture; the guest shows the host's NPCs (copies made: `coop.npc list` "spawned", the
   creature object of each and its id; a copy is listed only while its creature is in the guest's game's object list).
2. The host mates the players' way (coop_creature_mating.mate_click: the game's own mating request opens its
   editor at once, the guest stays in its Creature stage); when its game picked no mate in 20 s, the creature stage's
   own evolution request (`coop.vote evolve`) opens it.
3. While the host edits (16 s, past the 4 s away delay): the guest's copies are the same creatures under the same ids.
4. The host leaves its editor (done): 10 s later the guest still has those creatures under those ids, and the host's
   states reach them again (each copy's state age under 1.5 s).
PASS: at least 80 % of the copies of step 1 (and at least 3) are the same creature with the same id at the end of 3 and
of 4. SPORE_FIX_OFF=npc.editor_hold: destroyed in the editor (and new ids after it): FAIL.

  SPORE_LANE=N python3 tools/tests/coop_npc_editor_hold.py
"""
import re
import time

from coop_creature_mating import mate, mate_click
from twoinst import Checks, start

DONE = (1416, 1062)  # the creature editors' green check (1440x1080 test window)
PX = re.compile(r'#(\d+) species=[0-9A-F]+ spawned .*? age=([-\d.]+).*? ptr=([0-9A-Fa-fx]+)')


def mode(con):
    return con.call('game.mode')['out']


def in_editor(con):
    return 'Editor' in mode(con)


def wait(cond, secs, step=1.0):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return None


def proxies(guest):
    """{id: (creature pointer, state age s)} of the guest's copies with a creature in its game's list now"""
    return {int(m.group(1)): (m.group(3), float(m.group(2))) for m in PX.finditer(guest.call('coop.npc list')['out'])}


def kept(before, now):
    """ids of `before` that are the same creature in `now`"""
    return [i for i, (p, _) in before.items() if i in now and now[i][0] == p]


def main():
    host, guest = start('creature')
    ck = Checks()
    before = wait(lambda: (lambda d: d if len(d) >= 3 else None)(proxies(guest)), 40)
    ck(before, f'the guest shows the host\'s NPCs ({len(before or {})} copies made)')
    if not before:
        ck.faults(host=host, guest=guest)
        return ck.result()
    m0 = mode(host)
    print(f'  guest copies before: {sorted(before)}', flush=True)

    # 2. the host's editor, the players' way when its game picked a mate near the nest (else the creature stage's own
    # evolution request, as its handler builds it when the avatar meets its mate: `coop.vote evolve`); either way the
    # guest's game stays in its Creature stage (an Evolve now offer)
    mt = wait(lambda: (lambda m: m if m and m['ptr'] and m['flags'] & 0x10 else None)(mate(host)), 20)
    if mt:
        mate_click(host, ck)
    else:
        print('  no mate picked by the host\'s game in 20 s: ' + host.call('coop.vote evolve')['out'].strip()[:120], flush=True)
    ck(wait(lambda: in_editor(host), 120), f'the host is in its editor ({mode(host).strip()})')
    ck(not in_editor(guest), f'the guest\'s game stays in its stage ({mode(guest).strip()})')
    before = proxies(guest) or before  # (the copies right before: a few come and go while the host walks to its mate)

    # 3. while the host edits
    worst = None
    for _ in range(8):
        time.sleep(2)
        if not in_editor(host):
            break
        k = kept(before, proxies(guest))
        worst = k if worst is None or len(k) < len(worst) else worst
    st = guest.call('coop.npc status')['out']
    print('  guest:', ' '.join(l for l in st.splitlines() if 'rebound=' in l or 'npc(guest)' in l)[:400], flush=True)
    need = max(3, int(0.8 * len(before) + 0.999))
    ck(worst is not None and len(worst) >= need,
       f'while the host edits the guest still shows the same animals under the same ids '
       f'({len(worst or [])} of {len(before)} kept at the worst sample, {need} needed)')

    # 4. after the editor
    if in_editor(host):
        time.sleep(3)  # (the editor's interface comes up)
        host.call('click %d %d' % DONE)
    ck(wait(lambda: mode(host) == m0 and host.call('coop.avatar')['ok'], 90), f'the host is back in its stage ({mode(host).strip()})')
    time.sleep(10)
    after = proxies(guest)
    k = kept(before, after)
    fresh = [i for i in k if after[i][1] < 1.5]
    print(f'  guest copies after: {sorted(after)} (kept {sorted(k)})', flush=True)
    ck(len(k) >= need, f'after the host\'s editor the guest shows the same animals under the same ids ({len(k)} of '
       f'{len(before)}, {need} needed)')
    ck(len(fresh) >= need, f'the host\'s states reach them again ({len(fresh)} of the kept copies with a state under 1.5 s old)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
