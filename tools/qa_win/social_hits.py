"""Creature: a partner befriending by real input must never hurt the creature on the host (BUG-9136, BUG-0021).

    py tools/qa_win/social_hits.py <guest> <host instance> <npc id>... [--size 1280x960]
        guest: 'msi' (real clicks belong on the MSI) or a QA instance number (only with the owner's OK: it moves this
        PC's mouse); --size: the guest's window (the social stance button is at W/2 - 30, H - 82)

For each NPC the guest enters the social stance, steps next to it (`coop.social goto`), clicks it like a player and
presses the first social key three times; the host's log is counted for "guest N hit npc". Prints the guest's
`coop.prules` strike line (social_kept = sings the guard stopped). Exit code 1 when the host took a hit from the guest.
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(__file__))
import qa  # noqa: E402

HOST_LOG = os.path.join(qa.QA_COPY, 'SporebinEP1', 'mods', 'logs', 'sporekit.%d.log')


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    size = sys.argv[sys.argv.index('--size') + 1] if '--size' in sys.argv else '1280x960'
    args = [a for a in args if a != size]
    if len(args) < 3:
        sys.exit(__doc__)
    w, h = (int(v) for v in size.split('x'))
    guest, host_inst = qa.console(args[0]), int(args[1])
    qa.console(host_inst)  # the host must be a QA game too
    log = HOST_LOG % host_inst

    def stance():
        m = re.search(r'eax=0x([0-9A-Fa-f]+)', qa.call(guest, 'call Simulator::cCreatureGameData::GetAbilityMode cdecl'))
        return int(m.group(1), 16) if m else None

    def social_stance():
        for _ in range(4):
            if stance() == 0:
                return True
            qa.call(guest, 'click %d %d' % (w // 2 - 30, h - 82))
            time.sleep(1.5)
        return stance() == 0

    def host_hits():
        return len(re.findall(r'guest \d+ hit npc', open(log, encoding='utf-8', errors='replace').read()))

    def screen(nid):
        for line in qa.call(guest, 'coop.targets all').splitlines():
            f = line.split()
            if len(f) == 5 and f[0] == 'obj' and f[1] == 'n%d' % nid:
                return int(f[3]), int(f[4])
        return None

    before = host_hits()
    for nid in (int(a) for a in args[2:]):
        social_stance()
        engaged = False
        for _ in range(6):
            qa.call(guest, 'coop.social goto %d' % nid)
            time.sleep(1.5)
            xy = screen(nid)
            if not xy or not (20 < xy[0] < w - 20 and 90 < xy[1] < h - 160):
                qa.call(guest, 'drag %d %d %d %d right' % (w // 2, h // 2, w // 2 + 210, h // 2))
                time.sleep(1.5)
                continue
            social_stance()
            qa.call(guest, 'click %d %d' % xy)
            time.sleep(2.5)
            if re.search(r'engaged=%d\b' % nid, qa.call(guest, 'coop.social')):
                engaged = True
                break
        for _ in range(3):
            qa.call(guest, 'key 49 down')
            time.sleep(0.6)
            qa.call(guest, 'key 49 up')
            time.sleep(4)
        print('npc %d: engaged=%s stance=%s' % (nid, engaged, stance()))
    time.sleep(3)
    hits = host_hits() - before
    print([l for l in qa.call(guest, 'coop.prules').splitlines() if 'strike' in l][0])
    print('hits from the guest on the host: %d' % hits)
    sys.exit(1 if hits else 0)


if __name__ == '__main__':
    main()
