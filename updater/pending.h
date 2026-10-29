/*
 * The deferred files of an update (BUG-0150), shared by the loader (loader.c, plain C) and the
 * updater (winupdate.cpp apply_pending): an update could not replace a file the game held open
 * (Data\SporeCoop.package), so it left <file>.new and listed the file, relative to the game folder,
 * one per line in SporebinEP1\mods\sporecoop.pending. Before the next start the new files take
 * their place. An entry whose swap fails again (another instance on the same game folder still
 * holds the file, a scanner locks it) stays in the list for the start after that; the list is
 * deleted only when nothing is left. Before, the list was deleted whatever happened and the .new
 * file was never applied. The replaced file is kept as <file>.sporecoop-old (removed by
 * upd::cleanup_old; the loader removes it at once when it can).
 *
 * Only Win32 calls, no C runtime: the loader is a small proxy DLL.
 */
#ifndef SPORECOOP_PENDING_H
#define SPORECOOP_PENDING_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#ifndef SC_OLD_SUFFIX
#define SC_OLD_SUFFIX L".sporecoop-old"
#endif

/* tests (BUG-0150): 0 = the old behaviour (the list is deleted whatever the swaps did) */
static int sc_pending_keep_failed = 1;

/* applies the list in <root>\SporebinEP1\mods\sporecoop.pending; returns how many entries wait
 * for a later start (-1: no list) */
static int sc_apply_pending_unlocked(const wchar_t *root, int delete_old)
{
    wchar_t list[MAX_PATH * 2];
    HANDLE f;
    DWORD size, got = 0, kept_len = 0, written;
    char *buf, *kept, *line;
    int left = 0;
    if (lstrlenW(root) > MAX_PATH)
        return -1;
    lstrcpyW(list, root);
    lstrcatW(list, L"\\SporebinEP1\\mods\\sporecoop.pending");
    f = CreateFileW(list, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return -1;
    size = GetFileSize(f, NULL);
    if (size == INVALID_FILE_SIZE || size > (1u << 20)) { /* the whole list (was: 4096 bytes) */
        CloseHandle(f);
        return -1;
    }
    buf = (char *)HeapAlloc(GetProcessHeap(), 0, size + 1);
    kept = (char *)HeapAlloc(GetProcessHeap(), 0, size + 2);
    if (!buf || !kept) {
        CloseHandle(f);
        if (buf)
            HeapFree(GetProcessHeap(), 0, buf);
        if (kept)
            HeapFree(GetProcessHeap(), 0, kept);
        return -1;
    }
    if (!ReadFile(f, buf, size, &got, NULL))
        got = 0;
    CloseHandle(f);
    buf[got] = 0;
    line = buf;
    while (*line) {
        char *end = line, *p, c;
        int bad, k, waits = 0;
        wchar_t target[MAX_PATH * 2], fresh[MAX_PATH * 2], old[MAX_PATH * 2];
        while (*end && *end != '\n' && *end != '\r')
            end++;
        c = *end;
        *end = 0;
        /* plain relative names only (the updater writes them; never "..", drives or roots) */
        bad = !*line || line[0] == '/' || line[0] == '\\' || end - line > MAX_PATH - 20;
        for (p = line; *p; p++)
            if (*p == ':' || (p[0] == '.' && p[1] == '.'))
                bad = 1;
        if (!bad) {
            lstrcpyW(target, root);
            lstrcatW(target, L"\\");
            k = lstrlenW(target);
            for (p = line; *p && k < MAX_PATH * 2 - 20; p++)
                target[k++] = (wchar_t)(*p == '/' ? '\\' : (unsigned char)*p);
            target[k] = 0;
            lstrcpyW(fresh, target);
            lstrcatW(fresh, L".new");
            lstrcpyW(old, target);
            lstrcatW(old, SC_OLD_SUFFIX);
            if (GetFileAttributesW(fresh) != INVALID_FILE_ATTRIBUTES) {
                int had = GetFileAttributesW(target) != INVALID_FILE_ATTRIBUTES;
                if (had && !MoveFileExW(target, old, MOVEFILE_REPLACE_EXISTING)) {
                    waits = 1; /* still open somewhere: the next start */
                } else if (!MoveFileExW(fresh, target, MOVEFILE_REPLACE_EXISTING)) {
                    if (had)
                        MoveFileExW(old, target, MOVEFILE_REPLACE_EXISTING); /* keep the game working */
                    waits = 1;
                } else if (delete_old && had) {
                    DeleteFileW(old);
                }
            }
            if (waits) {
                for (p = line; *p; p++)
                    kept[kept_len++] = *p;
                kept[kept_len++] = '\n';
                left++;
            }
        }
        if (!c)
            break;
        line = end + 1;
    }
    if (left && sc_pending_keep_failed) {
        f = CreateFileW(list, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        if (f != INVALID_HANDLE_VALUE) {
            WriteFile(f, kept, kept_len, &written, NULL);
            CloseHandle(f);
        }
    } else {
        DeleteFileW(list);
    }
    HeapFree(GetProcessHeap(), 0, buf);
    HeapFree(GetProcessHeap(), 0, kept);
    return sc_pending_keep_failed ? left : 0;
}

/* The name of a cross-process lock for a file or folder (BUG-0243, BUG-0245): "Local\<prefix><8 hex
 * digits>", the same for every form of the same path (8.3 names, drive mappings, case) from coop.dll,
 * SporeKit, the loader and the setup: the full path, resolved through an open handle when it exists,
 * without a \\?\ prefix and trailing separators, lowercased, FNV-1a 32 over the UTF-16 units.
 * Returns 0 on failure. */
static int sc_lock_name(const wchar_t *prefix, const wchar_t *path, wchar_t *out, int out_len)
{
    wchar_t full[MAX_PATH * 2], fin[MAX_PATH * 2];
    const wchar_t *p = full;
    DWORD n = GetFullPathNameW(path, MAX_PATH * 2, full, 0), attr;
    HANDLE h;
    unsigned int hash = 2166136261u;
    int len, i, plen = lstrlenW(prefix);
    static const wchar_t hex[] = L"0123456789abcdef";
    if (!n || n >= MAX_PATH * 2 || plen + 7 + 8 + 1 > out_len)
        return 0;
    attr = GetFileAttributesW(full);
    if (attr != INVALID_FILE_ATTRIBUTES) {
        h = CreateFileW(full, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0, OPEN_EXISTING,
                        FILE_FLAG_BACKUP_SEMANTICS, 0);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD m = GetFinalPathNameByHandleW(h, fin, MAX_PATH * 2, 0);
            CloseHandle(h);
            if (m && m < MAX_PATH * 2)
                p = fin;
        }
    }
    if (p[0] == L'\\' && p[1] == L'\\' && p[2] == L'?' && p[3] == L'\\') {
        p += 4;
        if ((p[0] == L'U' || p[0] == L'u') && (p[1] == L'N' || p[1] == L'n') && (p[2] == L'C' || p[2] == L'c') &&
            p[3] == L'\\')
            p += 4;
    }
    len = lstrlenW(p);
    while (len > 0 && (p[len - 1] == L'\\' || p[len - 1] == L'/'))
        len--;
    /* lowercase like the file system (kernel32 only: the loader imports nothing else) */
    if (p >= full && p < full + MAX_PATH * 2) { /* never map a buffer onto itself */
        lstrcpynW(fin, p, len + 1);
        p = fin;
    }
    len = LCMapStringW(LOCALE_INVARIANT, LCMAP_LOWERCASE, p, len, full, MAX_PATH * 2);
    if (len <= 0)
        return 0;
    for (i = 0; i < len; i++) {
        hash ^= (unsigned int)(unsigned short)full[i];
        hash *= 16777619u;
    }
    lstrcpyW(out, L"Local\\");
    lstrcatW(out, prefix);
    len = lstrlenW(out);
    for (i = 0; i < 8; i++)
        out[len + i] = hex[(hash >> (28 - 4 * i)) & 15];
    out[len + 8] = 0;
    return 1;
}

/* The update lock (BUG-0243): a named mutex per game folder (sc_lock_name "SporeCoopUpdate-"). An
 * update holds it while it swaps files (apply_pack); a start of the loader or of coop.dll on the same
 * folder (a hot reload, a second instance) then neither applies the pending swaps nor deletes the
 * backups the running update needs for its rollback, and the game's exit waits for it. 0 = no lock
 * anywhere (tests: update.lock off, win_tests --old-lock). */
static int sc_update_lock_fix = 1;

/* try the lock without waiting: 1 = go on (*held: the handle to release, may be 0 when the lock
 * cannot be made at all), 0 = an update holds it or it cannot be opened (skip this time) */
static int sc_update_trylock(const wchar_t *root, HANDLE *held)
{
    wchar_t name[160];
    HANDLE h;
    DWORD r;
    *held = 0;
    if (!sc_update_lock_fix || !sc_lock_name(L"SporeCoopUpdate-", root, name, 160))
        return 1;
    h = CreateMutexW(NULL, FALSE, name);
    if (!h)
        return GetLastError() == ERROR_ACCESS_DENIED ? 0 : 1;
    r = WaitForSingleObject(h, 0);
    if (r == WAIT_OBJECT_0 || r == WAIT_ABANDONED) {
        *held = h;
        return 1;
    }
    CloseHandle(h);
    return 0;
}

static void sc_update_unlock(HANDLE h)
{
    if (h) {
        ReleaseMutex(h);
        CloseHandle(h);
    }
}

/* applies the list in <root>\SporebinEP1\mods\sporecoop.pending; returns how many entries wait
 * for a later start (-1: no list, or an update in this folder is running: the next start) */
static int sc_apply_pending(const wchar_t *root, int delete_old)
{
    HANDLE h;
    int r;
    if (!sc_update_trylock(root, &h))
        return -1;
    r = sc_apply_pending_unlocked(root, delete_old);
    sc_update_unlock(h);
    return r;
}

#endif
