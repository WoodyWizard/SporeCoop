"""Windows QA console for the local QA copy and the MSI test PC (docs/06-testing.md#testing-two-pc-msi).

    py tools/qa_win/qa.py <instance> <console command...>   a QA game on this PC (instance N: TCP 47800 + N - 1)
    py tools/qa_win/qa.py msi <console command...>          the MSI's game through the SSH tunnel (local 47850; msi2, msi3: 47851, 47852)

Safety (both learnt the hard way, 2026-11-11):
* a local instance is refused unless the process listening on its port runs from the QA copy (out/<copy>/native):
  Steam relaunched the owner's own install once and a test command would have reached it;
* the MSI port is refused unless it belongs to ssh.exe (the tunnel), never a game on this PC.
Console `click`, `drag`, `wheel` and `key` drive the real mouse and keyboard of the PC that runs that game: on this
PC only with the owner's OK (never while they use it); on the MSI they are fine.
"""
import ctypes
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
from sporectl import Console  # noqa: E402

QA_COPY = os.environ.get('SPORECOOP_QA_COPY', os.path.join(ROOT, 'out', 'release36', 'native'))
MSI_PORT = 47850


def owner_path(port):
    """the executable of the process listening on TCP `port` on this PC, or None"""
    out = subprocess.run(['netstat', '-ano', '-p', 'TCP'], capture_output=True, text=True).stdout
    for line in out.splitlines():
        p = line.split()
        if len(p) >= 5 and p[1].endswith(':%d' % port) and p[3] == 'LISTENING':
            h = ctypes.windll.kernel32.OpenProcess(0x1000, False, int(p[4]))
            if not h:
                return None
            buf = ctypes.create_unicode_buffer(1024)
            n = ctypes.c_ulong(1024)
            ok = ctypes.windll.kernel32.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(n))
            ctypes.windll.kernel32.CloseHandle(h)
            return buf.value if ok else None
    return None


def console(target):
    """a Console for instance number `target` (int / digit string) or 'msi'; raises SystemExit when unsafe"""
    t = str(target).lower()
    if t in ('msi', 'msi2', 'msi3'):  # msiN: the MSI's Nth test game (msi.ps1 -Instance N), its tunnel 47849 + N
        port = MSI_PORT + (int(t[3:]) - 1 if len(t) > 3 else 0)
        path = owner_path(port)
        if not path or not path.lower().endswith('ssh.exe'):
            sys.exit('[refused] port %d is not the MSI tunnel (%s): start it with tools/qa_win/msi.ps1 tunnel' % (port, path))
        return Console(port - 47800 + 1)
    inst = int(target)
    path = owner_path(47800 + inst - 1)
    exe = os.path.normcase(os.path.join(QA_COPY, 'SporebinEP1', 'SporeApp.exe'))
    if not path or os.path.normcase(path) != exe:
        sys.exit('[refused] port %d is not a QA game of %s: %s' % (47800 + inst - 1, QA_COPY, path))
    return Console(inst)


def call(con, line):
    return con.call(line).get('out', '')


if __name__ == '__main__':
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    r = console(sys.argv[1]).call(' '.join(sys.argv[2:]))
    print(('' if r.get('ok') else '[not ok] ') + str(r.get('out', r)))
