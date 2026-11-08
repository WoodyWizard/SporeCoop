#!/usr/bin/env python3
"""The state a player's or an NPC's snapshot carries, and where the receiver applies each field (class BUG-0419: state
sent but not applied; BUG-0698). In `make test`.

Every member of the snapshot structs of mods/coop/net/protocol.h (PlayerState, NpcSpawn, NpcState) and every PF_ /
NF_ flag must be listed below: either with the source file and a pattern of its apply site on the receiver (checked:
the pattern must be in the file), or as not applied with the reason. A new field, or a listed apply site that is
gone, fails this check: a field is never again sent without anybody deciding what the other game does with it.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
C = os.path.join(ROOT, 'mods', 'coop')

A = 'apply'
N = 'not'
FIELDS = {
    'PlayerState': {
        'id': (A, 'net/session.cpp', r'\bid\b'),  # the sender: the session fills it from the peer
        'time_ms': (A, 'coop.cpp', r'time_ms'),  # the interpolation buffer's clock
        'pos': (A, 'modes.cpp', r's\.pos'),
        'rot': (A, 'modes.cpp', r's\.rot'),
        'vel': (A, 'modes.cpp', r's\.vel'),
        'flags': (A, 'modes.cpp', r's\.flags'),  # the bits: FLAGS below
        'anim': (N, 'always 0: the animations of a player go by MSG_ANIM (coop.cpp), one message per animation start'),
        'health': (A, 'creature_hp.cpp', r'drive_puppet'),  # Creature; Space: modes.cpp space_puppet_health (BUG-0697)
        'max_health': (A, 'modes.cpp', r'space_puppet_health\(mh, s\.health, s\.max_health'),
        'species': (A, 'coop.cpp', r'\.species'),
        'mode': (A, 'coop.cpp', r'\.mode\b'),
        'world': (A, 'coop.cpp', r'\.world\b'),
        'scale': (A, 'modes.cpp', r's_in\.scale / own'),  # Cell: the sender's world scale (BUG-0519)
        'agl': (A, 'modes.cpp', r'agl'),
        'body': (A, 'modes.cpp', r'puppet_body\(p, s_in\)'),  # BUG-0733: the puppet's size (Creature)
    },
    'NpcSpawn': {
        'net_id': (A, 'npc.cpp', r'sp\.net_id'),
        'key': (A, 'npc.cpp', r'sp\.key'),
        'scale': (A, 'npc.cpp', r'kScale'),  # the fields engine (BUG-0461)
        'flags': (A, 'npc.cpp', r'sp\.flags'),
        'pos': (A, 'npc.cpp', r'sp\.pos'),
        'rot': (A, 'npc.cpp', r'sp\.rot'),
        'health': (A, 'npc.cpp', r'sp\.health'),
        'max_health': (A, 'npc.cpp', r'drive_puppet\(px\.c, s\.health, px\.max_health'),  # BUG-0710
    },
    'NpcState': {
        'net_id': (A, 'npc.cpp', r's\.net_id'),
        'pos': (A, 'npc.cpp', r's\.pos'),
        'rot': (A, 'npc.cpp', r's\.rot'),
        'vel': (A, 'npc.cpp', r's\.vel'),
        'health': (A, 'npc.cpp', r'set_health\(px\.c, s\.health\)'),
        'flags': (A, 'npc.cpp', r's\.flags'),
    },
}
FLAGS = {
    'PF_IN_WORLD': (A, 'coop.cpp', r'PF_IN_WORLD'),
    'PF_MOVING': (A, 'modes.cpp', r'PF_MOVING'),
    'PF_RUNNING': (N, 'the gait follows the speed the drive gives the puppet (its velocity), not a flag'),
    'PF_JUMPING': (A, 'modes.cpp', r'PF_JUMPING'),
    'PF_DEAD': (A, 'coop.cpp', r'PF_DEAD'),
    'PF_SWIMMING': (N, 'never set: a swimming creature is placed by its position and the water rules of each game'),
    'PF_SNEAKING': (A, 'modes.cpp', r'PF_SNEAKING'),
    'PF_RESYNC': (A, 'coop.cpp', r'PF_RESYNC'),
    'NF_DEAD': (A, 'npc.cpp', r'NF_DEAD'),
    'NF_FAMILY': (A, 'npc.cpp', r'NF_FAMILY'),
    'NF_IMPRESSED': (A, 'npc.cpp', r'NF_IMPRESSED'),
    'NF_PACK': (A, 'npc.cpp', r'NF_PACK'),
    'NF_SUMMONED': (A, 'npc.cpp', r'NF_SUMMONED'),
    'NF_AIR': (A, 'npc.cpp', r'NF_AIR'),
}


def members(text, name):
    m = re.search(r'struct ' + name + r' \{(.*?)\n\};', text, re.S)
    if not m:
        return None
    body = m.group(1).split('void write')[0]
    out = []
    for line in body.splitlines():
        line = line.split('//')[0].strip()
        if not line or line.startswith(('static', 'void', 'bool', '}')) or '(' in line:
            continue
        line = re.sub(r'\[[^\]]*\]', '', line)        # key[3]
        line = re.sub(r'\{[^}]*\}', '', line)          # = {0, 0, 0}
        line = re.sub(r'=\s*[^,;]+', '', line)          # = 0
        decl = re.match(r'[\w:]+\s+(.*);', line)       # the type, then the names
        if decl:
            out += [v.strip() for v in decl.group(1).split(',') if v.strip()]
    return out


def main():
    proto = open(os.path.join(C, 'net', 'protocol.h')).read()
    bad = []
    files = {}

    def check(where, entry):
        if entry[0] == N:
            return
        _, f, pat = entry
        if f not in files:
            p = os.path.join(C, f)
            files[f] = open(p).read() if os.path.exists(p) else ''
        if not re.search(pat, files[f]):
            bad.append(f'{where}: the apply site {f} /{pat}/ is gone')

    for st, table in FIELDS.items():
        ms = members(proto, st)
        if ms is None:
            bad.append(f'struct {st} not found in protocol.h')
            continue
        for m in ms:
            if m not in table:
                bad.append(f'{st}.{m}: a field nobody decided about (list its apply site or why it is not applied)')
        for m, e in table.items():
            if m not in ms:
                bad.append(f'{st}.{m}: listed, not in the struct any more')
            else:
                check(f'{st}.{m}', e)
    for fl in re.findall(r'\b((?:PF|NF)_[A-Z_]+)\s*=', proto):
        if fl not in FLAGS:
            bad.append(f'{fl}: a flag nobody decided about')
        else:
            check(fl, FLAGS[fl])
    for b in bad:
        print('  FAIL', b)
    n = sum(len(t) for t in FIELDS.values()) + len(FLAGS)
    print(f'state fields: {n} listed, {len(bad)} problem(s)')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
