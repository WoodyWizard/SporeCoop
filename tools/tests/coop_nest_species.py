#!/usr/bin/env python3
"""The guest sees the host's species at the host's nests (player report 2026-10-25: at the same nest
the host saw its blue "Woopsies" and the card "Impress 3 Woopsies to ally", the guest small creatures
of another look and the card "The Datzu are your allies!").

Creation ids are per profile and sequential: two players who both made creatures own different
creations under the same keys. Spore populates the host's planet with the host's own creations, and
the guest's game resolved such a key to its own creation (its records, and its caches by key of the
creaturedata and the baked model): the host's NPCs, their nest and their species card on the guest
were the guest's creature. Here the guest's My Creations get another creature (the Kirgle) under the
key of the host's "TigerHorse" (290229B9); the host has an NPC of it next to the players. Checked:

  body      the guest builds the host's NPC from the host's creation (the bem the guest's game finds
            for the proxy's species is the host's), before and after the world went to the guest again
  relation  the host's species table reaches the guest under the guest's key for the NPCs and under
            the host's key (the save's herds), with the same values

  coop_nest_species.py
"""
import os
import re
import struct
import subprocess
import time

import lane
from twoinst import Checks, Console, shot, start, wait_up, watch
import twoinst
from coop_creature_desync import kill_game, npcs, wait

# "TigerHorse", a creature of the host's My Creations that no test gave the guest (its caches have
# nothing of this key: the "Judge" 29020CC4 of other tests left the guest's creaturedata and baked
# model of it, and the guest showed the Judge there whatever its own records held)
HOST_ONLY = 0x290229B9
OTHER = 0x290225CD      # "Kirgle": the guest's creation under the host's creature's key
KEY_PREFIX = '40626200/2B978C46'


def fixtures_dir():
    common = subprocess.run(['git', 'rev-parse', '--path-format=absolute', '--git-common-dir'], cwd=lane.ROOT,
                            capture_output=True, text=True).stdout.strip()
    return os.path.join(os.path.dirname(os.path.dirname(common)), 'SporeCoop', 'out', 'fixtures')


def read_pkg(path):
    d = open(path, 'rb').read()
    cnt, off = struct.unpack_from('<I', d, 0x24)[0], struct.unpack_from('<I', d, 0x40)[0]
    assert struct.unpack_from('<I', d, off)[0] == 4, 'index flags'
    recs, p = [], off + 8
    for _ in range(cnt):
        t, g, i, o, s, us, c, _x = struct.unpack_from('<IIIIIIHH', d, p)
        p += 28
        assert c == 0, 'compressed record'
        recs.append((t, g, i, d[o:o + (s & 0x7FFFFFFF)]))
    return recs


def write_pkg(path, recs):
    body, offs = bytearray(96), []
    for t, g, i, data in recs:
        offs.append(len(body))
        body += data
    index = bytearray(struct.pack('<II', 4, 0))
    for (t, g, i, data), o in zip(recs, offs):
        index += struct.pack('<IIIIIIHH', t, g, i, o, len(data) | 0x80000000, len(data), 0, 1)
    io = len(body)
    body += index
    struct.pack_into('<4sII', body, 0, b'DBPF', 3, 0)
    struct.pack_into('<I', body, 0x24, len(recs))
    struct.pack_into('<I', body, 0x2C, len(index))
    struct.pack_into('<I', body, 0x3C, 3)
    struct.pack_into('<I', body, 0x40, io)
    open(path, 'wb').write(body)


def colliding_creations(out, dst, src):
    """the guest's My Creations with its creation `src` also under the key `dst` (dst's own records
    dropped): another player's profile that made a different creature under the same id"""
    recs = [r for r in read_pkg(os.path.join(fixtures_dir(), 'creations-guest.package')) if r[2] != dst]
    recs += [(t, g, dst, data) for t, g, i, data in recs if i == src]
    write_pkg(out, recs)


def bem(con, inst):
    m = re.search(r'bem ([0-9A-F]{8}) (\S+)', con.call(f'coop.creations bem {inst:08X}' if isinstance(inst, int)
                                                       else f'coop.creations bem {inst}')['out'])
    return m.group(2) if m and m.group(2) != 'none' else None


def table(con):
    """{instance: (relation, x, impressed, killed)} of the creature species"""
    t = {}
    for line in con.call('coop.social species')['out'].splitlines():
        m = re.match(r'species ([0-9A-F]{8})/2B978C46/40626200 relation=(-?\d+) x=(\d+) impressed=(\d+) killed=(\d+)',
                     line.strip())
        if m:
            t[m.group(1)] = tuple(int(x) for x in m.groups()[1:])
    return t


def wait_in(host, guest, timeout=240):
    """the guest plays in the host's world and the host has its puppet (a rejoined guest gets a new
    player id: wait_guest_in's 'together 1 1' does not hold while the old one times out)"""
    t0 = time.time()
    while time.time() - t0 < timeout:
        f1 = twoinst.host_frame(host)
        time.sleep(0.7)
        if twoinst.host_frame(host) == f1:  # the "game saved" box stops the host's frames
            host.call('key enter')
        if guest.call('coop.avatar')['ok'] and re.search(r"remote \d+ 'Bob' puppet=[0-9A-F]+ state=yes",
                                                         host.call('coop.status')['out']):
            return True
    return False


def restart_guest(host):
    """the guest again with the colliding My Creations, joining the running host"""
    for m in twoinst._monitors:
        m.stop = True
    kill_game(lane.GUEST)
    time.sleep(3)
    colliding_creations(os.path.join(lane.GUEST_DIR, 'EditorSaves.package'), HOST_ONLY, OTHER)
    subprocess.run(['rm', '-f', os.path.join(lane.GUEST_DIR, 'SporeCoop', 'choices.ini')])
    lane.run_guest()
    if not wait_up(lane.GUEST, 90):
        lane.run_guest()
        assert wait_up(lane.GUEST), 'the guest did not start'
    guest = Console(lane.GUEST)
    wait(lambda: 'GalaxyGameEntry' in guest.call('game.mode')['out'], 120)
    guest.call('wait.frames 200 60000')
    for name in [n.strip() for n in os.environ.get('SPORE_FIX_OFF', '').split(',') if n.strip()]:
        # twoinst.start switched them off in the guest game this one replaces (BUG-0200's twin run)
        print('  SPORE_FIX_OFF (restarted guest): ' + guest.call(f'coop.fix {name} off')['out'].strip(), flush=True)
    guest.call(lane.JOIN)
    ok = wait_in(host, guest)
    watch(lane.HOST, lane.GUEST)
    return guest, ok


def checks(ck, host, guest, nid, host_bem, tag, own_bem=None):
    # body: the proxy of the host's NPC is built from the host's creation
    def proxy():
        e = npcs(guest).get(nid)
        return e if e and e[1] == 'spawned' else None
    e = wait(proxy, 30)
    ck(e, f'{tag}: the guest builds the host\'s creature n{nid} ({npcs(guest).get(nid)})')
    if e:
        got = bem(guest, e[0])
        print(f'  {tag}: the guest\'s proxy has species {e[0]}, its body {got} (the host\'s {host_bem})')
        ck(got == host_bem, f'{tag}: the guest shows the host\'s creation, not its own of that key '
                            f'(species {e[0]}: bem {got}, the host\'s {host_bem})')
    # BUG-0200: what the guest's game itself gives for the host's key (the save's herds and nests of the host's
    # creature, and the cards it makes from them, look the species up by that key): the host's creation, not
    # the guest's own creation of the same id
    def prof():
        m = re.search(r'profile \S+ key ([0-9A-F]{8}) bem ([0-9A-F]{8}) aliased (\d)',
                      guest.call(f'coop.creations speciesof {HOST_ONLY:08X}')['out'])
        return m.groups() if m and m.group(2) == host_bem else None
    p = wait(prof, 10)
    out = guest.call(f'coop.creations speciesof {HOST_ONLY:08X}')['out'].strip()
    print(f'  {tag}: the guest\'s species profile of the host\'s key: {out}')
    ck(p, f'{tag}: the guest\'s game gives the host\'s creation for the host\'s key, its herds and nests ({out}, '
          f'the host\'s bem {host_bem}, the guest\'s own {own_bem})')
    # relation: the host's record of the species, on the guest under the guest's key for its NPCs and
    # under the host's key (the save's herds and nests of it)
    local = e[0] if e else None
    hk = f'{HOST_ONLY:08X}'

    def same():
        ht, gt = table(host), table(guest)
        h = ht.get(hk)
        return (h, gt) if h and local and gt.get(local) == h and gt.get(hk) == h else None
    r = wait(same, 15)
    ht, gt = table(host), table(guest)
    print(f'  {tag}: host table {ht.get(hk)}, guest {local}: {gt.get(local)}, guest {hk}: {gt.get(hk)}')
    ck(r, f'{tag}: the host\'s record of its creature is the guest\'s record of the species it shows and of its herds')


def main():
    host, guest = start('creature')
    ck = Checks()
    guest, ok = restart_guest(host)
    ck(ok, 'the guest with the colliding My Creations joined')
    for c in (host, guest):
        c.call('coop.npc strays off')  # the console-spawned NPC's herd has no archetype (creature_e84)
        c.call('coop.team hp 1000')
    host_bem, own_bem = bem(host, HOST_ONLY), bem(guest, HOST_ONLY)
    print(f'  {HOST_ONLY:08X}: the host\'s bem {host_bem}, the guest\'s own {own_bem}')
    ck(host_bem and own_bem and host_bem != own_bem,
       'the guest owns another creation under the key of the host\'s creature')

    out = host.call(f'coop.npc spawn {HOST_ONLY:08X}')['out']
    ck('spawned' in out, f'the host has an NPC of its own creation {HOST_ONLY:08X} ({out.strip()})')
    ids = wait(lambda: [i for i, (sp, st, p) in npcs(host).items() if sp == f'{HOST_ONLY:08X}'], 10)
    ck(ids, f'the host mirrors it ({ids})')
    if not ids:
        return ck.result()
    nid = ids[0]
    # the host's record of the species (impressed 2 of 3, as social games leave it): sent on change
    print(' ', host.call(f'coop.social record {HOST_ONLY:08X} 3 2')['out'].strip())
    time.sleep(3)
    checks(ck, host, guest, nid, host_bem, 'spawned', own_bem)
    shot(guest, 'nest_species_guest.png')
    shot(host, 'nest_species_host.png')

    # the world (with the herd of the host's creature in it) goes to the guest again
    out = host.call('coop.save')['out']
    t0 = time.time()
    while time.time() - t0 < 12:  # the "game saved" box stops the host's frames
        f1 = twoinst.host_frame(host)
        time.sleep(0.7)
        if twoinst.host_frame(host) == f1:
            host.call('key enter')
    ck('saved' in out, f'the host saved ({out.strip()})')
    def last_load():  # the newest load line (a count in the last 400 lines stops growing once
        # the older lines scroll out of that window: BUG-0209)
        lines = [l for l in guest.call('log 400')['out'].splitlines() if 'world: loading ' in l]
        return lines[-1] if lines else ''
    l0 = last_load()
    host.call('coop.share')
    loaded = wait(lambda: last_load() not in ('', l0), 120)
    ok = loaded and wait_in(host, guest)
    ck(ok, 'the guest loaded the host\'s world again')
    time.sleep(6)
    # the save's herd of the species keeps the host's key on the guest (the guest's game learns it
    # from its local creature; a proxy goes into such a herd when it is a nest: proxy_herd)
    lst = guest.call('coop.npc list')['out']
    nests = dict(re.findall(r'nest ([0-9A-F]{8})/40626200 herd=(\S+)', lst))
    print(f'  the guest\'s herds learned from its world: {sorted(nests)}')
    checks(ck, host, guest, nid, host_bem, 'after the world went again', own_bem)
    shot(guest, 'nest_species_guest_after.png')

    # BUG-0200: after the session the guest's own games give its own creation for that key again
    guest.call('coop.leave')
    def own_back():
        out = guest.call(f'coop.creations speciesof {HOST_ONLY:08X}')['out']
        return out if f'bem {own_bem}' in out and 'aliased 0' in out else None
    back = wait(own_back, 20)
    out = guest.call(f'coop.creations speciesof {HOST_ONLY:08X}')['out'].strip()
    print(f'  after the guest left: {out}', flush=True)
    ck(back, f'after the session the guest\'s game gives its own creation for that key again ({out})')
    ck.faults(host=host, guest=guest)
    open(twoinst.STATE, 'w').write('{}')  # the guest's My Creations are not the fixture's any more
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
