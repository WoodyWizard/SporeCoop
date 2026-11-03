#!/usr/bin/env python3
"""BUG-0451 review item 7: the relay releases the effect reference it was given (no leak).

`cEffectsManager::CreateEffect` writes the new effect into an intrusive pointer and AddRefs it for
the caller (EffectWorld::CreateEffect 0xA71300 calls the effect's AddRef, vtable slot 0x0). The
relay keep that reference until it forgets the effect (stopped, expired, ended, evicted, session
end) and then calls Release (vtable slot 0x4). Without it every received world effect stays
allocated for the session, even after Stop: the game counts 40 relayed effects a second for 60 s
here, about 2400 objects.

Two 60 s windows on the same receiver (the guest), at the relay rate limit:
  effects.release on   the relayed effects are stopped after 8 s and released: released == created,
                       the live list ends at 0, the private commit changes little;
  effects.release off  the old behaviour: released stays 0 and the private commit grows by the
                       leaked objects (about 3.5 KB each in the measured run).
The switch exists for this test (`coop.fix effects.release`).

  coop_effects_release.py   (SPORE_FIX_OFF=creature.effects_relay fails: nothing is relayed)
"""
import re
import time

from twoinst import Checks, start

SECS = 60


def busy(con):
    for line in con.call('coop.status')['out'].splitlines():
        if line.startswith('busy:'):
            return line.split(':', 1)[1].strip()
    return '?'


def free_games(h, g, seconds=30):
    t0 = time.time()
    while time.time() - t0 < seconds:
        if busy(h) == '-' and busy(g) == '-':
            return True
        for c in (h, g):
            c.call('key enter')
            c.call('click 1418 1044')
        time.sleep(1)
    return busy(h) == '-' and busy(g) == '-'


def commit_kb(con):
    m = re.search(r'commit_private_kb=(\d+)', con.call('mem.commit')['out'])
    return int(m.group(1)) if m else -1


def mem_line(con):
    # the support diagnostics: the `mem:` line is the game's free address space (the memory walk on
    # its thread; empty until the first minute) next to the private commit from `mem.commit`
    for line in con.call('coop.diag status')['out'].splitlines():
        if line.startswith('mem:'):
            return line.strip()
    return ''


def live(con):
    m = re.search(r'effects world live: live=(\d+) stopped=(\d+) expired=(\d+) released=(\d+) dropped=(\d+) '
                  r'release_fix=(\d)', con.call('coop.effects')['out'])
    return {k: int(v) for k, v in zip(('live', 'stopped', 'expired', 'released', 'dropped', 'fix'),
                                      m.groups() if m else ())} if m else {}


def played(con):
    m = re.search(r'effects: fix=\d sent=\d+ received=\d+ played=(\d+)', con.call('coop.effects')['out'])
    return int(m.group(1)) if m else 0


def window(h, g, on):
    g.call(f'coop.fix effects.release {"on" if on else "off"}')
    time.sleep(1)
    kb0, live0, played0, mem0 = commit_kb(g), live(g), played(g), mem_line(g)
    h.call(f'coop.effects load 40 1000 E1B24DDB')  # the relay's rate limit
    time.sleep(SECS)
    h.call('coop.effects load 0')
    time.sleep(12)  # the 8 s stop plus the list drain
    kb1, live1, played1, mem1 = commit_kb(g), live(g), played(g), mem_line(g)
    d = {'on': on, 'kb': kb1 - kb0, 'created': played1 - played0, 'released': live1.get('released', 0) - live0.get('released', 0),
         'dropped': live1.get('dropped', 0) - live0.get('dropped', 0), 'live': live1.get('live'), 'fix': live1.get('fix'),
         'kb0': kb0, 'kb1': kb1, 'mem0': mem0, 'mem1': mem1}
    print(f'  release {"on " if on else "off"}: created {d["created"]}, released {d["released"]}, live {d["live"]}, '
          f'commit {d["kb"]:+d} KB ({kb0} -> {kb1}), dropped {d["dropped"]}', flush=True)
    print(f'    {mem0}', flush=True)
    print(f'    {mem1}', flush=True)
    return d


def main():
    h, g = start('creature')
    ck = Checks()
    ck(g.call('coop.avatar')['ok'], 'the guest is in the host\'s world')
    ck(free_games(h, g), f'both games take input (busy: {busy(h)} / {busy(g)})')
    time.sleep(2)
    ck('fix' in live(g), f'coop.effects has the release counters ({live(g)})')
    on = window(h, g, True)
    off = window(h, g, False)
    ck(on['fix'] == 1, 'the release fix is on in the first window')
    ck(on['created'] > 2000, f'the relay ran at its rate limit for 60 s ({on["created"]} effects)')
    ck(on['released'] >= on['created'] * 0.95,
       f'every relayed effect was released ({on["released"]} of {on["created"]})')
    ck(on['live'] == 0, f'the live list ended empty ({on["live"]})')
    ck(off['fix'] == 0, 'the release fix is off in the second window')
    ck(off['created'] > 2000, f'the second window relayed as many ({off["created"]})')
    ck(off['released'] == 0, f'with the fix off nothing is released ({off["released"]})')
    ck(off['live'] == 0, f'the live list ends empty there too ({off["live"]})')
    # the leaked objects of the off window: about 3.5 KB each; the on window keeps the commit flat
    ck(off['kb'] - on['kb'] > 4096,
       f'the off window leaks much more memory than the on window ({off["kb"]:+d} vs {on["kb"]:+d} KB)')
    ck(on['dropped'] == 0 and off['dropped'] == 0, 'no reference is dropped on a world change here')
    ck.faults(host=h, guest=g)
    raise SystemExit(ck.result())


if __name__ == '__main__':
    main()
