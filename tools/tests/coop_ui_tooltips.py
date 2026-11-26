#!/usr/bin/env python3
"""Spore-style hover tooltips on co-op window buttons (BUG-0874).

Verifies by real mouse moves (`move <x> <y>`) against `coop.ui state`:
 1. Moving mouse over the Host button shows NO tooltip before 0.8 s (< 0.5 s).
 2. Resting over the Host button for >= 1.0 s shows the native tooltip window with English text.
 3. Moving mouse away immediately hides the tooltip.
 4. Switching to Russian (`coop.uistate lang ru`) shows the Russian tooltip after >= 1.0 s hover.
 5. Moving away hides it cleanly; no faults.

Twin: ui_tooltips_off (SPORE_FIX_OFF=ui.tooltips) must FAIL.
"""
import os
import re
import time

from coop_ui import Ui
from twoinst import Checks, shot, start


def parse_ui_state(raw):
    st = {}
    for line in raw.splitlines():
        if line.startswith('open='):
            for kv in line.split():
                if '=' in kv:
                    k, v = kv.split('=', 1)
                    st[k] = v
            continue
        parts = line.split(' ', 1)
        if len(parts) < 2:
            continue
        key, rest = parts
        f = rest.split(' ', 5)
        if len(f) >= 5 and all(re.fullmatch(r'-?[\d.]+', x) for x in f[:4]):
            st[key] = {'rect': [int(float(x)) for x in f[:4]], 'vis': f[4] == '1',
                       'text': f[5].strip("'") if len(f) > 5 else ''}
    return st


def ui_state(con):
    return parse_ui_state(con.call('coop.ui state')['out'])


def center(rect):
    return (rect[0] + rect[2]) // 2, (rect[1] + rect[3]) // 2


def main():
    host, guest = start('creature')
    ck = Checks()
    hui = Ui(host, 'host')
    suf = '_off' if os.environ.get('SPORE_FIX_OFF') else ''

    # ---- 0. BUG-1584: whole-row hover + multi-line detailed tooltip on Settings page (RU, 0.35 s delay) ----
    host.call('coop.uistate lang ru')
    host.call('coop.uistate 1')
    st_lob = hui.wait(lambda s: s.get('open') == '1' and s.get('settings', {}).get('vis'), 15, 'host lobby')
    if st_lob and st_lob.get('settings', {}).get('vis'):
        hui.click('settings')
        st_set = hui.wait(lambda s: s.get('page') == 'settings' and s.get('set1', {}).get('vis'), 15, 'settings page')
        if st_set and st_set.get('window') and st_set.get('set1', {}).get('vis'):
            wx1 = st_set['window']['rect'][0]
            # Hover the LEFT setting name label (far left of the value button at wx1+466)
            lx = wx1 + 150
            ly = center(st_set['set1']['rect'])[1]
            host.call('move 4 540')
            time.sleep(0.25)
            host.call(f'move {lx} {ly}')
            t_set = time.time()
            s_set = None
            while time.time() - t_set < 1.2:
                cur = ui_state(host)
                if cur.get('tooltip', {}).get('vis'):
                    s_set = cur
                    break
                time.sleep(0.08)
            if s_set is None:
                s_set = ui_state(host)
            tip_s = s_set.get('tooltip', {})
            th_s = (tip_s['rect'][3] - tip_s['rect'][1]) if tip_s.get('rect') else 0
            print(f'  settings label hover ({time.time() - t_set:.2f}s): btn={s_set.get("tooltip_btn")} h={th_s} tip={tip_s!r}', flush=True)
            ck(tip_s.get('vis') and s_set.get('tooltip_btn') == 'set1' and th_s > 48 and ' | ' in tip_s.get('text', ''),
               f'RU local setting label hover shows multi-line detailed tooltip (h={th_s}, tip={tip_s!r})')
            shot(host, f'ui_tooltip_setting_local_ru{suf}.png')

            # Page 2: session setting with long detailed description (e.g. DNA / progress)
            if st_set.get('setnext', {}).get('vis'):
                hui.click('setnext')
                time.sleep(0.35)
                st_p2 = ui_state(host)
                pick = next((f'set{i}' for i in range(12) if st_p2.get(f'set{i}', {}).get('vis')), None)
                if pick:
                    ly2 = center(st_p2[pick]['rect'])[1]
                    host.call('move 4 540')
                    time.sleep(0.2)
                    host.call(f'move {lx} {ly2}')
                    t_p2 = time.time()
                    s_p2 = None
                    while time.time() - t_p2 < 1.2:
                        cur = ui_state(host)
                        if cur.get('tooltip', {}).get('vis'):
                            s_p2 = cur
                            break
                        time.sleep(0.08)
                    if s_p2 is None:
                        s_p2 = ui_state(host)
                    tip_p2 = s_p2.get('tooltip', {})
                    th_p2 = (tip_p2['rect'][3] - tip_p2['rect'][1]) if tip_p2.get('rect') else 0
                    print(f'  settings p2 label hover ({time.time() - t_p2:.2f}s): btn={s_p2.get("tooltip_btn")} h={th_p2} tip={tip_p2!r}', flush=True)
                    ck(tip_p2.get('vis') and s_p2.get('tooltip_btn') == pick and th_p2 > 48 and ' | ' in tip_p2.get('text', ''),
                       f'RU session setting label hover shows multi-line detailed tooltip (h={th_p2}, tip={tip_p2!r})')
                    shot(host, f'ui_tooltip_setting_session_ru{suf}.png')
            host.call('move 4 540')
            time.sleep(0.25)
            hui.click('setback')
            time.sleep(0.25)

    # Leave session so host opens on the Host step (where the Host button lives)
    host.call('coop.leave')
    t0 = time.time()
    while time.time() - t0 < 15 and host.call('coop.status')['out'].splitlines()[:1] != ['offline']:
        time.sleep(0.25)

    host.call('coop.uistate lang en')
    host.call('coop.uistate 1')
    st = hui.wait(lambda s: s.get('open') == '1' and s.get('page') == 'connect', 15, 'host connect page')
    if st and st.get('step') == 'choose' and st.get('gohost', {}).get('vis'):
        hui.click('gohost')
    st = hui.wait(lambda s: s.get('step') == 'host' and s.get('host', {}).get('vis'), 15, 'host step')
    ck(st is not None and st['host']['vis'], 'host step with Host button visible')
    if not st or not st.get('host', {}).get('vis'):
        ck.faults(host=host, guest=guest)
        return ck.result()

    # Move pointer off the dialog first: tooltip must be hidden
    host.call('move 4 540')
    time.sleep(0.4)
    s0 = ui_state(host)
    ck(s0.get('tooltip') is not None and not s0['tooltip']['vis'],
       f'tooltip hidden when pointer is off buttons ({s0.get("tooltip")!r})')

    # ---- 1. Move over Host button: no tooltip before 0.8 s ----
    hx, hy = center(st['host']['rect'])
    host.call(f'move {hx} {hy}')
    t_move = time.time()
    time.sleep(0.25)
    elapsed_early = time.time() - t_move
    s_early = ui_state(host)
    print(f'  at {elapsed_early:.2f}s: tooltip={s_early.get("tooltip")!r}', flush=True)
    ck(elapsed_early < 0.75 and s_early.get('tooltip') and not s_early['tooltip']['vis'],
       f'no tooltip before 0.8 s (at {elapsed_early:.2f}s, {s_early.get("tooltip")!r})')

    # ---- 2. Rest >= 1.0 s: tooltip visible with English text ----
    rem = max(0.0, 1.05 - (time.time() - t_move))
    if rem > 0:
        time.sleep(rem)
    s_en = None
    t_poll = time.time()
    while time.time() - t_poll < 1.5:
        cur = ui_state(host)
        if cur.get('tooltip', {}).get('vis'):
            s_en = cur
            break
        time.sleep(0.1)
    if s_en is None:
        s_en = ui_state(host)
    tip_en = s_en.get('tooltip', {})
    print(f'  at {time.time() - t_move:.2f}s (EN): btn={s_en.get("tooltip_btn")} tip={tip_en!r}', flush=True)
    ck(tip_en.get('vis') and s_en.get('tooltip_btn') == 'host' and 'Start hosting' in tip_en.get('text', ''),
       f'EN tooltip visible over Host after 1.0 s (btn={s_en.get("tooltip_btn")}, tip={tip_en!r})')
    shot(host, f'ui_tooltip_host_en{suf}.png')

    # ---- 3. Move mouse away: hidden immediately ----
    host.call('move 4 540')
    time.sleep(0.35)
    s_away = ui_state(host)
    print(f'  after move away: tooltip={s_away.get("tooltip")!r}', flush=True)
    ck(s_away.get('tooltip') and not s_away['tooltip']['vis'],
       f'tooltip hidden when mouse moves away ({s_away.get("tooltip")!r})')

    # ---- 4. Russian tooltip check ----
    host.call('coop.uistate lang ru')
    time.sleep(0.35)
    host.call(f'move {hx} {hy}')
    t_ru = time.time()
    time.sleep(1.05)
    s_ru = None
    t_poll = time.time()
    while time.time() - t_poll < 1.5:
        cur = ui_state(host)
        if cur.get('tooltip', {}).get('vis'):
            s_ru = cur
            break
        time.sleep(0.1)
    if s_ru is None:
        s_ru = ui_state(host)
    tip_ru = s_ru.get('tooltip', {})
    print(f'  at {time.time() - t_ru:.2f}s (RU): btn={s_ru.get("tooltip_btn")} tip={tip_ru!r}', flush=True)
    ck(tip_ru.get('vis') and 'Открыть сервер' in tip_ru.get('text', ''),
       f'RU tooltip visible over Host after 1.0 s ({tip_ru!r})')
    shot(host, f'ui_tooltip_host_ru{suf}.png')

    # ---- 5. Move away and restore English ----
    host.call('move 4 540')
    host.call('coop.uistate lang en')
    time.sleep(0.35)
    s_end = ui_state(host)
    ck(s_end.get('tooltip') and not s_end['tooltip']['vis'], 'tooltip hidden after Russian check')

    host.call('coop.uistate 0')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
