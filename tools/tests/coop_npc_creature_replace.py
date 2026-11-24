#!/usr/bin/env python3
"""BUG-1388: Creature standing NPC re-placed 5-6 m by the host's game snaps its guest proxy immediately.

After an evolution editor in Creature stage, the host's game re-places standing nest members 5-6 m in
one step (host speed 0) while the guest's remade proxies stand at the old spots. Without
`npc.creature_replace_snap`, the proxy's snapshot buffer smoothed the 5.6 m jump at 3 m/s and drove
the proxy through walk_place, leaving it 5-7 m off for 2-3 s (failing the campaign proxies gate).

1. Creature fixture, 2 players. Pick a standing NPC mirrored on both games (preferring the players'
   nest species).
2. Remake the guest proxy (`coop.npc forget <id>`) so it starts freshly spawned, like post-editor
   `npc.proxy_identity`, and let it settle at the host NPC's position.
3. Re-place the standing host NPC 5.6 m along the ground (`coop.npc replace <id> 5.6`, speed 0).
4. Within 0.7 s, verify the guest snapped the proxy (`creature_replaces` incremented and guest proxy
   is within 1.5 m of the host NPC's new position, without walking 2-3 s behind it).

  SPORE_FIX_OFF=npc.creature_replace_snap  FAILS (creature_replaces=0 and proxy trails > 2 m off)
"""
import math
import re
import time

from twoinst import Checks, game_runs, start

POS_RE = re.compile(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)')


def npc_pos(con, nid):
    for line in (con.call('coop.npc list')['out'] or '').splitlines():
        if line.startswith(f'#{nid} ') or f' #{nid} ' in line:
            m = POS_RE.search(line)
            if m:
                return tuple(float(m.group(i)) for i in (1, 2, 3))
    return None


def guest_replaces(con):
    m = re.search(r'creature_replaces=(\d+)', con.call('coop.status')['out'] or '')
    return int(m.group(1)) if m else 0


def find_pair(host, guest):
    own_m = re.search(r'key=0x([0-9A-Fa-f]+)', host.call('coop.avatar')['out'] or '')
    own_sp = own_m.group(1).upper().zfill(8) if own_m else ''
    h_rows = {}
    for m in re.finditer(r'#(\d+) \S+ species=([0-9A-Fa-f]+) hp=([\d.]+) dist=([\d.]+)(?! DEAD).*pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)',
                         host.call('coop.npc list')['out'] or ''):
        if float(m.group(3)) > 0:
            h_rows[int(m.group(1))] = (m.group(2).upper().zfill(8), float(m.group(4)),
                                       (float(m.group(5)), float(m.group(6)), float(m.group(7))))
    g_ids = set()
    for m in re.finditer(r'#(\d+) species=\S+ spawned ', guest.call('coop.npc list')['out'] or ''):
        g_ids.add(int(m.group(1)))
    both = []
    for nid in sorted(g_ids & set(h_rows)):
        sp, dist_h, pos = h_rows[nid]
        both.append((0 if sp == own_sp else 1, dist_h, nid, sp))
    both.sort()
    return both[0][2:] if both else (None, None)


def main():
    host, guest = start('creature')
    ck = Checks()
    game_runs(host, 60)
    game_runs(guest, 60)
    nid, sp = None, None
    t0 = time.time()
    while time.time() - t0 < 30:
        nid, sp = find_pair(host, guest)
        if nid is not None:
            break
        time.sleep(1.0)
    if not ck(nid is not None, f'mirrored Creature NPC found ({nid} species={sp})'):
        ck.faults(host=host, guest=guest)
        return ck.result()

    # Remake the proxy fresh (like post-editor npc.proxy_identity)
    guest.call(f'coop.npc forget {nid}')
    t0 = time.time()
    while time.time() - t0 < 15:
        hp, gp = npc_pos(host, nid), npc_pos(guest, nid)
        if hp and gp and math.dist(hp, gp) < 1.5:
            break
        time.sleep(0.3)
    hp0, gp0 = npc_pos(host, nid), npc_pos(guest, nid)
    print(f'  remade proxy #{nid} (species {sp}): host={hp0} guest={gp0} err={hp0 and gp0 and round(math.dist(hp0, gp0), 2)}',
          flush=True)
    ck(hp0 is not None and gp0 is not None and math.dist(hp0, gp0) < 2.0,
       f'remade proxy settled at host NPC before re-placement (err={hp0 and gp0 and round(math.dist(hp0, gp0), 2)})')

    rep0 = guest_replaces(guest)
    out = host.call(f'coop.npc replace {nid} 5.6')['out'].strip()
    print(f'  host replace: {out}', flush=True)
    time.sleep(0.55)
    hp1, gp1 = npc_pos(host, nid), npc_pos(guest, nid)
    rep1 = guest_replaces(guest)
    err = math.dist(hp1, gp1) if hp1 and gp1 else 999.0
    print(f'  after 0.55 s: host={hp1} guest={gp1} err={err:.2f} m creature_replaces={rep0}->{rep1}', flush=True)
    ck(rep1 > rep0, f'guest snapped standing re-placement (creature_replaces {rep0} -> {rep1})')
    ck(err < 1.8, f'guest proxy reached host NPC within 0.55 s (err {err:.2f} m < 1.8 m)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
