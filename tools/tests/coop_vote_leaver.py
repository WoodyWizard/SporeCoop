#!/usr/bin/env python3
"""A leaver's I'm ready flag is not inherited by the next player of its id (BUG-0772).

The host (Alice) and Bob play the fixture; the host opens an action vote (`coop.vote action`). A scripted guest
(tools/fakepeer.py's protocol: it never sends a player state, like a game still in the menu or loading the host's
world) joins, presses I'm ready (MSG_READY) and leaves. A second scripted guest joins and gets the same id (the
lowest free one). Bob clicks "I'm ready" in his co-op window. Checks, on the games:
* the vote still waits (the newcomer has not pressed ready): the host's vote is active, its action has not run, and
  the lobby table the host sends (what every co-op window shows) has the newcomer not ready; the host's and Bob's
  windows show ready 2/3;
* the newcomer presses ready: the vote passes and its action runs once on the host;
* no game logged a fault.
A regression test: the ticket's path was not reached on 0.7.12 either (the host's player_join makes the leaver's
Remote, so player_leave erases its flag; lab 20261112-111316 with the guard's first switch off).
"""
import re
import struct
import time

from coop_net_robust import Peer, keep_frames
from party import Party, Player
from twoinst import Checks, start
import lane

MSG_READY, MSG_LOBBY = 28, 29


def actions_run(host):
    m = re.search(r'actions run (\d+)', host.call('coop.vote')['out'])
    return int(m.group(1)) if m else -1


def vote_active(host):
    return host.call('coop.vote')['out'].startswith('vote active')


def wait_for(pred, secs, step=0.3):
    t0 = time.time()
    while time.time() - t0 < secs:
        if pred():
            return True
        time.sleep(step)
    return pred()


def lobby_ready(peer, pid):
    """the ready flag of `pid` in the last lobby table the host sent to `peer` (None: none yet)"""
    for typ, p in reversed(peer.got):
        if typ != MSG_LOBBY or not p:
            continue
        n = p[0]
        for i in range(n):
            if 1 + 4 * i + 4 > len(p):
                break
            eid, ready, _ = struct.unpack_from('<BBH', p, 1 + 4 * i)
            if eid == pid:
                return ready
        return None
    return None


def joined(peer, secs=20):
    return wait_for(lambda: peer.id is not None, secs)


def main():
    h, g = start()
    ck = Checks()
    p = Party(Player('Alice', h, lane.HOST, netname=''), [Player('Bob', g, lane.GUEST, lane.JOIN, '')], 'creature')
    bob = p.guests[0]
    n0 = actions_run(h)
    print('  host:', h.call('coop.vote action')['out'].strip().splitlines()[0], flush=True)
    ck(vote_active(h), 'the host opened an action vote')

    leaver = Peer('Leaver')
    ck(joined(leaver), f'a scripted guest that sends no player state joined (id {leaver.id})')
    if leaver.id is None:
        return ck.result()
    keep_frames(h, 8)
    leaver.send(MSG_READY, b'\x01')
    ck(wait_for(lambda: leaver.pending() == 0, 10), 'its I\'m ready reached the host')
    time.sleep(1.5)
    print(f'  with the leaver ready: host window "{p.vote_line(p.host)}"', flush=True)
    leaver.bye()
    ck(wait_for(lambda: not re.search(r'^\s*\[\d+\] Leaver', h.call('coop.status')['out'], re.M), 15), 'the scripted guest left')
    time.sleep(1)

    newcomer = Peer('Newcomer')
    ck(joined(newcomer), f'a second scripted guest joined (id {newcomer.id})')
    if newcomer.id is None:
        leaver.stop = True
        return ck.result()
    ck(newcomer.id == leaver.id, f'it got the leaver\'s id ({newcomer.id} = {leaver.id})')
    keep_frames(h, 8)
    ck(vote_active(h), 'the vote is still open')
    ck(p.vote_ready(bob), 'Bob clicks "I\'m ready"')
    time.sleep(4)
    still = vote_active(h)
    lines = {x.name: p.vote_line(x) for x in p.all}
    nr = lobby_ready(newcomer, newcomer.id)
    print(f'  after Bob\'s click: vote active {still}, actions run {actions_run(h)} (was {n0}), the newcomer ready in the '
          f'host\'s lobby table: {nr}; windows {lines}', flush=True)
    ck(still and actions_run(h) == n0, 'the vote waits for the newcomer (it did not pass with the leaver\'s flag)')
    ck(nr == 0, 'the lobby table every window shows has the newcomer not ready')
    ck(all('ready 2/3' in v for v in lines.values()), f'the host\'s and Bob\'s windows show ready 2/3: {lines}')

    newcomer.send(MSG_READY, b'\x01')
    ck(wait_for(lambda: not vote_active(h), 10), 'the newcomer presses ready: the vote passes')
    ck(wait_for(lambda: actions_run(h) == n0 + 1, 15), f'its action ran once ({n0} -> {actions_run(h)})')
    newcomer.bye()
    time.sleep(2)
    ck.faults(host=h, guest=g)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
