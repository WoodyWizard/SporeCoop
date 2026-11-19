#!/usr/bin/env python3
"""BUG-1293 (found by the Lineages fork, PR #196, its BUG-9231): some host creatures stay invisible on the guest.

A host NPC of a host's own creation is keyed on the guest by the guest's own key for that creation (0x7D......,
creations::local_species). When that key builds nothing on the guest, the proxy's spawn failed "unknown species" and
asked the host for the GUEST's key, which the host never has ("player 1 asks for species 7D...: not found here" every
30 s): the NPC hit the guest but was never drawn.

Two games on the creature fixture. Guest: `coop.creations breakremap <K>` points its key for the host avatar's species
K at a key nothing registered (as a remap this game cannot build). Host: `coop.npc spawn <K>` next to its avatar.
Checked on the guest's game (its census): a new NPC proxy of the spawned creature is in its world within 35 s (built
from the host's key), and the host's log has no "asks for species 7DFFFF.." line. A key frame of the guest shows it.
SPORE_FIX_OFF=npc.species_fallback,creations.ask_host_key must FAIL (the proxy never appears; the host answers
"not found here").

  SPORE_LANE=9 python3 tools/tests/coop_npc_host_key.py"""
import re
import time

import census
from harness import log_since, log_size
from twoinst import Checks, keyframe, start




def proxies(guest, keys):
    return {r['id']: r for r in census.dump(guest).rows
            if r['kind'] == 'c' and r['own'] == 'proxy' and r['key'] in keys and r['alive'] == 1}


def main():
    host, guest = start('creature')
    ck = Checks()
    m = re.search(r'key=0x([0-9A-F]{8})', host.call('coop.avatar')['out'])
    k = m.group(1) if m else None
    ck(k is not None, f'the host\'s avatar species is known ({k})')
    if not k:
        return ck.result()
    fake = '7DFFFF' + k[-2:]
    print('  guest: ' + guest.call(f'coop.creations breakremap {k}')['out'].strip(), flush=True)
    before = proxies(guest, (k, fake))
    host.call('coop.fix npc.strays off')  # a spawned creature looks like a stray puppet to the host's stray pass
    hpos = log_size(host.port - 47799)
    out = host.call(f'coop.npc spawn {k}')['out'].strip()
    print('  host: ' + out, flush=True)
    ck(out.startswith('spawned'), 'the host\'s game made a creature of its avatar\'s species')
    new, t0 = {}, time.time()
    while time.time() - t0 < 35:
        new = {i: r for i, r in proxies(guest, (k, fake)).items() if i not in before}
        if new:
            break
        time.sleep(1)
    row = next(iter(new.values()), None)
    print(f'  on the guest: {"npc %d key %s at %s" % (row["id"], row["key"], census.fmt(row["pos"])) if row else "none"}',
          flush=True)
    keyframe('npc_host_key', 'the guest\'s view: the host\'s newly spawned creature (its avatar\'s species) '
             'stands next to the host\'s puppet, drawn (not missing); the host\'s view: the same creature next to its avatar')
    st = guest.call('coop.npc')['out']
    fb = re.search(r'npc species fallback: on=(\d) host_key_builds=(\d+)', st)
    print('  guest: ' + (fb.group(0) if fb else '(no fallback line)'), flush=True)
    ck(row is not None, 'the guest\'s game has the host\'s new creature in its world (built from the host\'s key)')
    hlog = '\n'.join(log_since(host.port - 47799, hpos))
    asked = re.findall(r'asks for species (7DFFFF[0-9A-F]{2}): not found here', hlog)
    ck(not asked, 'the host is never asked for the guest\'s own key (no "not found here")')
    host.call('coop.fix npc.strays on')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
