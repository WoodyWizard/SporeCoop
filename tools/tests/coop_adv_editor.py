#!/usr/bin/env python3
"""WP-AE (BUG-0863): Shared Adventure Editor (adventure.shared_editor) on two real Spore instances.

1. Host and guest join at the galaxy menu, enter the tutorial adventure under shared place id 0ADF0E01, and
   transition into the native Adventure Editor (`coop.adveditor enter`, cScenarioMode +0xCC == 1).
2. Host places a marker (`coop.adveditor create auto <x> <y> <z> 1.5`): guest's native cScenarioResource marker
   pool receives the real marker at the matching position.
3. Guest moves that marker (`coop.adveditor move <uid> <x2> <y2> <z2> 2.0`, acquiring its lease): host's native
   marker moves to match (x2, y2, z2).
4. Guest runs Undo (`coop.adveditor undo`): the marker reverts to (x, y, z) natively on BOTH host and guest.
5. Host deletes the marker (`coop.adveditor delete <uid>`): removed natively on BOTH host and guest.
6. 0 faults on both instances. SPORE_FIX_OFF=adventure.shared_editor leaves guest unsynced -> FAIL.

  SPORE_LANE=6 python3 tools/tests/coop_adv_editor.py
"""
import math
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


def editor_status(c):
    out = safe_call(c, 'coop.adveditor status')
    m = re.search(
        r'sub=(\d+)\((\w+)\).*?synced=(\d+).*?rev=(\d+)\s+native_markers=(\d+)\s+draft_markers=(\d+)\s+acts=(\d+)',
        out,
    )
    if not m:
        return None
    return {
        'sub': int(m.group(1)),
        'sub_name': m.group(2),
        'synced': int(m.group(3)),
        'rev': int(m.group(4)),
        'native_markers': int(m.group(5)),
        'draft_markers': int(m.group(6)),
        'acts': int(m.group(7)),
        'raw': out,
    }


def list_markers(c):
    out = safe_call(c, 'coop.adveditor markers')
    markers = []
    for m in re.finditer(
        r'uid=(\d+)\s+slot=(\d+)\s+class=(-?\d+)\s+key=([0-9A-F:]+)\s+pos=\(([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)\)\s+scale=([-\d.]+)\s+lease=(-?\d+)',
        out,
    ):
        markers.append({
            'uid': int(m.group(1)),
            'slot': int(m.group(2)),
            'class_id': int(m.group(3)),
            'key': m.group(4),
            'pos': (float(m.group(5)), float(m.group(6)), float(m.group(7))),
            'scale': float(m.group(8)),
            'lease': int(m.group(9)),
        })
    return markers


def dist3(a, b):
    return math.sqrt((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2)


def find_marker_near(markers, target, tol=0.35):
    for mk in markers:
        if dist3(mk['pos'], target) <= tol:
            return mk
    return None


def find_marker_uid(markers, uid):
    for mk in markers:
        if mk['uid'] == uid:
            return mk
    return None


def start_adventure(inst):
    return subprocess.Popen([sys.executable, os.path.join(HERE, 'adventure_start.py'), '-i', str(inst)])


def boot_into_editor(ck):
    lane.sh('kill')
    time.sleep(2)
    lane.sh('fixture', 'restore', 'creature', lane.HOST_PROFILE)
    lane.sh('fixture', 'restore', 'creature', lane.GUEST_PROFILE)
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

    # Launch both games directly from the Galaxy Menu into the native Adventure Editor on stock adventure F63B6851
    for who, c in (('host', h), ('guest', g)):
        print(f'  {who} enter editor: ' + safe_call(c, 'coop.adveditor enter F63B6851 408A0000'), flush=True)
    in_edit = wait_until(
        lambda: mode_name(h) == 'Scenario' and mode_name(g) == 'Scenario'
                and (editor_status(h) or {}).get('sub') == 1 and (editor_status(g) or {}).get('sub') == 1,
        180, step=1.5, what='both games in Adventure Editor (sub=1)',
    )
    ck(bool(in_edit), f'1. both games entered Adventure Editor (host={editor_status(h)}, guest={editor_status(g)})')
    if in_edit:
        h.call('wait.frames 120 60000')
        g.call('wait.frames 120 60000')
    return h, g


def main():
    ck = Checks()
    h, g = boot_into_editor(ck)
    hs = editor_status(h)
    gs = editor_status(g)
    if not hs or not gs or hs['sub'] != 1 or gs['sub'] != 1:
        lane.sh('kill')
        return ck.result()

    # Wait briefly for guest snapshot sync (when fix is on, synced becomes 1 quickly; when off, stays 0)
    wait_until(lambda: (editor_status(g) or {}).get('synced') == 1, 15, step=1.0, what='guest editor snapshot sync')
    print(f'  host status: {(editor_status(h) or {}).get("raw", "")}', flush=True)
    print(f'  guest status: {(editor_status(g) or {}).get("raw", "")}', flush=True)

    h_before = list_markers(h)
    g_before = list_markers(g)
    print(f'  baseline native markers: host={len(h_before)} guest={len(g_before)}', flush=True)

    # Choose a placement coordinate near an existing marker on the adventure planet
    anchor = h_before[0]['pos'] if h_before else (0.0, 0.0, 200.0)
    p1 = (round(anchor[0] + 6.0, 2), round(anchor[1] + 4.0, 2), round(anchor[2] + 0.5, 2))
    p2 = (round(anchor[0] + 14.0, 2), round(anchor[1] - 3.0, 2), round(anchor[2] + 1.5, 2))

    # 2. Host places a marker -> guest native cScenarioResource receives the real marker at p1
    create_out = safe_call(h, f'coop.adveditor create auto {p1[0]:.2f} {p1[1]:.2f} {p1[2]:.2f} 1.50')
    print('  host create: ' + create_out.replace('\n', ' | '), flush=True)
    m_uid = re.search(r'created uid=(\d+)', create_out)
    uid = int(m_uid.group(1)) if m_uid else 0

    host_has_p1 = wait_until(
        lambda: find_marker_near(list_markers(h), p1) is not None,
        10, step=0.5, what='host native marker at p1',
    )
    guest_has_p1 = wait_until(
        lambda: find_marker_near(list_markers(g), p1) is not None,
        15, step=0.5, what='guest native marker at p1',
    )
    hm1 = find_marker_near(list_markers(h), p1)
    gm1 = find_marker_near(list_markers(g), p1)
    print(f'  after create: host_m={hm1} guest_m={gm1}', flush=True)
    ck(bool(host_has_p1) and bool(guest_has_p1),
       f'2. host marker creation replicated to guest native cScenarioData at {p1} (host={hm1}, guest={gm1})')

    if uid == 0 and gm1:
        uid = gm1['uid']

    # 3. Guest moves the marker (acquiring lease) -> host native marker moves to p2
    move_target_uid = uid if uid > 0 else (gm1['slot'] if gm1 else 0)
    move_out = safe_call(g, f'coop.adveditor move {move_target_uid} {p2[0]:.2f} {p2[1]:.2f} {p2[2]:.2f} 2.00')
    print('  guest move: ' + move_out.replace('\n', ' | '), flush=True)
    both_at_p2 = wait_until(
        lambda: find_marker_near(list_markers(h), p2) is not None and find_marker_near(list_markers(g), p2) is not None,
        15, step=0.5, what='both games native marker at p2',
    )
    hm2 = find_marker_near(list_markers(h), p2)
    gm2 = find_marker_near(list_markers(g), p2)
    print(f'  after guest move: host_m={hm2} guest_m={gm2}', flush=True)
    ck(bool(both_at_p2), f'3. guest move replicated to host native marker at {p2} (host={hm2}, guest={gm2})')
    keyframe('adv_editor_moved', 'WP-AE shared Adventure Editor marker moved by guest', {'host': h, 'guest': g}, settle=2)

    # 4. Guest runs Undo -> marker reverts to p1 on BOTH host and guest
    undo_out = safe_call(g, 'coop.adveditor undo')
    print('  guest undo: ' + undo_out.replace('\n', ' | '), flush=True)
    both_reverted = wait_until(
        lambda: find_marker_near(list_markers(h), p1) is not None and find_marker_near(list_markers(g), p1) is not None,
        15, step=0.5, what='both games marker reverted to p1 after guest undo',
    )
    hm3 = find_marker_near(list_markers(h), p1)
    gm3 = find_marker_near(list_markers(g), p1)
    print(f'  after guest undo: host_m={hm3} guest_m={gm3}', flush=True)
    ck(bool(both_reverted), f'4. guest Undo reverted marker to {p1} on both host and guest (host={hm3}, guest={gm3})')

    # 5a. While guest holds lease on uid, host delete is rejected (lease protection)
    del_uid = uid if uid > 0 else (hm3['slot'] if hm3 else 0)
    if uid > 0:
        locked_out = safe_call(h, f'coop.adveditor delete {del_uid}')
        print('  host delete while leased: ' + locked_out.replace('\n', ' | '), flush=True)
        ck('locked by another player' in locked_out and find_marker_uid(list_markers(h), uid) is not None,
           f'5a. host delete rejected while guest holds lease on uid={uid}')
        rel_out = safe_call(g, f'coop.adveditor lease {uid} release')
        print('  guest release lease: ' + rel_out.replace('\n', ' | '), flush=True)
        wait_until(
            lambda: (find_marker_uid(list_markers(h), uid) or {}).get('lease', 0) == -1,
            10, step=0.5, what='host lease cleared after guest release',
        )

    # 5b. Host deletes marker -> removed natively on BOTH host and guest
    del_out = safe_call(h, f'coop.adveditor delete {del_uid}')
    print('  host delete: ' + del_out.replace('\n', ' | '), flush=True)
    both_deleted = wait_until(
        lambda: (find_marker_uid(list_markers(h), uid) is None if uid > 0 else find_marker_near(list_markers(h), p1) is None)
                and (find_marker_uid(list_markers(g), uid) is None if uid > 0 else find_marker_near(list_markers(g), p1) is None),
        15, step=0.5, what='marker deleted on both host and guest',
    )
    h_after = list_markers(h)
    g_after = list_markers(g)
    print(f'  after delete: host={len(h_after)} guest={len(g_after)}', flush=True)
    ck(bool(both_deleted) and len(h_after) == len(h_before) and len(g_after) == len(g_before),
       f'5b. host DeleteMarker removed marker natively on both games (host {len(h_before)}->{len(h_after)}, guest {len(g_before)}->{len(g_after)})')

    # 6. Both games alive, 0 faults
    alive = {k: c for k, c in (('host', h), ('guest', g)) if mode_name(c) != 'gone'}
    ck(len(alive) == 2, f'6. both games still running ({", ".join(alive) or "none"})')
    ck.faults(**alive)
    lane.sh('kill')
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
