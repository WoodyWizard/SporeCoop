#!/usr/bin/env python3
"""BUG-0869 (0.8.0 WP-C): Cell warm reconnect reuses the unchanged Cell world.

Cell fixture, two players:
  Part 1 (warm reconnect):
    1. Guest joins host in Cell (full MSG_WORLD_OFFER transfer -> receipt issued and armed).
    2. Guest disconnects (`coop.leave`) while staying in the Cell tide pool, then reconnects.
    3. Verify ZERO new MSG_WORLD_OFFER sent (`warm_resumes >= 1`, `world_offers_sent` and
       `world_offers_recv` unchanged), guest stays in Cell without reloading, and partner cells
       (host + guest puppets) and NPC mirrors reappear within 3 s.
  Part 2 (control - host save invalidation):
    4. Guest disconnects again; host triggers a save (`coop.save`) and waits for it to finish.
    5. Guest reconnects: stored receipt is invalidated -> host falls back to a full world transfer
       (`warm_fallbacks >= 1`, `world_offers_sent` +1, `world_offers_recv` +1), guest reloads into
       Cell, partner cells and NPC mirrors return, 0 faults on both games.
  OFF-twin (`SPORE_FIX_OFF=cell.warm_reconnect`):
    Part 1 always sends a new MSG_WORLD_OFFER and reloads the guest (`warm_resumes == 0`) -> FAIL.

  SPORE_LANE=5 python3 tools/tests/coop_cell_warm_reconnect.py
"""
import re
import time

from harness import mode, waiter
import lane
import twoinst
from twoinst import Checks, start

wait = waiter(step=0.5)


def warm_stats(con):
    out = con.call('coop.status')['out']
    m = re.search(
        r'warm_reconnect: enabled=(\d+) warm_resumes=(\d+) warm_fallbacks=(\d+) '
        r'world_offers_sent=(\d+) world_offers_recv=(\d+) host_receipts=(\d+) '
        r'guest_receipt=([0-9A-Fa-f]+) \((\w+)\)',
        out,
    )
    if not m:
        return None
    return dict(
        enabled=int(m.group(1)),
        warm_resumes=int(m.group(2)),
        warm_fallbacks=int(m.group(3)),
        world_offers_sent=int(m.group(4)),
        world_offers_recv=int(m.group(5)),
        host_receipts=int(m.group(6)),
        guest_receipt=int(m.group(7), 16),
        guest_state=m.group(8),
        raw=out,
    )


def mirrors(con):
    m = re.search(r'cell\(guest\):.*?mirrors=(\d+)', con.call('coop.status')['out'])
    return int(m.group(1)) if m else 0


def players_count(con):
    m = re.search(r'players=(\d+)/(\d+)', con.call('coop.status')['out'])
    return int(m.group(1)) if m else 0


def scalepos_puppets(con):
    out = con.call('coop.pool scalepos')['out']
    return {int(i): (float(x), float(y)) for i, x, y in re.findall(r'p(\d)=\((-?[\d.]+) (-?[\d.]+)\)', out)}


def store_writes(con):
    m = re.search(r'writes=(\d+)', con.call('coop.store').get('out') or '')
    return int(m.group(1)) if m else -1


def host_frame(con):
    m = re.search(r'frames?=(\d+)', con.call('status').get('out') or '')
    return int(m.group(1)) if m else -1


def wait_cell_ready(host, guest, timeout=90.0):
    return wait(
        lambda: 'Cell' in mode(guest)
        and players_count(host) >= 2
        and mirrors(guest) > 0
        and len(scalepos_puppets(host)) >= 1
        and len(scalepos_puppets(guest)) >= 1,
        timeout,
    )


def main():
    host, guest = start('cell')
    ck = Checks()

    # Initial Cell session ready (full world transfer completed)
    ck(wait_cell_ready(host, guest, 60.0), 'initial Cell session synced (guest mirrors + both partner cells)')
    h0 = warm_stats(host)
    g0 = warm_stats(guest)
    ck(h0 is not None and g0 is not None, 'coop.status reports warm_reconnect counters')
    if h0 is None or g0 is None:
        return ck.result()

    # Wait briefly for guest receipt to arm (only when cell.warm_reconnect is enabled)
    if g0['enabled']:
        wait(lambda: (warm_stats(guest) or {}).get('guest_state') == 'active', 10.0)
        h0, g0 = warm_stats(host), warm_stats(guest)
    print(f'  baseline host:  {h0}', flush=True)
    print(f'  baseline guest: {g0}', flush=True)
    if g0['enabled']:
        ck(h0['host_receipts'] >= 1 and g0['guest_receipt'] != 0 and g0['guest_state'] == 'active',
           f'initial Cell share armed receipt (host_receipts={h0["host_receipts"]}, '
           f'guest_receipt={g0["guest_receipt"]:016X}, state={g0["guest_state"]})')

    # ---------------- Part 1: warm reconnect on unchanged Cell world ----------------
    print('  Part 1: dropping guest connection (staying in Cell) and reconnecting...', flush=True)
    guest.call('coop.leave')
    ck(wait(lambda: players_count(host) == 1, 15.0), 'Part 1: host saw guest leave (players=1)')
    time.sleep(2.0)
    ck('Cell' in mode(guest), f'Part 1: guest stayed in Cell while offline ({mode(guest).strip()})')

    t_join = time.time()
    join_out = guest.call(f'coop.join 127.0.0.1:{lane.PORT}')['out'].strip()
    print(f'  Part 1 guest join: {join_out}', flush=True)

    left_cell = False
    partner_t = None
    mirror_t = None
    # Poll rapidly during the first 6 s to verify no reload and fast partner return
    while time.time() - t_join < 6.0:
        gm = mode(guest)
        if 'Cell' not in gm:
            left_cell = True
        else:
            if partner_t is None and len(scalepos_puppets(host)) >= 1 and len(scalepos_puppets(guest)) >= 1:
                partner_t = time.time() - t_join
            if mirror_t is None and mirrors(guest) > 0:
                mirror_t = time.time() - t_join
        time.sleep(0.25)

    h1 = warm_stats(host)
    g1 = warm_stats(guest)
    print(f'  Part 1 host:  {h1}', flush=True)
    print(f'  Part 1 guest: {g1}', flush=True)
    print(f'  Part 1 timings: left_cell={left_cell}, partner_t={partner_t}, mirror_t={mirror_t}', flush=True)

    ck(h1['warm_resumes'] >= h0['warm_resumes'] + 1,
       f'Part 1: host accepted warm reconnect (warm_resumes {h0["warm_resumes"]} -> {h1["warm_resumes"]})')
    ck(g1['warm_resumes'] >= g0['warm_resumes'] + 1,
       f'Part 1: guest accepted warm reconnect ack (warm_resumes {g0["warm_resumes"]} -> {g1["warm_resumes"]})')
    ck(h1['world_offers_sent'] == h0['world_offers_sent'],
       f'Part 1: host sent ZERO new MSG_WORLD_OFFER ({h0["world_offers_sent"]} -> {h1["world_offers_sent"]})')
    ck(g1['world_offers_recv'] == g0['world_offers_recv'],
       f'Part 1: guest received ZERO new MSG_WORLD_OFFER ({g0["world_offers_recv"]} -> {g1["world_offers_recv"]})')
    ck(not left_cell, 'Part 1: guest stayed in the Cell tide pool without reload')
    ck(partner_t is not None and partner_t <= 3.0,
       f'Part 1: partner cells reappeared on both host and guest within 3 s ({partner_t})')
    ck(mirror_t is not None and mirror_t <= 3.0,
       f'Part 1: guest resumed mirroring host NPC cells within 3 s ({mirror_t})')

    # Wait for Cell ready before Part 2 (handles OFF-twin which reloaded during Part 1)
    if not wait_cell_ready(host, guest, 90.0):
        print('  warning: guest did not finish reloading before Part 2', flush=True)
        ck.faults(host=host, guest=guest)
        return ck.result()

    # ---------------- Part 2: host saves while guest is offline -> fallback to full transfer ----------------
    print('  Part 2: disconnecting guest, saving on host, and reconnecting...', flush=True)
    guest.call('coop.leave')
    ck(wait(lambda: players_count(host) == 1, 15.0), 'Part 2: host saw guest leave (players=1)')

    w0 = store_writes(host)
    save_out, refused, _ = twoinst.save_taken(host, 30.0, 'Part 2 host save')
    print(f'  Part 2 host save: {save_out} (refusals={refused})', flush=True)
    ck(save_out.startswith('saved (mode'), f'Part 2: host save accepted ({save_out})')
    # Wait until the save finishes writing and its box is dismissed
    t_sv = time.time()
    while time.time() - t_sv < 25.0:
        f1 = host_frame(host)
        time.sleep(0.5)
        if host_frame(host) == f1:
            host.call('key enter')
        box = host.call('coop.save box')['out']
        if store_writes(host) > w0 and 'saved_box=0' in box:
            break
    time.sleep(2.0)

    h2 = warm_stats(host)
    g2 = warm_stats(guest)
    print(f'  Part 2 pre-rejoin host:  {h2}', flush=True)
    print(f'  Part 2 pre-rejoin guest: {g2}', flush=True)

    join2 = guest.call(f'coop.join 127.0.0.1:{lane.PORT}')['out'].strip()
    print(f'  Part 2 guest join: {join2}', flush=True)

    # Wait for host to send a new MSG_WORLD_OFFER and guest to reload back into Cell
    ck(wait(lambda: (warm_stats(host) or {}).get('world_offers_sent', 0) > h2['world_offers_sent'], 45.0),
       'Part 2: host fell back to sending a new MSG_WORLD_OFFER after host save')
    ck(wait(lambda: (warm_stats(guest) or {}).get('world_offers_recv', 0) > g2['world_offers_recv'], 45.0),
       'Part 2: guest received the new MSG_WORLD_OFFER')
    ck(wait_cell_ready(host, guest, 90.0), 'Part 2: guest reloaded saved Cell world and partner cells returned')

    h3 = warm_stats(host)
    g3 = warm_stats(guest)
    print(f'  Part 2 post-rejoin host:  {h3}', flush=True)
    print(f'  Part 2 post-rejoin guest: {g3}', flush=True)

    ck(h3['world_offers_sent'] == h2['world_offers_sent'] + 1,
       f'Part 2: exactly 1 new MSG_WORLD_OFFER sent ({h2["world_offers_sent"]} -> {h3["world_offers_sent"]})')
    ck(h3['warm_resumes'] == h2['warm_resumes'],
       f'Part 2: invalidated receipt was NOT warm-resumed ({h2["warm_resumes"]} -> {h3["warm_resumes"]})')
    if h3['enabled']:
        ck(h3['warm_fallbacks'] >= h2['warm_fallbacks'] + 1,
           f'Part 2: host counted warm reconnect fallback ({h2["warm_fallbacks"]} -> {h3["warm_fallbacks"]})')

    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
