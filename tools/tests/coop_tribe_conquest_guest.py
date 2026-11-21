#!/usr/bin/env python3
"""Tribe: a village the host's game conquers is conquered in the guest's game too (BUG-0777).

Discord 2026-11-09 ("their settlement is captured, the host's is not"): the conquest (the hut's fall, the village's
conquered flag, the goal mission's end) ran only in the game that saw the hut fall; the guest's realign kept the
guest's copy of the hut at 1 hp at least, so the guest's game never conquered the village.

1. Two games on the tribe fixture; the host's game forms an NPC tribe 45 m away (`coop.tribe npc_tribe`).
2. The guest's members and one of the host's raid its hut (`coop.tribe order <id> 15 <hut>`, the raid click's
   DoAction); once they hit it the hut's health is cut to 40 (`coop.tribe huthp`); the raid ends the tribe on the
   host (the game's own blows, the hut's state 2).
3. Within 30 s of the host's game conquering the village, the guest's game has the same: the village's own
   conquered flag (cTribe +0x556, `coop.realign planet village` fl bit 2) and its hut dead or gone.
   SPORE_FIX_OFF=realign.hut_fall: the guest's village stands unconquered with its hut: FAIL.
4. (BUG-0983) the guest's conquest caption closes itself; then the guest's game runs again (no cinematic, the game
   time not standing).
--click-at-close (suite tribe_caption_race, BUG-1101): the guest clicks the caption's check 7-9.6 s after it came up,
   about when the mod closes it after 8 s (a player who reads it for 8 s; the campaign's driver clicks after 6-10 s): a click and the forced end
   in the same moment left the game time standing for the rest of the stage (Carol 30 min, Dave 16 min in
   campaign_tribe_noclick of the 0.7.14 final check). When the host's game plays no conquest caption either (the game
   left it out: 3 of 19 runs up to 2026-11-16, 0.7.14 too), the result is INCONCLUSIVE (BUG-1205).
   BUG-1377: the caption is the end of the village's cTribeToTribeMission (TribeToTribeMission_Complete), which the
   host's game makes from a player's raid click only; `coop.tribe order` (a DoAction) makes none, so the caption
   came only when the game had made the village's mission by itself (lab 20261120-133703: INCONCLUSIVE). The raid now
   starts with the guest's own raid order of its members (`coop.tribe act_obj 15`, the guest's order path: the host
   makes the village's mission for it, BUG-1068) and waits for the host's guest_missions count.
"""
import argparse
import re
import time

from coop_tribe_conquer_orders import HUT, huts
from coop_tribe_gather_stall import pos_of
from coop_tribe_tools import dist, members
from play_tribe import frame_point
from twoinst import Checks, keyframe, start


def planet(con, kind):
    """[(pos, st, fl)] of this game's whole-planet list of that kind"""
    out = []
    for ln in con.call(f'coop.realign planet {kind}')['out'].splitlines():
        m = re.match(r'pl \S+ \(([-\d.]+) ([-\d.]+) ([-\d.]+)\) st=(\d+) fl=(\d+)', ln)
        if m:
            out.append((tuple(float(m.group(i)) for i in (1, 2, 3)), int(m.group(4)), int(m.group(5))))
    return out


def near(rows, p, tol):
    best = min(rows, key=lambda r: dist(r[0], p), default=None)
    return best if best and dist(best[0], p) < tol else None


CAPTION_OK = (1410, 1048)  # the green check of a caption that waits for the player (coop_campaign.py)


def guest_missions(host):
    """the host's count of villages whose mission a guest's raid or gift made (tribe.guest_mission, BUG-1068)"""
    m = re.search(r'guest_missions=(\d+)', host.call('coop.status')['out'])
    return int(m.group(1)) if m else -1


def busy_of(con):
    m = re.search(r'^busy: (\S+)', con.call('coop.status')['out'], re.M)
    return m.group(1) if m else '-'


def click_at_close(guest, host):
    """BUG-1101: the guest's conquest caption comes up; clicks on its check from 7 to 9.6 s after (the mod's forced
    end comes at 8 s). (played on the guest, played on the host): BUG-1205, the host's game too plays no conquest
    caption now and then (3 of 19 runs up to 2026-11-16, 0.7.14 too); then the guest's has nothing to play"""
    t0, since, host_seen = time.time(), None, False
    while time.time() - t0 < 40 and since is None:
        if 'cinematic' in busy_of(guest):
            since = time.time()
        else:
            host_seen = host_seen or 'cinematic' in busy_of(host)
            time.sleep(0.15)
    if since is None:
        print(f'  the guest\'s game played no caption within 40 s of the fall (the host\'s game: '
              f'{"a caption" if host_seen else "none either"})', flush=True)
        return False, host_seen
    guest.call(f'move {CAPTION_OK[0]} {CAPTION_OK[1]}')
    while time.time() < since + 7.0:
        time.sleep(0.05)
    n = 0
    while time.time() < since + 9.6:
        guest.call(f'click {CAPTION_OK[0]} {CAPTION_OK[1]}')
        n += 1
        time.sleep(0.12)
    print(f'  the guest clicked the caption\'s check {n} times 7-9.6 s after it came up', flush=True)
    return True, True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--click-at-close', action='store_true')
    a = ap.parse_args()
    host, guest = start('tribe')
    ck = Checks()
    print('  guest: ' + guest.call('coop.pauses trace')['out'].strip(), flush=True)  # BUG-1101: who pauses
    time.sleep(5)
    mem = members(guest)
    own = sorted(i for i, o in mem.items() if o != 0)[:3]
    hown = sorted(i for i, o in mem.items() if o == 0)[-1:]
    if not ck(bool(own), f"the guest has members ({own}), the host's raider {hown}"):
        return ck.result()
    before = {h for h, _ in huts(host)}
    out = host.call('coop.tribe npc_tribe 3 45')['out'].strip()
    print('  host: ' + out, flush=True)
    m = re.search(r'at \(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', out)
    if not ck(bool(m), 'the host formed an NPC tribe'):
        return ck.result()
    tp = tuple(float(m.group(i)) for i in (1, 2, 3))
    time.sleep(3)
    new = sorted((dist(p, tp), h, p) for h, p in huts(host) if h not in before)
    if not ck(bool(new), f'the new tribe has a hut ({len(new)})'):
        return ck.result()
    _, hut, hp = new[0]
    t0 = time.time()
    while time.time() - t0 < 30 and not near(planet(guest, 'hut'), hp, 10):
        time.sleep(2)
    gh = near(planet(guest, 'hut'), hp, 10)
    gv = near(planet(guest, 'village'), tp, 40)
    print(f'  hut at {hp}; the guest\'s copy: hut {gh}, village {gv}', flush=True)
    ck(gh and gv, "the guest's game has the new village and its hut")
    raiders = own + hown
    if a.click_at_close:  # BUG-1377: the guest's raid order makes the village's mission on the host (BUG-1068)
        m0 = guest_missions(host)
        for mid in own:
            guest.call(f'coop.tribe select {mid} 1')
        print('  guest raid order: ' + guest.call('coop.tribe act_obj 15 %s %.2f %.2f %.2f' % ((HUT,) + hp))['out'].strip(),
              flush=True)
        t1 = time.time()
        while time.time() - t1 < 10 and guest_missions(host) <= m0:
            time.sleep(0.5)
        print(f'  host: guest_missions {m0} -> {guest_missions(host)} (the village\'s mission from the guest\'s raid)', flush=True)
    for mid in raiders:
        print(f'  raid #{mid}: ' + host.call('coop.tribe order %d 15 %.2f %.2f %.2f %s' % ((mid,) + hp + (HUT,)))['out'].strip(), flush=True)
    t0, cut, fell, last_hp, last_change = time.time(), False, None, None, time.time()
    while time.time() - t0 < 150:
        time.sleep(2)
        hs = host.call('coop.tribe huthp %.2f %.2f %.2f -' % hp)['out'].strip()
        mm = re.search(r'exact=([-\d.]+) state=(\d+)', hs)
        h, state = (float(mm.group(1)), int(mm.group(2))) if mm else (None, None)
        nearby = [mid for mid in raiders if pos_of(host, mid) and dist(pos_of(host, mid), hp) < 14]
        print(f'  {time.time() - t0:4.0f} s: {hs}; raiders near {nearby}', flush=True)
        if hs.startswith('no hut') or state == 2:
            fell = time.time() - t0
            break
        if h != last_hp:
            last_hp, last_change = h, time.time()
        elif cut and time.time() - last_change > 16:
            for mid in raiders:
                host.call('coop.tribe order %d 15 %.2f %.2f %.2f %s' % ((mid,) + hp + (HUT,)))
            last_change = time.time()
        if cut and h is not None and h > 10:
            cut = False  # the village mended it: cut again when raiders are at it
        if not cut and len(nearby) >= max(1, len(raiders) // 2):
            print('  cut: ' + host.call('coop.tribe huthp %.2f %.2f %.2f 2' % hp)['out'].strip(), flush=True)
            cut = True
    if not ck(fell is not None, f"the raid ended the hut in the host's game ({fell and round(fell)} s)"):
        ck.faults(host=host, guest=guest)
        return ck.result()
    no_caption = False
    if a.click_at_close:
        played, host_played = click_at_close(guest, host)
        # BUG-1205: no caption in the host's game either: the game left it out (the scenario did not happen), not the
        # mod; the conquest checks below still run, the result is INCONCLUSIVE when they pass
        no_caption = not played and not host_played
        if not no_caption:
            ck(played, "the guest's game played the conquest caption")
    hvil = gvil = ghut = None
    t0 = time.time()
    while time.time() - t0 < 30:
        hvil = near(planet(host, 'village'), tp, 40)
        gvil = near(planet(guest, 'village'), tp, 40)
        ghut = near(planet(guest, 'hut'), hp, 10)
        print(f'  {time.time() - t0:4.0f} s after the fall: host village {hvil} | guest village {gvil}, guest hut {ghut}', flush=True)
        if hvil and gvil and hvil[2] & 2 and gvil[2] & 2 and (not ghut or ghut[2] & 2):
            break
        time.sleep(2)
    print('  guest: ' + guest.call('coop.realign')['out'].strip().splitlines()[0], flush=True)
    ck(hvil and hvil[2] & 2, f"the host's game conquered the village ({hvil})")
    ck(gvil and gvil[2] & 2, f"the guest's game has the village conquered too ({gvil})")
    ck(not ghut or ghut[2] & 2, f"the guest's hut of that village is down ({ghut})")
    # BUG-0983: the guest's game plays the conquest caption too; it must not stand paused for the guest's click
    # (the host's closes after 8 s, BUG-0531; the conquest and the upgrade captions: ~16 s)
    t0, seen = time.time(), []
    while time.time() - t0 < 40:
        busy = busy_of(guest)
        seen.append(busy)
        if 'cinematic' not in busy and 'paused' not in busy and time.time() - t0 > 4:
            break
        time.sleep(2)
    print(f'  the guest\'s game after the conquest (busy, every 2 s): {seen}', flush=True)
    print('  guest: ' + guest.call('coop.pauses')['out'].strip(), flush=True)
    ck('cinematic' not in seen[-1], f"the guest's game is not left in the conquest caption ({time.time() - t0:.0f} s, "
                                    f"busy {seen[-1]})")
    # the game time runs again too: a reason of the conquest's pauses left counted froze the guest's world for the
    # rest of the stage (campaign_tribe_noclick of the 0.7.14 final check: Carol 30 min, Dave 16 min `busy=paused`)
    ck('paused' not in seen[-1], f"the guest's game time runs again after the conquest ({time.time() - t0:.0f} s, "
                                 f"busy {seen[-1]})")
    frame_point({'host': host, 'guest': guest}, hp)  # BUG-0981: the village in view, not the home village
    keyframe('conquered', 'the conquered village on both screens: the hut down, no village members standing')
    ck.faults(host=host, guest=guest)
    if no_caption:
        return ck.inconclusive("neither game played the conquest caption (the host's game left it out, BUG-1205)")
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
