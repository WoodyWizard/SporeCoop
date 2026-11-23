#!/usr/bin/env python3
"""The host's store salt survives a moment in which another program holds salt.txt open for writing (BUG-1424, NET-12
of the 0.7.15.5 audit).

Every stored player's key on the host is a digest of the host profile's salt (Games-sporecoop-store/salt.txt) and the
player's token. store.cpp opened salt.txt with FILE_SHARE_READ only and took any failure to open it for "no file": a
sync or backup tool (or an antivirus) holding the file with write access at the first ask of a run made a new salt
and wrote it over the old one; every stored player of every world of that host had another key from then on.

creature fixture, the host's `coop.store saltprobe` (tests): it opens salt.txt as such a tool does (read and write
access, every share flag), forgets the salt as at the first ask of a run, asks for it and the guest's key while the
file is held, lets go and asks again:
  1. the file was held, a guest is there and has a key;
  2. the guest's key after the hold is its key of before, and the salt in memory is the old one;
  3. salt.txt holds the same bytes as before (not replaced); while held the salt was the old one or none (no new one).
The probe puts the old salt back afterwards for the tests that follow.
SPORE_FIX_OFF=store.unreadable_kept (suite store_salt_held_off, the twin): the share violation is "no file", a new
salt is made: checks 2 and 3 FAIL.

  SPORE_LANE=4 python3 tools/tests/coop_store_salt_held.py
"""
import re

from twoinst import Checks, start


def main():
    host, guest = start('creature')
    ck = Checks()
    out = host.call('coop.store saltprobe').get('out') or ''
    print('  ' + out.strip(), flush=True)
    m = dict(re.findall(r'(\w+)=(\S+)', out))
    ck(m.get('held') == '1' and m.get('guest', '-1') != '-1' and m.get('key_before', '-').startswith('s:'),
       f'1. salt.txt held by "another program", the guest has a key ({m.get("key_before")})')
    ck(m.get('key_after') == m.get('key_before') and m.get('salt_same') == '1',
       f'2. the guest\'s key after the hold is the one of before ({m.get("key_after")})')
    ck(m.get('file_same') == '1' and m.get('during') in ('same', 'none'),
       f'3. salt.txt not replaced (file_same={m.get("file_same")}), no new salt while held (during={m.get("during")})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
