#!/usr/bin/env python3
"""WP-A (BUG-1474, BUG-0793): shared Galactic Adventures run (adventure.shared) and per-captain puppet scale
(adventure.captain_scale) in the tutorial adventure.

1. Host and guest join at the galaxy menu, enter the tutorial adventure (shared forced place id 0ADF0E01), and close
   the intro card (play state 3).
2. Per-captain puppet scale (adventure.captain_scale): each game's partner puppet renders at 1.0x the partner's own
   captain scale (within 5 %). SPORE_FIX_OFF=adventure.captain_scale renders at 1.15x -> FAIL.
3. Shared goals & act advance (adventure.shared):
   - guest credits goal slot 0 (`coop.adventure credit 0`): host's own goal count / act reflects it;
   - host jumps to act 1 (`coop.adventure act 1`): guest's own act advances to 1 automatically (no act command sent to
     guest). SPORE_FIX_OFF=adventure.shared leaves guest in act 0 -> FAIL.
4. Host-decided outcome (adventure.shared):
   - guest triggers local failure (`coop.adventure state 6`): guest's own play state stays 3 (held while host plays);
   - host triggers victory (`coop.adventure state 5`): guest's own play state becomes 5.
   SPORE_FIX_OFF=adventure.shared lets guest fail locally at 6 and ignores host's 5 -> FAIL.

  SPORE_LANE=N python3 tools/tests/coop_adventure_shared.py
"""
import os
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
import lane  # noqa: E402
from harness import wait_until  # noqa: E402
from sporectl import Console  # noqa: E402
from twoinst import Checks, keyframe, wait_up  # noqa: E402


def safe_call(c, line):
    try:
        return c.call(line)['out'].strip()
    except Exception as e:  # noqa: BLE001
        return f'no answer ({e.__class__.__name__})'


def mode_name(c):
    parts = safe_call(c, 'game.mode').split()
    return parts[1] if len(parts) > 1 else 'gone'


def adv_state_act(c):
    m = re.search(r'state=(-?\d+) act=(-?\d+)', safe_call(c, 'coop.adventure'))
    return (int(m.group(1)), int(m.group(2))) if m else None


def adv_goal0(c):
    m = re.search(r'goal slot=0 done=(\d+) count=(\d+)/(\d+)', safe_call(c, 'coop.adventure goals'))
    return (int(m.group(1)), int(m.group(2)), int(m.group(3))) if m else None


def adv_bodies(c):
    text = safe_call(c, 'coop.adventure bodies')
    av = re.search(r'avatar serial=(\d+) pol=([0-9A-F]+) scale=([0-9.]+)', text)
    pup = re.search(r'puppet id=(\d+) serial=(\d+) pol=([0-9A-F]+) scale=([0-9.]+)', text)
    return (
        (int(av.group(1)), av.group(2), float(av.group(3))) if av else None,
        (int(pup.group(1)), int(pup.group(2)), pup.group(3), float(pup.group(4))) if pup else None,
    )


def start_adventure(inst):
    return subprocess.Popen([sys.executable, os.path.join(HERE, 'adventure_start.py'), '-i', str(inst)])


def boot_into_tutorial(ck):
    lane.sh('kill')
    time.sleep(2)
    lane.sh('restore', 'creature')
    lane.sh('deploy')
    for f in os.listdir(lane.HOST_DIR):
        if f.endswith('.package'):
            shutil.copy2(os.path.join(lane.HOST_DIR, f), os.path.join(lane.GUEST_DIR, f))
    lane.run_guest()
    ck(wait_up(lane.GUEST, 180), 'guest starts')
    time.sleep(5)
    lane.run_host()
    ck(wait_up(lane.HOST, 180), 'host starts')
    h, g = Console(lane.HOST), Console(lane.GUEST)
    for c in (h, g):
        wait_until(lambda c=c: 'GalaxyGameEntry' in safe_call(c, 'game.mode'), 180, step=1.0)
        c.call('wait.frames 700 90000')
    for name in [n.strip() for n in os.environ.get('SPORE_FIX_OFF', '').split(',') if n.strip()]:
        for who, c in (('host', h), ('guest', g)):
            print(f'  SPORE_FIX_OFF ({who}): ' + safe_call(c, f'coop.fix {name} off'), flush=True)
    for who, c in (('host', h), ('guest', g)):
        print(f'  {who}: ' + safe_call(c, 'coop.adventure place 0ADF0E01'), flush=True)
    print('  host: ' + safe_call(h, lane.HOST_CMD), flush=True)
    print('  guest: ' + safe_call(g, f'cheat coop join 127.0.0.1:{lane.PORT} Bob'), flush=True)
    joined = wait_until(lambda: re.search(r'remote 1\b', safe_call(h, 'coop.status')) is not None, 60, step=1.0)
    ck(bool(joined), 'guest joined host at the galaxy menu')

    for order in (['host'], ['guest']):
        procs = [start_adventure(lane.HOST if w == 'host' else lane.GUEST) for w in order]
        for p in procs:
            p.wait(timeout=900)
        for w in order:
            c = h if w == 'host' else g
            if mode_name(c) == 'GalaxyGameEntry':
                start_adventure(lane.HOST if w == 'host' else lane.GUEST).wait(timeout=900)
    ck(mode_name(h) == 'Scenario' and mode_name(g) == 'Scenario',
       f'1. both games entered the tutorial adventure (host {mode_name(h)}, guest {mode_name(g)})')
    for who, c in (('host', h), ('guest', g)):
        for i in range(6):
            sa = adv_state_act(c)
            if sa and sa[0] == 3:
                break
            if i % 2 == 0:
                c.call('key enter')
            time.sleep(5)
        print(f'  {who}: state/act {adv_state_act(c)}', flush=True)
    ck(all((adv_state_act(c) or (0, 0))[0] == 3 for c in (h, g)), '1. both games playing (state 3)')
    return h, g


def main():
    ck = Checks()
    h, g = boot_into_tutorial(ck)
    if mode_name(h) != 'Scenario' or mode_name(g) != 'Scenario':
        lane.sh('kill')
        return ck.result()

    # Wait for partner captain puppets to appear on both games
    wait_until(lambda: adv_bodies(h)[1] is not None and adv_bodies(g)[1] is not None, 30, step=1.0, what='captain puppets')
    hav, hpup = adv_bodies(h)
    gav, gpup = adv_bodies(g)
    print(f'  host bodies: av={hav} pup={hpup}; guest bodies: av={gav} pup={gpup}', flush=True)
    ck(hav is not None and hpup is not None and gav is not None and gpup is not None,
       '2. both games have avatar and partner captain puppet')
    if hav and hpup and gav and gpup:
        hr = hpup[3] / gav[2] if gav[2] > 0 else 0.0
        gr = gpup[3] / hav[2] if hav[2] > 0 else 0.0
        ck(0.95 <= hr <= 1.05 and 0.95 <= gr <= 1.05,
           f'2. partner captain puppets render at true 1.0x scale (host ratio {hr:.3f}, guest ratio {gr:.3f})')

    # 3a. Guest credits goal 0 -> host reflects credit natively
    hg0_before = adv_goal0(h)
    print(f'  host goal0 before: {hg0_before}', flush=True)
    print('  guest credit 0: ' + safe_call(g, 'coop.adventure credit 0').replace('\n', ' | '), flush=True)
    goal_synced = wait_until(
        lambda: (adv_goal0(h) is not None and hg0_before is not None and adv_goal0(h)[1] > hg0_before[1]) or
                ((adv_state_act(h) or (0, 0))[1] > 0),
        15, step=1.0, what='host goal0 credit from guest',
    )
    print(f'  host goal0 after: {adv_goal0(h)}, host state/act: {adv_state_act(h)}', flush=True)
    ck(bool(goal_synced), '3a. guest goal credit forwarded and applied on host')

    # 3b. Host jumps to act 1 -> guest follows to act 1 automatically
    print('  host act 1: ' + safe_call(h, 'coop.adventure act 1').replace('\n', ' | '), flush=True)
    guest_act1 = wait_until(lambda: (adv_state_act(g) or (0, -1))[1] == 1, 25, step=1.0, what='guest follows to act 1')
    print(f'  acts after host act 1: host={adv_state_act(h)} guest={adv_state_act(g)}', flush=True)
    ck(bool(guest_act1), f'3b. guest followed host into act 1 automatically (host={adv_state_act(h)}, guest={adv_state_act(g)})')
    keyframe('shared_act1', 'WP-A shared adventure act 1 on host and guest', {'host': h, 'guest': g}, settle=3)

    # 4a. Guest local SetState(6) is held while host plays
    print('  guest state 6: ' + safe_call(g, 'coop.adventure state 6').replace('\n', ' | '), flush=True)
    time.sleep(2)
    gsa_after_fail = adv_state_act(g)
    ck(gsa_after_fail is not None and gsa_after_fail[0] == 3,
       f'4a. guest local SetState(6) held while host plays (guest={gsa_after_fail})')

    # 4b. Host wins (SetState 5) -> guest mirrors victory (state 5)
    print('  host state 5: ' + safe_call(h, 'coop.adventure state 5').replace('\n', ' | '), flush=True)
    guest_won = wait_until(lambda: (adv_state_act(g) or (0, 0))[0] == 5, 15, step=1.0, what='guest mirrors host victory')
    ck(bool(guest_won), f'4b. guest mirrored host victory state 5 (host={adv_state_act(h)}, guest={adv_state_act(g)})')

    alive = {k: c for k, c in (('host', h), ('guest', g)) if mode_name(c) != 'gone'}
    ck(len(alive) == 2, f'5. both games still running ({", ".join(alive) or "none"})')
    ck.faults(**alive)
    lane.sh('kill')
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
