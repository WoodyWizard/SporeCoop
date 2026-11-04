"""BUG-0345 helpers: a guest's own-creation state (its choice in choices.ini and the record of this world in
worlds.ini) as a test found it, so the test can choose what it needs and leave the profile as it was.
Since BUG-0345 a guest plays its own creation by default only in a world it played it in: a test that needs
the guest's own creature chooses it (`choose_own`), and puts the state back at the end (`restore`)."""
import re


def save(guest, kind='creature'):
    out = guest.call('coop.creations worlds')['out']
    rec = re.search(kind + r'=([0-9A-F]{8})', out)
    ch = re.search(r'choice_' + kind + r'=(\S+)', out)
    return {'kind': kind, 'record': rec.group(1) if rec else '00000000', 'choice': ch.group(1) if ch else '-'}


def own_keys(guest, kind='creature'):
    """the guest's own creations of a kind, newest first"""
    return [l.split()[1] for l in guest.call('coop.creations list')['out'].splitlines() if l.startswith(kind + ' ')]


def choose_own(guest, kind='creature'):
    """the guest chooses its newest own creation; returns its key (None without one)"""
    own = own_keys(guest, kind)
    if own:
        guest.call(f'coop.creations choose {kind} {own[0]}')
    return own[0] if own else None


def restore(guest, st):
    k = st['kind']
    if st['choice'] == '-':
        guest.call(f'coop.creations unchoose {k}')
    else:
        guest.call(f'coop.creations choose {k} {st["choice"]}')
    guest.call(f'coop.creations worlds set {k} {st["record"]}')
