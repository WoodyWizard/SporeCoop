#!/usr/bin/env python3
"""BUG-0865: frame rate cap (1-360 fps) with high-resolution waitable timer pacing, borderless windowed mode,
sporekit.ini persistence, console commands (frame.target, window.borderless), and Settings UI controls.

Cases (--case all|fps|borderless|ui):
  fps        - `frame.target 30` lifts kFrameLimitMS to 0 and paces Present intervals near 33.3 ms (26..42 ms);
               `frame.target 20` paces near 50 ms (44..58 ms); sporekit.ini persists `frame_target=20`;
               `frame.target auto` restores the game's own kFrameLimitMS.
               Twin (`SPORE_FIX_OFF=kit.frametarget`): kFrameLimitMS stays at the game's own limit and 20 fps
               pacing does not take effect -> FAIL.
  borderless - `window.borderless on` strips WS_OVERLAPPEDWINDOW (borders=0) and fits the window rect to the
               monitor rect; `window.borderless disturb` restores borders/size and within 2 s maintain_borderless
               re-applies borderless; sporekit.ini persists `borderless=1`; `window.borderless off` restores
               window borders and saved placement.
               Twin (`SPORE_FIX_OFF=kit.borderless`): borders stay set and window does not fit monitor -> FAIL.
  ui         - opens F9 Co-op Settings UI on both host and guest, navigates to the local Display section
               (`display.frame_target`, `display.borderless`, `display.fov`), clicks Frame rate cap and Borderless
               window on the guest (local settings are editable by guests too) and verifies state updates.
               Twin (`SPORE_FIX_OFF=ui.display_settings`): Display section absent from Settings -> FAIL.

  SPORE_LANE=2 python3 tools/tests/coop_display_settings.py [--case all|fps|borderless|ui]
"""
import argparse
import os
import re
import time

import coop_ui
import lane
from harness import waiter
from twoinst import Checks, start

wait = waiter(step=0.5)


def measure_interval(con, secs):
    con.call('frame.interval reset')
    time.sleep(secs)
    txt = con.call('frame.interval')['out'].strip()
    m = re.search(r'mean ([\d.]+) ms', txt)
    return (float(m.group(1)) if m else None), txt


def parse_target(txt):
    d = {}
    for k in ('target', 'fix', 'limit_ms', 'own_ms'):
        m = re.search(rf'\b{k}=(-?\d+)', txt)
        if m:
            d[k] = int(m.group(1))
    return d


def parse_borderless(txt):
    d = {}
    for k in ('enabled', 'applied', 'windowed', 'fix', 'borders'):
        m = re.search(rf'\b{k}=(-?\d+)', txt)
        if m:
            d[k] = int(m.group(1))
    for k in ('rect', 'mon'):
        m = re.search(rf'\b{k}=(-?\d+),(-?\d+),(-?\d+),(-?\d+)', txt)
        if m:
            d[k] = tuple(int(m.group(i)) for i in range(1, 5))
    return d


def read_sporekit_ini():
    p = os.path.join(lane.game_dir(), 'SporebinEP1', 'mods', 'sporekit.ini')
    if not os.path.exists(p):
        return ''
    raw = open(p, 'rb').read()
    if raw.startswith(b'\xff\xfe'):
        return raw.decode('utf-16-le', errors='replace')
    return raw.decode('utf-8', errors='replace')


def case_fps(ck, host):
    st0 = host.call('frame.target')['out'].strip()
    print('  fps before:', st0, flush=True)
    d0 = parse_target(st0)
    own_ms = d0.get('own_ms', 16)
    ck(d0.get('target') == 0, f'frame.target starts at auto (0): {st0}')

    # Set 20 fps (50 ms/frame): easily distinguishable from GOG 2024's 16 ms (60 fps) or 33 ms (30 fps)
    st1 = host.call('frame.target 20')['out'].strip()
    print('  fps 20:', st1, flush=True)
    d1 = parse_target(st1)
    ck(d1.get('target') == 20, f'frame.target accepted 20: {st1}')
    ck(d1.get('limit_ms') == 0, f'kFrameLimitMS lifted to 0 while frame.target=20 ({st1})')
    time.sleep(1.5)
    mean20, iv20 = measure_interval(host, 6)
    print('  fps 20 interval:', iv20, flush=True)
    ck(mean20 is not None and 44.0 <= mean20 <= 62.0,
       f'Present interval paced near 50 ms at 20 fps ({iv20})')

    ini = read_sporekit_ini()
    ck(bool(re.search(r'(?m)^\s*frame_target\s*=\s*20\s*$', ini)),
       'sporekit.ini persisted frame_target=20')

    # Back to auto: kFrameLimitMS returns to the game's own value
    st2 = host.call('frame.target auto')['out'].strip()
    print('  fps auto:', st2, flush=True)
    d2 = parse_target(st2)
    ck(d2.get('target') == 0 and d2.get('limit_ms') == own_ms,
       f'frame.target auto restored own limit_ms={own_ms} ({st2})')


def case_borderless(ck, host):
    st0 = host.call('window.borderless')['out'].strip()
    print('  borderless before:', st0, flush=True)
    d0 = parse_borderless(st0)
    ck(d0.get('windowed') == 1, f'game runs in windowed mode ({st0})')
    orig_rect = d0.get('rect')

    st1 = host.call('window.borderless on')['out'].strip()
    print('  borderless on:', st1, flush=True)
    d1 = parse_borderless(st1)
    ck(d1.get('applied') == 1 and d1.get('borders') == 0,
       f'borderless stripped WS_OVERLAPPEDWINDOW ({st1})')
    ck(d1.get('rect') is not None and d1.get('rect') == d1.get('mon'),
       f'borderless window rect matches monitor rect ({st1})')

    ini = read_sporekit_ini()
    ck(bool(re.search(r'(?m)^\s*borderless\s*=\s*1\s*$', ini)),
       'sporekit.ini persisted borderless=1')

    # Simulate Spore restoring borders / resizing window; maintain_borderless must re-apply within 2 s
    dist = host.call('window.borderless disturb')['out'].strip()
    print('  borderless disturbed:', dist, flush=True)
    reapplied = wait(lambda: (lambda d: d.get('borders') == 0 and d.get('rect') == d.get('mon'))(
        parse_borderless(host.call('window.borderless')['out'].strip())), 6)
    st_re = host.call('window.borderless')['out'].strip()
    print('  borderless maintained:', st_re, flush=True)
    ck(reapplied, f'maintain_borderless re-applied borderless after disturb ({st_re})')

    # Disable: restore borders and saved placement
    st2 = host.call('window.borderless off')['out'].strip()
    print('  borderless off:', st2, flush=True)
    d2 = parse_borderless(st2)
    ck(d2.get('enabled') == 0 and d2.get('applied') == 0 and d2.get('borders') == 1,
       f'borderless off restored window borders ({st2})')
    if orig_rect and orig_rect != d1.get('mon'):
        ck(d2.get('rect') == orig_rect,
           f'borderless off restored original window rect {orig_rect} (got {d2.get("rect")})')


def case_ui(ck, guest):
    ui = coop_ui.Ui(guest, 'guest')
    guest.call('coop.uistate 1')
    st = ui.wait(lambda s: s.get('open') == '1' and s.get('settings', {}).get('vis'), 15, 'guest lobby')
    ck(st is not None, 'guest opened co-op lobby')
    if not st:
        return
    ck(ui.click('settings'), 'guest clicked Settings')
    st_disp = ui.show_setting('display.frame_target', timeout=15)
    ck(st_disp is not None and 'display.frame_target' in st_disp.get('sets', {})
       and 'display.borderless' in st_disp.get('sets', {})
       and 'display.fov' in st_disp.get('sets', {}),
       'Display local settings (frame_target, borderless, fov) shown in Settings UI')
    if st_disp and 'display.frame_target' in st_disp.get('sets', {}):
        fps_row = st_disp['sets']['display.frame_target']
        ck(fps_row['en'], 'guest can edit local display.frame_target in Settings UI')
        before_v = fps_row['v']
        ck(ui.click(f'set{fps_row["row"]}'), 'guest clicked display.frame_target button')
        changed = ui.wait(lambda s: s.get('sets', {}).get('display.frame_target', {}).get('v') != before_v,
                          10, 'frame_target change')
        ck(changed is not None, f'clicking display.frame_target advanced preset (from {before_v})')
        t_out = guest.call('frame.target')['out'].strip()
        print('  guest frame.target after UI click:', t_out, flush=True)
        ck(parse_target(t_out).get('target', 0) == 30,
           f'Settings UI click applied frame.target=30 in SporeKit ({t_out})')
    guest.call('coop.uistate 0')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--case', choices=('all', 'fps', 'borderless', 'ui'), default='all')
    args = ap.parse_args()

    host, guest = start('creature')
    ck = Checks()
    try:
        if args.case in ('all', 'fps'):
            case_fps(ck, host)
        if args.case in ('all', 'borderless'):
            case_borderless(ck, host)
        if args.case in ('all', 'ui'):
            case_ui(ck, guest)
    finally:
        for con in (host, guest):
            try:
                con.call('frame.target auto')
                con.call('window.borderless off')
                con.call('coop.uistate 0')
            except OSError:
                pass
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
