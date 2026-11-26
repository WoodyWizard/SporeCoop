#!/usr/bin/env python3
"""UI privacy (streamer mode + auto-mask on vote popups) and 5-language live i18n verification (BUG-1572, BUG-1575).

1. Start two instances on `cell` fixture and open host/guest F9 lobby.
2. Verify raw host IP is shown by default when manually opening F9.
3. Toggle streamer mode (`coop.uistate hide_ip 1`): verify host & guest lobby mask IP as `***.***.***.***:<port>`
   without leaking raw IPv4 octets. Toggle back off (`hide_ip 0`) and verify IP returns.
4. Close F9 on both games, trigger a real vote (`coop.vote test`): verify F9 auto-opens on host and guest with
   `vote_auto=1` and IP masked (`***.***.***.***:`) on BOTH screens even when `hide_ip=0`. End the vote, reopen
   F9 manually, and verify `vote_auto=0` and IP returns.
5. Cycle live UI language through `ru`, `hu`, `es`, `zh`, and back to `en` (`coop.uistate lang <code>`):
   - Verify localized `Settings` and `Report a problem` captions for every language.
   - Verify `Report a problem` dynamically widens in Russian (`> 210 px`) and restores exact English golden width (`210 px`).
   - Verify Russian Settings uses `на лету` (and never `живая`).
PASS: 1..5 with zero faults.
SPORE_FIX_OFF=ui.hide_ip (`ui_privacy_i18n_off`): `hide_ip` and vote auto-popup fail to mask IP -> FAIL.

  SPORE_LANE=2 python3 tools/tests/coop_ui_privacy_i18n.py
"""
import re
import time

from coop_ui import Ui
import twoinst
from twoinst import Checks, start

IPV4_RE = re.compile(r'\b\d{1,3}(?:\.\d{1,3}){3}\b')


def parse_privacy(raw_out):
    m = re.search(r'^privacy\s+hide_ip=(\d+)\s+vote_auto=(\d+)\s+lang=(\S+)', raw_out, re.M)
    if not m:
        return None
    return {'hide_ip': int(m.group(1)), 'vote_auto': int(m.group(2)), 'lang': m.group(3)}


def wait_for(pred, timeout=15.0, step=0.25):
    t0 = time.time()
    while time.time() - t0 < timeout:
        v = pred()
        if v:
            return v
        time.sleep(step)
    return False


def main():
    twoinst.forget_settings()
    host, guest = start('cell')
    ck = Checks()
    hu, gu = Ui(host, 'host'), Ui(guest, 'guest')
    time.sleep(1.5)

    # Ensure clean baseline: English, hide_ip=0, F9 open on both
    host.call('coop.uistate lang en')
    guest.call('coop.uistate lang en')
    host.call('coop.uistate hide_ip 0')
    guest.call('coop.uistate hide_ip 0')
    host.call('coop.uistate 1')
    guest.call('coop.uistate 1')
    time.sleep(0.5)

    h_raw = host.call('coop.uistate')['out']
    g_raw = guest.call('coop.uistate')['out']
    hs, gs = hu.state(), gu.state()
    hp, gp = parse_privacy(h_raw), parse_privacy(g_raw)
    print(f'  baseline: hostaddr_h={hs.get("hostaddr")!r} hostaddr_g={gs.get("hostaddr")!r} priv_h={hp}', flush=True)
    ck(hp is not None and hp['hide_ip'] == 0 and hp['vote_auto'] == 0, f'baseline privacy state parsed ({hp})')
    ck('***' not in hs.get('hostaddr', '') and IPV4_RE.search(hs.get('hostaddr', '')) is not None,
       f'host lobby shows real IP when hide_ip=0 ({hs.get("hostaddr")!r})')

    # 2. Persistent streamer mode masks IP on both host and guest
    host.call('coop.uistate hide_ip 1')
    guest.call('coop.uistate hide_ip 1')
    time.sleep(0.4)
    hs_m, gs_m = hu.state(), gu.state()
    print(f'  hide_ip=1: hostaddr_h={hs_m.get("hostaddr")!r} hostaddr_g={gs_m.get("hostaddr")!r}', flush=True)
    ck('***.***.***.***:' in hs_m.get('hostaddr', '') and IPV4_RE.search(hs_m.get('hostaddr', '')) is None,
       f'host hide_ip=1 masks IP while keeping port ({hs_m.get("hostaddr")!r})')
    ck('***.***.***.***:' in gs_m.get('hostaddr', '') and IPV4_RE.search(gs_m.get('hostaddr', '')) is None,
       f'guest hide_ip=1 masks IP while keeping port ({gs_m.get("hostaddr")!r})')

    # Restore hide_ip=0 and close F9 before testing vote auto-mask
    host.call('coop.uistate hide_ip 0')
    guest.call('coop.uistate hide_ip 0')
    host.call('coop.uistate 0')
    guest.call('coop.uistate 0')
    time.sleep(0.4)

    # 3. Vote auto-popup masks IP automatically on both host and guest even with hide_ip=0
    host.call('coop.vote test')
    vote_opened = wait_for(
        lambda: hu.state().get('open') == '1' and gu.state().get('open') == '1',
        timeout=10.0,
    )
    time.sleep(0.4)
    h_vr = host.call('coop.uistate')['out']
    g_vr = guest.call('coop.uistate')['out']
    hs_v, gs_v = hu.state(), gu.state()
    hp_v, gp_v = parse_privacy(h_vr), parse_privacy(g_vr)
    print(f'  vote_open: hostaddr_h={hs_v.get("hostaddr")!r} priv_h={hp_v} | hostaddr_g={gs_v.get("hostaddr")!r} priv_g={gp_v}',
          flush=True)
    ck(bool(vote_opened), 'vote auto-opened F9 on host and guest')
    ck(hp_v is not None and hp_v['vote_auto'] == 1 and '***.***.***.***:' in hs_v.get('hostaddr', '') and
       IPV4_RE.search(hs_v.get('hostaddr', '')) is None,
       f'host vote auto-popup masks IP automatically ({hs_v.get("hostaddr")!r}, {hp_v})')
    ck(gp_v is not None and gp_v['vote_auto'] == 1 and '***.***.***.***:' in gs_v.get('hostaddr', '') and
       IPV4_RE.search(gs_v.get('hostaddr', '')) is None,
       f'guest vote auto-popup masks IP automatically ({gs_v.get("hostaddr")!r}, {gp_v})')

    # Finish vote and manually reopen F9 -> unmasked again
    host.call('coop.vote go')
    wait_for(lambda: hu.state().get('open') == '0', timeout=10.0)
    host.call('coop.uistate 1')
    time.sleep(0.4)
    hs_after = hu.state()
    hp_after = parse_privacy(host.call('coop.uistate')['out'])
    ck(hp_after is not None and hp_after['vote_auto'] == 0 and '***' not in hs_after.get('hostaddr', ''),
       f'manual F9 reopen after vote restores unmasked IP ({hs_after.get("hostaddr")!r}, {hp_after})')

    # 4. 5-language live i18n check (RU, HU, ES, ZH, EN) + dynamic report button width + RU "на лету"
    expected = [
        ('ru', 'Настройки', 'Сообщить об ошибке'),
        ('hu', 'Beállítások', 'Hiba jelentése'),
        ('es', 'Ajustes', 'Reportar problema'),
        ('zh', '设置', '报告问题'),
        ('en', 'Settings', 'Report a problem'),
    ]
    for code, exp_set, exp_rep in expected:
        host.call(f'coop.uistate lang {code}')
        time.sleep(0.3)
        raw = host.call('coop.uistate')['out']
        st = hu.state()
        pr = parse_privacy(raw)
        set_txt = (st.get('settings') or {}).get('text', '')
        rep = st.get('report') or {}
        rep_txt = rep.get('text', '')
        rep_w = (rep['rect'][2] - rep['rect'][0]) if 'rect' in rep else 0
        print(f'  lang={code}: priv={pr} settings={set_txt!r} report={rep_txt!r} report_w={rep_w}', flush=True)
        ck(pr is not None and pr['lang'] == code and set_txt == exp_set and rep_txt == exp_rep,
           f'lang={code} translated Settings ({set_txt!r}) and Report ({rep_txt!r})')
        if code == 'ru':
            ck(rep_w > 210, f'Russian Report button dynamically widened > 210px (got {rep_w}px)')
            # Open Settings page in Russian and check live tag uses "на лету" (never "живая")
            hu.click('settings')
            time.sleep(0.4)
            set_raw = host.call('coop.uistate')['out']
            ck('на лету' in set_raw and 'живая' not in set_raw,
               'Russian Settings uses "на лету" instead of "живая"')
            hu.click('back')
            time.sleep(0.3)
        if code == 'en':
            ck(rep_w == 210, f'English Report button keeps exact golden width 210px (got {rep_w}px)')

    # Clean up UI state
    host.call('coop.uistate 0')
    guest.call('coop.uistate 0')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
