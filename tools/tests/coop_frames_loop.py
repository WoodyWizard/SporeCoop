#!/usr/bin/env python3
"""Frames from the game loop (issue #4): another overlay (the Steam overlay while Steam is online)
can keep Present from SporeKit; the game draws, SporeKit saw no frame and the mod stopped (no Co-op
item, F9 did nothing, no network). SporeKit 0.3.3 then runs the frames from App::Canvas::Tick.
`frames.source loop` makes the guest ignore Present, as in that case; co-op must keep working."""
import re
import time

from twoinst import Checks, host_frame, start


def npc_hits(host):
    m = re.search(r'hits_applied=(\d+)', host.call('coop.npc list')['out'])
    return int(m.group(1)) if m else -1


def main():
    host, guest = start()
    ck = Checks()
    time.sleep(5)
    out = guest.call('frames.source loop')['out']
    time.sleep(3)
    out = guest.call('frames.source')['out']
    ck('game loop' in out, f'guest: frames come from the game loop ({out.strip()})')
    f1 = host_frame(guest)
    time.sleep(3)
    f2 = host_frame(guest)
    ck(f2 > f1 + 30, f'guest: frames go on without Present ({f1} -> {f2})')

    st = guest.call('coop.status')['out']
    ck('connected' in st, 'guest: still connected')
    ck(re.search(r"remote 0 '.*' puppet=(?!00000000)", st) is not None, 'guest: the host puppet is there')

    # a hit on an NPC proxy goes through the guest's frame to the host. The host lists every NPC it
    # tracks; the guest may not have spawned a proxy for each yet, and the host takes a hit only on
    # an NPC it sees now (BUG-0294): try the listed NPCs until one hit lands.
    ids = re.findall(r'#(\d+) [0-9A-F]+ species=\S+ hp=([\d.]+)', host.call('coop.npc list')['out'])
    ck(bool(ids), 'host: an NPC to hit')
    if ids:
        h0, h1, tried = npc_hits(host), npc_hits(host), []
        deadline = time.time() + 15
        while h1 <= h0 and time.time() < deadline:
            for nid, _ in ids:
                r = guest.call(f'coop.npc hit {nid} 1')['out'].strip()
                tried.append(f'{nid}: {r}')
                time.sleep(1)
                h1 = npc_hits(host)
                if h1 > h0:
                    break
        ck(h1 > h0, f'host: the guest hit arrived ({h0} -> {h1}; tried {tried[-4:]})')

    # F9 is read in the mod's frame
    guest.call('key 0x78')
    time.sleep(2)
    ui = guest.call('coop.uistate')['out']
    ck('open=1' in ui, 'guest: F9 opened the co-op window')
    guest.call('key 0x78')

    guest.call('frames.source auto')
    time.sleep(2)
    out = guest.call('frames.source')['out']
    ck('Present' in out, f'guest: frames come from Present again ({out.strip()})')
    ck('total faults: 0' in guest.call('exc')['out'], 'guest: no fault')
    ck('total faults: 0' in host.call('exc')['out'], 'host: no fault')
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
