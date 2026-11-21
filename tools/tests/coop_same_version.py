#!/usr/bin/env python3
"""BUG-9284 (the private friends build): only the same SporeCoop version plays together.

The owner's test of 2026-11-20: a friend on the official 0.7.15.5 joined the friends build 0.7.15.94 with only a
"versions differ" warning; own progress, own cards and own cells did not work with it. On this build a guest whose
coop version differs from the host's is refused at its join, with a text on both games.

Both games in the galaxy menu (no world needed). `coop.sameversion as <v>` makes a game say it runs another coop version
in its next HELLO / WELCOME (a test seam: the rest of the version line stays).
  1. the same version: the guest joins;
  2. the guest says 0.7.15.5 with its own gate off (like an official 0.7.15.5 game, which has no gate): the host does
     not let it in (no second player on the host), its refusal counter is 1, the host's log names the guest and why
     ("was not let in: this build plays only with the same version"), the guest's log has the refusal text
     ("not the same version: ... has SporeCoop 0.7.15.94, you have SporeCoop 0.7.15.5");
  3. a host that says 0.7.15.5 with its gate off (an older host lets everybody in): this build's guest leaves at the
     host's WELCOME (its refusal counter is 1, no second player on the host afterwards);
  4. the twin: the gate off on both games (`coop.fix net.same_version off`): the other version joins with the warning
     only (0.7.15). On a build without the gate `coop.sameversion` is unknown and step 2 fails.
  5. no caught fault on either game.

  SPORE_LANE=N python3 tools/tests/coop_same_version.py
"""
import time

import lane
from lane import sh
from sporectl import Console
from twoinst import Checks, wait_up


def out(con, line):
    r = con.call(line)
    return (r.get('out') or r.get('error') or '').strip()


def wait(f, secs, step=1.0):
    t = time.time() + secs
    while time.time() < t:
        try:
            if f():
                return True
        except (OSError, ValueError, KeyError):
            pass
        time.sleep(step)
    return False


def log_text(inst):
    try:
        with open(lane.game_log(inst), encoding='utf-8', errors='replace') as f:
            return f.read()
    except OSError:
        return ''


def main():
    ck = Checks()
    sh('kill')
    time.sleep(2)
    sh('deploy')
    lane.run_host()
    time.sleep(2)
    lane.run_guest()
    for inst, run in ((lane.HOST, lane.run_host), (lane.GUEST, lane.run_guest)):
        if not wait_up(inst, 90):
            run()
            assert wait_up(inst), f'instance {inst} did not start'
    host, guest = Console(lane.HOST), Console(lane.GUEST)
    wait(lambda: 'GalaxyGameEntry' in out(host, 'game.mode') and 'GalaxyGameEntry' in out(guest, 'game.mode'), 180, 2)
    time.sleep(5)

    def joined():
        return 'players=2' in out(host, 'coop.status')

    def reset():
        out(guest, 'coop.leave')
        time.sleep(2)
        out(host, 'coop.leave')
        time.sleep(2)
        for con in (host, guest):
            out(con, 'coop.sameversion real')
            out(con, 'coop.fix net.same_version on')

    def session():
        out(host, lane.HOST_CMD)
        time.sleep(2)
        out(guest, lane.JOIN)

    reset()
    first = out(host, 'coop.sameversion')
    if not ck(first.startswith('same version: gate=1'), f'the host has the gate on ({first[:80]})'):
        return ck.result()
    session()
    ck(wait(joined, 30), '1: the same version joins')
    reset()

    out(guest, 'coop.fix net.same_version off')
    out(guest, 'coop.sameversion as 0.7.15.5')
    session()
    time.sleep(10)
    ck(not joined(), '2: another version: the host does not let the guest in')
    ck('refused=1' in out(host, 'coop.sameversion'), '2: the host counted one refusal')
    ck('was not let in: this build plays only with the same version' in log_text(lane.HOST),
       '2: the host reads who was not let in and why')
    ck('not the same version:' in log_text(lane.GUEST) and 'you have SporeCoop 0.7.15.5' in log_text(lane.GUEST),
       '2: the guest reads why it was refused (both versions)')
    reset()

    out(host, 'coop.fix net.same_version off')
    out(host, 'coop.sameversion as 0.7.15.5')
    session()
    time.sleep(10)
    ck(not joined(), '3: the guest leaves a host of another version')
    ck('refused=1' in out(guest, 'coop.sameversion'), '3: the guest counted one refusal')
    reset()

    out(host, 'coop.fix net.same_version off')
    out(guest, 'coop.fix net.same_version off')
    out(guest, 'coop.sameversion as 0.7.15.5')
    session()
    ck(wait(joined, 30), '4: the gate off (twin): another version joins with the warning only')
    reset()

    for name, con in (('host', host), ('guest', guest)):
        st = out(con, 'coop.status')
        ck(' faults=0' in st or 'faults' not in st, f'5: no caught fault on the {name}')
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
