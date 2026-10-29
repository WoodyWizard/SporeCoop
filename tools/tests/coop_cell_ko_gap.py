#!/usr/bin/env python3
"""Cell stage: a knocked-out partner stays knocked out through a late refresh (BUG-0238).

The owner of a knockout refreshes it every 0.25 s (MSG_CELL_KO, reliable). BUG-0061 ended a remote
knockout after 2 s without a refresh, so one late refresh (a retransmit under loss, a hitch) drew
the partner "up" (opaque), made it a helper again (BUG-0131's rule broken for that moment) and the
next refresh replayed its death effect. The fix keeps it to its own deadline (`left` + 2 s).

`coop.pool kodrop <s>` on the host ignores MSG_CELL_KO for <s> seconds while the guest's position
states keep coming (the guest stays in the host's partners, so the window is exact).

1. The guest is knocked out far from the host (no revive).
2. Host: kodrop 2.6. The host's game draws the guest's cell translucent (op 0.35) in every sample
   through the gap and 1 s after it, and no second death effect plays (`fx` of `ko[1]`).
3. Host: kodrop 3, and 2.5 s into it the host takes a lethal bite: its only partner is knocked out,
   so the host is not knocked out (the game's own death runs, BUG-0131).

SPORE_FIX_OFF=pool.ko_grace must FAIL (op 1.00 after 2 s, a second effect, the host knocked out).
"""
import re
import time

from twoinst import Checks, start


def num(out, key):
    m = re.search(r'\b' + key + r'=(-?\d+(?:\.\d+)?)', out)
    return float(m.group(1)) if m else None


def pool(con):
    return con.call('coop.pool')['out']


def ko_line(out, pid=1):
    m = re.search(r'^ko\[%d\]=(\d+) .*op=(-?[\d.]+) fx=(\d+)' % pid, out, re.MULTILINE)
    return (int(m.group(1)), float(m.group(2)), int(m.group(3))) if m else None


def puppet_op(out, pid=1):
    m = re.search(r'^puppet %d op=([\d.]+)' % pid, out, re.MULTILINE)
    return float(m.group(1)) if m else None


def wait(cond, secs, step=0.25):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return None


def sample(host, secs, label):
    """the host's view of the guest every 0.1 s: (t, puppet op, ko state, fx)"""
    rows, t0 = [], time.time()
    while time.time() - t0 < secs:
        host.call('coop.pool goto 0 0')  # the host stays where it is (it must not come to revive the guest)
        out = pool(host)
        k = ko_line(out)
        rows.append((round(time.time() - t0, 2), puppet_op(out), k[0] if k else None, k[2] if k else None))
        time.sleep(0.1)
    bad = [r for r in rows if r[1] is None or abs(r[1] - 0.35) > 0.02 or r[2] != 1]
    print(f'  {label}: {len(rows)} samples, {len(bad)} not knocked out; first bad {bad[:3]}')
    return rows, bad


def main():
    host, guest = start('cell')
    ck = Checks()
    ck(wait(lambda: (num(pool(guest), 'mirrors') or 0) > 5, 30), 'the guest mirrors the host pool')
    host.call('coop.pool goto 0 0')

    def far():  # the guest 30 m from the host (tp needs the host in the guest's partners)
        print('  guest: ' + guest.call('coop.pool tp 30 0')['out'].strip())
        time.sleep(1.0)
        return (num(pool(guest), 'partner_dist') or 0) > 20
    ck(wait(far, 20, 0.5), 'the guest is 30 m from the host')
    print('  guest: ' + ' '.join(re.findall(r'\b(?:pos=\([^)]*\)|partner_dist=[\d.]+)', pool(guest))))
    host.call('coop.pool hurt 1 50')
    ck(wait(lambda: num(pool(guest), 'downed') == 1, 5), 'the guest is knocked out')
    k0 = wait(lambda: ko_line(pool(host)) if (ko_line(pool(host)) or (0,))[0] == 1 else None, 3)
    ck(k0 is not None, f'the host sees the guest knocked out ({k0})')
    if not k0:
        ck.faults(host=host, guest=guest)
        return ck.result()
    fx0 = k0[2]
    # 2. a gap of 2.6 s in the refreshes, then 1 s more
    print('  host: ' + host.call('coop.pool kodrop 2.6')['out'])
    rows, bad = sample(host, 3.6, 'gap 2.6 s')
    ck(not bad, f'the host draws the guest knocked out through the gap ({len(bad)} of {len(rows)} samples not)')
    g = pool(guest)
    print('  guest: ' + ' '.join(re.findall(r'\b(?:downed|partner_dist|revives|respawns)=[\d.]+', g)))
    ck(num(g, 'downed') == 1, 'the guest is still knocked out in its own game (the gap was a gap, not a revive)')
    k1 = ko_line(pool(host))
    ck(k1 is not None and k1[2] == fx0, f'no second death effect after the gap (fx {fx0} -> {k1 and k1[2]})')
    # 3. a lethal bite on the host 2.5 s into another gap: the guest is no helper
    print('  host: ' + host.call('coop.pool kodrop 3')['out'])
    rows, bad = sample(host, 2.5, 'gap 3 s')
    ck(not bad, f'still knocked out at 2.5 s into the gap ({len(bad)} of {len(rows)} samples not)')
    g = pool(guest)
    ck(num(g, 'downed') == 1, 'the guest is still knocked out in its own game before the host\'s lethal bite')
    r = host.call('coop.pool selfhit 250')['out']
    print('  host: ' + r)
    ck(num(r, 'downed') == 0, f'the host is not knocked out: its only partner is knocked out ({r})')
    time.sleep(1.0)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
