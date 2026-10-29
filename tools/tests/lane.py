"""The test lane this process runs in (docs/10-dev-pipeline.md). SPORE_LANE=N (default 1):
lane 1 = the real install in the "Games" bottle, lane N>1 = bottle SporeLabN + game folder
SPORE-lab/laneN (tools/spore.sh lane create N). Each lane has its own instances, ports and
profiles, so lanes run in parallel without seeing each other."""
import os
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SPORE = os.path.join(ROOT, 'tools', 'spore.sh')

LANE = int(os.environ.get('SPORE_LANE', '1'))
# lanes 1..10: console ports (TCP) 47800..47819, co-op ports (UDP) 47810..47828 do not collide
# (TCP and UDP ports are separate); LAN discovery finds hosts of lanes 1..5 only (the UI test)
assert 1 <= LANE <= 10, 'lanes 1..10 (copies of the GOG 2024 install since 2026-10-24)'
HOST = 2 * LANE - 1   # SporeKit instance numbers: console port 47800 + N - 1
GUEST = 2 * LANE
PORT = 47810 + HOST - 1  # the host's co-op port
BOTTLE = 'Games' if LANE == 1 else f'SporeLab{LANE}'


def _user():
    """$USER, else the password database's name (the name Wine falls back to for drive_c/users), else
    'user' (a container uid without a passwd entry: imports keep working, a game test fails visibly at
    its fixture step). BUG-0252: make test ran without $USER into a KeyError."""
    if os.environ.get('USER'):
        return os.environ['USER']
    try:
        import getpass
        return getpass.getuser()
    except (KeyError, OSError, ImportError):
        return 'user'


APPDATA = os.path.expanduser(f'~/.var/app/com.usebottles.bottles/data/bottles/bottles/{BOTTLE}/drive_c/users/'
                             + _user() + '/AppData/Roaming')
HOST_PROFILE = 'test'
GUEST_PROFILE = 'inst2' if LANE == 1 else f'inst{GUEST}'
HOST_DIR = os.path.join(APPDATA, 'SporeKit-' + HOST_PROFILE, 'Spore')
GUEST_DIR = os.path.join(APPDATA, 'SporeKit-' + GUEST_PROFILE, 'Spore')
JOIN = f'cheat coop join 127.0.0.1:{PORT} Bob'
if os.environ.get('SPORE_NET'):  # the guest goes through a bad network (tools/tests/netem.py)
    import netem
    JOIN = f'cheat coop join 127.0.0.1:{netem.from_env(PORT, LANE)} Bob'
HOST_CMD = f'cheat coop host {PORT} Alice'


def sh(*args, check=True):
    subprocess.run([SPORE, *args], check=check, stdout=subprocess.DEVNULL)


def run_host():
    sh('run', '-i', str(HOST), '-p', HOST_PROFILE)


def run_guest():
    if LANE == 1:
        sh('run', '-i', '2')
    else:
        sh('run', '-i', str(GUEST), '-p', GUEST_PROFILE)


_MAIN = None


def game_dir(n=None):
    """the game folder of lane n (default: this lane), from any checkout: lane 1 = the folder that
    holds the main checkout (SPORE/SporeCoop), lane N = SPORE-lab/laneN next to it (tools/spore.sh,
    lab.lane_game). Found from the git common dir, so a worktree (../SporeCoop-wt/<branch>) gives the
    same folder as the main checkout (BUG-0068)."""
    global _MAIN
    n = LANE if n is None else n
    if _MAIN is None:
        common = subprocess.run(['git', 'rev-parse', '--path-format=absolute', '--git-common-dir'],
                                cwd=ROOT, capture_output=True, text=True).stdout.strip()
        _MAIN = os.path.dirname(os.path.dirname(common)) if common else os.path.dirname(ROOT)
    return _MAIN if n == 1 else os.path.join(os.path.dirname(_MAIN), 'SPORE-lab', f'lane{n}')


def game_log(inst):
    """the SporeKit log of a game instance (instance 1: sporekit.log, else sporekit.<inst>.log).
    Instances 2N-1 and 2N run in lane N, the extra guests 20+N and 30+N too (BUG-0266)."""
    n = (inst + 1) // 2 if inst <= 20 else inst - 20 if inst <= 30 else inst - 30
    return os.path.join(game_dir(n), 'SporebinEP1', 'mods', 'logs',
                        'sporekit.log' if inst == 1 else f'sporekit.{inst}.log')


def out_dir(*sub):
    """per-lane output (logs, screenshots) so parallel lanes do not overwrite each other"""
    p = os.path.join(ROOT, 'out', *(sub if LANE == 1 else (f'lane{LANE}',) + sub))
    os.makedirs(p, exist_ok=True)
    return p
