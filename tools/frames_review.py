#!/usr/bin/env python3
"""Contact sheets of a lab run's key frames, for a review by eye (docs/06-testing.md#testing-key-frames).

  tools/frames_review.py <run> [--max 80] [--test name[,name]] [--kind key|phase|sameview]

<run> is a run id (out/lab/<run>) or a folder. For every key frame (twoinst.keyframe) one JPEG with the games'
frames side by side (host first) under a label (test, tag, note = what should be visible, each game's mode and
position) goes into <run>/frames/sheets/, with index.html (the sheets in order) and sheets.md (one line per sheet:
the path and the note, for an agent that opens them with its image reader). At most --max sheets (the sameview
frames first, then the other frames spread over the tests). Needs Pillow; without it the tool says so and exits 2.
"""
import argparse
import html
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(HERE, 'tests'))
import frames  # noqa: E402


def resolve(run):
    if os.path.isdir(run):
        return os.path.abspath(run)
    p = os.path.join(ROOT, 'out', 'lab', run)
    return p if os.path.isdir(p) else None


def build(run_dir, limit=80, tests=None, kind=None, out=None):
    """the sheets of a run; returns (list of (sheet path, entry), out folder)"""
    entries = frames.load_run(run_dir)
    if tests:
        entries = [e for e in entries if e.get('test') in tests]
    if kind:
        entries = [e for e in entries if e.get('kind') == kind]
    chosen = frames.select(entries, limit)
    out = out or os.path.join(run_dir, 'frames', 'sheets')
    os.makedirs(out, exist_ok=True)
    for f in os.listdir(out):  # a new build replaces the old sheets
        if f.endswith('.jpg') or f in ('index.html', 'sheets.md'):
            os.remove(os.path.join(out, f))
    made = []
    for i, e in enumerate(chosen, 1):
        p = os.path.join(out, frames.sheet_name(i, e))
        if frames.make_sheet(e, p):
            made.append((p, e))
    write_index(out, made, len(entries), run_dir)
    return made, out


def write_index(out, made, total, run_dir):
    rel = os.path.relpath
    with open(os.path.join(out, 'sheets.md'), 'w') as f:
        f.write(f'# Key frame sheets of {os.path.basename(run_dir)}\n\n')
        f.write(f'{len(made)} sheets of {total} key frames. Review checklist: docs/06-testing.md#testing-key-frames\n\n')
        for p, e in made:
            f.write(f'* `{p}`: {e.get("test")} {e.get("tag")} ({e.get("kind")}): {e.get("note") or ""}\n')
    rows = []
    for p, e in made:
        cap = html.escape(f'{e.get("test")} #{int(e.get("nn") or 0):02d} {e.get("tag")} [{e.get("kind")}]')
        note = html.escape(e.get('note') or '')
        rows.append(f'<figure id="{html.escape(os.path.basename(p))}"><figcaption><b>{cap}</b><br>{note}</figcaption>'
                    f'<a href="{html.escape(rel(p, out))}"><img src="{html.escape(rel(p, out))}" loading="lazy"></a>'
                    f'</figure>')
    page = ('<!doctype html><meta charset="utf-8"><title>Key frames ' + html.escape(os.path.basename(run_dir))
            + '</title><style>body{background:#18181c;color:#ddd;font:14px sans-serif;margin:16px}'
              'img{max-width:100%;border:1px solid #444}figure{margin:0 0 28px}b{color:#fe8}</style>'
            + f'<h1>Key frames of {html.escape(os.path.basename(run_dir))}</h1>'
            + f'<p>{len(made)} sheets of {total} key frames; checklist: docs/06-testing.md#testing-key-frames</p>'
            + '\n'.join(rows))
    with open(os.path.join(out, 'index.html'), 'w') as f:
        f.write(page)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('run')
    ap.add_argument('--max', type=int, default=80)
    ap.add_argument('--test', default='')
    ap.add_argument('--kind', default='')
    a = ap.parse_args()
    if not frames.have_pil():
        print('frames_review: Pillow is missing (python3 -m pip install --user pillow): no sheets')
        return 2
    run = resolve(a.run)
    if not run:
        print(f'frames_review: no run folder {a.run}')
        return 1
    made, out = build(run, a.max, [t for t in a.test.split(',') if t] or None, a.kind or None)
    print(f'{len(made)} sheets in {out}')
    print(f'  index: {os.path.join(out, "index.html")}')
    print(f'  list:  {os.path.join(out, "sheets.md")}')
    return 0 if made else 1


if __name__ == '__main__':
    sys.exit(main())
