#!/usr/bin/env python3
"""Self-test of the shared test harness (make test; D4 of 0.7.15, BUG-1185; docs/06-testing.md#testing-harness).
No game: fake consoles and a fake clock.

  copies    every copy tools/harness_codemod.py replaces (COPIES) and the harness function it became give the same
            result (or the same exception) and ask the console the same commands, over a set of answers
  waits     the wait template in each form the codemod accepts (names, defaults, fixed sleep, end None / one more call /
            True-False) and the waiter(...) it writes: the same value, the same calls of cond and the same sleeps
  api       wait_until with a reason prints the time or the last value; poll; kv; npc_list of host and guest lines;
            pick_npc leaves out the host's species, its family, the dead, the weak, the ones the guest does not
            mirror and the excluded (BUG-0437); the result lines, and the lab reads inconclusive() as INCONCLUSIVE
  lint      test_lint.py flags a test that copies a harness helper again
  codemod   harness_codemod.py --self-test (plan, rewrite, verify)
"""
import contextlib
import io
import itertools
import math
import os
import re
import struct
import sys
import tempfile
import types

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))
import harness  # noqa: E402
import harness_codemod  # noqa: E402
import labverdict  # noqa: E402

OK = True


def ck(cond, msg):
    global OK
    print(('  ok   ' if cond else '  FAIL ') + msg, flush=True)
    OK &= bool(cond)
    return cond


TEXTS = [
    '', '   ', 'zz',
    '0x0B Creature 12\nEditor\n',
    '1F\n',
    'busy: away,editor\ncinematic 0123ABCD playing\neax=0x1fA\nframe=123\nworld \'Foo bar\' open\npuppet=0000ABCD\n'
    'pos=(1.5 -2 3)\navatar here pos=(4.5 -1.25)\nshared money=12.5 shared food=-3\ndna=7.5\ntotal faults: 3\n'
    'mirrors=7 key=5 x=-2\n',
    'status line\nbusy:  -  \nnpc pos=(1 2)\navatar pos=(-0.5 2)\nmirrors=x key=-11\n',
    'busy: -\ncinematic 0123abcd playing\npos=(1. 2. 3.)\ntotal faults: 0\nshared money=-1\nworld \'\'\n',
]


class Con:
    """a fake console: every command answers `out`, or raises it when it is an exception"""
    def __init__(self, out):
        self.out, self.calls = out, []

    def call(self, line):
        self.calls.append(line)
        if isinstance(self.out, BaseException):
            raise self.out
        return {'ok': True, 'out': self.out}


ARGS = {
    'out': TEXTS, 'text': TEXTS + [None], 'key': ['mirrors', 'key', 'x', 'nope'],
    'pat': [r'key=(-?\d+)', r'world \'([^\']*)\'', r'(busy.*?editor)', r'(nothing)'],
    'a': [(0.0, 0.0), (1.0, 2.0, 3.0), None, ()], 'b': [(3.0, 4.0), (1.0, 2.0, 5.5), None],
    'xs': [[], [3.0], [5, 1, 4, 2], [0.1] * 7], 'q': [0.0, 0.5, 0.9, 1.0],
    'data': [b'', b'abc', bytes(range(40))], 'what': ['Foo', 'x y'], 'inst': [1, 2, 3], 'off': [0, 5, 10_000],
}


def run(fn, args):
    try:
        return ('ok', fn(*args))
    except Exception as e:  # the same exception type counts as the same behaviour
        return ('raise', type(e).__name__)


def check_copies(fake_lane):
    pairs = 0
    bad = []
    for text, hname in harness_codemod.COPIES.items():
        ns = {'re': re, 'math': math, 'os': os, 'struct': struct, 'lane': fake_lane}
        exec(text, ns)
        name = re.match(r'def (\w+)', text).group(1)
        orig, new = ns[name], getattr(harness, hname)
        params = list(orig.__code__.co_varnames[:orig.__code__.co_argcount])
        varargs = bool(orig.__code__.co_flags & 0x04)
        choices = []
        for p in params:
            if p == 'con':
                choices.append(['con'])
            else:
                choices.append(ARGS[p])
        extra = [(), ('world',), ('trade', 'all', 3)] if varargs else [()]
        for combo in itertools.product(*choices):
            for va in extra:
                for answer in TEXTS + [OSError('gone'), ValueError('bad')]:
                    c1, c2 = Con(answer), Con(answer)
                    a1 = [c1 if x == 'con' else x for x in combo] + list(va)
                    a2 = [c2 if x == 'con' else x for x in combo] + list(va)
                    r1, r2 = run(orig, a1), run(new, a2)
                    pairs += 1
                    if r1 != r2 or c1.calls != c2.calls:
                        bad.append(f'{hname} for {text.splitlines()[0]} on {combo} {answer!r}: {r1} / {r2}, '
                                   f'{c1.calls} / {c2.calls}')
                    if 'con' not in combo:
                        break
    for b in bad[:5]:
        print('    ' + b)
    ck(not bad, f'copies: {len(harness_codemod.COPIES)} copies and their harness functions agree on {pairs} calls')


class Clock:
    def __init__(self):
        self.t, self.trace = 1000.0, []

    def time(self):
        return self.t

    def sleep(self, s):
        self.trace.append(('sleep', s))
        self.t += s


def cond_of(kind, clock):
    n = [0]

    def cond():
        n[0] += 1
        clock.trace.append(('cond', n[0]))
        clock.t += 0.01
        if kind == 'never':
            return 0
        if kind == 'third':
            return 'yes' if n[0] >= 3 else ''
        if kind == 'late':  # only after the timeout: the final call sees it
            return 7 if clock.t > 1003.0 else None
        return [1]
    return cond


def check_waits():
    forms = 0
    bad = []
    for c, t in (('cond', 'secs'), ('pred', 'timeout'), ('f', 't')):
        for tdef in (None, '3'):
            for step in ('step=0.5', 'step=1.0', None):
                sleeps = ['step'] if step else ['0.5', '1', '0.3']
                for s in sleeps:
                    params = [c, t + ('=' + tdef if tdef else '')] + ([step] if step else [])
                    head = 'def wait(' + ', '.join(params) + '):\n'
                    bodies = [harness_codemod.WAIT_BODY.format(c=c, t=t, s=s, v=v, last=last)
                              for v in ('r', 'v') for last in ('None', c + '()')]
                    bodies.append(harness_codemod.BOOL_BODY.format(c=c, t=t, s=s))
                    for body in bodies:
                        text = head + body
                        fn_node = harness_codemod.ast.parse(text).body[0]
                        form = harness_codemod.wait_form(fn_node, text)
                        if form is None:
                            bad.append(f'not recognised: {text!r}')
                            continue
                        forms += 1
                        call = harness_codemod.waiter_call(form)
                        for kind in ('never', 'third', 'late', 'now'):
                            for args in ([3], [3, 0.25]) if step else ([3],):
                                if tdef and args == [3]:
                                    args_l = [[], [3]]
                                else:
                                    args_l = [args]
                                for a in args_l:
                                    k1, k2 = Clock(), Clock()
                                    ns = {'time': k1}
                                    exec(text, ns)
                                    r1 = ns['wait'](cond_of(kind, k1), *a)
                                    saved = harness.time
                                    harness.time = k2
                                    try:
                                        w = eval(call, {'waiter': harness.waiter})
                                        r2 = w(cond_of(kind, k2), *a)
                                    finally:
                                        harness.time = saved
                                    if r1 != r2 or k1.trace != k2.trace:
                                        bad.append(f'{text!r} -> {call} ({kind}, {a}): {r1!r} / {r2!r}')
    for b in bad[:5]:
        print('    ' + b)
    ck(not bad, f'waits: {forms} forms of the wait template '
       'and their waiter(...) give the same value, cond calls and sleeps')
    # an odd variant (a try around the call, a loop of another shape) is not taken
    odd = ('def wait(cond, secs, step=1.0):\n    t0 = time.time()\n    while time.time() - t0 < secs:\n        try:\n'
           '            v = cond()\n        except OSError:\n            v = None\n        if v:\n            return v\n'
           '        time.sleep(step)\n    return None')
    ck(harness_codemod.wait_form(harness_codemod.ast.parse(odd).body[0], odd) is None, 'waits: an odd variant is left')
    try:
        harness.waiter()(lambda: 1)
        ck(False, 'waits: a waiter without a timeout default needs one')
    except TypeError:
        ck(True, 'waits: a waiter without a timeout default needs one')


def captured(fn, *a, **kw):
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        r = fn(*a, **kw)
    return r, buf.getvalue()


HOST_LIST = (
    '  #3 0ABC species=065CBE70 hp=80.00 dist=4.0 pos=(1.0 2.0 3.0) spd=0.0 vel=(0.0 0.0 0.0) upd=64/64 velfr=0/64 '
    'moved64=0.0 family body=1 impressed=0 agl=0.00\n'
    '  #4 0ABD species=11111111 hp=0.00 dist=5.0 DEAD pos=(1.0 2.0 3.0) spd=0.0\n'
    '  #5 0ABE species=11111111 hp=3.00 dist=5.0 pos=(1.0 2.0 3.0) spd=0.0\n'
    '  #6 0ABF species=22222222 hp=50.00 dist=60.0 pos=(9.0 8.0 7.0) spd=1.0 pack\n'
    '  #7 0AC0 not updated for 900 ms\n'
    '  #8 0AC1 species=065CBE70 hp=90.00 dist=6.0 pos=(0.0 0.0 0.0) spd=0.0\n'
    '  #9 0AC2 species=33333333 hp=40.00 dist=12.5 pos=(-1.5 0.0 2.0) spd=0.0 on_partner body=1\n'
    '  #10 0AC3 species=44444444 hp=40.00 dist=20.0 pos=(5.0 5.0 5.0) spd=0.0 family\n')
GUEST_LIST = ('  nest 00000001/00000002 herd=0 alive=1 noun=0\n'
              '  #6 species=22222222 spawned hp=50.00 dist=10.0 err=0.10 age=0.1 want=(0 0 0)\n'
              '  #9 species=33333333 spawned hp=40.00 dist=3.0 err=0.10\n'
              '  #8 species=065CBE70 pending hp=0.00 dist=-1.0 err=-1.00\n')


class NpcCon:
    def __init__(self, npcs, avatar='coop.avatar: key=0x65CBE70 pos=(0 0 0)'):
        self.npcs, self.avatar = npcs, avatar

    def call(self, line):
        return {'ok': True, 'out': self.npcs if line == 'coop.npc list' else self.avatar if line == 'coop.avatar' else ''}


def check_api():
    k = Clock()
    saved = harness.time
    harness.time = k
    try:
        r, text = captured(harness.wait_until, cond_of('third', k), 10, 1.0, what='the third call')
        ck(r == 'yes' and re.search(r'^  wait the third call: \d+\.\d s$', text, re.M), f'api: wait_until says how long '
           f'it waited ({text.strip()})')
        r, text = captured(harness.wait_until, cond_of('never', k), 2, 0.5, what='nothing')
        ck(r is None and 'not within 2 s (last value 0)' in text, f'api: on a timeout the last value ({text.strip()})')
        r, text = captured(harness.wait_until, cond_of('never', k), 2, 0.5)
        ck(r is None and text == '', 'api: without a reason wait_until prints nothing (the codemod\'s waits)')
        k.t = 1000.0
        r, text = captured(harness.wait_until, cond_of('late', k), 3, 1.0, final=True, what='late')
        ck(r == 7 and 'last value 7' in text, 'api: final=True: one more call after the timeout')
        k.t = 0.0
        vals = harness.poll(lambda: k.t, 2.0, 0.5)
        ck(vals == [0.0, 0.5, 1.0, 1.5, 2.0], f'api: poll reads every step until the timeout ({vals})')
        k.t = 0.0
        vals = harness.poll(lambda: k.t, 5.0, 0.5, until=lambda v: v >= 1.0)
        ck(vals == [0.0, 0.5, 1.0], f'api: poll stops at until ({vals})')
    finally:
        harness.time = saved
    d = harness.kv('cell(host): food=12 mirrors=7 key=0x1F a.b=3 x=1 x=2 =bad not:this')
    ck(d == {'food': '12', 'mirrors': '7', 'key': '0x1F', 'a.b': '3', 'x': '1'}, f'api: kv ({d})')
    ck(harness.kv(None) == {}, 'api: kv of no text')
    hl = harness.npc_list(None, HOST_LIST)
    ck([n['id'] for n in hl] == ['3', '4', '5', '6', '8', '9', '10'], 'api: npc_list reads the host lines (not the '
       '"not updated" one)')
    n3, n4, n6 = hl[0], hl[1], hl[3]
    ck(n3['family'] and n3['species'] == '065CBE70' and n3['hp'] == 80.0 and n3['pos'] == (1.0, 2.0, 3.0)
       and n3['spawned'] is None and n4['dead'] and n6['pack'] and n6['dist'] == 60.0, 'api: npc_list fields')
    gl = harness.npc_list(None, GUEST_LIST)
    ck([(n['id'], n['spawned']) for n in gl] == [('6', True), ('9', True), ('8', False)], 'api: npc_list reads the '
       'guest lines')
    host, guest = NpcCon(HOST_LIST), NpcCon(GUEST_LIST)
    ck(harness.own_species(host) == '065CBE70', 'api: own_species pads to 8 digits')
    p = harness.pick_npc(host)
    ck(p and p['id'] == '6', f'api: pick_npc leaves out the family (#3), the dead (#4), the weak (#5) '
       f'and the host\'s species (#8): #{p and p["id"]}')
    p = harness.pick_npc(host, guest)
    ck(p and p['id'] == '6', 'api: pick_npc with a guest: one it mirrors')
    p = harness.pick_npc(host, guest, exclude=[6])
    ck(p and p['id'] == '9', 'api: pick_npc leaves out the excluded')
    p = harness.pick_npc(host, max_dist=15)
    ck(p and p['id'] == '9', 'api: pick_npc max_dist')
    p = harness.pick_npc(host, wild=False)
    ck(p and p['id'] == '3', 'api: pick_npc wild=False takes the first alive one')
    ck(harness.pick_npc(host, NpcCon('')) is None, 'api: pick_npc: none the guest mirrors')
    k = Clock()
    harness.time = k
    try:
        r, text = captured(harness.pick_npc, host, NpcCon(''), timeout=3, step=1.0, what='a wild NPC')
    finally:
        harness.time = saved
    ck(r is None and 'not within 3 s' in text, 'api: pick_npc with a timeout tries again and says so')
    r, text = captured(harness.check_line, 0, 'the thing')
    ck(r == 0 and text == '  FAIL the thing\n', 'api: check_line FAIL')
    r, text = captured(harness.check_line, 'x', 'the thing')
    ck(r == 'x' and text == '  ok   the thing\n', 'api: check_line ok')
    r, text = captured(harness.result_line, False)
    ck(r == 1 and text == 'RESULT: FAIL\n', 'api: result_line')
    r, text = captured(harness.inconclusive, 'no free planet after a jump')
    ck(r == 0 and labverdict.output_verdict(text) == (labverdict.INCONCLUSIVE, 'no free planet after a jump'),
       f'api: the lab reads inconclusive() as INCONCLUSIVE ({text.strip()})')
    ck(labverdict.output_verdict('  FAIL x\nRESULT: FAIL\n' + text)[0] is None, 'api: a RESULT: FAIL wins')


def main():
    with tempfile.TemporaryDirectory() as d:
        logs = {}
        for inst in (1, 2):
            p = os.path.join(d, f'sporekit.{inst}.log')
            with open(p, 'wb') as f:
                f.write(('line one\nline zwei \xfc\n' * inst).encode('utf-8') + b'\xff\xfe broken\n')
            logs[inst] = p
        fake_lane = types.ModuleType('lane')
        fake_lane.game_log = lambda inst: logs.get(inst, os.path.join(d, 'missing.log'))
        saved = sys.modules.get('lane')
        sys.modules['lane'] = fake_lane
        try:
            check_copies(fake_lane)
        finally:
            if saved is not None:
                sys.modules['lane'] = saved
            else:
                del sys.modules['lane']
    check_waits()
    check_api()
    import test_lint
    with tempfile.TemporaryDirectory() as d:
        os.makedirs(os.path.join(d, 'tools', 'tests'))
        open(os.path.join(d, 'tools', 'harness_codemod.py'), 'w').close()
        copy = "import re\n\n\ndef faults(con):\n    m = re.search(r'total faults: (\\d+)', con.call('exc')['out'])\n" \
               "    return int(m.group(1)) if m else -1\n"
        open(os.path.join(d, 'tools', 'tests', 'coop_copy.py'), 'w').write(copy)
        open(os.path.join(d, 'tools', 'tests', 'coop_clean.py'), 'w').write('from harness import faults\n')
        probs = test_lint.copy_problems(d)
        ck(len(probs) == 1 and 'coop_copy.py: copies the harness helper(s) faults' in probs[0],
           f'lint: test_lint flags a new copy of a harness helper ({probs})')
    r, text = captured(harness_codemod.self_test)
    ck(r == 0, 'codemod: harness_codemod.py --self-test' + ('' if r == 0 else '\n' + text))
    print('RESULT:', 'OK' if OK else 'FAIL')
    return 0 if OK else 1


if __name__ == '__main__':
    sys.exit(main())
