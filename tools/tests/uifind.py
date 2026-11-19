"""Spore UI windows by caption for test bots (coop.ui find): where a button is on the screen and
whether it is shown, so a bot clicks it like a player would (pause menu, dialogs)."""
import re

LINE = re.compile(r'(?:- |Window )\S+ id=\S+ cmd=\S+ fl=0x([0-9A-Fa-f]+) \[(-?\d+) (-?\d+) (-?\d+) (-?\d+)\]')


def find(con, caption):
    """[(x, y, visible)] for every window with this caption: its centre on the screen and whether it
    and all its parents are visible (flag 0x1); coordinates of a window are relative to its parent"""
    out = []
    group = []
    for line in con.call(f'coop.ui find "{caption}"').get('out', '').splitlines() + ['']:
        if line.startswith('- ') and group:
            out.append(group)
            group = []
        m = LINE.match(line)
        if m:
            group.append((int(m.group(1), 16), *(int(m.group(i)) for i in range(2, 6))))
    if group:
        out.append(group)
    res = []
    for g in out:
        ox = sum(w[1] for w in g[1:])
        oy = sum(w[2] for w in g[1:])
        f, x0, y0, x1, y1 = g[0]
        res.append((ox + (x0 + x1) // 2, oy + (y0 + y1) // 2, all(w[0] & 1 for w in g)))
    return res


def shown(con, caption):
    """(x, y) of the first visible window with this caption, or None"""
    for x, y, vis in find(con, caption):
        if vis:
            return x, y
    return None


def press(con, caption, expect=None, timeout=10):
    """click a shown button like a player (hover, then click) and wait for what it opens (expect: a
    caption) or for the button to go away; click again only when nothing happened (a click right
    after a menu opened is sometimes ignored). A menu stays "shown" under a dialog it opened."""
    import time
    t0 = time.time()
    while time.time() - t0 < timeout:
        p = shown(con, caption)
        if not p:
            time.sleep(0.5)
            continue
        con.call(f'move {p[0]} {p[1]}')
        time.sleep(0.3)
        con.call(f'click {p[0]} {p[1]}')
        t1 = time.time()
        while time.time() - t1 < 3:
            time.sleep(0.5)
            if expect and shown(con, expect):
                return True
            if not expect and not shown(con, caption):
                return True
    return False


def where(con, ctl):
    """(x, y) of the centre of the shown window with this control id (`coop.ui where`: only through visible parents),
    or None. Use it when a caption is not unique: `coop.ui find` returns the first window with the caption, and a
    hidden copy (the captain panel's own Accept and Decline, BUG-0304) hides the shown one."""
    out = con.call('coop.ui where %08X' % ctl).get('out') or ''
    m = re.search(r'%08X (-?\d+) (-?\d+) (-?\d+) (-?\d+)' % ctl, out)
    if not m:
        return None
    x0, y0, x1, y1 = (int(v) for v in m.groups())
    return (x0 + x1) // 2, (y0 + y1) // 2
