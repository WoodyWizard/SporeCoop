#!/usr/bin/env python3
"""coop.fix: one switch command over every bug fix's test switch (BUG-0116, BUG-0214).

One game of the lane, at the galaxy menu:
  * `coop.fix list` names every switch registered in the sources (COOP_FIX and COOP_FIX_UNSAFE
    lines, mods/coop/fix_table.cpp included), all on, the unsafe ones marked;
  * `coop.fix all off` turns every safe one off and skips the unsafe ones ("N off, M unsafe
    skipped"), `all off --unsafe` turns every one off, `coop.fix all on` back on;
  * a switch and its old command see the same bool (`coop.pool fix`, `coop.npc strays`,
    `coop.worldfix`, `coop.corefix`, `coop.creations worlddrop`, `coop.fleet keepposse`), both ways;
  * an unknown name is refused, an old issue's name gets the registry name as a hint.

  SPORE_LANE=6 python3 tools/tests/coop_fix_registry.py"""
import glob
import os
import re
import sys

import lane
from diag_check import start_host
from twoinst import Checks

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def source_names():
    """{name: unsafe} of every registration in the sources"""
    names = {}
    for p in sorted(glob.glob(os.path.join(ROOT, 'mods', 'coop', '*.cpp'))):
        for unsafe, n in re.findall(r'COOP_FIX(_UNSAFE)?\(\s*"([^"]+)"', open(p, encoding='utf-8').read()):
            names[n] = bool(unsafe)
    return names


def states(c):
    out = c.call('coop.fix list')['out']
    st = {}
    for line in out.splitlines():
        m = re.match(r'^(\S+\.\S+) (on|off) ', line)
        if m:
            st[m.group(1)] = m.group(2) == 'on'
    return out, st


def main():
    ck = Checks()
    names = source_names()
    unsafe = sorted(n for n, u in names.items() if u)
    safe = len(names) - len(unsafe)
    print(f'  {len(names)} switches in the sources, {len(unsafe)} unsafe')
    ck(len(names) >= 50, 'the sources register the fix switches (COOP_FIX)')
    c = start_host(ck)
    try:
        out, st = states(c)
        print('  ' + out.splitlines()[0] if out else '  (no answer)')
        ck(set(st) == set(names), f'coop.fix list shows every registered switch ({len(st)} of {len(names)}; '
           f'missing {sorted(set(names) - set(st))[:5]})')
        ck('refused' not in out.splitlines()[0], 'no refused (duplicate) registration')
        ck(st and all(st.values()), 'every fix is on by default')

        ck(all(re.search(r'^%s on \S+(?: \S+)* unsafe  ' % re.escape(n), out, re.M) for n in unsafe),
           'coop.fix list marks the unsafe switches')
        ck('all %d fixes off, %d unsafe skipped' % (safe, len(unsafe)) in c.call('coop.fix all off')['out'],
           'coop.fix all off answers (unsafe skipped)')
        _, st = states(c)
        ck(st and all(st[n] == names[n] for n in st), 'after all off: every safe switch off, every unsafe one on')
        ck('all %d fixes off, 0 unsafe skipped' % len(names) in c.call('coop.fix all off --unsafe')['out'],
           'coop.fix all off --unsafe answers')
        _, st = states(c)
        ck(st and not any(st.values()), 'after all off --unsafe: every switch off')
        # the old commands read the same bools
        ck('purge=0' in c.call('coop.pool fix')['out'], 'coop.pool fix shows purge off')
        ck('busy_slot 0' in c.call('coop.worldfix')['out'], 'coop.worldfix shows busy_slot off')
        ck('vote_scope 0' in c.call('coop.corefix')['out'], 'coop.corefix shows vote_scope off')

        ck('all %d fixes on' % len(names) in c.call('coop.fix all on')['out'], 'coop.fix all on answers')
        _, st = states(c)
        ck(st and all(st.values()), 'after all on: every switch on again')
        ck('purge=1' in c.call('coop.pool fix')['out'], 'coop.pool fix shows purge on')

        # one switch, both ways
        c.call('coop.npc strays off')
        _, st = states(c)
        ck(st.get('npc.strays') is False, 'coop.npc strays off shows in coop.fix')
        c.call('coop.fix npc.strays on')
        c.call('coop.fix pool.hostof off')
        ck('hostof=0' in c.call('coop.pool fix')['out'], 'coop.fix pool.hostof off shows in coop.pool fix')
        c.call('coop.pool fix hostof 1')
        _, st = states(c)
        ck(st.get('npc.strays') is True and st.get('pool.hostof') is True, 'both back on')

        # BUG-0214: the switches that were outside the registry, through their old commands
        for cmd, name in (('coop.creations worlddrop off', 'creations.world_drop'),
                          ('coop.fleet keepposse 0', 'fleet.posse'),
                          ('coop.corefix async_resolve 0', 'net.async_resolve')):
            c.call(cmd)
            _, st = states(c)
            ck(st.get(name) is False, f'{cmd} shows as {name} off')
            c.call(f'coop.fix {name} on')
        r = c.call('coop.fix no.such off')
        ck('unknown fix' in r['out'], 'an unknown name is refused')
        r = c.call('coop.fix session_end off')
        ck('did you mean session.end' in r['out'], 'an old name gets the registry name as a hint')
        _, st = states(c)
        ck(st and all(st.values()), 'every switch on at the end')
    finally:
        c.call('coop.fix all on')
        lane.sh('kill', check=False)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
