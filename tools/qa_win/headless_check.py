"""Phase 0 spike (docs/12-server-and-players.md#server-spike-hidden): what the host's game still does under a condition.
usage: py tools/qa_win/headless_check.py <label> [seconds] [radius m]
Measures the host (QA 9): CPU (% of one core), frames, and how many of its creatures moved more than 0.5 m within 60 m of
the host's creature and within 60 m of the partner's puppet (player 1), plus the guest's NPC state receive count."""
import math, re, subprocess, sys, time
ROOT = r'C:\Users\bandi\AI_Projects\Spore\Spore Multiplayer\SporeCoop'
LABEL = sys.argv[1]
RADIUS = float(sys.argv[3]) if len(sys.argv) > 3 else 60
SECS = float(sys.argv[2]) if len(sys.argv) > 2 else 30


def qa(inst, cmd):
    return subprocess.run(['py', 'tools/qa_win/qa.py', str(inst), cmd], cwd=ROOT, capture_output=True, text=True).stdout


def info():
    s = qa(9, 'info')
    pid = int(re.search(r'pid=(\d+)', s).group(1))
    frame = int(re.search(r'frame=(\d+)', s).group(1))
    src = re.search(r'frames from: (.*)', s)
    return pid, frame, src.group(1).strip() if src else '?'


def cpu(pid):
    r = subprocess.run(['powershell', '-NoProfile', '-Command', '(Get-Process -Id %d).TotalProcessorTime.TotalSeconds' % pid],
                       capture_output=True, text=True).stdout.strip()
    return float(r.replace(',', '.'))


def mem(pid):
    r = subprocess.run(['powershell', '-NoProfile', '-Command', '(Get-Process -Id %d).WorkingSet64' % pid],
                       capture_output=True, text=True).stdout.strip()
    return int(r) / 1e6


def npcs():
    out = {}
    for m in re.finditer(r'#(\d+) ([0-9A-F]{8}) species=\S+ hp=([\d.]+) .*?pos=\(([^)]*)\)', qa(9, 'coop.npc list')):
        out[m.group(2)] = tuple(map(float, m.group(4).split()))
    return out


def anchors():
    a = re.search(r'pos=\(([^)]*)\)', qa(9, 'coop.avatar'))
    s = re.search(r"remote 1 '\S+' puppet=\S+ state=\S+ age=\S+ pos=\(([^)]*)\)", qa(9, 'coop.status'))
    f = lambda m: tuple(map(float, m.group(1).split())) if m else None
    return f(a), f(s)


def guest_rx():
    m = re.search(r'states_rx=(\d+)|rx=(\d+)/s', qa(8, 'coop.npc'))
    return m.group(0) if m else '?'


pid, f0, src = info()
c0 = cpu(pid)
n0 = npcs()
host, partner = anchors()
t0 = time.time()
time.sleep(SECS)
pid, f1, src = info()
c1 = cpu(pid)
n1 = npcs()
dt = time.time() - t0
host2, partner2 = anchors()


def moved_near(anchor):
    near = [k for k, p in n0.items() if anchor and math.dist(p, anchor) < RADIUS and k in n1]
    moved = [k for k in near if math.dist(n0[k], n1[k]) > 0.5]
    return len(moved), len(near)


mh, nh = moved_near(host)
mp, np_ = moved_near(partner)
d = math.dist(host, partner) if host and partner else -1
print('%s: %.0f s, frames from %s, fps %.1f, host CPU %.0f %% of a core, %.0f MB; partner %.0f m from the host; '
      'creatures moved near the host %d/%d, near the partner %d/%d' %
      (LABEL, dt, src, (f1 - f0) / dt, 100 * (c1 - c0) / dt, mem(pid), d, mh, nh, mp, np_), flush=True)
