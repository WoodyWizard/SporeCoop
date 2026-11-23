#!/usr/bin/env python3
"""Tribe: a member mirror the host removes between a guest's save and its end does not come back as an orphan or a
freed pointer (BUG-1522, CIV-1's bug BUG-1416 in Tribe).

While a guest's save is written the mod takes the mirrors it made out of every tribe's member (+0x340) and selectable
(+0x354) vector (BUG-0470) and puts them back at the save's end, on the next frame, after that frame's network
receive. A host state in that frame that no longer names a mirror removes it: the mirror was in no vector, so the
removal was refused and the mirror only forgotten (BUG-0844), and the save's end put it back: a member of the guest's
player tribe that no mirror is bound to (it stands there for good, the host's member gets a second mirror), or with
tribe.member_lifetime off a freed pointer in the vectors.

1. Two games on the tribe fixture. The host's game makes 3 more members (`coop.tribe spawn 3`): the guest spawns
   mirrors for them (the mirrors a save takes out).
2. The guest arms `coop.tribe savekill on` (tests: its next save's end first removes one taken mirror as guest_state's
   sweep does for a member the host no longer sends, and the host's states skip that member on the guest from then on,
   as if the host's game had removed it) and saves with the game's own save (`coop.save`).
3. PASS, 8 s later: the save's end ran with the removal
   (save_took killed=1), no entry of any tribe's vectors on the guest is a citizen the game's list no longer has
   (`coop.tribe tribelists` unlisted=0), the guest's player tribe has no more orphans than before (members no mirror
   is bound to) and no member more than the host's beyond the difference before (the game's member vectors of both),
   no fault for 15 s.
SPORE_FIX_OFF=tribe.save_took_listed must FAIL (an orphan more, one member more than the host's).

  SPORE_LANE=3 python3 tools/tests/coop_tribe_save_took.py
"""
import re
import time

from coop_creature_e84 import close_boxes
from twoinst import Checks, game_runs, keyframe, save_taken, start


def lists(con):
    """(player tribe members, its orphans, unlisted entries of every tribe, save_took dict, raw)"""
    out = con.call('coop.tribe tribelists')['out']
    members = orphans = unlisted = 0
    for m in re.finditer(r'tribe \d+( \(player\))? members=(\d+) unlisted=(\d+) selectable=\d+ unlisted=(\d+) orphans=(\d+)', out):
        unlisted += int(m.group(3)) + int(m.group(4))
        if m.group(1):
            members, orphans = int(m.group(2)), int(m.group(5))
    t = re.search(r'save_took: fix=(\d+) taken=(\d+) back=(\d+) gone=(\d+) returned=(\d+) cleared=(\d+) killed=(\d+) armed=(\d+)', out)
    took = dict(zip(('fix', 'taken', 'back', 'gone', 'returned', 'cleared', 'killed', 'armed'), map(int, t.groups()))) if t else {}
    return members, orphans, unlisted, took, out


def main():
    host, guest = start('tribe')
    ck = Checks()
    game_runs(guest, 90)
    print('  host:', host.call('coop.tribe spawn 3')['out'].strip()[:80], flush=True)
    time.sleep(8)
    hm, _, hu, _, _ = lists(host)
    gm, go, gu, _, _ = lists(guest)
    print(f'  before: host members {hm} (unlisted {hu}), guest members {gm} orphans {go} unlisted {gu}', flush=True)
    ck(gu == 0, f'before the save no tribe vector entry on the guest is a freed citizen ({gu})')
    print('  guest:', guest.call('coop.tribe savekill on')['out'].strip(), flush=True)
    said, _, _ = save_taken(guest, 30, 'guest save')
    ck(said.startswith('saved (mode'), f'the guest saves its game ({said[:80]})')
    close_boxes(guest, 10)
    time.sleep(8)
    hm2, _, _, _, _ = lists(host)
    gm2, go2, gu2, took, out = lists(guest)
    print(f'  after: host members {hm2}, guest members {gm2} orphans {go2} unlisted {gu2}; {took}', flush=True)
    print('   ', ' | '.join(out.strip().splitlines())[:400], flush=True)
    if took.get('killed', 0) < 1:
        ck.faults(host=host, guest=guest)
        return ck.inconclusive(f'the save took no member mirror out, nothing removed in the window ({took})')
    ck(took.get('armed') == 0, f'the save\'s end ran with a taken mirror removed first ({took})')
    ck(gu2 == 0, f'no tribe vector entry on the guest is a citizen the game removed (unlisted {gu2})')
    ck(go2 <= go, f'no member of the guest\'s player tribe is left without its mirror (orphans {go} -> {go2})')
    ck(gm2 - hm2 <= gm - hm, f'the guest\'s player tribe has no member more than the host\'s (guest {gm} -> {gm2}, host '
       f'{hm} -> {hm2})')
    keyframe('after_save', 'the guest after its save: the player tribe as on the host (no member standing apart, no '
             'double)', settle=0)
    t0 = time.time()
    while time.time() - t0 < 15:
        time.sleep(3)
        lists(guest)  # (the vectors walked again: a freed entry read here as by the tribe's update)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
