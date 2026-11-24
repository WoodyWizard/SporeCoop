#!/usr/bin/env python3
"""BUG-0873: shared creations library persists across sessions under its own Sporepedia tab without polluting My Creations.

Two games on the creature fixture (cold start gives host and guest distinct creations-host.package /
creations-guest.package). Checks:
1. Background sync (creations 1.3): host receives at least one of the guest's creations into
   `SporeCoop/SharedLibrary/` (`index.bin` + `<hash>.png`) and imports it via `ImportPNG` into the Pollination DB.
2. Live game state:
   - `coop.library feed lib` (calling the real `FeedItem_GetAssetKeys` hook) returns >= 1 imported creation with
     `in_rm=1` (registered in Spore's ResourceManager).
   - `coop.library feed my` verifies `AssetBrowserFeedLists` in game memory contains the Shared Creations category
     (`tab_in_default=1`) and none of the imported library keys leak into My Creations (`leaked=0`).
3. Deduplication: re-ingesting an already stored PNG reports `added=0` and keeps the entry count unchanged.
4. Session-end persistence: after the guest leaves (`coop.leave`), the files on disk, Pollination keys (`in_rm=1`),
   and Sporepedia feed (`count >= 1`, `leaked=0`, `tab_in_default=1`) all persist.
5. Failed-import retry: copying one real PNG, clearing the library, arming `coop.library failnext`, and ingesting it
   causes the first `ImportPNG` attempt to fail (`imp=0`, `att=1`), and `coop.library retry` imports it cleanly
   (`imp=1`, `in_rm=1`).

FAIL twins:
- SPORE_FIX_OFF=creations.shared_library: no creations synced/imported, Shared Creations tab absent.
- SPORE_FIX_OFF=creations.library_my_filter: imported library creations leak into My Creations (`leaked > 0`).
- SPORE_FIX_OFF=creations.library_retry: failed import gives up permanently (`gave_up=1`), retry never imports it.

  SPORE_LANE=6 python3 tools/tests/coop_shared_library.py
"""
import glob
import os
import re
import shutil
import time

from harness import wait_until
import lane
from twoinst import Checks, start


def parse_feed_lib(con):
    text = con.call('coop.library feed lib')['out']
    m = re.search(r'\bcount=(\d+)\s+tab=(\d+)', text)
    keys = re.findall(r'([0-9A-Fa-f]{8})!([0-9A-Fa-f]{8})\.([0-9A-Fa-f]{8})\s+in_rm=(\d+)', text)
    return dict(
        raw=text,
        count=int(m.group(1)) if m else 0,
        tab=int(m.group(2)) if m else 0,
        keys=[(g, i, t, int(rm)) for g, i, t, rm in keys],
    )


def parse_feed_my(con):
    text = con.call('coop.library feed my')['out']
    m = re.search(r'\bhidden=(\d+)\s+leaked=(\d+)\s+tab_in_default=(\d+)', text)
    return dict(
        raw=text,
        hidden=int(m.group(1)) if m else 0,
        leaked=int(m.group(2)) if m else 0,
        tab_in_default=int(m.group(3)) if m else 0,
    )


def parse_entries(con):
    text = con.call('coop.library list')['out']
    rows = []
    for m in re.finditer(
        r'([0-9a-f]{16})\s+group=([0-9A-Fa-f]{8})\s+imp=(\d+)\s+key=([0-9A-Fa-f]{8})!([0-9A-Fa-f]{8})\.([0-9A-Fa-f]{8})\s+'
        r'att=(\d+)\s+gave_up=(\d+)\s+foreign=(\d+)',
        text,
    ):
        rows.append(dict(
            hash=m.group(1),
            group=m.group(2),
            imp=int(m.group(3)),
            key=(m.group(4), m.group(5), m.group(6)),
            att=int(m.group(7)),
            gave_up=int(m.group(8)),
            foreign=int(m.group(9)),
        ))
    return text, rows


def main():
    host, guest = start('creature')
    ck = Checks()
    # The creature fixture populates host's My Creations with PNGs (own_pngs=7) while guest starts empty;
    # verify guest receives, imports, isolates, persists offline, deduplicates, and retries host's creations.
    lib_dir = os.path.join(lane.GUEST_DIR, 'SporeCoop', 'SharedLibrary')
    retry_copy = os.path.join(lane.GUEST_DIR, 'SporeCoop', 'retry_test.png')

    try:
        # 1. Wait for creations 1.3 background sync + ImportPNG on guest
        def synced():
            fl = parse_feed_lib(guest)
            return fl if fl['count'] >= 1 and any(rm == 1 for _, _, _, rm in fl['keys']) else None

        fl = wait_until(synced, 45, 1.5, 'guest receives and imports host creations')
        list_raw, rows = parse_entries(guest)
        print('  guest library:', list_raw.splitlines()[0] if list_raw else '-', flush=True)
        ck(fl is not None and fl['count'] >= 1,
           f'guest Shared Creations feed lists imported partner creations (count={fl["count"] if fl else 0})')
        ck(fl is not None and all(rm == 1 for _, _, _, rm in fl['keys']) and len(fl['keys']) >= 1,
           'all feed keys exist in Spore ResourceManager (in_rm=1)')

        # 2. Verify persistent files on disk
        idx_file = os.path.join(lib_dir, 'index.bin')
        png_files = sorted(glob.glob(os.path.join(lib_dir, '*.png')))
        ck(os.path.isfile(idx_file) and len(png_files) >= 1,
           f'SharedLibrary persisted on disk (index={os.path.isfile(idx_file)}, pngs={len(png_files)})')

        # 3. Verify Sporepedia tab registration and My Creations isolation
        fm = parse_feed_my(guest)
        print('  guest feed my:', fm['raw'].strip(), flush=True)
        ck(fm['tab_in_default'] == 1,
           f'AssetBrowserFeedLists default root includes Shared Creations category (tab_in_default={fm["tab_in_default"]})')
        ck(fm['hidden'] >= 1 and fm['leaked'] == 0,
           f'imported partner creations stay out of My Creations (hidden={fm["hidden"]}, leaked={fm["leaked"]})')

        # 4. Verify deduplication on re-ingesting an existing PNG
        if png_files:
            n_before = len(rows)
            shutil.copyfile(png_files[0], retry_copy)
            dedup_out = guest.call('coop.library ingest retry_test.png dedup')['out'].strip()
            _, rows_after = parse_entries(guest)
            ck('added=0' in dedup_out and len(rows_after) == n_before,
               f'duplicate PNG rejected cleanly ({dedup_out}, entries {n_before}->{len(rows_after)})')

        # 5. End session and verify the shared library persists offline
        guest.call('coop.leave')
        left = wait_until(lambda: not re.search(r"remote \d+ 'Bob'", host.call('coop.status')['out']),
                          20, 1.0, 'guest leaves session')
        ck(left, 'session ended cleanly')
        time.sleep(2)
        fl_after = parse_feed_lib(guest)
        fm_after = parse_feed_my(guest)
        ck(fl_after['count'] >= 1 and all(rm == 1 for _, _, _, rm in fl_after['keys']),
           f'Shared Creations feed persists after session end (count={fl_after["count"]})')
        ck(fm_after['tab_in_default'] == 1 and fm_after['leaked'] == 0,
           f'Shared Creations tab and My Creations filter persist offline (tab={fm_after["tab_in_default"]}, leaked={fm_after["leaked"]})')

        # 6. Verify failed-import retry path (fails when creations.library_retry is off)
        if os.path.isfile(retry_copy):
            guest.call('coop.library clear')
            guest.call('coop.library failnext')
            ing = guest.call('coop.library ingest retry_test.png retry_check')['out'].strip()
            print('  retry ingest:', ing, flush=True)

            def failed_once():
                _, r = parse_entries(guest)
                return r[0] if (len(r) == 1 and r[0]['imp'] == 0 and r[0]['att'] >= 1) else None

            first = wait_until(failed_once, 15, 0.5, 'first ImportPNG attempt fails as injected')
            ck(first is not None, f'first ImportPNG failure recorded ({first})')

            guest.call('coop.library retry')

            def retried_ok():
                _, r = parse_entries(guest)
                fl_r = parse_feed_lib(guest)
                return (r[0], fl_r) if (len(r) == 1 and r[0]['imp'] == 1 and fl_r['count'] >= 1 and
                                        any(rm == 1 for _, _, _, rm in fl_r['keys'])) else None

            ret = wait_until(retried_ok, 20, 0.5, 'retry imports PNG into Pollination DB')
            ck(ret is not None,
               f'failed import succeeded on retry (imp={ret[0]["imp"] if ret else 0}, feed={ret[1]["count"] if ret else 0})')

        ck.faults(host=host, guest=guest)
        return ck.result()
    finally:
        if os.path.exists(retry_copy):
            os.remove(retry_copy)
        try:
            host.call('coop.library clear')
        except OSError:
            pass
        try:
            guest.call('coop.library clear')
        except OSError:
            pass


if __name__ == '__main__':
    raise SystemExit(main())
