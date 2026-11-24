// Persistent shared creations library (BUG-0873, creations 1.3, docs/re/creations.md#creations-lineages-sporepedia-feeds).
//  - Storage: PNG cards + atomic index.bin under %APPDATA%/Spore/SporeCoop/SharedLibrary
//  - Import: native App::Thumbnail_cImportExport::ImportPNG (outside Present) + Pollination DB flush
//  - Verification: DecodePNG with pinned IO::FileStream (BUG-9094) skips provisional Cell->Creature mismatched PNGs
//  - Sporepedia: registers our "Shared Creations" category on AssetBrowserFeedLists (default + GA roots),
//    serves library keys from FeedItem_GetAssetKeys, and filters received keys out of My Creations (0x146C1D8C)
//  - Wire (creations 1.3): MSG_CREATION kinds 13 (Have/Want hash lists) and 14 (paced PNG transfer), host relay,
//    reconnect dedup, receiver-decided foreign creations policy
//  - Retries: failed PNG imports retry 60 s later, up to 3 times per run
#include "creations_internal.h"
#include "creations_library.h"
#include "library_core.h"
#include "net/compat.h"

#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace creations {

using namespace library;

static bool g_lib_fix = true;
COOP_FIX("creations.shared_library", "BUG-0873", g_lib_fix,
         "creations: keep partner creations in a persistent shared library with its own Sporepedia tab");

static bool g_lib_my_filter_fix = true;
COOP_FIX("creations.library_my_filter", "BUG-0873", g_lib_my_filter_fix,
         "creations: keep shared-library partner creations out of My Creations");

static bool g_lib_retry_fix = true;
COOP_FIX("creations.library_retry", "BUG-0873", g_lib_retry_fix,
         "creations: retry failed shared-library PNG imports instead of losing them");

// Receiver policy: whether to accept creations a partner received from others (default off; toggled by user/test)
static bool g_allow_foreign = false;

// Resolved EP1 SporeApp.exe (dc04aee5) symbols
static uintptr_t a_ThumbGet = 0;
static uintptr_t a_ImportPng = 0;
static uintptr_t a_DecodePng = 0;
static uintptr_t a_ThumbMetaDtor = 0;
static uintptr_t a_FileStreamCtor = 0;
static uintptr_t a_FileStreamOpen = 0;
static uintptr_t a_FileStreamAddRef = 0;
static uintptr_t a_FileStreamDtor = 0;
static uintptr_t a_PropMgrGet = 0;
static uintptr_t a_PropGetArrayKey = 0;
static uintptr_t a_PropSetArrayKey = 0;
static uintptr_t a_GetDbById = 0;
static uintptr_t a_VecReserve = 0;

constexpr uint32_t kPollinationDbArea = 0x011AC19Du;
constexpr uint32_t kFeedListGroup = 0x6EDC12D4u;
constexpr uint32_t kPropFeedListCategories = 0x744717C0u;
constexpr uint32_t kFeedListRoots[3] = {0x2EA8FB98u, 0x1B327962u, 0xFDECEF02u}; // default, ScenarioDefault, ScenarioGameplaySkinning

typedef void(SK_THISCALL* FeedItem_GetAssetKeys_t)(void* feed, void* outVec);
static FeedItem_GetAssetKeys_t o_FeedGetKeys = nullptr;

// Module state
static LibraryState g_state;
static bool g_loaded_disk = false;
static bool g_tab_state = false;
static bool g_tab_dirty = true;
static bool g_import_queued = false;
static bool g_feed_sync_queued = false;
static std::wstring g_lib_dir;

struct OwnPng {
    uint64_t hash = 0;
    uint32_t group = 0;
    uint32_t instance = 0;
    std::string name;
    std::vector<uint8_t> png;
};
static std::map<uint64_t, OwnPng> g_own_pngs;
static ULONGLONG g_es_stamp = 0;
static double g_last_es_scan = -100.0;
static double g_last_send_at = -100.0;

// Per-session peer sync state (cleared on session end, NOT touching persistent g_state!)
struct PeerWant {
    uint8_t peer = 0;
    uint64_t hash = 0;
};
static std::deque<PeerWant> g_send_queue;
static std::map<uint8_t, std::set<uint64_t>> g_peer_have;
static std::set<uint8_t> g_have_sent_to;
static unsigned g_sent_pngs = 0;
static unsigned g_recv_pngs = 0;
static unsigned g_import_ok = 0;
static unsigned g_import_fail = 0;
static unsigned g_feed_lib_calls = 0;
static unsigned g_feed_my_filtered = 0;
static bool g_test_fail_next = false;

static bool peer_reads_lib(uint8_t id)
{
    bool has = false;
    std::vector<net::FamilyRev> v;
    const net::FamilyInfo* mine = net::family_info((uint8_t)net::Family::creations);
    return mine && C.sess && C.sess->peer_families(id, has, v) &&
           net::compat::has_minor(v, has, (uint8_t)net::Family::creations, mine->rev, kLibMinor);
}

static bool host_reads_lib()
{
    bool has = false;
    std::vector<net::FamilyRev> v;
    const net::FamilyInfo* mine = net::family_info((uint8_t)net::Family::creations);
    return mine && C.sess && C.sess->host_families(has, v) &&
           net::compat::has_minor(v, has, (uint8_t)net::Family::creations, mine->rev, kLibMinor);
}

static bool ensure_lib_dir()
{
    if (!g_lib_dir.empty())
        return true;
    if (!find_paths() || g_dir.empty())
        return false;
    g_lib_dir = g_dir + L"\\SharedLibrary";
    CreateDirectoryW(g_lib_dir.c_str(), nullptr);
    wchar_t buf[16] = {};
    GetPrivateProfileStringW(L"library", L"foreign", L"0", buf, 16, (g_dir + L"\\choices.ini").c_str());
    g_allow_foreign = buf[0] == L'1';
    return true;
}

static std::wstring png_path_for(uint64_t hash)
{
    return g_lib_dir + L"\\" + wide(png_filename(hash).c_str());
}

static std::wstring index_path() { return g_lib_dir + L"\\index.bin"; }

static bool write_file_atomic(const std::wstring& path, const uint8_t* data, size_t len)
{
    std::wstring tmp = path + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    BOOL ok = (len == 0) || (WriteFile(f, data, (DWORD)len, &written, nullptr) && written == (DWORD)len);
    FlushFileBuffers(f);
    CloseHandle(f);
    if (!ok) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

static bool read_file_bytes(const std::wstring& path, std::vector<uint8_t>& out, size_t max_bytes = kMaxPngBytes)
{
    out.clear();
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER sz{};
    bool ok = GetFileSizeEx(f, &sz) && sz.QuadPart > 0 && (uint64_t)sz.QuadPart <= max_bytes;
    if (ok) {
        out.resize((size_t)sz.QuadPart);
        DWORD got = 0;
        ok = ReadFile(f, out.data(), (DWORD)out.size(), &got, nullptr) && got == (DWORD)out.size();
        if (!ok)
            out.clear();
    }
    CloseHandle(f);
    return ok;
}

static void save_index_disk()
{
    if (!ensure_lib_dir())
        return;
    std::vector<uint8_t> bytes = encode_index(g_state.save_index());
    write_file_atomic(index_path(), bytes.data(), bytes.size());
}

// Verify embedded creation group via DecodePNG with a pinned stack IO::FileStream (BUG-9094).
static bool decode_png_group(const std::wstring& path, uint32_t& out_group)
{
    out_group = 0;
    if (!a_ThumbGet || !a_DecodePng || !a_ThumbMetaDtor || !a_FileStreamCtor || !a_FileStreamOpen || !a_FileStreamAddRef ||
        !a_FileStreamDtor)
        return false;
    void* imp = skg::cdecl_call<void*>(a_ThumbGet);
    if (!imp)
        return false;

    alignas(16) uint8_t stream[0x240] = {};
    skg::thiscall<void*>(a_FileStreamCtor, stream, path.c_str());
    // Pin the stack FileStream so DecodePNG's temporary reference never calls operator delete on stack memory.
    skg::thiscall<uint32_t>(a_FileStreamAddRef, stream);
    bool opened = skg::thiscall<bool>(a_FileStreamOpen, stream, 1u, 3u, 1u, 0u);
    if (!opened) {
        skg::thiscall<void>(a_FileStreamDtor, stream);
        return false;
    }

    // Initialize ThumbnailMetadata[0x90] strings at +0x28/+0x40/+0x50/+0x60 to static empty sentinel (cap - begin == 2).
    static uint16_t kEmptyW[2] = {0, 0};
    alignas(16) uint8_t meta[0x90] = {};
    constexpr size_t kStrOffs[4] = {0x28, 0x40, 0x50, 0x60};
    for (size_t off : kStrOffs) {
        auto* p = (uintptr_t*)(meta + off);
        p[0] = (uintptr_t)kEmptyW;
        p[1] = (uintptr_t)kEmptyW;
        p[2] = (uintptr_t)(kEmptyW + 1);
    }

    void* decoded_stream = nullptr;
    bool ok = skg::thiscall<bool>(a_DecodePng, imp, (void*)stream, (void*)meta, &decoded_stream);
    if (ok)
        out_group = *(uint32_t*)(meta + 0x08);

    // Exact native cleanup order: release decoded stream -> destroy metadata -> destroy pinned FileStream.
    if (decoded_stream && skg::readable(decoded_stream, 4))
        vcall<uint32_t>(decoded_stream, 0x08 / 4); // Release
    skg::thiscall<void>(a_ThumbMetaDtor, meta);
    skg::thiscall<void>(a_FileStreamDtor, stream);
    return ok && is_creation_group(out_group);
}

static void flush_pollination()
{
    if (!a_GetDbById)
        return;
    void* db = skg::cdecl_call<void*>(a_GetDbById, kPollinationDbArea);
    if (db && skg::readable(db, 4))
        vcall<bool>(db, 0x24 / 4); // Database::Flush
}

// Import one PNG card into Pollination.package via App::Thumbnail_cImportExport::ImportPNG (must run outside Present!).
static bool import_png_now(const std::wstring& path, AssetKey& out_key)
{
    out_key = {};
    if (g_test_fail_next) {
        g_test_fail_next = false;
        return false;
    }
    if (!a_ThumbGet || !a_ImportPng)
        return false;
    void* imp = skg::cdecl_call<void*>(a_ThumbGet);
    if (!imp)
        return false;
    uint32_t rk[3] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
    bool added = skg::thiscall<bool>(a_ImportPng, imp, path.c_str(), rk);
    AssetKey k{rk[0], rk[1], rk[2]};
    if (!k.valid())
        return false;
    if (added)
        flush_pollination();
    out_key = k;
    return true;
}

static void load_from_disk()
{
    if (g_loaded_disk || !ensure_lib_dir())
        return;
    g_loaded_disk = true;
    std::vector<uint8_t> idx_bytes;
    std::vector<IndexEntry> idx;
    if (read_file_bytes(index_path(), idx_bytes, 4u << 20) && decode_index(idx_bytes.data(), idx_bytes.size(), idx)) {
        g_state.load_index(idx);
    }

    // Reconcile with *.png files in SharedLibrary/
    std::set<uint64_t> on_disk;
    WIN32_FIND_DATAW fd{};
    std::wstring pat = g_lib_dir + L"\\*.png";
    HANDLE hf = FindFirstFileW(pat.c_str(), &fd);
    if (hf != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            uint64_t h = 0;
            if (!parse_png_filename(slotfs::narrow(fd.cFileName), h))
                continue;
            on_disk.insert(h);
            if (!g_state.entries.count(h)) {
                g_state.ingest(h, 0, true, "", "", false);
            }
        } while (FindNextFileW(hf, &fd));
        FindClose(hf);
    }

    // Drop index entries whose PNG file is missing and no longer known in RM; re-queue ones missing from RM.
    bool dirty = false;
    for (auto it = g_state.entries.begin(); it != g_state.entries.end();) {
        bool has_file = on_disk.count(it->first) != 0;
        if (it->second.is_imported()) {
            Key rk{it->second.imported.instance, it->second.imported.type, it->second.imported.group};
            if (!key_known(rk)) {
                if (has_file) {
                    g_state.mark_needs_import(it->first);
                    dirty = true;
                } else {
                    g_state.imported_keys.erase(it->second.imported);
                    it = g_state.entries.erase(it);
                    dirty = true;
                    continue;
                }
            }
        } else if (!has_file) {
            it = g_state.entries.erase(it);
            dirty = true;
            continue;
        }
        ++it;
    }
    if (dirty)
        save_index_disk();
    H->log(M, SK_INFO, "library: loaded %u entries (%u imported) from %s", (unsigned)g_state.entries.size(),
           (unsigned)g_state.imported_keys.size(), slotfs::narrow(g_lib_dir).c_str());
}

// Scan EditorSaves.package for local player's own creations (for dedup, My Creations protection, and offering to partners).
static void scan_own_creations(double now, bool force = false)
{
    if (!force && now - g_last_es_scan < 10.0)
        return;
    g_last_es_scan = now;
    if (!ensure_lib_dir())
        return;

    std::wstring es_path = g_dir.substr(0, g_dir.find_last_of(L"\\/")) + L"\\EditorSaves.package";
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(es_path.c_str(), GetFileExInfoStandard, &fad))
        return;
    ULONGLONG stamp = ((ULONGLONG)fad.ftLastWriteTime.dwHighDateTime << 32) | fad.ftLastWriteTime.dwLowDateTime;
    stamp ^= ((ULONGLONG)fad.nFileSizeLow << 13);
    if (!force && stamp == g_es_stamp && !g_own_pngs.empty())
        return;
    g_es_stamp = stamp;

    std::vector<uint8_t> raw;
    if (!read_file_bytes(es_path, raw, 128u << 20))
        return;
    dbpf::Package pk;
    if (!pk.load(raw))
        return;

    std::set<uint64_t> hashes;
    std::set<AssetKey> keys;
    std::map<uint64_t, OwnPng> found;

    // Collect keys that have a real BEM model in EditorSaves.package
    std::set<std::pair<uint32_t, uint32_t>> has_bem;
    for (const auto& e : pk.entries()) {
        if (e.key.type == kBem && is_creation_group(e.key.group)) {
            has_bem.insert({e.key.group, e.key.instance});
            // Record the creation key under its resource type for My Creations protection
            uint32_t rtype = 0;
            switch (e.key.group) {
                case 0x40616200u: rtype = 0x3D97A8E4u; break; // cll
                case 0x40626200u: rtype = 0x2B978C46u; break; // crt
                case 0x40636200u: rtype = 0x2399BE55u; break; // bld
                case 0x40646200u: rtype = 0x24682294u; break; // vcl
                case 0x40656200u: rtype = 0x476A98C7u; break; // ufo
                case 0x40666200u: rtype = 0x438F6347u; break; // flr
            }
            if (rtype)
                keys.insert({e.key.instance, rtype, e.key.group});
        }
    }

    for (const auto& e : pk.entries()) {
        if (e.key.type != kPng || !is_creation_group(e.key.group) || e.size > kMaxPngBytes)
            continue;
        if (!has_bem.count({e.key.group, e.key.instance}))
            continue;
        std::vector<uint8_t> png;
        if (!pk.read(e, png) || !is_valid_png(png.data(), png.size()))
            continue;
        uint64_t h = content_hash64(png.data(), png.size());
        hashes.insert(h);

        std::string author, cname;
        if (const dbpf::Entry* me = pk.find({e.key.instance, kMeta, e.key.group})) {
            std::vector<uint8_t> m;
            if (pk.read(*me, m))
                dbpf::metadata_names(m, author, cname);
        }
        if (cname.empty())
            cname = hex(e.key.instance);

        OwnPng op;
        op.hash = h;
        op.group = e.key.group;
        op.instance = e.key.instance;
        op.name = trim_label(cname);
        op.png = std::move(png);
        found[h] = std::move(op);
    }

    bool changed = found.size() != g_own_pngs.size();
    if (!changed) {
        for (const auto& [h, _] : found) {
            if (!g_own_pngs.count(h)) {
                changed = true;
                break;
            }
        }
    }
    g_own_pngs = std::move(found);
    g_state.set_own(std::move(hashes), std::move(keys));
    if (changed)
        g_have_sent_to.clear(); // re-announce new creations to connected peers
}

// ---- Sporepedia tab registration (outside Present) ----

static void sync_feed_lists_task(void*)
{
    g_feed_sync_queued = false;
    if (!a_PropMgrGet || !a_PropGetArrayKey || !a_PropSetArrayKey)
        return;
    void* pm = skg::cdecl_call<void*>(a_PropMgrGet);
    if (!pm || !skg::readable(pm, 4))
        return;
    bool want = g_lib_fix;
    int updated = 0;
    for (uint32_t root_id : kFeedListRoots) {
        void* pl = nullptr;
        bool got = vcall<bool>(pm, 0x2C / 4, root_id, kFeedListGroup, &pl);
        if (!got || !pl)
            continue;
        size_t cnt = 0;
        uint32_t* arr = nullptr;
        if (skg::cdecl_call<bool>(a_PropGetArrayKey, pl, kPropFeedListCategories, &cnt, &arr) && arr && cnt < 128) {
            std::vector<uint32_t> keys;
            keys.reserve((cnt + 1) * 3);
            bool present = false;
            for (size_t i = 0; i < cnt; ++i) {
                uint32_t inst = arr[i * 3 + 0];
                uint32_t typ = arr[i * 3 + 1];
                uint32_t grp = arr[i * 3 + 2];
                if (inst == kLibCatId) {
                    present = true;
                    if (!want)
                        continue;
                }
                keys.push_back(inst);
                keys.push_back(typ);
                keys.push_back(grp);
            }
            if (want && !present) {
                keys.push_back(kLibCatId);
                keys.push_back(0u);
                keys.push_back(0u);
                skg::cdecl_call<void>(a_PropSetArrayKey, pl, kPropFeedListCategories, (size_t)(keys.size() / 3),
                                      keys.data());
                updated++;
            } else if (!want && present) {
                skg::cdecl_call<void>(a_PropSetArrayKey, pl, kPropFeedListCategories, (size_t)(keys.size() / 3),
                                      keys.data());
                updated++;
            }
        }
        vcall<uint32_t>(pl, 0x04 / 4); // IPropertyList::Release
    }
    g_tab_state = want;
    g_tab_dirty = false;
    if (updated > 0)
        H->log(M, SK_INFO, "library: Sporepedia Shared Creations tab %s (%d feed list(s))",
               want ? "registered" : "removed", updated);
}

// ---- FeedItem_GetAssetKeys hook (0x00668D60) ----

static void SK_THISCALL h_FeedGetKeys(void* feed, void* outVec)
{
    if (!feed || !outVec || !skg::readable((uint8_t*)feed + 0x178, 0x10)) {
        o_FeedGetKeys(feed, outVec);
        return;
    }
    uint32_t item_id = *(uint32_t*)((uint8_t*)feed + 0x17C);

    if (g_lib_fix && item_id == kLibItemId) {
        g_feed_lib_calls++;
        auto* vec = (uintptr_t*)outVec; // begin +0, end +4, cap +8 (stride 16)
        vec[1] = vec[0];
        std::vector<AssetKey> keys = g_state.feed_keys();
        if (!keys.empty() && a_VecReserve) {
            skg::thiscall<void>(a_VecReserve, outVec, keys.size());
            auto* dst = (uint32_t*)vec[0];
            if (dst) {
                for (size_t i = 0; i < keys.size(); ++i) {
                    dst[i * 4 + 0] = keys[i].instance;
                    dst[i * 4 + 1] = keys[i].type;
                    dst[i * 4 + 2] = keys[i].group;
                    dst[i * 4 + 3] = 0u;
                }
                vec[1] = vec[0] + keys.size() * 16u;
            }
        }
        return;
    }

    o_FeedGetKeys(feed, outVec);

    if (g_lib_fix && g_lib_my_filter_fix && item_id == kMyCreationsItemId && skg::readable(outVec, 12)) {
        auto* vec = (uintptr_t*)outVec;
        uintptr_t beg = vec[0], end = vec[1];
        if (beg && end > beg && ((end - beg) & 15u) == 0) {
            size_t n = (end - beg) / 16u;
            auto* items = (uint32_t*)beg;
            size_t w = 0;
            for (size_t i = 0; i < n; ++i) {
                AssetKey k{items[i * 4 + 0], items[i * 4 + 1], items[i * 4 + 2]};
                if (g_state.should_hide_from_my_creations(k)) {
                    g_feed_my_filtered++;
                    continue;
                }
                if (w != i)
                    std::memcpy(items + w * 4, items + i * 4, 16);
                w++;
            }
            vec[1] = beg + w * 16u;
        }
    }
}

// ---- Outside-frame PNG import worker ----

static void import_one_task(void*)
{
    g_import_queued = false;
    if (!g_lib_fix || !ensure_lib_dir())
        return;
    double now = H ? H->time_seconds() : 0.0;
    std::vector<uint64_t> due = g_state.due_imports(now);
    if (due.empty())
        return;

    uint64_t h = due.front();
    auto it = g_state.entries.find(h);
    if (it == g_state.entries.end())
        return;
    std::wstring path = png_path_for(h);

    // Optional group check on provisional Cell->Creature PNGs (BUG-9095): if source_group is known, verify decoded group
    if (it->second.source_group != 0) {
        uint32_t dec_group = 0;
        if (decode_png_group(path, dec_group) && dec_group != it->second.source_group) {
            H->log(M, SK_INFO, "library: skipping provisional PNG %016llx (group 0x%08X != 0x%08X)",
                   (unsigned long long)h, dec_group, it->second.source_group);
            it->second.gave_up = true;
            save_index_disk();
            return;
        }
    }

    AssetKey imported{};
    if (import_png_now(path, imported)) {
        g_state.mark_imported(h, imported);
        g_import_ok++;
        save_index_disk();
        H->log(M, SK_INFO, "library: imported %016llx '%s' as %08X!%08X.%08X", (unsigned long long)h,
               it->second.name.c_str(), imported.group, imported.instance, imported.type);
    } else {
        g_state.record_import_failure(h, now, g_lib_retry_fix);
        g_import_fail++;
        H->log(M, SK_WARN, "library: ImportPNG failed for %016llx (attempt %d/%d, retry=%d)", (unsigned long long)h,
               it->second.attempts, kMaxRetriesPerRun, (int)g_lib_retry_fix);
    }
}

// ---- Wire sending helpers (MSG_CREATION kinds 13 & 14) ----

static void send_lib_payload(int to, uint8_t kind, uint32_t id, const std::vector<uint8_t>& payload)
{
    if (!C.sess || !C.sess->connected() || payload.empty())
        return;
    uint8_t owner = C.sess->my_id();
    constexpr size_t kChunk = 900;
    uint32_t total = (uint32_t)payload.size();
    for (uint32_t off = 0; off < total; off += kChunk) {
        uint16_t n = (uint16_t)std::min<size_t>(kChunk, total - off);
        net::Writer w;
        w.u8(owner);
        w.u8(kind);
        w.u32(id);
        w.u32(total);
        w.u32(off);
        w.u16(n);
        w.raw(payload.data() + off, n);
        if (to < 0)
            C.sess->broadcast_reliable(net::MSG_CREATION, w);
        else
            C.sess->send_reliable_to((uint8_t)to, net::MSG_CREATION, w);
    }
}

static ListMsg build_my_have()
{
    ListMsg m;
    m.subkind = kListHave;
    for (const auto& [h, op] : g_own_pngs) {
        if (m.have.size() >= kMaxListItems)
            break;
        m.have.push_back({h, op.group, 1u}); // origin_own = 1
    }
    // Also offer previously received library creations flagged as foreign (origin_own = 0) so receivers who opt in
    // can pull them, while default receivers skip them.
    for (const auto& [h, e] : g_state.entries) {
        if (m.have.size() >= kMaxListItems)
            break;
        if ((e.flags & kFlagNoShare) != 0 || !is_creation_group(e.source_group))
            continue;
        if (g_own_pngs.count(h))
            continue;
        m.have.push_back({h, e.source_group, 0u}); // origin_own = 0 (foreign)
    }
    return m;
}

static void announce_have_to(uint8_t peer)
{
    if (!g_lib_fix || !C.sess || !C.sess->connected())
        return;
    if (is_host() ? !peer_reads_lib(peer) : !host_reads_lib())
        return;
    ListMsg m = build_my_have();
    if (m.have.empty())
        return;
    std::vector<uint8_t> wire = encode_list(m);
    send_lib_payload(peer, library::kKindLibList, fnv(wire.data(), wire.size()), wire);
    g_have_sent_to.insert(peer);
}

static bool load_png_to_send(uint64_t hash, PngMsg& out)
{
    if (auto it = g_own_pngs.find(hash); it != g_own_pngs.end()) {
        out.hash = it->second.hash;
        out.group = it->second.group;
        out.flags = 1u; // origin_own
        out.sender = C.sess ? C.sess->name_of(C.sess->my_id()) : "";
        out.name = it->second.name;
        out.png = it->second.png;
        return true;
    }
    if (auto it = g_state.entries.find(hash); it != g_state.entries.end()) {
        if ((it->second.flags & kFlagNoShare) != 0 || !is_creation_group(it->second.source_group))
            return false;
        std::vector<uint8_t> png;
        if (!read_file_bytes(png_path_for(hash), png) || !is_valid_png(png.data(), png.size()))
            return false;
        if (content_hash64(png.data(), png.size()) != hash)
            return false;
        out.hash = hash;
        out.group = it->second.source_group;
        out.flags = 0u; // foreign
        out.sender = it->second.sender;
        out.name = it->second.name;
        out.png = std::move(png);
        return true;
    }
    return false;
}

static bool store_received_png(uint64_t hash, uint32_t group, bool origin_own, const std::string& sender,
                               const std::string& name, const uint8_t* png_data, size_t png_len,
                               bool skip_if_own = true)
{
    if (!g_lib_fix || !ensure_lib_dir() || !is_valid_png(png_data, png_len))
        return false;
    if (hash == 0)
        hash = content_hash64(png_data, png_len);
    bool added = g_state.ingest(hash, group, origin_own, sender, name, skip_if_own);
    if (!added)
        return false;
    if (!write_file_atomic(png_path_for(hash), png_data, png_len)) {
        g_state.entries.erase(hash);
        return false;
    }
    save_index_disk();
    return true;
}

// ---- Public hooks called from creations.cpp ----

void library_init()
{
    a_ThumbGet = H->sym("App::Thumbnail_cImportExport::Get");
    a_ImportPng = H->sym("App::Thumbnail_cImportExport::ImportPNG");
    a_DecodePng = H->sym("App::Thumbnail_cImportExport::DecodePNG");
    a_ThumbMetaDtor = H->sym("SporeCoop::ThumbnailMetadata_Dtor");
    a_FileStreamCtor = H->sym("SporeCoop::FileStream_PathCtor");
    a_FileStreamOpen = H->sym("IO::FileStream::Open");
    a_FileStreamAddRef = H->sym("IO::FileStream::AddRef");
    a_FileStreamDtor = H->sym("SporeCoop::FileStream_Dtor");
    a_PropMgrGet = H->sym("App::IPropManager::Get");
    a_PropGetArrayKey = H->sym("App::Property::GetArrayKey");
    a_PropSetArrayKey = H->sym("SporeCoop::PropertyList_SetArrayKey");
    a_GetDbById = H->sym("Resource::GetDatabaseByID");
    a_VecReserve = H->sym("SporeCoop::AssetKeyVector_Reserve");

    if (uintptr_t a = H->sym("SporeCoop::FeedItem_GetAssetKeys"))
        H->hook_create(M, (void*)a, (void*)h_FeedGetKeys, (void**)&o_FeedGetKeys);

    g_tab_dirty = true;
    H->log(M, SK_INFO, "library: ready (import=%u decode=%u feed_hook=%u)", (unsigned)(a_ImportPng != 0),
           (unsigned)(a_DecodePng != 0), (unsigned)(o_FeedGetKeys != nullptr));
}

void library_shutdown()
{
    g_send_queue.clear();
    g_peer_have.clear();
    g_have_sent_to.clear();
}

bool library_kind_on(uint8_t kind)
{
    return g_lib_fix && (kind == library::kKindLibList || kind == library::kKindLibPng);
}

void library_tick(double now)
{
    if (!H)
        return;

    // Keep Sporepedia feed lists synchronized with g_lib_fix toggle
    if ((g_tab_dirty || g_tab_state != g_lib_fix) && !g_feed_sync_queued) {
        g_feed_sync_queued = true;
        H->run_outside_frame(sync_feed_lists_task, nullptr);
    }

    if (!g_lib_fix)
        return;

    load_from_disk();
    scan_own_creations(now);

    // Queue at most one ImportPNG outside Present at a time
    if (!g_import_queued && !g_state.due_imports(now).empty()) {
        g_import_queued = true;
        H->run_outside_frame(import_one_task, nullptr);
    }

    // Session background sync (creations 1.3 peers only)
    if (!C.sess || !C.sess->connected())
        return;

    if (is_host()) {
        for (const auto& p : C.sess->players()) {
            if (p.id != 0 && !g_have_sent_to.count(p.id) && peer_reads_lib(p.id))
                announce_have_to(p.id);
        }
    } else if (!g_have_sent_to.count(0) && host_reads_lib()) {
        announce_have_to(0);
    }

    // Paced PNG sending (1 PNG every kSendPaceSec)
    if (!g_send_queue.empty() && now - g_last_send_at >= kSendPaceSec) {
        PeerWant pw = g_send_queue.front();
        g_send_queue.pop_front();
        PngMsg pm;
        if (load_png_to_send(pw.hash, pm)) {
            std::vector<uint8_t> wire = encode_png_msg(pm);
            send_lib_payload(pw.peer, library::kKindLibPng, (uint32_t)(pm.hash & 0xFFFFFFFFu), wire);
            g_last_send_at = now;
            g_sent_pngs++;
        }
    }
}

void library_message(uint8_t kind, uint8_t from, uint8_t owner, const std::vector<uint8_t>& data)
{
    if (!g_lib_fix)
        return;
    load_from_disk();

    if (kind == library::kKindLibList) {
        ListMsg m;
        if (!decode_list(data.data(), data.size(), m))
            return;
        if (m.subkind == kListHave) {
            ListMsg want;
            want.subkind = kListWant;
            for (const auto& it : m.have) {
                g_peer_have[from].insert(it.hash);
                bool origin_own = (it.flags & 1u) != 0;
                if (g_state.should_want(it.hash, origin_own, g_allow_foreign) && want.want.size() < kMaxListItems)
                    want.want.push_back(it.hash);
            }
            if (!want.want.empty()) {
                std::vector<uint8_t> w = encode_list(want);
                send_lib_payload(from, library::kKindLibList, fnv(w.data(), w.size()), w);
            }
            // Host relays a guest's Have announcement to other creations 1.3 guests (marking relayed entries foreign=0
            // unless originally owned by the sender)
            if (is_host() && from != 0) {
                for (const auto& p : C.sess->players()) {
                    if (p.id != 0 && p.id != from && peer_reads_lib(p.id))
                        send_lib_payload(p.id, library::kKindLibList, fnv(data.data(), data.size()), data);
                }
            }
            return;
        }
        if (m.subkind == kListWant) {
            for (uint64_t h : m.want) {
                if (g_send_queue.size() >= 64)
                    break;
                if (g_own_pngs.count(h) || g_state.entries.count(h)) {
                    bool dup = false;
                    for (const auto& q : g_send_queue)
                        if (q.peer == from && q.hash == h) {
                            dup = true;
                            break;
                        }
                    if (!dup)
                        g_send_queue.push_back({from, h});
                }
            }
            return;
        }
        return;
    }

    if (kind == library::kKindLibPng) {
        PngMsg pm;
        if (!decode_png_msg(data.data(), data.size(), pm))
            return;
        bool origin_own = (pm.flags & 1u) != 0;
        if (!g_state.should_want(pm.hash, origin_own, g_allow_foreign))
            return;
        std::string sender = pm.sender.empty() && C.name_of ? C.name_of(owner) : pm.sender;
        if (store_received_png(pm.hash, pm.group, origin_own, sender, pm.name, pm.png.data(), pm.png.size(), true)) {
            g_recv_pngs++;
            H->log(M, SK_INFO, "library: received PNG %016llx '%s' from %s (%u bytes)", (unsigned long long)pm.hash,
                   pm.name.c_str(), sender.c_str(), (unsigned)pm.png.size());
        }
    }
}

void library_join(uint8_t id)
{
    g_have_sent_to.erase(id);
    g_peer_have.erase(id);
}

void library_leave(uint8_t id)
{
    g_have_sent_to.erase(id);
    g_peer_have.erase(id);
    std::erase_if(g_send_queue, [id](const PeerWant& pw) { return pw.peer == id; });
}

void library_end()
{
    // Persistent library entries stay on disk and in Sporepedia! Only transient session sync queues clear.
    g_send_queue.clear();
    g_peer_have.clear();
    g_have_sent_to.clear();
}

void library_on_partner_records(const std::string& sender, const std::string& creation_name, uint32_t group,
                                const std::vector<dbpf::Record>& recs, bool origin_own)
{
    if (!g_lib_fix || !is_creation_group(group))
        return;
    load_from_disk();
    for (const auto& r : recs) {
        if (r.key.type != kPng || r.key.group != group)
            continue;
        if (!is_valid_png(r.data.data(), r.data.size()))
            continue;
        uint64_t h = content_hash64(r.data.data(), r.data.size());
        if (store_received_png(h, group, origin_own, sender, creation_name, r.data.data(), r.data.size(), true)) {
            g_recv_pngs++;
            H->log(M, SK_INFO, "library: captured partner creation %016llx '%s' from %s", (unsigned long long)h,
                   creation_name.c_str(), sender.c_str());
        }
        break;
    }
}

std::string library_status()
{
    size_t pending = 0, gave_up = 0;
    for (const auto& [_, e] : g_state.entries) {
        if (!e.is_imported()) {
            if (e.gave_up)
                gave_up++;
            else
                pending++;
        }
    }
    char buf[384];
    snprintf(buf, sizeof(buf),
             "library: fix=%d my_filter=%d retry=%d foreign=%d tab=%d entries=%u imported=%u pending=%u gave_up=%u "
             "own_pngs=%u sent=%u recv=%u import_ok=%u import_fail=%u feed_lib=%u my_filtered=%u",
             (int)g_lib_fix, (int)g_lib_my_filter_fix, (int)g_lib_retry_fix, (int)g_allow_foreign, (int)g_tab_state,
             (unsigned)g_state.entries.size(), (unsigned)g_state.imported_keys.size(), (unsigned)pending,
             (unsigned)gave_up, (unsigned)g_own_pngs.size(), g_sent_pngs, g_recv_pngs, g_import_ok, g_import_fail,
             g_feed_lib_calls, g_feed_my_filtered);
    return buf;
}

std::string library_debug(const std::vector<std::string>& args)
{
    load_from_disk();
    if (!args.empty() && args[0] == "list") {
        std::string out = library_status() + "\n";
        char line[256];
        for (const auto& [h, e] : g_state.entries) {
            snprintf(line, sizeof(line),
                     "  %016llx group=%08X imp=%d key=%08X!%08X.%08X att=%d gave_up=%d foreign=%d sender='%s' name='%s'\n",
                     (unsigned long long)h, e.source_group, (int)e.is_imported(), e.imported.group, e.imported.instance,
                     e.imported.type, e.attempts, (int)e.gave_up, (int)e.is_foreign(), e.sender.c_str(),
                     e.name.c_str());
            out += line;
        }
        return out;
    }

    if (!args.empty() && args[0] == "foreign" && args.size() >= 2) {
        g_allow_foreign = (args[1] == "on" || args[1] == "1");
        if (ensure_lib_dir())
            WritePrivateProfileStringW(L"library", L"foreign", g_allow_foreign ? L"1" : L"0",
                                       (g_dir + L"\\choices.ini").c_str());
        return std::string("library: foreign=") + (g_allow_foreign ? "on" : "off");
    }

    if (!args.empty() && args[0] == "scan") {
        scan_own_creations(H ? H->time_seconds() : 0.0, true);
        g_have_sent_to.clear();
        return library_status();
    }

    if (!args.empty() && args[0] == "retry") {
        g_state.reset_retry_timers();
        if (!g_import_queued && H) {
            g_import_queued = true;
            H->run_outside_frame(import_one_task, nullptr);
        }
        return library_status();
    }

    if (!args.empty() && args[0] == "failnext") {
        g_test_fail_next = true;
        return "library: next ImportPNG will fail once (test)";
    }

    if (!args.empty() && args[0] == "ingest" && args.size() >= 2) {
        std::wstring p = wide(args[1].c_str());
        if (args[1].find(':') == std::string::npos && args[1].find('\\') == std::string::npos &&
            args[1].find('/') == std::string::npos && ensure_lib_dir())
            p = g_dir + L"\\" + p;
        std::vector<uint8_t> bytes;
        if (!read_file_bytes(p, bytes) || !is_valid_png(bytes.data(), bytes.size()))
            return "library: invalid or unreadable PNG";
        uint64_t h = content_hash64(bytes.data(), bytes.size());
        std::string nm = args.size() >= 3 ? args[2] : "test";
        bool added = store_received_png(h, 0, true, "test", nm, bytes.data(), bytes.size(), false);
        char buf[128];
        snprintf(buf, sizeof(buf), "library: ingest hash=%016llx added=%d", (unsigned long long)h, (int)added);
        return buf;
    }

    if (!args.empty() && args[0] == "feed" && args.size() >= 2) {
        // Inspect live Sporepedia feed keys through our real FeedItem_GetAssetKeys hook!
        if (!o_FeedGetKeys)
            return "library: FeedItem_GetAssetKeys hook not installed";
        bool is_my = args[1] == "my";
        bool is_lib = args[1] == "lib";
        if (!is_my && !is_lib)
            return "library: usage: coop.library feed <lib|my>";

        // For 'lib', invoke our hooked FeedItem_GetAssetKeys with a minimal FeedItem stub holding +0x17C = kLibItemId.
        // For 'my', report both the real My Creations filter predicate over every imported library key and the
        // AssetBrowserFeedLists presence of our Shared Creations category.
        if (is_lib) {
            alignas(16) uint8_t fake_feed[0x190] = {};
            *(uint32_t*)(fake_feed + 0x178) = 0x48A6F111u;
            *(uint32_t*)(fake_feed + 0x17C) = kLibItemId;
            uintptr_t vec[3] = {0, 0, 0};
            h_FeedGetKeys(fake_feed, vec);
            size_t count = (vec[0] && vec[1] >= vec[0]) ? (vec[1] - vec[0]) / 16u : 0;
            std::string out;
            char hdr[128];
            snprintf(hdr, sizeof(hdr), "library: feed lib count=%u tab=%d\n", (unsigned)count, (int)g_tab_state);
            out += hdr;
            auto* items = (uint32_t*)vec[0];
            for (size_t i = 0; i < count; ++i) {
                Key rk{items[i * 4 + 0], items[i * 4 + 1], items[i * 4 + 2]};
                bool in_rm = key_exists(rk);
                char l[112];
                snprintf(l, sizeof(l), "  %08X!%08X.%08X in_rm=%d\n", rk.group, rk.instance, rk.type, (int)in_rm);
                out += l;
            }
            if (vec[0]) {
                uintptr_t a_free = H->sym("GeneralAllocator::delete_");
                if (a_free)
                    skg::cdecl_call<void>(a_free, (void*)vec[0]);
            }
            return out;
        }
        // Check AssetBrowserFeedLists default root directly in game memory
        bool in_default = false;
        if (a_PropMgrGet && a_PropGetArrayKey) {
            void* pm = skg::cdecl_call<void*>(a_PropMgrGet);
            void* pl = nullptr;
            if (pm && vcall<bool>(pm, 0x2C / 4, kFeedListRoots[0], kFeedListGroup, &pl) && pl) {
                size_t cnt = 0;
                uint32_t* arr = nullptr;
                if (skg::cdecl_call<bool>(a_PropGetArrayKey, pl, kPropFeedListCategories, &cnt, &arr) && arr) {
                    for (size_t i = 0; i < cnt; ++i)
                        if (arr[i * 3] == kLibCatId)
                            in_default = true;
                }
                vcall<uint32_t>(pl, 0x04 / 4);
            }
        }
        size_t hidden = 0, leaked = 0;
        for (const auto& k : g_state.imported_keys) {
            if (g_lib_fix && g_lib_my_filter_fix && g_state.should_hide_from_my_creations(k))
                hidden++;
            else
                leaked++;
        }
        char buf[192];
        snprintf(buf, sizeof(buf), "library: feed my hidden=%u leaked=%u tab_in_default=%d", (unsigned)hidden,
                 (unsigned)leaked, (int)in_default);
        return buf;
    }

    if (!args.empty() && args[0] == "clear") {
        if (ensure_lib_dir()) {
            for (const auto& [h, _] : g_state.entries)
                DeleteFileW(png_path_for(h).c_str());
            DeleteFileW(index_path().c_str());
        }
        g_state.entries.clear();
        g_state.imported_keys.clear();
        return "library: cleared";
    }

    return library_status();
}

} // namespace creations
