#!/usr/bin/env python3
"""Cell: concurrent editor spends and own-wallet spends cannot drive Cell DNA negative (BUG-1538).

A player report (0.7.16.1) showed a Cell player's DNA going negative after the first Cell editor update.
In Cell, cCellSerializableData+0x2C stores `spent`, and displayed DNA is `DNAForFood(food) - spent`.
Selling starting parts can produce negative `spent` (giving extra DNA), so only the upper bound
`spent <= DNAForFood(food)` may be clamped.

1. Shared DNA (`all.shared_dna shared`): host sets spent to leave 5 DNA (`coop.pool spend set`), then
   guest reports a concurrent Cell editor spend (`coop.pool spend fake`) exceeding the remaining 5 DNA.
   PASS: both host and guest clamp `spent <= DNAForFood(food)` and keep `dna >= 0` (clamped >= 1 on host).
   SPORE_FIX_OFF=cell.dna_nonneg (cell_dna_nonneg_off): host spent overshoots DNAForFood(food) and DNA
   goes negative on both host and guest: must FAIL.
2. Selling starting parts (`spent < 0`): host sets `spent = -10`: both host and guest keep `spent == -10`
   (clamping does not destroy part-sell refunds).
3. Own DNA (`all.shared_dna own`): guest reports an editor spend exceeding its DNA: both host's wallet
   record and guest's own `spent` stay clamped (`dna >= 0`).

  SPORE_LANE=2 python3 tools/tests/coop_cell_dna_nonneg.py
"""
import re
import time

import settings_lib as sl
from harness import waiter
from twoinst import Checks, start

wait = waiter(step=0.5)


def spend_info(con):
    out = con.call('coop.pool spend')['out'].strip()
    d = {'raw': out}
    for k in ('sent', 'got', 'clamped', 'spent', 'dna', 'food'):
        m = re.search(rf'\b{k}=(-?\d+)', out)
        if m:
            d[k] = int(m.group(1))
    return d


def main():
    host, guest = start('cell')
    ck = Checks()
    time.sleep(3)

    # 1. Shared DNA concurrent overspend
    ck(sl.force(host, [guest], 'all.shared_dna', 'shared'), 'shared DNA active on both')
    time.sleep(2)
    h0 = spend_info(host)
    g0 = spend_info(guest)
    print(f'  initial host:  {h0["raw"]}', flush=True)
    print(f'  initial guest: {g0["raw"]}', flush=True)
    ck('spent' in h0 and 'dna' in h0 and 'food' in h0, 'host Cell spend state readable')
    if 'spent' not in h0:
        return ck.result()

    # Exact DNAForFood(food) = dna + spent (when dna > 0)
    dna_total = h0['dna'] + h0['spent']
    if dna_total < 10:
        # Spend floor works even at 0 food, but use a positive offset for clarity
        leave = 0
        overspend = 25
    else:
        leave = 5
        overspend = dna_total + 15
    target_host_spent = dna_total - leave

    print(f'  dna_total={dna_total}: host spend set {target_host_spent}, guest spend fake {overspend}', flush=True)
    host.call(f'coop.pool spend set {target_host_spent}')
    time.sleep(1)
    got0 = spend_info(host)['got']
    clamped0 = spend_info(host)['clamped']
    print('  guest fake:', guest.call(f'coop.pool spend fake {overspend}')['out'].strip(), flush=True)

    got_ok = wait(lambda: spend_info(host)['got'] > got0, 20)
    ck(got_ok, f'host received guest spend (got {got0} -> {spend_info(host)["got"]})')
    time.sleep(3)  # let wsync propagate host spent back to guest

    h1 = spend_info(host)
    g1 = spend_info(guest)
    print(f'  after shared overspend host:  {h1["raw"]}', flush=True)
    print(f'  after shared overspend guest: {g1["raw"]}', flush=True)
    h1_total = h1['dna'] + h1['spent'] if h1['dna'] > 0 else dna_total
    # If food rose while waiting, recompute allowable upper bound safely: raw DNA = DNAForFood - spent must be >= 0
    ck(h1['dna'] >= 0 and h1['spent'] <= max(dna_total, h1_total) + (h1['food'] - h0['food']) * 5,
       f'host Cell DNA did not go negative (spent={h1["spent"]}, dna={h1["dna"]}, dna_total0={dna_total})')
    ck(g1['dna'] >= 0 and g1['spent'] <= max(dna_total, h1_total) + (g1['food'] - g0['food']) * 5,
       f'guest Cell DNA did not go negative (spent={g1["spent"]}, dna={g1["dna"]}, dna_total0={dna_total})')
    ck(h1['clamped'] > clamped0,  # counter: confirms the shared-pool overspend clamp triggered; spent and dna are checked above
       f'host clamped the concurrent overspend (clamped {clamped0} -> {h1["clamped"]})')

    # 2. Selling starting parts (spent < 0) must stay intact
    host.call('coop.pool spend set -10')
    g_refund = wait(lambda: spend_info(guest)['spent'] == -10, 15)
    h2 = spend_info(host)
    g2 = spend_info(guest)
    print(f'  part-sell refund host:  {h2["raw"]}', flush=True)
    print(f'  part-sell refund guest: {g2["raw"]}', flush=True)
    ck(h2['spent'] == -10 and g_refund and g2['spent'] == -10,
       f'negative spent from selling starting parts preserved (host={h2["spent"]}, guest={g2["spent"]})')

    # 3. Own DNA wallet overspend stays non-negative
    host.call('coop.pool spend set 0')
    time.sleep(1)
    ck(sl.force(host, [guest], 'all.shared_dna', 'own'), 'own DNA active on both')
    time.sleep(2)
    got1 = spend_info(host)['got']
    g_before_own = spend_info(guest)
    own_over = max(g_before_own['dna'] + g_before_own['spent'], 20) + 25
    print('  guest own fake:', guest.call(f'coop.pool spend fake {own_over}')['out'].strip(), flush=True)
    ck(wait(lambda: spend_info(host)['got'] > got1, 20), 'host received guest own-wallet spend')
    time.sleep(3)
    g3 = spend_info(guest)
    h3 = spend_info(host)
    print(f'  after own overspend host:  {h3["raw"]}', flush=True)
    print(f'  after own overspend guest: {g3["raw"]}', flush=True)
    ck(g3['dna'] >= 0 and g3['spent'] <= max(dna_total, g_before_own['dna'] + g_before_own['spent']) + max(0, g3['food'] - g0['food']) * 5,
       f'guest own-wallet Cell DNA did not go negative (spent={g3["spent"]}, dna={g3["dna"]})')

    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
