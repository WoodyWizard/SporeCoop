#!/usr/bin/env python3
"""BUG-0812: `coop.ui show|btn|font|caption|tree <hex>` with a number that is no window of the UI tree must not
call into it. The commands took any number as a window pointer and called its vtable: a control id (what
`coop.ui where` and the layouts print) or a typo was a wild pointer and the game faulted.

One game (the lane's host instance) at the galaxy menu:
1. a window address from `coop.ui tree` still works (`coop.ui btn <addr>` answers, `coop.ui tree 1 all <addr>`
   lists it);
2. its control id is taken as that window (`coop.ui tree 0 all <id>` names the same address);
3. numbers that are neither (of 12345678, 00000010, DEADBEE0, ... those no window of the menu's whole tree has as its
   address or control id: the menu has a window with control id 00000010) answer "no window" for show, btn, font,
   caption, area, align and the tree root;
4. the game has no fault (the game's own exception count, `exc`), no crash report, frames go on.
SPORE_FIX_OFF=core.ui_window_arg must FAIL (the game faults at the first wild number).

  SPORE_LANE=N python3 tools/tests/coop_ui_bad_arg.py
"""
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
import lane  # noqa: E402
from sporectl import Console  # noqa: E402
from twoinst import Checks, wait_up  # noqa: E402

BAD = ('12345678', '00000010', 'DEADBEE0', '7E57C0DE', '0000BEE5', '6E0B0D1E')


def call(c, line):
    try:
        return c.call(line)['out'].strip()
    except Exception as e:  # noqa: BLE001 (a game that died answers nothing)
        return f'no answer ({e.__class__.__name__})'


def frame(c):
    m = re.search(r'frame=(\d+)', call(c, 'frame'))
    return int(m.group(1)) if m else None


def main():
    ck = Checks()
    lane.sh('kill')
    time.sleep(2)
    lane.sh('deploy')
    lane.run_host()
    ck(wait_up(lane.HOST, 180), 'the game starts')
    h = Console(lane.HOST)
    for _ in range(180):
        if 'GalaxyGameEntry' in call(h, 'game.mode'):
            break
        time.sleep(1)
    h.call('wait.frames 300 60000')
    for name in [n.strip() for n in os.environ.get('SPORE_FIX_OFF', '').split(',') if n.strip()]:
        print('  SPORE_FIX_OFF: ' + call(h, f'coop.fix {name} off'), flush=True)
    tree = call(h, 'coop.ui tree 3')
    rows = re.findall(r'^\s*(\S+) ([0-9A-Fa-fx]+) id=0x([0-9A-F]{8})', tree, re.M)
    ids = {}
    for _, addr, cid in rows:
        ids.setdefault(cid, []).append(addr)
    pick = next(((a[0], c) for c, a in ids.items() if len(a) == 1 and c != '00000000'), None)
    ck(pick is not None, f'a window with a control id of its own in the menu ({len(rows)} windows)')
    if pick:
        addr, cid = pick
        addr = addr[2:] if addr.lower().startswith('0x') else addr
        out = call(h, f'coop.ui btn {addr}')
        print(f'  btn {addr}: {out[:120]}', flush=True)
        ck('no window' not in out and 'no answer' not in out, f'1. a window address still works ({out[:60]})')
        out = call(h, f'coop.ui tree 0 all {addr}')
        ck(addr.upper().lstrip('0') in out.upper(), f'1. the tree from that window lists it ({out[:80]})')
        out = call(h, f'coop.ui tree 0 all {cid}')
        print(f'  tree by control id {cid}: {out[:120]}', flush=True)
        ck(addr.upper().lstrip('0') in out.upper(), f'2. its control id {cid} is that window ({out[:80]})')
    full = call(h, 'coop.ui tree 40 all')
    taken = {a.upper().lstrip('0X').lstrip('0') for a in re.findall(r'^\s*\S+ ([0-9A-Fa-fx]+) id=', full, re.M)} | \
            {c.lstrip('0') for c in re.findall(r'id=0x([0-9A-F]{8})', full)}
    bad = [n for n in BAD if n.lstrip('0') not in taken][:4]
    print(f'  numbers that are no window of the tree: {bad} (of {len(BAD)})', flush=True)
    ck(len(bad) >= 3, 'three numbers no window has')
    f0 = frame(h)
    for n in bad:
        for line in (f'coop.ui show {n} 1', f'coop.ui btn {n}', f'coop.ui font {n} 1', f'coop.ui caption {n} x',
                     f'coop.ui area {n} 0 0 10 10', f'coop.ui align {n} 1', f'coop.ui tree 1 all {n}'):
            out = call(h, line)
            print(f'  {line}: {out[:100]}', flush=True)
            ck('no window' in out, f'3. "{line}" answers no window')
            if 'no answer' in out:
                break
        if 'no answer' in out:
            break
    time.sleep(2)
    f1 = frame(h)
    ck(f0 is not None and f1 is not None and f1 > f0, f'4. frames go on ({f0} -> {f1})')
    if f1 is not None:
        ck.faults(host=h)
    else:
        ck(False, '4. the game is still running')
    lane.sh('kill')
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
