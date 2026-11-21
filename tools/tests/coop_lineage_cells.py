#!/usr/bin/env python3
"""BUG-9275 (Lineages cells, ported from Lineages BUG-9111): "when you go up in the Cell stage, others like you start to
appear": the players' own cells appear in the host's Cell pool as NPC species, on both games.

What each game shows is read from the game itself: `coop.lineage` lists the live creature cells of the pool by the model
key the game draws them with (cell +0xFC), and for each cell of a player cell's model where it is, its model's diet (the
model data's carnivore / herbivore flags) and what its AI chases now (+0x22C, the target's kind from its .cell).

  1. the guest plays its own cell (its newest My Creations cell), the host the world's: both games are in the host's
     Cell world; the guest sends its other cells to the host (`guest_out`), the host lists the candidates: its own My
     Creations cells (from=host) and the guest's (from=guest:<id>); neither player's current cell is a candidate (its
     model signature is in the `current` list and no candidate has it);
  2. the host makes every native creature spawn a candidate (`coop.lineage share 100`, tests) and swims about: the host's
     pool holds cells of player models (live keys of the candidates) of the host's own cells and of the guest's; the
     guest's game draws the same cells from its own keys of those models (`remap <host key> -> <ours>` and live cells of
     those keys); screenshots of both games next to one;
  3. behaviour by diet: a carnivore-only model's cell never chases a plant, an AI with a herbivore model never chases
     meat or a creature (sampled for 60 s; at least one chase seen);
  4. cell.lineage_cells off (`coop.settings force`): the guest gets the setting; swimming into new tiles makes no swap
     (the host's `swapped` counter stays); on again: swaps again;
  5. no faults on either game.

A new feature: on main `coop.lineage` is an unknown command and step 1 fails (the twin is step 4: the setting off).
"""
import re
import time

import settings_lib as sl
from twoinst import Checks, shot, start


def out(con, line):
    return con.call(line).get('out', '')


def wait(f, secs, step=1.0):
    t = time.time() + secs
    while time.time() < t:
        try:
            if f():
                return True
        except (OSError, ValueError, TypeError, KeyError):
            pass
        time.sleep(step)
    return False


def lineage(con):
    s = out(con, 'coop.lineage')
    d = {'raw': s, 'cands': [], 'live': {}, 'pcells': [], 'remap': {}, 'current': set(), 'current_keys': set()}
    for k in ('pool', 'swapped', 'considered', 'guest_out', 'sent'):
        m = re.search(r'\b' + k + r'=(\d+)', s)
        d[k] = int(m.group(1)) if m else -1
    for ln in s.splitlines():
        m = re.match(r'cell (\w{8}) sig=(\w+) (\S\S) .* from=(\S+)', ln)
        if m:
            d['cands'].append({'key': m.group(1), 'sig': m.group(2), 'diet': m.group(3), 'from': m.group(4)})
        if ln.startswith('live'):
            d['live'] = {k: int(v) for k, v in re.findall(r'(\w{8})=(\d+)', ln)}
        m = re.match(r'pcell \d+ model=(\w+) diet=(\S+) d=([\d.]+) dx=(\S+) dy=(\S+) chase=\d+ chase_kind=(\S+)', ln)
        if m:
            d['pcells'].append({'model': m.group(1), 'diet': m.group(2), 'd': float(m.group(3)), 'dx': float(m.group(4)),
                                'dy': float(m.group(5)), 'chase': m.group(6)})
        m = re.match(r'remap (\w+) -> (\w+)', ln)
        if m:
            d['remap'][m.group(1)] = m.group(2)
        m = re.match(r'current (\S+) keys (\S+)', ln)
        if m:
            d['current'] = set() if m.group(1) == 'none' else set(m.group(1).split(','))
            d['current_keys'] = set() if m.group(2) == 'none' else set(m.group(2).split(','))
    return d


def own_cell(guest):
    """the guest plays its newest own cell (from its My Creations)"""
    lst = out(guest, 'coop.creations list')
    keys = re.findall(r'cell\s+(\w{8})', lst) or re.findall(r'(\w{8})', lst.split('cell', 1)[-1])
    m = re.search(r'mine cell: (\w{8})', out(guest, 'coop.creations'))
    if keys:
        guest.call(f'coop.creations choose cell {keys[0]}')
    return keys[0] if keys else (m.group(1) if m else None)


def roam(host, guest, k, sign=1):
    host.call('coop.pool goto %d %d' % ((15 * sign, 0) if k % 4 < 2 else (0, 15 * sign)))
    guest.call('coop.pool tp 3 0')


def swim_to(con, models, tag):
    """swim next to the nearest cell of one of these models; a screenshot"""
    for _ in range(8):
        near = [p for p in lineage(con)['pcells'] if p['model'] in models]
        if not near:
            return False
        n = min(near, key=lambda p: p['d'])
        if n['d'] < 3.5:
            break
        con.call('coop.pool goto %.1f %.1f' % (n['dx'] * 0.85, n['dy'] * 0.85))
        time.sleep(3)
    shot(con, tag)
    return True


def main():
    host, guest = start('cell', own_progress=True)
    for con in (host, guest):  # BUG-9285: this test checks the My Creations pool of BUG-9275 (the old behaviour)
        con.call('coop.fix cell.lineage_current off')
    ck = Checks()
    try:
        mine = own_cell(guest)
        ck(wait(lambda: lineage(host)['pool'] > 0 and lineage(guest)['guest_out'] > 0, 90, 3),
           f'the host lists player cells for its pool, the guest sent its own (guest played {mine})')
        time.sleep(20)  # the next build (15 s) knows the guest's current cell
        h = lineage(host)
        froms = {c['from'].split(':')[0] for c in h['cands']}
        print('  candidates: ' + ', '.join(f"{c['key']} {c['diet']} {c['from']}" for c in h['cands']), flush=True)
        ck('guest' in froms, f'the guest\'s own cells are candidates ({sorted(froms)})')
        ck('host' in froms or 'own=0' in out(host, 'coop.creations'), f'the host\'s own cells are candidates ({sorted(froms)})')
        ck(len(h['current']) >= 2 and not any(c['sig'] in h['current'] for c in h['cands']),
           f"neither player's current cell is a candidate (current {sorted(h['current'])})")
        host.call('coop.lineage share 100')
        for k in range(16):
            live = lineage(host)['live']
            keys = {c['key']: c['from'].split(':')[0] for c in h['cands']}
            seen = {keys[m] for m in live if m in keys}
            if 'guest' in seen and ('host' in seen or 'host' not in froms):
                break
            roam(host, guest, k)
            time.sleep(6)
        h = lineage(host)
        keys = {c['key']: c['from'].split(':')[0] for c in h['cands']}
        seen = {keys[m] for m in h['live'] if m in keys}
        ck('guest' in seen, f"the host's pool holds cells of the guest's cell models (live {h['live']})")
        ck('host' in seen or 'host' not in froms, "the host's pool holds cells of the host's cell models")
        g = lineage(guest)
        mapped = {g['remap'][k]: keys[k] for k in keys if k in g['remap']}
        gseen = {mapped[m] for m in g['live'] if m in mapped}
        ck(seen and gseen == seen, f"the guest's game draws the same player models from its own keys (live {g['live']}, "
                                    f"remap {g['remap']})")
        own = re.findall(r'player \d+ cell: (\w{8})', out(host, 'coop.creations'))
        own = [k for k in own if k.startswith('7C')]  # a guest's current cell (the world's own species spreads natively)
        bad = [m for m in h['live'] if m in h['current_keys'] or m in own]
        ck(not bad, f"no NPC of a player's current cell in the host's pool (current {sorted(h['current_keys'])} {own}, live {h['live']})")
        swim_to(guest, set(m for m, f in mapped.items() if f == 'guest'), 'lineage_cells_guest_sees_guest_model')
        swim_to(host, set(k for k, f in keys.items() if f == 'host') or set(keys), 'lineage_cells_host_sees_host_model')
        chases = []
        t0 = time.time()
        while time.time() - t0 < 60:
            chases += [(p['diet'], p['chase']) for p in lineage(host)['pcells'] if p['chase'] != 'none']
            time.sleep(4)
        print('  chases: ' + ', '.join(sorted({f'{d}:{k}' for d, k in chases})), flush=True)
        ck(chases, 'player-model cells chase something (their AI runs)')
        ck(not any(d == 'C-' and k == 'plant' for d, k in chases), 'a carnivore model never chases a plant')
        ck(not any(d == '-H' and k in ('meat', 'creature') for d, k in chases), 'a herbivore model never chases meat')
        ck(sl.force(host, [guest], 'cell.lineage_cells', 'off'), 'the guest plays with cell.lineage_cells off')
        s0 = lineage(host)['swapped']
        for k in range(8):
            roam(host, guest, k, -1)
            time.sleep(6)
        s1 = lineage(host)['swapped']
        ck(s1 == s0, f'off: no native spawn takes a player cell ({s0} -> {s1})')
        sl.force(host, [guest], 'cell.lineage_cells', 'on')
        for k in range(8):
            roam(host, guest, k)
            time.sleep(6)
        ck(lineage(host)['swapped'] > s1, 'on again: swaps again')
    finally:
        sl.reset([host, guest], ('cell.lineage_cells',))
        try:
            host.call('coop.lineage share 17')
        except OSError:
            pass
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
