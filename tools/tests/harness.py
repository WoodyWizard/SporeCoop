#!/usr/bin/env python3
"""Shared helpers of the game tests (D4 of 0.7.15, BUG-1185, TST-5; docs/06-testing.md#testing-harness).

Before 0.7.15 every test carried its own copy of these (94 `def wait`, 49 `def sp`, 50 `def mode`, ...);
tools/harness_codemod.py replaced the identical copies by imports from here, under the names the tests used
(`from harness import mode_id as mode`, `wait = waiter(step=1.0)`). A new test imports them instead of copying;
test_lint.py flags a new copy. Nothing here needs a game: harness_selftest.py (make test) checks every helper on a
fake console, and each one against the copies it replaced.

  waiting   wait_until(cond, timeout, step, what, final)   poll until cond() is truthy; with `what`, says how long it
                                                           waited (or the last value on a timeout)
            waiter(timeout, step, final, as_bool)          a wait(cond, timeout, step) with the defaults of a copy
            poll(fn, timeout, step, until)                 the values of fn() read every step s
  console   out, mode, mode_strip, mode_word, mode_id, sp, pool, num, pool_num, field, field_dotall, kv, faults,
            busy_line, busy_word, busy_list, cinematic, eax, frame, world_of, puppet
  positions avatar_pos (coop.avatar), cell_pos (the own cell in coop.pool), dist, dist_or_none
  states    shared_money, shared_food (coop.res), dna (coop.dna)
  logs      log_size(inst), log_since(inst, off) (the game's sporekit log of an instance)
  NPCs      npc_list(con), own_species(con), pick_npc(host, guest, ...): a wild NPC (not the host's species, not
            its family, alive, mirrored by the guest), not "the first listed" (BUG-0437, docs/06-testing.md#testing-harness-g2)
  results   check_line(cond, msg)   the `  ok   ` / `  FAIL ` line of a check (twoinst.Checks prints through it)
            result_line(ok)         `RESULT: OK` / `RESULT: FAIL`, returns the exit code
            inconclusive(why)       `RESULT: INCONCLUSIVE <why>` (A15: the precondition did not happen; never a pass,
                                    never either half of a FAIL/PASS twin; the older `RESULT: SKIP` means the same)

The console helpers take a console (`con.call(line)` returns {'ok', 'out'}, tools/sporectl.py); a test's party
players (coop_party: `p.call(line)` returns the text) need their own.
"""
import math
import os
import re
import time

# ---------------------------------------------------------------------------------------------------- waiting

_REQUIRED = object()


def _short(v, n=120):
    s = repr(v)
    return s if len(s) <= n else s[:n - 3] + '...'


def wait_until(cond, timeout, step=0.5, what=None, final=False):
    """call cond() every `step` s until it returns a truthy value (returned) or `timeout` s passed: then None, or with
    final=True the value of one more call. With `what` (what the test waits for) it prints how long it waited, or on
    a timeout the last value it saw: the timings of the tests become measurable (TST-5)."""
    t0 = time.time()
    last = None
    while time.time() - t0 < timeout:
        r = cond()
        if r:
            if what:
                print(f'  wait {what}: {time.time() - t0:.1f} s', flush=True)
            return r
        last = r
        time.sleep(step)
    r = cond() if final else None
    if what:
        print(f'  wait {what}: not within {timeout} s (last value {_short(r if final else last)})', flush=True)
    return r


def waiter(timeout=_REQUIRED, step=0.5, final=False, as_bool=False):
    """a wait(cond, timeout, step) with these defaults, the form of a test's own copy (tools/harness_codemod.py):
    final: one more cond() after the timeout (else None); as_bool: True / False instead of the value"""
    dflt_timeout, dflt_step = timeout, step

    def wait(cond, timeout=dflt_timeout, step=dflt_step):
        if timeout is _REQUIRED:
            raise TypeError('wait() missing its timeout')
        v = wait_until(cond, timeout, step, final=final)
        return bool(v) if as_bool else v

    return wait


def poll(fn, timeout, step=0.5, until=None):
    """the values of fn() read every `step` s for `timeout` s (at least one), up to the first that until(value) holds
    for: "it never fell below ... on the way" checks"""
    t0 = time.time()
    vals = []
    while True:
        v = fn()
        vals.append(v)
        if until is not None and until(v):
            break
        if time.time() - t0 >= timeout:
            break
        time.sleep(step)
    return vals


# ---------------------------------------------------------------------------------------------------- console


def out(con, cmd):
    """the text of a console command's answer"""
    return con.call(cmd)['out']


def mode(con):
    return con.call('game.mode')['out']


def mode_strip(con):
    return con.call('game.mode')['out'].strip()


def mode_word(con):
    """the first word of game.mode (the mode id as text), '' without one"""
    out = con.call('game.mode')['out'].split()
    return out[0] if out else ''


def mode_id(con):
    """the game mode as a number, -1 when the game did not answer"""
    try:
        return int(con.call('game.mode')['out'].split()[0], 16)
    except (OSError, ValueError, IndexError):
        return -1


def sp(con, *a):
    """`coop.space <a...>`, stripped"""
    return con.call('coop.space ' + ' '.join(str(x) for x in a))['out'].strip()


def pool(con):
    return con.call('coop.pool')['out']


def num(out, key):
    """the integer of `key=N` in a status text, None without one"""
    m = re.search(r'\b' + key + r'=(-?\d+)', out)
    return int(m.group(1)) if m else None


def pool_num(con, key):
    """num() of `coop.pool`"""
    m = re.search(r'\b' + key + r'=(-?\d+)', con.call('coop.pool')['out'])
    return int(m.group(1)) if m else None


def field(out, pat):
    """group 1 of the pattern in the text, None without a match"""
    m = re.search(pat, out)
    return m.group(1) if m else None


def field_dotall(text, pat):
    """field() with `.` across lines; a None text has no match"""
    m = re.search(pat, text or '', re.S)
    return m.group(1) if m else None


_KV = re.compile(r'(?<![\w.])([A-Za-z_][\w.]*)=(\S+)')


def kv(out):
    """every `key=value` of a status text as {key: value text} (the first one of a key wins); values are strings:
    a test converts what it reads, kv(out).get('mirrors')"""
    d = {}
    for m in _KV.finditer(out or ''):
        d.setdefault(m.group(1), m.group(2))
    return d


def faults(con):
    """the game's `total faults: N` (exc), -1 without the line"""
    m = re.search(r'total faults: (\d+)', con.call('exc')['out'])
    return int(m.group(1)) if m else -1


def busy_line(con):
    """the text after `busy:` in coop.status ('?' without the line)"""
    for line in con.call('coop.status')['out'].splitlines():
        if line.startswith('busy:'):
            return line.split(':', 1)[1].strip()
    return '?'


def busy_word(con):
    """the first word after `busy: ` in coop.status ('?' without one)"""
    m = re.search(r'^busy: (\S+)', con.call('coop.status')['out'], re.M)
    return m.group(1) if m else '?'


def busy_list(con):
    """busy_word() split at the commas (['?'] without one)"""
    m = re.search(r'^busy: (\S+)', con.call('coop.status')['out'], re.M)
    return m.group(1).split(',') if m else ['?']


def cinematic(con):
    """the id of the cinematic coop.status says is playing, None without one"""
    m = re.search(r'cinematic ([0-9A-F]{8}) playing', con.call('coop.status')['out'])
    return m.group(1) if m else None


def autosave_settled(con, timeout=45):
    """E2 (BUG-0864, BUG-1203): the host's autosave has nothing on its way (no milestone waiting, every posted save
    written) and the "game saved" box of a save made meanwhile had time to be confirmed, so a test's real input (Esc,
    a click) does not land in the middle of a save. True when settled within `timeout` s (also for a game without the
    command); the wait itself checks nothing of the test"""
    def now():
        a = kv(con.call('coop.autosave').get('out', ''))
        if 'pending' not in a:
            return True
        try:
            return a['pending'] == '0' and int(a['posted']) == int(a['confirmed']) + int(a['lost'])
        except (KeyError, ValueError):
            return True
    if now():
        return True
    ok = wait_until(now, timeout, step=1.0) is not None
    time.sleep(3)  # world.cpp confirms the box about 2 s after the write
    return ok


def eax(con, what):
    """the eax of `call <what>`, None without an answer"""
    m = re.search(r'eax=0x([0-9A-Fa-f]+)', con.call('call ' + what)['out'])
    return int(m.group(1), 16) if m else None


def frame(con):
    """the game's frame counter, -1 without one"""
    m = re.search(r'frame=(\d+)', con.call('frame').get('out', ''))
    return int(m.group(1)) if m else -1


def world_of(con):
    """the world name coop.avatar prints, None without one"""
    m = re.search(r"world '([^']*)'", con.call('coop.avatar')['out'])
    return m.group(1) if m else None


def puppet(con):
    """the partner's puppet (coop.status puppet=) as a number, 0 without one"""
    m = re.search(r'puppet=([0-9A-F]{8})', con.call('coop.status')['out'])
    return int(m.group(1), 16) if m else 0


# ---------------------------------------------------------------------------------------------------- positions


def avatar_pos(con):
    """the own creature's position (coop.avatar pos=(x y z)), None without one"""
    m = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', con.call('coop.avatar')['out'])
    return tuple(map(float, m.groups())) if m else None


def cell_pos(con):
    """the own cell's position in the pool (the `avatar` line of coop.pool), None without one"""
    m = re.search(r'^avatar .*?pos=\((-?[\d.]+) (-?[\d.]+)\)', con.call('coop.pool')['out'], re.MULTILINE)
    return (float(m.group(1)), float(m.group(2))) if m else None


def dist(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def dist_or_none(a, b):
    """dist(), None when a position is missing"""
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b))) if a and b else None


# ---------------------------------------------------------------------------------------------------- states


def shared_money(con):
    m = re.search(r'shared money=(-?[\d.]+)', con.call('coop.res')['out'])
    return float(m.group(1)) if m else None


def shared_food(con):
    m = re.search(r'shared food=(-?[\d.]+)', con.call('coop.res')['out'])
    return float(m.group(1)) if m else None


def dna(con):
    m = re.search(r'dna=(-?[\d.]+)', con.call('coop.dna')['out'])
    return float(m.group(1)) if m else None


# ---------------------------------------------------------------------------------------------------- logs


def log_size(inst):
    """the size of the game's log of an instance now (0 without one): log_since(inst, log_size(inst)) later reads
    what the game wrote in between"""
    import lane
    try:
        return os.path.getsize(lane.game_log(inst))
    except OSError:
        return 0


def log_since(inst, off):
    """the lines of the game's log of an instance from byte `off` on"""
    import lane
    try:
        with open(lane.game_log(inst), 'rb') as f:
            f.seek(off)
            return f.read().decode('utf-8', 'replace').splitlines()
    except OSError:
        return []


# ---------------------------------------------------------------------------------------------------- numbers


def pct(xs, q):
    """the q quantile (0..1) of the values, 0.0 of none"""
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(q * len(xs)))] if xs else 0.0


def fnv(data):
    """FNV-1 (32 bit) of bytes, the game's hash of names"""
    h = 0x811C9DC5
    for c in data:
        h = ((h * 0x1000193) & 0xFFFFFFFF) ^ c
    return h


# ---------------------------------------------------------------------------------------------------- NPCs

# the host's lines of `coop.npc list` (npc.cpp list()): "#id ptr species=X hp=H dist=D[ DEAD] pos=(x y z) ...[ pack]
# [ family][ on_partner] ..."; the guest's: "#id species=X spawned|pending hp=H dist=D ..."
_NPC_HOST = re.compile(r'^\s*#(\d+) (\S+) species=(\S+) hp=(-?[\d.]+) dist=(-?[\d.]+)( DEAD)?(.*)$')
_NPC_GUEST = re.compile(r'^\s*#(\d+) species=(\S+) (spawned|pending) hp=(-?[\d.]+) dist=(-?[\d.]+)( DEAD)?(.*)$')
_POS = re.compile(r'\bpos=\((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\)')


def npc_list(con, text=None):
    """the NPCs of `coop.npc list` in its order: dicts id (str), species, hp, dist, dead, pos (host lines: (x, y, z)),
    pack, family, on_partner (host lines), spawned (guest lines: True / False, host lines: None), line. Lines of an NPC
    "not updated" (maybe freed) are left out. text: an answer read before (no call)"""
    if text is None:
        text = con.call('coop.npc list')['out']
    res = []
    for line in text.splitlines():
        m = _NPC_HOST.match(line)
        if m:
            nid, _ptr, spc, hp, d, dead, rest = m.groups()
            p = _POS.search(rest)
            res.append(dict(id=nid, species=spc, hp=float(hp), dist=float(d), dead=bool(dead),
                            pos=tuple(map(float, p.groups())) if p else None, pack=' pack' in rest,
                            family=' family' in rest, on_partner=' on_partner' in rest, spawned=None,
                            line=line.strip()))
            continue
        m = _NPC_GUEST.match(line)
        if m:
            nid, spc, state, hp, d, dead, rest = m.groups()
            res.append(dict(id=nid, species=spc, hp=float(hp), dist=float(d), dead=bool(dead), pos=None, pack=False,
                            family=False, on_partner=False, spawned=state == 'spawned', line=line.strip()))
    return res


def own_species(con):
    """the species of the own creature (coop.avatar key=0x..., 8 hex digits), '' without one"""
    m = re.search(r'key=0x([0-9A-F]+)', con.call('coop.avatar')['out'])
    return m.group(1).zfill(8) if m else ''


def pick_npc(host, guest=None, wild=True, min_hp=5.0, min_dist=None, max_dist=None, exclude=(), timeout=0,
             step=1.0, what=None):
    """an NPC of the host's `coop.npc list` (its dict, npc_list()), or None:
         alive, hp > min_hp, not in exclude (ids);
         wild (default): not of the host's own species and not of its family (BUG-0437: the first listed NPC can be
           the host's own kind after a warm reset, and a hunt of it makes the family bite back);
         guest given: one the guest's game mirrors (spawned there);
         min_dist / max_dist: its distance from the host's creature.
       The first one of the list that fits; with a timeout, tried again every `step` s until one fits."""
    exclude = {str(x) for x in exclude}

    def once():
        own = own_species(host) if wild else ''
        mirrored = {n['id'] for n in npc_list(guest) if n['spawned']} if guest is not None else None
        for n in npc_list(host):
            if n['spawned'] is not None or n['dead'] or n['hp'] <= min_hp or n['id'] in exclude:
                continue
            if mirrored is not None and n['id'] not in mirrored:
                continue
            if wild and (n['family'] or (own and n['species'] == own)):
                continue
            if (min_dist is not None and n['dist'] < min_dist) or (max_dist is not None and n['dist'] > max_dist):
                continue
            return n
        return None

    if not timeout:
        return once()
    return wait_until(once, timeout, step, what=what)


# ---------------------------------------------------------------------------------------------------- results


def check_line(cond, msg):
    """the line of one check: `  ok   <msg>` or `  FAIL <msg>` (the lab's reasons are the FAIL lines); returns cond"""
    print(('  ok   ' if cond else '  FAIL ') + msg, flush=True)
    return cond


def result_line(ok):
    """the test's verdict line, RESULT: OK / RESULT: FAIL; returns the exit code"""
    print('RESULT:', 'OK' if ok else 'FAIL')
    return 0 if ok else 1


def inconclusive(why):
    """A15 (owner decision 11): the test's precondition did not happen, so it proved nothing either way: prints
    `RESULT: INCONCLUSIVE <why>` (the lab: never a pass, several in a row a failure) and returns the exit code 0. A test
    with a failed check prints RESULT: FAIL instead (twoinst.Checks.inconclusive)"""
    print(f'RESULT: INCONCLUSIVE {why}', flush=True)
    return 0
