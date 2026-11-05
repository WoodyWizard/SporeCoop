#!/usr/bin/env python3
"""Cell co-op ("Symbiosis") on two instances: one tide pool simulated by the host, guest mirrors,
guest bites applied by the host, shared food progress, knockout: revive by the partner or a small respawn next to it, regen."""
import os
import re
import time

from twoinst import Checks, shot, start


def pool(con):
    out = con.call('coop.pool')['out']  # avatar line + the cell(...) status line only
    d = {}
    for k in ('hp', 'local_gameplay_cells', 'food', 'mirrors', 'hits_in', 'hits_sent', 'revives', 'respawns',
              'purged', 'downed', 'own_points', 'own_eats'):
        m = re.search(k + r'=(-?\d+)', out)
        d[k] = int(m.group(1)) if m else None
    for k in ('size', 'target', 'body', 'partner_dist', 'revive_dist'):
        m = re.search(k + r'=(-?[\d.]+)', out)
        d[k] = float(m.group(1)) if m else None
    return d, out


def main():
    host, guest = start('cell')
    ck = Checks()
    time.sleep(5)
    # the host saved before sending the world: the game's "saved" box (pauses the game) is confirmed
    boxes = re.findall(r'id=0x06318E78 cmd=\S+ fl=0x([0-9A-F]+)', host.call('coop.ui tree 3')['out'])
    ck(not any(int(f, 16) & 1 for f in boxes), f'no "game saved" box left open on the host ({boxes})')
    h, ho = pool(host)
    g, go = pool(guest)
    # the counters belong to the game process: a warm run (lab --repeat) starts with the last run's
    rv0, rs0 = g['revives'] or 0, g['respawns'] or 0
    print(' ', ho.strip().replace('\n', ' | '))
    print(' ', go.strip().replace('\n', ' | '))
    ck((g['mirrors'] or 0) > 0, f'guest mirrors the host pool ({g["mirrors"]} cells)')
    ck(g['local_gameplay_cells'] is not None and g['local_gameplay_cells'] <= (g['mirrors'] or 0) + 1,
       f'guest has no cells of its own ({g["local_gameplay_cells"]} local for {g["mirrors"]} mirrors)')

    # the host swims around: its cell on the guest follows (position and facing)
    errs, rots, lags = [], [], []
    for i, (dx, dy) in enumerate([(25, 0), (0, 25), (-25, 0), (0, -25), (18, 18)]):
        host.call(f'coop.pool goto {dx} {dy}')
        for _ in range(6):
            time.sleep(0.4)
            hp_ = re.search(r'pos=\((-?[\d.]+) (-?[\d.]+)\)', host.call('coop.pool')['out'])
            st = guest.call('coop.status')['out']
            m = re.search(r"remote 0 'Alice'.*\n\s+puppet pos=\((-?[\d.]+) (-?[\d.]+) [^)]*\) err=([\d.]+) rot_err=(-?[\d.]+)", st)
            if m and hp_:
                errs.append(float(m.group(3)))
                rots.append(float(m.group(4)))
                lags.append(((float(m.group(1)) - float(hp_.group(1))) ** 2 + (float(m.group(2)) - float(hp_.group(2))) ** 2) ** 0.5)
            if i == 1 and _ == 3:
                shot(guest, 'cell_sync_guest.png')
                shot(host, 'cell_sync_host.png')
    print(f'  samples {len(errs)}: puppet vs remote state max {max(errs or [0]):.2f}, facing max {max(rots or [0]):.1f} deg, '
          f'vs the real host cell avg {sum(lags) / max(1, len(lags)):.2f} max {max(lags or [0]):.2f}')
    if len(errs) < 20:  # what the guest knows about the host (docs/known-issues.md)
        for name, con in (('guest', guest), ('host', host)):
            st = con.call('coop.status')['out']
            print(f'  {name} status:', ' | '.join(l.strip() for l in st.splitlines()
                                               if re.match(r'\s*(\[\d\]|remote |together |puppet |mode=|host |client|guest )', l)))
    ck(len(errs) >= 20, 'sync samples taken')
    ck(max(errs or [9]) < 0.3, f'the partner cell is exactly where its state says (max {max(errs or [9]):.2f})')
    r90 = sorted(rots)[int(len(rots) * 0.9) - 1] if rots else 999
    print('  facing errors:', ' '.join(f'{r:.0f}' for r in rots))
    ck(r90 < 10, f'and faces the same way (90% of samples within {r90:.1f} deg; a sharp turn can be caught mid-way)')
    ck(sum(lags) / max(1, len(lags)) < 2.0, f'close to the real host cell (avg {sum(lags) / max(1, len(lags)):.2f})')

    print(' ', guest.call('coop.pool bite')['out'])
    time.sleep(1)
    h2, _ = pool(host)
    ck((h2['hits_in'] or 0) > (h['hits_in'] or 0), f'the host applied the guest bite (hits_in {h["hits_in"]} -> {h2["hits_in"]})')

    f0 = h2['food']
    guest.call('coop.pool food 20')
    time.sleep(2)
    h3, _ = pool(host)
    g3, _ = pool(guest)
    # the host's own cell may eat a plant that floats into it meanwhile (BUG-0395: +12 in 1 of 130 runs)
    own = (h3['own_points'] or 0) - (h2['own_points'] or 0)
    ck(h3['food'] == f0 + 20 + own, f'shared food progress on the host ({f0} -> {h3["food"]}, the host ate +{own} itself)')
    for _ in range(6):  # the host sends its total every 0.5 s: a meal of its own may be on the way
        if g3['food'] == h3['food']:
            break
        time.sleep(0.5)
        g3, _ = pool(guest)
    ck(g3['food'] == h3['food'], f'guest has the same total ({g3["food"]})')

    # knockout far from the partner: nobody saves it at once
    print(' ', guest.call('coop.pool tp 9 0')['out'])
    time.sleep(1)
    print(' ', pool(guest)[1].splitlines()[0])
    host.call('coop.pool hurt 1 50')
    time.sleep(3)
    g4, go4 = pool(guest)
    print(' ', go4.splitlines()[0])
    ck(g4['downed'] == 1 and g4['hp'] == 1, f'a lethal bite knocks the guest out (downed {g4["downed"]}, hp {g4["hp"]})')
    ck('avatar_opacity=0.35' in go4, 'the knocked out cell is translucent for its owner')
    hk = host.call('coop.pool')['out']
    m = re.search(r'ko\[1\]=(\d) left=([\d.]+) hold=([\d.]+) op=([\d.-]+)', hk)
    print(' ', m.group(0) if m else hk)
    ck(m and m.group(1) == '1' and 0 < float(m.group(2)) < 10, 'the host sees the guest knocked out with a countdown')
    ck(m and m.group(4) == '0.35', 'and its cell translucent')
    shot(host, 'ko_host.png')
    shot(guest, 'ko_guest.png')
    # the partner comes right next to it: revive
    host.call('coop.pool tp 1 0')  # the host swims right up to the knocked out guest
    seen_hold = 0.0
    for i in range(8):
        time.sleep(0.5)
        host.call('coop.pool tp 1 0')
        m = re.search(r'ko\[1\]=1 left=[\d.]+ hold=([\d.]+)', host.call('coop.pool')['out'])
        if m and float(m.group(1)) > seen_hold:
            seen_hold = float(m.group(1))
            if seen_hold > 0.3 and seen_hold < 0.95:
                shot(host, 'ko_revive_host.png')
    ck(seen_hold > 0.1, f'the host sees the revive progress ({seen_hold:.2f})')
    time.sleep(1)
    g5, go5 = pool(guest)
    print(' ', go5.splitlines()[0])
    # 3, +1 regen every 3 s near the partner; a meal adds the food's hpValue up to 6 (docs/re/cell.md, AvatarEat):
    # BUG-0356's cold run had hp 6 after a plant eaten right after the revive (own_eats 1 -> 2), so a meal since the
    # knockout allows the full 6, and nothing else does
    ate = (g5['own_eats'] or 0) - (g4['own_eats'] or 0)
    hp_max = 6 if ate > 0 else 5
    ck(g5['downed'] == 0 and (g5['revives'] or 0) - rv0 == 1 and 3 <= (g5['hp'] or 0) <= hp_max,
       f'the partner right next to it revives it (revives +{(g5["revives"] or 0) - rv0}, hp {g5["hp"]}, meals since the '
       f'knockout {ate})')
    ck(g5['body'] == 1.0, 'a revived cell keeps its size')
    ck('revived' in host.call('coop.status')['out'], 'everybody sees the revive')

    # knockout again, nobody comes: after 10 s a small respawn next to the partner
    size0 = g5['target']
    time.sleep(3)  # a revived cell is invulnerable for 2 s
    guest.call('coop.pool tp 60 0')  # off the host's screen: an arrow on the edge
    time.sleep(1)
    host.call('coop.pool hurt 1 50')
    time.sleep(3)
    shot(host, 'ko_arrow_host.png')
    time.sleep(3)
    g6, _ = pool(guest)
    ck(g6['downed'] == 1 and (g6['respawns'] or 0) == rs0, 'still knocked out after 6 s')
    # the pool lives on after the respawn: the small cell drifts, gets pushed and eats what floats by
    # (seen: 23 m away and body 0.62 four seconds later), so the respawn is checked on the first sample
    for _ in range(40):
        g7, go7 = pool(guest)
        if (g7['respawns'] or 0) > rs0:
            break
        time.sleep(0.25)
    print(' ', go7.splitlines()[0])
    ck(g7['downed'] == 0 and (g7['respawns'] or 0) - rs0 == 1 and g7['hp'] == 6, f'respawn after 10 s (respawns +{(g7["respawns"] or 0) - rs0}, hp {g7["hp"]})')
    ck(g7['partner_dist'] is not None and 0 <= g7['partner_dist'] < 6, f'next to the partner (distance {g7["partner_dist"]})')
    ck(g7['body'] == 0.5 and g7['target'] < size0 * 0.6, f'small again (target size {size0} -> {g7["target"]})')
    for _ in range(16):  # the game smooths the size towards the target
        if g7['size'] is not None and g7['size'] < size0 * 0.75:
            break
        time.sleep(0.25)
        g7, _ = pool(guest)
    ck(g7['size'] < size0 * 0.75, f'the body really shrank (size {g7["size"]})')
    ck('respawned small' in host.call('coop.status')['out'], 'everybody sees the respawn')
    shot(guest, 'cell_small.png')
    shot(host, 'cell_small_host.png')
    hk = host.call('coop.pool')['out']
    ck(re.search(r'ko\[1\]=3 .* op=1.00', hk) is not None, 'the host sees it up again (opaque)')
    b0 = pool(guest)[0]['body'] or 0  # it may have eaten something on its own already
    guest.call('coop.pool food 20')
    time.sleep(1)
    g8, _ = pool(guest)
    grew = (g8['body'] or 0) - b0
    ck(0.15 < grew < 0.3 or g8['body'] == 1.0, f'eating grows it back (body {b0} -> {g8["body"]}, +0.01 per food point)')

    guest.call('coop.pool hp 3')
    time.sleep(7)
    g5, _ = pool(guest)
    ck((g5['hp'] or 0) > 3, f'regen next to the partner (hp 3 -> {g5["hp"]})')
    shot(host, 'cell_host.png')
    shot(guest, 'cell_guest.png')

    # Guest leave removes the host's mirrors from the game pool, not just the mod's list.
    # The local indices include a generation, so a new cell in the same slot is not the old mirror.
    leave_fix = os.environ.get('SPORE_CELL_LEAVE_FIX', '1')
    if leave_fix not in ('0', '1'):
        raise ValueError('SPORE_CELL_LEAVE_FIX must be 0 or 1')
    try:
        switch = guest.call(f'coop.pool leavefix {leave_fix}')
        ck(switch['ok'] and switch['out'].strip() == ('leave fix on' if leave_fix == '1' else 'leave fix off'),
           f'voluntary leave fix set to {leave_fix}')
        own_before = guest.call('coop.pool own')
        own = re.search(r'^avatar=(\d+) mate=(\d+) mate_alive=([01])$', own_before['out'].strip())
        ck(own_before['ok'] and own is not None and int(own.group(1)) != 0, 'guest avatar exists before leave')
        indices = []
        for _ in range(30):
            out = guest.call('coop.pool mirrors')['out']
            indices = re.findall(r'^mirror_local (\d+)$', out, re.MULTILINE)
            if indices:
                break
            time.sleep(1)
        indices = indices[::max(1, len(indices) // 10)][:10]
        ck(indices, f'guest has live mirror cells before leaving ({len(indices)} sampled)')
        leave = guest.call('coop.leave')
        ck(leave['ok'] and guest.call('coop.status')['out'].startswith('offline'), 'guest left the session')
        survivors = []
        for idx in indices:
            cell = guest.call(f'coop.pool cell {idx}')
            if not cell['ok'] or 'no such cell' not in cell['out']:
                survivors.append((idx, cell['out'].strip()))
        ck(indices and not survivors, f'the sampled mirror cells left the game pool (remaining: {survivors})')
        own_after = guest.call('coop.pool own')
        after = re.search(r'^avatar=(\d+) mate=(\d+) mate_alive=([01])$', own_after['out'].strip())
        ck(own is not None and own_after['ok'] and after is not None and after.group(1) == own.group(1),
           'the guest avatar remains in the pool after leave')
        if own and own.group(3) == '1':
            ck(after is not None and after.group(2) == own.group(2) and after.group(3) == '1',
               'the guest mate remains in the pool after leave')
        else:
            print('  no guest mate in the pool to check at leave')
    finally:
        guest.call('coop.pool leavefix 1')  # restore the default for warm runs
    frames = guest.call('wait.frames 30 10000')
    ck(frames['ok'], f'the guest continues drawing after leave ({frames["out"].strip()})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
