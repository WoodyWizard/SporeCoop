"""Decision 5EA (docs/06-testing.md#testing-pack-own-game): the partner's own game runs its pack. host = QA 9, partner = <guest inst> (8 or msi), player id <pid>.
usage: py tools/qa_win/pack_own_game.py <guest inst> <pid> [victim species] [n recruits]
Recruits n herbivores into the partner's pack (host side, as the partner's recruit), checks the partner's game runs them
(lends, states) and the host walks them (driven), then the partner's own attack order (its game's PlayerCommand at the
proxy) and counts the hits that land on the host's creature."""
import math, os, re, subprocess, sys, time
ROOT = r'C:\Users\bandi\AI_Projects\Spore\Spore Multiplayer\SporeCoop'
G, PID = sys.argv[1], int(sys.argv[2])
VICTIM = sys.argv[3] if len(sys.argv) > 3 else '06806931'
N = int(sys.argv[4]) if len(sys.argv) > 4 else 2
LOG9 = os.path.join(ROOT, 'out', 'release36', 'native', 'SporebinEP1', 'mods', 'logs', 'sporekit.9.log')


def qa(inst, cmd):
    return subprocess.run(['py', 'tools/qa_win/qa.py', str(inst), cmd], cwd=ROOT, capture_output=True, text=True).stdout


def say(*a):
    print(time.strftime('%H:%M:%S'), *a, flush=True)


def spawn(sp):
    r = qa(9, 'coop.npc spawn ' + sp)
    m = re.search(r'spawned ([0-9A-F]{8})', r)
    time.sleep(3)
    l = re.search(r'#(\d+) %s ' % m.group(1), qa(9, 'coop.npc list')) if m else None
    return (m.group(1), int(l.group(1))) if l else (None, None)


def npc_line(ptr):
    m = re.search(r'#\d+ %s species=\S+ hp=([\d.]+) .*?pos=\(([^)]*)\)' % ptr, qa(9, 'coop.npc list'))
    return (float(m.group(1)), tuple(map(float, m.group(2).split()))) if m else (None, None)


def own(inst):
    m = re.search(r'own game \(5EA\).*', qa(inst, 'coop.gpack'))
    return m.group(0) if m else '(no own line)'


def puppet_pos():
    s = qa(9, 'coop.status')
    m = re.search(r"remote %d '\S+' puppet=(\S+) state=\S+ age=\S+ pos=\(([^)]*)\)" % PID, s)
    return tuple(map(float, m.group(2).split())) if m else None


log0 = os.path.getsize(LOG9)
members = []
for _ in range(N):
    c, i = spawn('06539202')
    members.append(c)
    say('recruit', c, qa(9, 'coop.gpack recruit %d %d' % (PID, i)).strip().splitlines()[0])
time.sleep(8)
say('host :', own(9))
say('guest:', own(G))
g = qa(G, 'coop.gpack')
say('guest mirror:', (re.search(r'own pack panel.*', g) or [''])[0] if re.search(r'own pack panel.*', g) else '')
pp = puppet_pos()
for c in members:
    hp, pos = npc_line(c)
    say('member', c, 'hp', hp, 'dist to partner', round(math.dist(pos, pp), 1) if pos and pp else None)
v, vi = spawn(VICTIM)
hp0, _ = npc_line(v)
say('victim', v, '#%s' % vi, 'hp', hp0)
qa(9, 'coop.gpack watch %d' % vi)
say('partner order:', qa(G, 'coop.gpack hostorder %d' % vi).strip())
if os.environ.get('SHOT'):
    time.sleep(2)
    say('shot:', qa(9, 'shot ' + os.environ['SHOT']).strip()[:120])
for t in range(10):
    time.sleep(3)
    hp, pos = npc_line(v)
    ds = []
    for c in members:
        _, mp = npc_line(c)
        ds.append(round(math.dist(mp, pos), 1) if mp and pos else None)
    say('t=%ds victim hp %s, members to victim %s m' % (3 * (t + 1), hp, ds))
    if hp is None or hp <= 0:
        break
log = open(LOG9, encoding='utf-8', errors='replace')
log.seek(log0)
lines = log.read().splitlines()
hits = [l for l in lines if 'watched' in l and ' hit for ' in l]
say('hits on the victim (host log):', len(hits))
for l in hits[:10]:
    print('   ', l[l.index('watched'):][:160])
say('host :', own(9))
say('guest:', own(G))
