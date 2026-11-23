#!/usr/bin/env python3
"""A guest's co-op copy of the host's world that the guest played on in alone or as the host (BUG-1434).

A player report (2026-11-22, coop 0.7.15.5): a guest's received world is a copy of the host's whole galaxy, old
planets too, in a co-op slot (Games\\GameN with sporecoop.txt naming the host and the planet). After the first host
left, the players swapped roles: the new host's galaxy menu showed that copy, nothing said so, and it started an old
planet of the first host's galaxy instead of the world they had played. Its saves went into the copy, whose marker
still named the first host: the next share of the first host's world replaced that slot (the old folder deleted) or
tidied it into Games-sporecoop-backup.

--case keep (world.copy_own_save):
1. Creature fixture: the guest received the host's world into a co-op slot S and plays it.
2. The guest leaves the session and saves its game alone (`coop.save`): the game writes the planet file in S.
3. The guest joins again (the world goes into another slot, S is the one it plays; leaving S for the load, its game
   saves S once more) and the host shares once more (`coop.share`: S is no longer played, the old code took it as the
   slot of that host world).
4. PASS: S is still in the guest's Games folder and its planet file is the guest's save of step 3 (the same bytes),
   the guest's game loaded the host's world from another slot. SPORE_FIX_OFF=world.copy_own_save (copy_slot_keep_off): S is
   replaced by the share, the guest's save is gone: FAIL.

--case notice (world.copy_notice):
1. Creature fixture as above. The host leaves the session (the guest plays on in its copy S).
2. The guest hosts a session of its own from S.
3. PASS: a notification on the guest's screen (the game's UI: the co-op notification panel) says that it hosts the
   planet from its copy of Alice's world and names the planet the copy was made for. SPORE_FIX_OFF=world.copy_notice
   (copy_slot_notice_off): nothing is said: FAIL.

--case planets (0.7.17 WP-W1: world.share_planet, world.continue_coop):
1. Creature fixture. The host's slot gets two more planet files (copies of the played planet's save under other
   names: Oldworld.spo, Ancient.spo; the galaxy's own files do not list them), as a galaxy with older games.
2. The host shares (`coop.share`): the guest loads the host's world from a new co-op copy S.
3. PASS: S holds the played planet's file and the galaxy's files, none of the two old planets (the guest's disk), and
   the host's log line says two planets were left out. SPORE_FIX_OFF=world.share_planet (copy_slot_planets_off): S holds
   all three planets: FAIL.
4. Role swap: both leave the session; the guest goes to its galaxy menu (key frame: the galaxy menu of S, the
   session's planet with its save: `coop.menuhover list`), hosts a session of its own and its co-op window's greyed
   out "Send my world" reads "Continue <planet>": a click loads the session's planet from S (the guest's game: the
   Creature stage on that planet). The old host joins it and loads that world too (its game: the same planet, key
   frame). SPORE_FIX_OFF=world.continue_coop (copy_slot_continue_off): no Continue button: FAIL.
The two planet files are removed from the host's slot at the end (a warm reuse of the lane sees the fixture again).

  SPORE_LANE=4 python3 tools/tests/coop_copy_slot.py [--case keep|notice|planets]
"""
import argparse
import hashlib
import os
import re
import time

import lane
from coop_ui import Ui
from harness import waiter
from twoinst import Checks, keyframe, played_slot, save_taken, start, wait_guest_in

wait = waiter(step=1)


def copy_slot(guest):
    """('GameN', 'Planet') of the co-op copy the guest's save database points to, (None, None) without one"""
    m = re.search(r"slot \S*\\(Game\d+): a copy of (\S+)'s world, made for (.+?)(, your own save)? \(own marks",
                  guest.call('coop.status')['out'])
    return (m.group(1), m.group(3)) if m else (None, None)


def digest(path):
    try:
        with open(path, 'rb') as f:
            return hashlib.sha256(f.read()).hexdigest()
    except OSError:
        return None


def played_from(guest):
    """the slot folder name the guest's last load of a received world came from ('' if none in the status)"""
    s, _ = copy_slot(guest)
    return s or ''


def keep_case(host, guest, ck):
    slot, planet = copy_slot(guest)
    if not ck(slot, f"the guest plays the host's world from a co-op copy ({slot}, made for {planet})"):
        return
    spo = os.path.join(lane.GUEST_DIR, 'Games', slot, planet + '.spo')
    guest.call('coop.leave')
    time.sleep(3)
    t0 = time.time()
    said, _, _ = save_taken(guest, label='guest save (alone)')
    ck(said.startswith('saved (mode'), f'the guest saves its game alone ({said[:80]})')
    ck(wait(lambda: os.path.exists(spo) and os.path.getmtime(spo) >= t0 - 1, 30), f'the game wrote {slot}/{planet}.spo')
    guest.call('key enter')  # the "game saved" box
    time.sleep(4)  # the mod's slot watch (once a second) sees the save
    print('  guest:', [ln for ln in guest.call('coop.status')['out'].splitlines() if ln.startswith('slot ')], flush=True)
    print('  guest joins again:', guest.call(lane.JOIN)['out'].strip()[:120], flush=True)
    wait(lambda: not guest.call('coop.avatar')['ok'], 40)
    ck(wait_guest_in(host, guest), "the guest is in the host's world again (share 1)")
    s1 = played_from(guest)
    print(f'  share 1: the guest plays {s1}', flush=True)
    time.sleep(5)
    # (the guest's game saved S once more when it left it for share 1's load: S now holds that save)
    mine = digest(spo)
    print('  host:', host.call('coop.share')['out'].strip()[:120], flush=True)
    wait(lambda: not guest.call('coop.avatar')['ok'], 60)
    ck(wait_guest_in(host, guest), "the guest loaded the host's world again (share 2)")
    s2 = played_from(guest)
    print(f'  share 2: the guest plays {s2}', flush=True)
    now = digest(spo)
    ck(mine is not None and now == mine,
       f"the guest's own save in {slot} is still there, the same bytes ({(mine or '')[:12]} -> {(now or 'missing')[:12]})")
    ck(s2 and s2 != slot, f"the host's world went into another slot ({s2}, not {slot})")
    keyframe('copy_keep', "Creature after two shares: both play the host's world; the guest's own save of the copy "
             "is kept in its slot")


def notice_case(host, guest, ck):
    slot, planet = copy_slot(guest)
    if not ck(slot, f"the guest plays the host's world from a co-op copy ({slot}, made for {planet})"):
        return
    host.call('coop.leave')
    time.sleep(3)
    guest.call('coop.leave')
    time.sleep(2)
    hosted = False
    for _ in range(3):  # (the port of the session that just ended may be held for a moment, BUG-0302)
        out = (guest.call(f'cheat coop host {lane.PORT} Bob').get('out') or '').strip()
        print('  guest hosts:', out[:120], flush=True)
        hosted = 'host failed' not in out
        if hosted:
            break
        time.sleep(3)
    if not ck(hosted, 'the guest hosts a session of its own'):
        return
    want = re.compile(r"from your copy of Alice's world \(" + re.escape(slot) + r"\).*" + re.escape(planet))
    got = None
    t0 = time.time()
    while time.time() - t0 < 12 and not got:
        for ln in guest.call('coop.notify')['out'].splitlines():
            if ln.startswith('toast') and want.search(ln):
                got = ln
        time.sleep(0.3)
    print('  guest notification:', got, flush=True)
    ck(got, f"a notification on the new host's screen: it hosts {planet} from its copy of Alice's world ({slot}), "
       f"made for {planet}")
    keyframe('copy_notice', f"Creature on the guest, now the host: a notification says it hosts {planet} from its "
             "copy of Alice's world", players={'guest': guest}, settle=0)
    guest.call('coop.leave')


OLD_PLANETS = ('Oldworld.spo', 'Ancient.spo')


def world_of(con):
    """the planet file ('Name.spo') the game plays, '' without an avatar"""
    m = re.search(r"world '([^']*)'", con.call('coop.avatar').get('out') or '')
    return m.group(1) if m else ''


def wait_mode(con, name, secs):
    return wait(lambda: name in (con.call('game.mode').get('out') or ''), secs)


def host_again(con, name):
    for _ in range(3):  # (the port of the session that just ended may be held for a moment, BUG-0302)
        out = (con.call(f'cheat coop host {lane.PORT} {name}').get('out') or '').strip()
        print(f'  {name} hosts:', out[:120], flush=True)
        if 'host failed' not in out:
            return True
        time.sleep(3)
    return False


def planets_case(host, guest, ck):
    hslot, hplanet = played_slot()
    if not ck(hslot and hplanet, f"the host plays {hslot}/{hplanet}"):
        return
    hdir = os.path.join(lane.HOST_DIR, 'Games', hslot)
    played = hplanet if hplanet.lower().endswith('.spo') else hplanet + '.spo'
    src = os.path.join(hdir, played)
    made = []
    try:
        for name in OLD_PLANETS:  # older games of the host's galaxy (files only: the galaxy's files do not list them)
            with open(src, 'rb') as f, open(os.path.join(hdir, name), 'wb') as g:
                g.write(f.read())
            made.append(os.path.join(hdir, name))
        print('  host slot:', sorted(os.listdir(hdir)), flush=True)
        before, _ = copy_slot(guest)
        print('  host:', host.call('coop.share')['out'].strip()[:120], flush=True)
        wait(lambda: not guest.call('coop.avatar')['ok'], 60)
        ck(wait_guest_in(host, guest), "the guest loaded the host's world again")
        slot, planet = copy_slot(guest)
        print(f'  the guest plays {slot} (made for {planet}; before {before})', flush=True)
        if not ck(slot, "the guest plays the host's world from a co-op copy"):
            return
        gdir = os.path.join(lane.GUEST_DIR, 'Games', slot)
        files = sorted(os.listdir(gdir))
        print('  guest copy:', files, flush=True)
        low = [f.lower() for f in files]
        ck(played.lower() in low, f"the guest's copy {slot} holds the played planet {played}")
        old = [n for n in OLD_PLANETS if n.lower() in low]
        ck(not old, f"the guest's copy {slot} holds none of the host's older planets ({old or 'none'})")
        ck('stars.db' in low and 'ggeuserdata.dat' in low, "the guest's copy holds the galaxy's own files")
        st = [ln for ln in host.call('coop.status')['out'].splitlines() if ln.startswith('shares of the played planet')]
        print('  host:', st, flush=True)
        keyframe('copy_planets', f"Creature after the share: both play {planet}; the guest's copy {slot} holds that "
                 "planet only")
        # ---- the role swap: the guest continues the session's world as the new host
        host.call('coop.leave')
        time.sleep(3)
        guest.call('coop.leave')
        time.sleep(2)
        print('  guest:', guest.call('coop.menu')['out'].strip()[:120], flush=True)
        if not ck(wait_mode(guest, 'GalaxyGameEntry', 90), "the guest is in its galaxy menu"):
            return
        guest.call('wait.frames 240 60000')
        menu = guest.call('coop.menuhover list')['out']
        print('  guest menu:', menu.strip().replace('\n', ' | ')[:600], flush=True)
        stem = played[:-4]
        ck(re.search(r"name='" + re.escape(stem) + r"' saved=1", menu),
           f"the guest's galaxy menu lists {stem} with its save")
        keyframe('copy_planets_menu', f"the guest's galaxy menu (its copy {slot}): the planet {stem} with its save, "
                 "the host's older planets are not there", players={'guest': guest}, settle=0)
        if not ck(host_again(guest, 'Bob'), 'the guest hosts a session of its own'):
            return
        print('  guest:', guest.call('coop.continue')['out'].strip(), flush=True)
        gui = Ui(guest, 'guest')
        guest.call('coop.uistate 1')
        want = 'Continue ' + stem[:13]
        st = gui.wait(lambda x: x['page'] == 'lobby' and x['share']['vis'] and x['share']['text'].startswith(want), 20,
                      'the Continue button')
        ck(st, f"the new host's co-op window offers \"{want}\" ({(st or {}).get('share', {}).get('text')})")
        if not st:
            return
        gui.click('share')
        guest.call('coop.uistate 0')
        ok = wait(lambda: world_of(guest).lower() == played.lower(), 120)
        ck(ok and wait_mode(guest, 'Creature', 30),
           f"the new host plays the session's world {played} again ({world_of(guest) or 'nothing'})")
        print('  old host joins:', host.call(f'cheat coop join 127.0.0.1:{lane.PORT} Alice')['out'].strip()[:120],
              flush=True)
        wait(lambda: not host.call('coop.avatar')['ok'], 60)
        ok = wait(lambda: world_of(host).lower() == played.lower(), 240)
        ck(ok, f"the old host, now a guest, plays {played} too ({world_of(host) or 'nothing'})")
        keyframe('copy_planets_swap', f"Creature after the role swap: the new host continued {stem}, the old host "
                 "joined it: both in that world")
        host.call('coop.leave')
        guest.call('coop.leave')
    finally:
        for p in made:
            try:
                os.remove(p)
            except OSError:
                pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--case', choices=('keep', 'notice', 'planets'), default='keep')
    a = ap.parse_args()
    host, guest = start('creature')
    ck = Checks()
    {'keep': keep_case, 'notice': notice_case, 'planets': planets_case}[a.case](host, guest, ck)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
