#!/usr/bin/env python3
"""The host in an editor while the guest plays (BUG-0205).

After an evolution vote every player evolves in its own editor; the guest who leaves its editor
first plays on while the host still edits. The host sends no NPC states meanwhile, and until
BUG-0205 the guest counted the host "away" only in the main menu: the host's NPCs stood frozen on
the guest (proxies without AI and without states) for as long as the host edited. A guest's goal
card reward (MSG_SOCIAL K_GOAL) that reached the host in its editor (no creature) was dropped.

1. The host gives one species of the planet the "allied" relation (`coop.social record`), both
   evolve (`coop.vote editor`, the guest's real "I'm ready"), the guest leaves its editor at once.
2. The guest plays on (printed: what its game has and moves, see 5).
3. The guest reports a goal reward of 25 DNA for that species while the host is in its editor.
4. The host leaves its editor: its game DNA grew by the 25, the guest mirrors the host's NPCs again.
5. The host loads its world again (`coop.load`) while the guest watches its creatures (printed).
Steps 2 and 5 are printed, not checked: after its own editor the guest's planet held no creatures to
watch, and the reload was shorter than the away delay (BUG-0205 History). The check is step 4.

--nofix: `coop.fix core.away_goal off` on both (the goal part of BUG-0205; its away part's switch was retired in
0.7.15, FAIL run 20261101-145308 on record: not verified that the goal part alone fails).

  SPORE_LANE=7 python3 tools/tests/coop_away_editor.py [--nofix]
"""
import argparse
import re
import time

from harness import dna, mode
import census
from coop_ui import Ui
from twoinst import Checks, lane_state, shot, start

DONE = (1416, 1062)  # the green "done" check of the creature editor (1440x1080 test window)


def watch(guest, when, n=4, every=5.0):
    """the game's own positions of the guest's creatures (not the avatar, not a player's puppet):
    dumps `every` s apart; (moved > 1 m from the first dump, creatures seen) at the best dump"""
    dumps = [census.dump(guest)]
    for _ in range(n):
        time.sleep(every)
        dumps.append(census.dump(guest))
    best = []
    for later in dumps[1:]:
        ds = [d for _, d in census.moved(dumps[0], later, own=('local', 'proxy'))]
        if len([d for d in ds if d > 1.0]) >= len([d for d in best if d > 1.0]):
            best = ds
    moving = [d for d in best if d > 1.0]
    last = dumps[-1]
    print(f'  guest {when}: {last.count("c", ("local",))} creatures of its own, {last.count("c", ("proxy",))} proxies; '
          f'moved > 1 m within {n * every:.0f} s: {len(moving)} of {len(best)}')
    return len(moving), len(best)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nofix', action='store_true')
    a = ap.parse_args()
    host, guest = start('creature')
    ck = Checks()
    gu = Ui(guest, 'guest')
    if a.nofix:
        for c in (host, guest):
            c.call('coop.fix core.away_goal off')
    time.sleep(8)
    # 1. a species of the planet the host's table calls allied (relation 6)
    sp = re.findall(r'species ([0-9A-F]{8})/2B978C46/40626200 relation=(-?\d+)', host.call('coop.social species')['out'])
    npcs = [r['key'] for r in census.dump(host).rows if r['own'] == 'npc']
    key = sp[0][0] if sp else (npcs[0] if npcs else None)
    ck(key is not None, f'a species of the planet on the host ({len(sp)} records, {len(npcs)} NPCs)')
    if key:
        print(' ', host.call(f'coop.social record {key} 6 0')['out'].strip(), key)
    d0 = dna(host)
    m0 = mode(host)
    print(' ', host.call('coop.vote editor')['out'].strip().splitlines()[0])
    time.sleep(2)
    gu.wait(lambda s: s['ready']['vis'], 10, 'ready button')
    gu.click('ready')
    t0 = time.time()
    while time.time() - t0 < 90 and (mode(host) == m0 or mode(guest) == m0):
        time.sleep(1)
    ck(mode(host) != m0 and mode(guest) != m0, f'both are in their editors ({mode(host)}, {mode(guest)})')
    guest.call('wait.frames 200 60000')
    guest.call('click %d %d' % DONE)
    t0 = time.time()
    while time.time() - t0 < 60 and mode(guest) != m0:
        time.sleep(1)
    ck(mode(guest) == m0 and mode(host) != m0, f'the guest plays again, the host is still in its editor ({mode(host)})')
    t_back = time.time()
    # 2. the guest's game moves its creatures again
    while time.time() - t_back < 20 and 'host_away=1' not in guest.call('coop.status')['out']:
        time.sleep(1)
    time.sleep(3)
    print('  guest:', [ln for ln in guest.call('coop.npc')['out'].splitlines() if 'host_away' in ln][:1])
    print('  host avatar in its editor:', host.call('coop.avatar')['ok'])
    m_ed, n_ed = watch(guest, 'while the host edits')
    shot(guest, 'away_editor_guest.png')
    # 3. a goal reward reaches the host in its editor
    if key:
        print(' ', guest.call(f'coop.social goal {key} 6 25')['out'].strip())
    time.sleep(3)
    print('  host:', [ln for ln in host.call('coop.social')['out'].splitlines() if 'goal' in ln][:1])
    # 4. the host leaves its editor
    host.call('wait.frames 200 60000')
    host.call('click %d %d' % DONE)
    t0 = time.time()
    while time.time() - t0 < 90 and not (mode(host) == m0 and host.call('coop.avatar')['ok']):
        time.sleep(1)
    ck(mode(host) == m0, 'the host is back in the stage')
    t0 = time.time()
    back = None
    while time.time() - t0 < 150:
        if guest.call('coop.avatar')['ok'] and 'host_away=0' in guest.call('coop.status')['out'] and \
                'together 1 1' in host.call('coop.status')['out'] and \
                census.dump(guest).count('c', ('proxy',)) > 0:
            back = time.time() - t0
            break
        time.sleep(2)
    ck(back is not None, f'the guest mirrors the host\'s NPCs again ({back and round(back)} s)')
    time.sleep(5)
    d1 = dna(host)
    print(f'  host DNA {d0} -> {d1};', [ln for ln in host.call('coop.social')['out'].splitlines() if 'goal' in ln][:1])
    ck(d0 is not None and d1 is not None and abs((d1 - d0) - 25) < 0.6,
       f'the goal reward sent while the host edited is paid when it is back ({d0} -> {d1}, +25 expected)')
    # 5. the host loads its world again: the guest plays on meanwhile (with its own creatures; the
    # host's proxies frozen in place before BUG-0205)
    time.sleep(10)
    lst = lane_state()
    slot, name = lst.get('planet', 'Game0/Scalinus').split('/')
    if lst.get('slot', 9) != 9:
        slot = f'Game{lst["slot"]}'
    print(' ', host.call(f'coop.load {slot} {name}')['out'].strip())
    t0 = time.time()
    while time.time() - t0 < 30 and 'Load' not in mode(host):
        time.sleep(0.5)
    time.sleep(5)
    loading = 'Load' in mode(host)
    m_ld, n_ld = watch(guest, 'while the host loads', n=3, every=3.0)
    print(f'  host still loading after the watch: {"Load" in mode(host)} ({mode(host)})')
    # printed only: on lane 7 the reload took < 5 s (shorter than the 4 s away delay plus the watch),
    # and after its own editor the guest had no creatures to watch (BUG-0205 History)
    print(f'  (not checked) host loading during the watch: {loading}; moved {m_ld} of {n_ld}, '
          f'while it edited {m_ed} of {n_ed}')
    t0 = time.time()
    while time.time() - t0 < 150 and not (host.call('coop.avatar')['ok'] and 'host_away=0' in guest.call('coop.status')['out']):
        time.sleep(2)
    time.sleep(8)
    hc, gc = census.dump(host), census.dump(guest)
    probs = census.diff(hc, gc, pos_m=30)
    print(f'  census after: host {hc.count("c", ("npc",))} NPCs, guest {gc.count("c", ("proxy",))} proxies, '
          f'{len(probs)} differences {probs[:5]}')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
