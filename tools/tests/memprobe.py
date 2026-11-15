"""Where a test script's memory goes (BUG-0916, docs/06-testing.md#testing-lab-memory).

SPORE_MEMPROBE=1 (or =trace for tracemalloc too: slower, names the allocating lines): a thread of the test process
writes its RSS every second to <run or out>/memprobe.<test>.<pid>.log and, each time the RSS passed
SPORE_MEMPROBE_MB (default 300) and grew by a quarter since the last dump, every thread's Python stack and (trace)
the 12 largest allocation sites. twoinst imports this module, so every lab test has the switch.
"""
import os
import sys
import threading
import time
import traceback


def rss_mb():
    try:
        with open('/proc/self/statm') as f:
            return int(f.read().split()[1]) * os.sysconf('SC_PAGE_SIZE') / 1048576
    except (OSError, ValueError, IndexError):
        return 0.0


def stacks(limit=14):
    """every thread's Python stack as text (the innermost `limit` frames)"""
    names = {t.ident: t.name for t in threading.enumerate()}
    out = []
    for ident, frame in sys._current_frames().items():
        if ident == threading.get_ident():
            continue
        out.append(f'-- thread {names.get(ident, ident)}')
        out += [ln.rstrip() for ln in traceback.format_stack(frame)[-limit:]]
    return '\n'.join(out)


def _run(path, trace, limit_mb):
    last_dump = 0.0
    with open(path, 'a') as f:
        f.write(f'# memprobe pid {os.getpid()} {" ".join(sys.argv)} trace={trace} dump over {limit_mb} MB\n')
        while True:
            r = rss_mb()
            f.write(f'{time.strftime("%H:%M:%S")} rss={r:.0f}M threads={threading.active_count()}\n')
            if r > limit_mb and r > last_dump * 1.25:
                last_dump = r
                f.write(f'== dump at {r:.0f} MB\n{stacks()}\n')
                if trace:
                    import tracemalloc
                    snap = tracemalloc.take_snapshot()
                    for st in snap.statistics('traceback')[:12]:
                        f.write(f'  {st.size / 1048576:.1f} MB in {st.count} blocks:\n')
                        f.write(''.join(f'    {ln}\n' for ln in st.traceback.format()[-8:]))
                    del snap
            f.flush()
            time.sleep(1.0)


def start():
    mode = os.environ.get('SPORE_MEMPROBE', '')
    if mode in ('', '0') or getattr(start, 'on', False):
        return None
    start.on = True
    trace = mode == 'trace'
    if trace:
        import tracemalloc
        tracemalloc.start(8)
    d = os.environ.get('SPORE_LAB_RUN', '').strip() or os.path.join(
        os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), 'out')
    os.makedirs(d, exist_ok=True)
    test = os.environ.get('SPORE_LAB_TEST', '').strip() or os.path.splitext(os.path.basename(sys.argv[0] or 'test'))[0]
    path = os.path.join(d, f'memprobe.{test}.{os.getpid()}.log')
    try:
        limit = float(os.environ.get('SPORE_MEMPROBE_MB', '300'))
    except ValueError:
        limit = 300.0
    threading.Thread(target=_run, args=(path, trace, limit), daemon=True, name='memprobe').start()
    return path
