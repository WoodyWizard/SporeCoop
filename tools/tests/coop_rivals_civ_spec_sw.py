#!/usr/bin/env python3
"""WP-081-2 (BUG-1563): Civ Rivals guaranteed nation/capital specialty conversion to match the guest's
Tribe consequence card + guest superweapon mWealth validation and spending.

civ_npc fixture, two players:
  1. Scenario 1 (rivals.civ_spec_convert):
     - Before enabling Civ Rivals, set all free NPC cities on host and guest to Military (spec=0) so no
       free NPC nation naturally matches Economic (spec=2).
     - Give guest (player 1) an Industrious Tribe consequence card (ally + conquer -> 0xC2CA9495 -> wants
       Economic spec=2).
     - Switch Civ to Rivals (distance=far).
     - Verify host takes over an NPC nation, converts its capital city specialty (+0x540 and +0x29C) to
       Economic (2), records civ_start_spec=2 in history, increments spec_converted >= 1, and replicates
       the converted specialty in SK_ASSIGN so guest ALSO sees capital city spec=2 (+0x540 and +0x29C)
       with spec_converted >= 1.
  2. Scenario 2 (rivals.civ_sw_spend):
     - Unlock superweapon type 3 (cost 2000, safe at zero target vector) on the guest's cCivilization.
     - 2a (Insufficient funds): set guest wealth to 500 (< 2000), have guest request `coop.civ super 3`.
       Verify host refuses it (sw_refused_funds >= 1, sw fired != 3, wealth unchanged).
     - 2b (Sufficient funds + NPC budget refill guard): set guest wealth to 5000 and plant a non-zero
       domestic NPC budget (+0x4AC = 1500.0) on host, then have guest request `coop.civ super 3`.
       Verify superweapon fires (sw_spent >= 1, sw fired == 3), guest mWealth drops by 2000 on both host
       and guest without double-spend or NPC budget refill (+0x4AC == 0 and +0x4B0 == 0).
  3. SPORE_FIX_OFF=rivals.civ_spec_convert,rivals.civ_sw_spend: neither specialty conversion nor direct
     superweapon spend validation runs -> FAIL.

  SPORE_LANE=3 python3 tools/tests/coop_rivals_civ_spec_sw.py
"""
import re
import struct
import time

import twoinst
from play_civ import civ_state
from twoinst import Checks, start


def cr_out(con):
    return con.call('coop.civ rivals').get('out') or ''


def parse_cr(txt):
    """Parse `coop.civ rivals` output into header dict and nations dict."""
    hdr = {}
    first = txt.splitlines()[0] if txt else ''
    for k, v in re.findall(r'(\w+)=([^\s]+)', first):
        hdr[k] = v
    nations = {}
    for m in re.finditer(
        r'nation p=(\d+) pol=0x([0-9A-Fa-f]+) cities=(\d+) vehs=(\d+) wealth=(-?\d+) '
        r'cap=\((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\)(?: spec=(-?\d+))?',
        txt,
    ):
        pid = int(m.group(1))
        nations[pid] = dict(
            pol=int(m.group(2), 16),
            cities=int(m.group(3)),
            vehs=int(m.group(4)),
            wealth=int(m.group(5)),
            cap=(float(m.group(6)), float(m.group(7)), float(m.group(8))),
            spec=int(m.group(9)) if m.group(9) is not None else -1,
        )
    return hdr, nations


def parse_factions(txt):
    """Parse `coop.civ factions` -> {pol_id: ptr_int}."""
    out = {}
    for m in re.finditer(r'ptr=(?:0x)?([0-9A-Fa-f]+)\s+pol=0x([0-9A-Fa-f]+)', txt):
        out[int(m.group(2), 16)] = int(m.group(1), 16)
    return out


def parse_city_ptrs(txt):
    """Parse `coop.civ` city lines -> list of dicts(pol, ptr, spec)."""
    rows = []
    for m in re.finditer(
        r'^city\s+(-?\d+)\s+(?:0x)?([0-9A-Fa-f]+)\s+pol=([0-9A-Fa-f]+).*?\bspec=(-?\d+)',
        txt,
        re.M,
    ):
        rows.append(dict(ci=int(m.group(1)), ptr=int(m.group(2), 16), pol=int(m.group(3), 16), spec=int(m.group(4))))
    return rows


def read_i32(con, addr):
    out = con.call(f'mem.read 0x{addr:08X} 4').get('out') or ''
    hx = re.findall(r'\b([0-9A-Fa-f]{2})\b', out)
    if len(hx) < 4:
        return None
    return struct.unpack('<i', bytes(int(x, 16) for x in hx[:4]))[0]


def read_f32(con, addr):
    out = con.call(f'mem.read 0x{addr:08X} 4').get('out') or ''
    hx = re.findall(r'\b([0-9A-Fa-f]{2})\b', out)
    if len(hx) < 4:
        return None
    return struct.unpack('<f', bytes(int(x, 16) for x in hx[:4]))[0]


def write_f32(con, addr, val):
    b = struct.pack('<f', float(val))
    con.call(f'mem.write 0x{addr:08X} ' + ' '.join(f'{x:02X}' for x in b))


def wait_for(fn, timeout=15.0, step=0.25):
    t0 = time.time()
    while time.time() - t0 < timeout:
        v = fn()
        if v:
            return v
        time.sleep(step)
    return None


def main():
    twoinst.forget_settings()
    host, guest = start('civ_npc')
    ck = Checks()
    try:
        time.sleep(2.5)
        cities0, _, _ = civ_state(host)
        ck(len(cities0) >= 4, f'civ_npc fixture has at least 4 cities (got {len(cities0)})')
        if len(cities0) < 4:
            return ck.result()

        # ---- Scenario 1: Guaranteed specialty conversion (rivals.civ_spec_convert) ----
        # Force every free NPC city on both host and guest to Military (0) so NO free NPC matches Economic (2).
        set_h = host.call('coop.civ rivals setspec all_npc 0').get('out') or ''
        set_g = guest.call('coop.civ rivals setspec all_npc 0').get('out') or ''
        print('  setspec all_npc 0:', set_h.strip(), '|', set_g.strip(), flush=True)

        # Give guest (player 1) an Industrious Tribe card (1 ally + 1 conquer -> 0xC2CA9495 -> Economic spec=2).
        t1 = host.call('coop.history tribe 1 ally').get('out') or ''
        t2 = host.call('coop.history tribe 1 conquer').get('out') or ''
        print('  guest tribe outcomes:', t1.strip(), '|', t2.strip(), flush=True)
        ck('CARD=C2CA9495' in t2.upper(), f'guest Tribe consequence card is Industrious 0xC2CA9495 ({t2.strip()})')

        # Switch Civ to Rivals: guest takes over an NPC nation (all are Military=0) and must convert to Economic=2.
        host.call('coop.rivals distance far')
        host.call('coop.rivals mode civ rivals')

        h_cr = wait_for(
            lambda: cr_out(host) if ('active=1' in cr_out(host) and 'assigned=2' in cr_out(host)) else None,
            12.0,
        )
        if not h_cr:
            h_cr = cr_out(host)
        h_hdr, h_nat = parse_cr(h_cr)
        print('  host civ_rivals:', h_cr.splitlines()[0] if h_cr else '<empty>', flush=True)
        ck(
            h_hdr.get('active') == '1' and 1 in h_nat,
            f'host Civ Rivals active with guest nation assigned ({h_cr.splitlines()[0] if h_cr else ""})',
        )
        if h_hdr.get('active') != '1' or 1 not in h_nat:
            return ck.result()

        guest_pol = h_nat[1]['pol']

        g_cr = wait_for(
            lambda: cr_out(guest)
            if (
                parse_cr(cr_out(guest))[0].get('swap') == '1'
                and parse_cr(cr_out(guest))[0].get('player_pol', '').lower() == f'0x{guest_pol:08x}'
            )
            else None,
            15.0,
        )
        if not g_cr:
            g_cr = cr_out(guest)
        g_hdr, g_nat = parse_cr(g_cr)
        print('  guest civ_rivals:', g_cr.splitlines()[0] if g_cr else '<empty>', flush=True)

        # Wait briefly for SK_ASSIGN specialty sync on guest
        wait_for(
            lambda: any(
                r['pol'] == guest_pol and r['spec'] == 2
                for r in parse_city_ptrs(guest.call('coop.civ').get('out') or '')
            ),
            6.0,
        )

        h_hdr, h_nat = parse_cr(cr_out(host))
        g_hdr, g_nat = parse_cr(cr_out(guest))
        h_cities = parse_city_ptrs(host.call('coop.civ').get('out') or '')
        g_cities = parse_city_ptrs(guest.call('coop.civ').get('out') or '')
        h_gcap = next((r for r in h_cities if r['pol'] == guest_pol), None)
        g_gcap = next((r for r in g_cities if r['pol'] == guest_pol), None)

        h_cap29c = read_i32(host, h_gcap['ptr'] + 0x29C) if h_gcap else None
        g_cap29c = read_i32(guest, g_gcap['ptr'] + 0x29C) if g_gcap else None

        ck(
            int(h_hdr.get('spec_converted', '0')) >= 1
            and h_nat[1]['spec'] == 2
            and h_gcap is not None
            and h_gcap['spec'] == 2
            and h_cap29c == 2,
            f'host converted guest capital to Economic (spec_converted={h_hdr.get("spec_converted")}, '
            f'nation_spec={h_nat[1]["spec"]}, city_spec={h_gcap["spec"] if h_gcap else None}, +0x29C={h_cap29c})',
        )
        ck(
            int(g_hdr.get('spec_converted', '0')) >= 1
            and 1 in g_nat
            and g_nat[1]['spec'] == 2
            and g_gcap is not None
            and g_gcap['spec'] == 2
            and g_cap29c == 2,
            f'guest synced converted capital to Economic (spec_converted={g_hdr.get("spec_converted")}, '
            f'nation_spec={g_nat.get(1, {}).get("spec")}, city_spec={g_gcap["spec"] if g_gcap else None}, +0x29C={g_cap29c})',
        )

        # Verify history recorded the converted starting specialty (start=2 = Economic)
        hist_out = host.call('coop.history status').get('out') or ''
        ck(
            'start=2' in hist_out,
            f'history recorded converted Economic start specialty (start=2) for guest ({hist_out.strip().splitlines()[-1] if hist_out.strip() else ""})',
        )

        # ---- Scenario 2: Guest superweapon mWealth validation & spend (rivals.civ_sw_spend) ----
        h_factions = parse_factions(host.call('coop.civ factions').get('out') or '')
        g_factions = parse_factions(guest.call('coop.civ factions').get('out') or '')
        h_gciv_ptr = h_factions.get(guest_pol)
        g_gciv_ptr = g_factions.get(guest_pol)
        ck(bool(h_gciv_ptr), f'resolved host guest cCivilization ptr (0x{h_gciv_ptr or 0:08X})')
        if not h_gciv_ptr:
            return ck.result()

        # Unlock superweapon type 3 (cost=2000, non-capital-blocked, safe at zero target vector) on guest's civ
        host.call(f'mem.write 0x{h_gciv_ptr + 0x484 + 3:08X} 01')
        if g_gciv_ptr:
            guest.call(f'mem.write 0x{g_gciv_ptr + 0x484 + 3:08X} 01')

        # 2a. Insufficient funds: set guest wealth to 500 (< 2000), request superweapon type 3 -> refused
        cur_w = parse_cr(cr_out(host))[1][1]['wealth']
        host.call(f'coop.civ rivals addmoney 1 {500 - cur_w}')
        wait_for(lambda: parse_cr(cr_out(host))[1][1]['wealth'] <= 600, 5.0)
        w_short_pre = parse_cr(cr_out(host))[1][1]['wealth']

        guest.call('coop.civ super 3')
        refused_ok = wait_for(
            lambda: int(parse_cr(cr_out(host))[0].get('sw_refused_funds', '0')) >= 1,
            5.0,
        )
        h_hdr_short, h_nat_short = parse_cr(cr_out(host))
        sw_short = host.call('coop.civ sw').get('out') or ''
        print('  insufficient funds check:', sw_short.strip(), 'wealth=', h_nat_short[1]['wealth'], flush=True)
        ck(
            bool(refused_ok)
            and 'fired=-1' in sw_short
            and abs(h_nat_short[1]['wealth'] - w_short_pre) < 250,
            f'insufficient funds superweapon refused cleanly (sw_refused_funds={h_hdr_short.get("sw_refused_funds")}, '
            f'sw="{sw_short.strip()}", wealth {w_short_pre}->{h_nat_short[1]["wealth"]})',
        )

        # 2b. Sufficient funds + NPC budget refill guard: set wealth=5000 and plant domestic budget=1500
        cur_w2 = parse_cr(cr_out(host))[1][1]['wealth']
        host.call(f'coop.civ rivals addmoney 1 {5000 - cur_w2}')
        write_f32(host, h_gciv_ptr + 0x4AC, 1500.0)
        write_f32(host, h_gciv_ptr + 0x4B0, 500.0)
        time.sleep(0.3)
        w_fire_pre = read_f32(host, h_gciv_ptr + 0x98)

        guest.call('coop.civ super 3')
        spent_ok = wait_for(
            lambda: int(parse_cr(cr_out(host))[0].get('sw_spent', '0')) >= 1,
            8.0,
        )
        w_fire_post = read_f32(host, h_gciv_ptr + 0x98)
        dom_post = read_f32(host, h_gciv_ptr + 0x4AC)
        exp_post = read_f32(host, h_gciv_ptr + 0x4B0)
        sw_post = host.call('coop.civ sw').get('out') or ''
        h_hdr_post, h_nat_post = parse_cr(cr_out(host))

        # Wait for guest synced player_money to reflect the 2000 deduction
        g_money_synced = wait_for(
            lambda: abs(int(parse_cr(cr_out(guest))[0].get('player_money', '0')) - int(w_fire_post or 0)) < 300,
            6.0,
        )
        g_money_post = int(parse_cr(cr_out(guest))[0].get('player_money', '0'))
        print(
            f'  superweapon fire: wealth {w_fire_pre:.0f}->{w_fire_post:.0f} dom={dom_post:.1f} exp={exp_post:.1f} '
            f'guest_money={g_money_post} sw_spent={h_hdr_post.get("sw_spent")} ({sw_post.strip()})',
            flush=True,
        )

        drop = (w_fire_pre - w_fire_post) if (w_fire_pre is not None and w_fire_post is not None) else -1.0
        ck(
            bool(spent_ok)
            and 'fired=3' in sw_post
            and abs(drop - 2000.0) < 300.0
            and dom_post is not None
            and abs(dom_post) < 0.01
            and exp_post is not None
            and abs(exp_post) < 0.01
            and bool(g_money_synced),
            f'guest superweapon fired and deducted exact 2000 from mWealth with zeroed NPC budgets '
            f'(drop={drop:.0f}, dom={dom_post}, exp={exp_post}, sw_spent={h_hdr_post.get("sw_spent")}, '
            f'guest_money={g_money_post})',
        )

        # ---- Scenario 3: Guest starting specialty picker (BUG-1590, rivals.civ_spec_pick) ----
        # Guest starts as Economic (2) above and still owns only 1 city (its capital): switch to Military (0), then Religious (1).
        r_mil = (guest.call('coop.civ rivals spec mil').get('out') or '').strip()
        mil_ok = wait_for(
            lambda: (
                parse_cr(cr_out(host))[1].get(1, {}).get('spec') == 0
                and any(
                    r['pol'] == guest_pol and r['spec'] == 0
                    for r in parse_city_ptrs(guest.call('coop.civ').get('out') or '')
                )
            ),
            6.0,
        )
        r_rel = (guest.call('coop.civ rivals spec rel').get('out') or '').strip()
        rel_ok = wait_for(
            lambda: (
                parse_cr(cr_out(host))[1].get(1, {}).get('spec') == 1
                and any(
                    r['pol'] == guest_pol and r['spec'] == 1
                    for r in parse_city_ptrs(guest.call('coop.civ').get('out') or '')
                )
            ),
            6.0,
        )
        h_hdr_sp, h_nat_sp = parse_cr(cr_out(host))
        hist_sp = host.call('coop.history status').get('out') or ''
        print(
            f'  guest spec pick: mil="{r_mil}" rel="{r_rel}" spec_picked={h_hdr_sp.get("spec_picked")} '
            f'host_spec={h_nat_sp.get(1, {}).get("spec")}',
            flush=True,
        )
        ck(
            r_mil == 'ok'
            and bool(mil_ok)
            and r_rel == 'ok'
            and bool(rel_ok)
            and int(h_hdr_sp.get('spec_picked', '0')) >= 2
            and 'start=3' in hist_sp,
            f'guest switched starting specialty Eco -> Mil -> Rel on host & guest (BUG-1590: '
            f'mil={mil_ok}, rel={rel_ok}, spec_picked={h_hdr_sp.get("spec_picked")})',
        )
    finally:
        twoinst.forget_settings()
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
