#!/usr/bin/env python3
"""BUG-0871: depth handle in the Creature editor moves eligible parts along their skin normal without crashing.

Two games on the creature fixture. The host opens the Creature editor via the evolution vote (`coop.vote editor` +
guest clicks ready) so the real 3D editor view is live. Inside the Creature editor:
- `coop.depth list` lists the model's rigblocks (`model_type=0x9EA3031A`) with eligibility and twin indices.
- Selecting a spine vertebra (`vert=1`) never shows the depth handle (`visible=0`).
- Selecting an eligible non-spine part (`elig=1`, preferring a symmetric part with `twin >= 0`) shows the depth handle
  (`visible=1`), with a finite unit outward `normal` and matching `pos` / `model_pos`.
- Moving along the depth handle (real mouse drag along `axis_screen` when projected with sufficient screen span, or
  `coop.depth nudge 0.08` fallback) moves `EditorRigblock +0x48` (`pos`) and `Graphics::Model +0x0C` (`model_pos`)
  straight along `normal` (lateral drift < 0.01), and moves its mirrored twin (`+0x3E4`) by the X-mirrored delta.
- Pressing Ctrl+Z undoes the committed move back to `pos0` (verifying `cEditor::CommitEditHistory`).
- Nudging again and leaving with Done saves and returns to Creature stage with zero faults.

FAIL twin SPORE_FIX_OFF=editor.depth: handle stays hidden (`visible=0`), drag/nudge does not move the part.

  SPORE_LANE=6 python3 tools/tests/coop_editor_depth.py
"""
import math
import re
import time

from coop_ui import Ui
from harness import mode, wait_until
from twoinst import Checks, keyframe, start

DONE = (1416, 1062)  # green "done" check of the creature editor (1440x1080)


def parse_vec3(text, name):
    m = re.search(rf'\b{name}=\((-?\d+\.\d+),(-?\d+\.\d+),(-?\d+\.\d+)\)', text)
    return (float(m.group(1)), float(m.group(2)), float(m.group(3))) if m else None


def parse_depth(con):
    text = con.call('coop.depth')['out']
    m_vis = re.search(r'\bvisible=(\d+)', text)
    m_type = re.search(r'\bmodel_type=0x([0-9A-Fa-f]+)', text)
    m_twin = re.search(r'\btwin=(\d+)', text)
    m_hs = re.search(r'\bhandle_screen=(\d+)\((-?\d+\.\d+),(-?\d+\.\d+)\)', text)
    m_ax = re.search(r'\baxis_screen=\((-?\d+\.\d+),(-?\d+\.\d+)\)', text)
    return dict(
        raw=text,
        visible=int(m_vis.group(1)) if m_vis else 0,
        model_type=m_type.group(1).upper() if m_type else '',
        pos=parse_vec3(text, 'pos'),
        model_pos=parse_vec3(text, 'model_pos'),
        normal=parse_vec3(text, 'normal'),
        twin=int(m_twin.group(1)) if m_twin else 0,
        twin_pos=parse_vec3(text, 'twin_pos'),
        projected=int(m_hs.group(1)) if m_hs else 0,
        hx=float(m_hs.group(2)) if m_hs else 0.0,
        hy=float(m_hs.group(3)) if m_hs else 0.0,
        ax=float(m_ax.group(1)) if m_ax else 0.0,
        ay=float(m_ax.group(2)) if m_ax else 0.0,
    )


def parse_list(con):
    text = con.call('coop.depth list')['out']
    rows = []
    for m in re.finditer(
        r'\[(\d+)\]\s+part=([0-9A-Fa-f]+)!([0-9A-Fa-f]+)\s+elig=(\d+)\s+vert=(\d+)\s+'
        r'pos=\((-?\d+\.\d+),(-?\d+\.\d+),(-?\d+\.\d+)\)\s+twin=(-?\d+)\s+screen=(\w+)',
        text,
    ):
        rows.append(dict(
            idx=int(m.group(1)),
            group=m.group(2).upper(),
            inst=m.group(3).upper(),
            elig=int(m.group(4)),
            vert=int(m.group(5)),
            pos=(float(m.group(6)), float(m.group(7)), float(m.group(8))),
            twin=int(m.group(9)),
            screen=m.group(10),
        ))
    return text, rows


def vsub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def vdot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def vlen(a):
    return math.sqrt(vdot(a, a))


def main():
    host, guest = start('creature')
    ck = Checks()
    gu = Ui(guest, 'guest')
    time.sleep(2)
    m0 = mode(host)

    print(' ', host.call('coop.vote editor')['out'].strip(), flush=True)
    time.sleep(2)
    gu.wait(lambda s: s['ready']['vis'], 15, 'ready button')
    gu.click('ready')

    entered = wait_until(lambda: mode(host) != m0 and mode(guest) != m0, 60, 1.0, 'both enter Creature editor')
    ck(entered, f'host entered Creature editor ({mode(host).strip()})')
    if not entered:
        ck.faults(host=host, guest=guest)
        return ck.result()

    host.call('wait.frames 150 60000')
    guest.call('wait.frames 150 60000')

    list_out, rows = parse_list(host)
    print(f'  rigblocks: {len(rows)}', flush=True)
    ck('model_type=0x9EA3031A' in list_out and len(rows) >= 3,
       f'Creature editor model loaded with rigblocks ({len(rows)})')

    vert = next((r for r in rows if r['vert'] == 1), None)
    elig_rows = [r for r in rows if r['elig'] == 1 and r['vert'] == 0]
    # Prefer a symmetric part projected on screen so both real drag and twin mirroring are tested
    elig = (
        next((r for r in elig_rows if r['twin'] >= 0 and r['screen'] == 'ok'), None)
        or next((r for r in elig_rows if r['twin'] >= 0), None)
        or (elig_rows[0] if elig_rows else None)
    )
    ck(vert is not None and elig is not None,
       f'found vertebra ({vert and vert["idx"]}) and eligible part ({elig and elig["idx"]}, twin={elig and elig["twin"]})')
    if not vert or not elig:
        ck.faults(host=host, guest=guest)
        return ck.result()

    # Spine vertebra must never get a depth handle
    host.call(f'coop.depth select {vert["idx"]}')
    host.call('wait.frames 8 5000')
    st_vert = parse_depth(host)
    ck(st_vert['visible'] == 0, f'spine vertebra has no depth handle (visible={st_vert["visible"]})')

    # Eligible part gets the depth handle
    host.call(f'coop.depth select {elig["idx"]}')
    host.call('wait.frames 15 10000')
    st0 = parse_depth(host)
    print('  before move:', st0['raw'].strip(), flush=True)
    ck(st0['visible'] == 1, f'eligible part shows depth handle (visible={st0["visible"]})')
    if st0['visible'] == 1:
        nlen = vlen(st0['normal']) if st0['normal'] else 0.0
        ck(abs(nlen - 1.0) < 0.02, f'skin normal is unit length ({nlen:.4f})')
        ck(st0['pos'] is not None and st0['model_pos'] is not None and vlen(vsub(st0['pos'], st0['model_pos'])) < 0.01,
           'EditorRigblock +0x48 matches Graphics::Model +0x0C before move')
        keyframe(host, 'depth_handle_visible')

    # Move along depth: try real mouse drag along screen axis first, fall back to nudge if drag is off-screen
    moved_by = 'none'
    st1 = st0
    if st0['visible'] == 1 and st0['projected'] == 1:
        ax_len = math.hypot(st0['ax'], st0['ay'])
        if ax_len >= 8.0 and 40.0 < st0['hx'] < 1400.0 and 40.0 < st0['hy'] < 1040.0:
            x1, y1 = int(round(st0['hx'])), int(round(st0['hy']))
            scale = min(1.0, 80.0 / ax_len)
            x2 = max(20, min(1420, int(round(st0['hx'] + st0['ax'] * scale))))
            y2 = max(20, min(1060, int(round(st0['hy'] + st0['ay'] * scale))))
            host.call(f'drag {x1} {y1} {x2} {y2}')
            host.call('wait.frames 15 10000')
            cand = parse_depth(host)
            if cand['pos'] and st0['pos'] and vlen(vsub(cand['pos'], st0['pos'])) >= 0.01:
                st1 = cand
                moved_by = f'drag ({x1},{y1})->({x2},{y2})'

    if moved_by == 'none':
        out_nudge = host.call('coop.depth nudge 0.08')['out'].strip()
        print('  nudge:', out_nudge, flush=True)
        host.call('wait.frames 12 10000')
        st1 = parse_depth(host)
        moved_by = 'nudge 0.08'

    print(f'  after {moved_by}:', st1['raw'].strip(), flush=True)
    delta = vsub(st1['pos'], st0['pos']) if (st1['pos'] and st0['pos']) else (0.0, 0.0, 0.0)
    mdelta = vsub(st1['model_pos'], st0['model_pos']) if (st1['model_pos'] and st0['model_pos']) else (0.0, 0.0, 0.0)
    along = vdot(delta, st0['normal']) if st0['normal'] else 0.0
    lateral = vlen((
        delta[0] - st0['normal'][0] * along,
        delta[1] - st0['normal'][1] * along,
        delta[2] - st0['normal'][2] * along,
    )) if st0['normal'] else 999.0

    ck(along >= 0.015 and lateral < 0.01,
       f'part moved along skin normal via {moved_by} (along={along:.4f}, lateral={lateral:.4f})')
    ck(vlen(vsub(delta, mdelta)) < 0.01,
       f'Graphics::Model +0x0C tracked EditorRigblock +0x48 (diff={vlen(vsub(delta, mdelta)):.4f})')

    if st0['twin'] == 1 and st0['twin_pos'] and st1['twin_pos']:
        tdelta = vsub(st1['twin_pos'], st0['twin_pos'])
        expected_twin = (-delta[0], delta[1], delta[2])
        err = vlen(vsub(tdelta, expected_twin))
        ck(err < 0.01, f'mirrored twin moved by X-mirrored delta (err={err:.4f})')

    keyframe(host, 'depth_after_move')

    # Ctrl+Z must undo the committed depth edit
    host.call('key ctrl down')
    host.call('key z')
    host.call('key ctrl up')
    host.call('wait.frames 25 10000')
    host.call(f'coop.depth select {elig["idx"]}')
    host.call('wait.frames 10 5000')
    st_undo = parse_depth(host)
    undo_err = vlen(vsub(st_undo['pos'], st0['pos'])) if (st_undo['pos'] and st0['pos']) else 999.0
    ck(undo_err < 0.015, f'Ctrl+Z undid the depth move back to original position (err={undo_err:.4f})')

    # Apply one more nudge and leave via Done to prove the saved creature does not crash
    host.call('coop.depth nudge 0.06')
    host.call('wait.frames 12 10000')
    guest.call('click %d %d' % DONE)
    host.call('click %d %d' % DONE)
    from coop_editor_sync import confirm_captions
    confirm_captions((host, guest), 20)
    back = wait_until(lambda: mode(host) == m0 and mode(guest) == m0, 90, 1.0, 'both return to Creature stage')
    ck(back, f'both games saved and left the editor cleanly ({mode(host).strip()})')

    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
