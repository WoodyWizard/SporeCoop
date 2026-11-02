// Screenshots for report archives (docs/02-modkit.md#modkit-report-shots): when a player marks a
// problem (a mod calls SkHost::report_shot), the next frame drawn through Present, with the overlay,
// is scaled on the GPU to at most kMaxW pixels wide, read back and written as a JPEG by a worker
// thread to mods\logs\shots (shots-i<N> for instance N > 1). The newest kKeep stay; every report
// archive takes them under shots/ and its report lists them with the line they belong to (the MARK).
//
// Never waits: the request returns at once, the capture is one StretchRect + one GetRenderTargetData
// of the small surface in the Present hook (the device's own thread, the frame already drawn), the
// JPEG is made off the main thread. Any failure is a log line; the report goes on without the shot.
// A crash or hang report never takes one: it is written outside the frame (another thread, or the
// main thread stopped somewhere), where the graphics device is not safe to use.
#include "core.h"
#include "jpeg.h"

#include <d3d9.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace sk {

static constexpr int kMaxW = 1280;
static constexpr size_t kKeep = 5;                  // screenshots kept (and put into an archive)
static constexpr size_t kMaxBytes = 450u << 10;     // a JPEG over this is made again at a lower quality
static constexpr double kMinGap = 2.0, kExpire = 3.0; // s between two shots; a request without a frame

struct Pending {
    bool on = false;
    std::wstring path;
    std::string name, caption;
    double t = 0;
};
static Pending g_pend;                // main thread only (the request and the Present hook)
static std::atomic<int> g_busy{0};    // a request waits for its frame or its JPEG is being written
static std::atomic<int> g_last_ok{1}; // the last request was written (report_shot_pending)
static double g_last_req = -1e9;

struct Job {
    std::wstring path;
    std::string name, caption;
    std::vector<uint8_t> bgrx; // w * h * 4
    int w = 0, h = 0, src_w = 0, src_h = 0;
    double frame_ms = 0;
    bool gpu_scaled = false;
};

static std::wstring shots_dir()
{
    return mods_dirw() + L"\\logs\\shots" + (instance_id() > 1 ? L"-i" + std::to_wstring(instance_id()) : L"");
}

struct ShotFile {
    std::wstring name;
    FILETIME t;
};

// the JPEGs in the folder, oldest first (by the time they were written)
static std::vector<ShotFile> shot_files()
{
    std::vector<ShotFile> v;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((shots_dir() + L"\\*.jpg").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return v;
    do
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            v.push_back({fd.cFileName, fd.ftLastWriteTime});
    while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(v.begin(), v.end(), [](const ShotFile& a, const ShotFile& b) {
        int c = CompareFileTime(&a.t, &b.t);
        return c ? c < 0 : a.name < b.name;
    });
    return v;
}

static void remove_shot(const std::wstring& name)
{
    std::wstring d = shots_dir() + L"\\";
    DeleteFileW((d + name).c_str());
    DeleteFileW((d + name + L".txt").c_str());
}

static void prune()
{
    auto v = shot_files();
    for (size_t i = 0; i + kKeep < v.size(); i++)
        remove_shot(v[i].name);
}

void report_shot_boot()
{
    // a JPEG the last run did not finish (the game ended while it was written)
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((shots_dir() + L"\\*.tmp").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do
            DeleteFileW((shots_dir() + L"\\" + fd.cFileName).c_str());
        while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    prune();
}

static std::string read_small(const std::wstring& path)
{
    std::vector<uint8_t> d;
    if (!read_file_tail(path, 4096, d))
        return "";
    std::string s(d.begin(), d.end());
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
        s.pop_back();
    return s;
}

std::vector<std::pair<std::wstring, std::string>> report_shot_files()
{
    std::vector<std::pair<std::wstring, std::string>> r;
    auto v = shot_files();
    size_t first = v.size() > kKeep ? v.size() - kKeep : 0;
    for (size_t i = first; i < v.size(); i++)
        r.push_back({shots_dir() + L"\\" + v[i].name, "shots/" + narrow(v[i].name)});
    return r;
}

std::string report_shots_text(bool crash)
{
    std::string s = "== Screenshots (shots/ in the archive: the game's screen when a problem was marked)\n";
    auto v = report_shot_files();
    for (auto& [full, name] : v) {
        std::string info = read_small(full + L".txt"); // "<w>x<h> <KB> KB | <the line it belongs to>"
        s += "  " + name + (info.empty() ? "" : "  " + info) + "\n";
    }
    if (v.empty())
        s += "  (none)\n";
    if (crash)
        s += "No screenshot of this moment: a crash or hang report is written outside the game's frame, where the\n"
             "graphics device is not safe to use (the screenshots above are of the marks).\n";
    return s + "\n";
}

int report_shot_clear()
{
    int n = 0;
    for (auto& f : shot_files()) {
        remove_shot(f.name);
        n++;
    }
    if (n)
        SKLOG(SK_INFO, "report shot: %d screenshots removed (switched off)", n);
    return n;
}

int report_shot_pending() { return g_busy.load() ? 1 : g_last_ok.load() ? 0 : -1; }

static int say(char* out, int cap, const std::string& s)
{
    if (out && cap > 0)
        snprintf(out, (size_t)cap, "%s", s.c_str());
    return 0;
}

int report_shot_request(const char* tag, const char* caption, char* out, int cap)
{
    if (!caption) {
        int n = report_shot_clear();
        say(out, cap, std::to_string(n) + " removed");
        return 0;
    }
    std::string why;
    double now = now_seconds();
    if (GetCurrentThreadId() != main_thread_id())
        why = "not on the main thread";
    else if (!d3d_device())
        why = "no graphics device yet";
    else if (g_busy.load())
        why = "the previous screenshot is not written yet";
    else if (now - g_last_req < kMinGap)
        why = "another screenshot was taken " + std::to_string((int)((now - g_last_req) * 1000)) + " ms ago";
    if (!why.empty()) {
        SKLOG(SK_INFO, "report shot: not taken (%s): %s", why.c_str(), caption);
        return say(out, cap, why);
    }
    std::string t;
    for (const char* p = tag && *tag ? tag : "shot"; *p && t.size() < 16; p++)
        if (isalnum((unsigned char)*p) || *p == '-' || *p == '_')
            t += (char)tolower((unsigned char)*p);
    if (t.empty())
        t = "shot";
    SYSTEMTIME st;
    GetLocalTime(&st);
    char b[96];
    snprintf(b, sizeof b, "%s-%04d%02d%02d-%02d%02d%02d", t.c_str(), st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
             st.wSecond);
    std::string base = b;
    double sess;
    if (log_session_time(sess) && sess >= 0)
        base += "-s" + std::to_string((long long)std::floor(sess));
    CreateDirectoryW((mods_dirw() + L"\\logs").c_str(), nullptr);
    CreateDirectoryW(shots_dir().c_str(), nullptr);
    std::string name = base + ".jpg";
    for (int i = 2; i < 100 && GetFileAttributesW((shots_dir() + L"\\" + widen(name)).c_str()) != INVALID_FILE_ATTRIBUTES; i++)
        name = base + "-" + std::to_string(i) + ".jpg";
    g_pend.on = true;
    g_pend.path = shots_dir() + L"\\" + widen(name);
    g_pend.name = name;
    g_pend.caption = caption;
    g_pend.t = now;
    g_last_req = now;
    g_last_ok = 0;
    g_busy = 1;
    say(out, cap, "shots/" + name);
    return 1;
}

static void fail(const std::string& name, const std::string& why)
{
    SKLOG(SK_WARN, "report shot: shots/%s not taken (%s); the report goes on without it", name.c_str(), why.c_str());
    g_busy = 0;
}

// frame_common, every frame of either source: a request no Present picked up
void report_shot_tick()
{
    if (g_pend.on && now_seconds() - g_pend.t > kExpire) {
        g_pend.on = false;
        fail(g_pend.name, "no frame was drawn through Present for 3 s: the game is not drawing, or another overlay "
                          "took Present over (frames from the game loop)");
    }
}

static bool write_file(const std::wstring& path, const std::vector<uint8_t>& d)
{
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    DWORD w = 0;
    bool ok = WriteFile(f, d.data(), (DWORD)d.size(), &w, nullptr) && w == d.size();
    CloseHandle(f);
    return ok;
}

static DWORD WINAPI shot_worker(void* p)
{
    Job* j = (Job*)p;
    double t0 = now_seconds();
    try {
        std::vector<uint8_t> rgb((size_t)j->w * j->h * 3);
        for (size_t i = 0, n = (size_t)j->w * j->h; i < n; i++) {
            rgb[i * 3 + 0] = j->bgrx[i * 4 + 2];
            rgb[i * 3 + 1] = j->bgrx[i * 4 + 1];
            rgb[i * 3 + 2] = j->bgrx[i * 4 + 0];
        }
        j->bgrx.clear();
        j->bgrx.shrink_to_fit();
        int w = j->w, h = j->h;
        if (w > kMaxW) { // the GPU could not scale it
            std::vector<uint8_t> small;
            image_shrink(rgb.data(), j->w, j->h, kMaxW, small, w, h);
            rgb.swap(small);
        }
        std::vector<uint8_t> jpg;
        int q = 0;
        for (int quality : {80, 65, 50, 35}) {
            q = quality;
            if (!jpeg_encode(rgb.data(), w, h, q, jpg, "SporeCoop report screenshot: " + j->caption))
                throw std::runtime_error("the JPEG encoder refused the image");
            if (jpg.size() <= kMaxBytes)
                break;
        }
        std::wstring tmp = j->path + L".tmp";
        if (!write_file(tmp, jpg) || !MoveFileExW(tmp.c_str(), j->path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            DeleteFileW(tmp.c_str());
            throw std::runtime_error("cannot write the file");
        }
        char info[64];
        snprintf(info, sizeof info, "%dx%d %u KB", w, h, (unsigned)((jpg.size() + 512) >> 10));
        std::string side = std::string(info) + " | " + j->caption + "\n";
        write_file(j->path + L".txt", std::vector<uint8_t>(side.begin(), side.end()));
        prune();
        SKLOG(SK_INFO, "report shot: shots/%s %s q%d of %dx%d (on the frame %.1f ms, %s; JPEG in %.0f ms on a worker thread) for: %s",
              j->name.c_str(), info, q, j->src_w, j->src_h, j->frame_ms, j->gpu_scaled ? "scaled on the GPU" : "read in full",
              (now_seconds() - t0) * 1000, j->caption.c_str());
        g_last_ok = 1;
        g_busy = 0;
    } catch (const std::exception& e) {
        fail(j->name, e.what());
    } catch (...) {
        fail(j->name, "out of memory");
    }
    delete j;
    return 0;
}

// the Present hook, after the overlay pass: the frame the player is about to see
void report_shot_on_present(void* devp)
{
    if (!g_pend.on)
        return;
    Pending p = std::move(g_pend);
    g_pend = Pending();
    auto* dev = (IDirect3DDevice9*)devp;
    double t0 = now_seconds();
    std::string err;
    IDirect3DSurface9 *bb = nullptr, *res = nullptr, *rt = nullptr, *sys = nullptr;
    Job* j = nullptr;
    try {
        j = new Job;
        do {
            HRESULT coop = dev->TestCooperativeLevel();
            if (coop != D3D_OK) {
                err = "the graphics device is lost (a fullscreen game in the background?)";
                break;
            }
            if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) { err = "GetBackBuffer"; break; }
            D3DSURFACE_DESC d{};
            bb->GetDesc(&d);
            j->src_w = (int)d.Width;
            j->src_h = (int)d.Height;
            UINT ow = d.Width, oh = d.Height;
            if (ow > (UINT)kMaxW) {
                oh = (UINT)std::max<int64_t>(1, (int64_t)d.Height * kMaxW / d.Width);
                ow = kMaxW;
            }
            bool plain = d.Format == D3DFMT_X8R8G8B8 || d.Format == D3DFMT_A8R8G8B8;
            IDirect3DSurface9* src = bb;
            UINT sw = d.Width, sh = d.Height;
            if (ow != d.Width || d.MultiSampleType != D3DMULTISAMPLE_NONE || !plain) {
                // on the GPU: resolve a multisampled back buffer first (it cannot be stretched), then
                // scale into a small X8R8G8B8 target (any format converts)
                IDirect3DSurface9* from = bb;
                if (d.MultiSampleType != D3DMULTISAMPLE_NONE &&
                    SUCCEEDED(dev->CreateRenderTarget(d.Width, d.Height, d.Format, D3DMULTISAMPLE_NONE, 0, FALSE, &res, nullptr)) &&
                    SUCCEEDED(dev->StretchRect(bb, nullptr, res, nullptr, D3DTEXF_NONE)))
                    from = res;
                D3DCAPS9 caps{};
                dev->GetDeviceCaps(&caps);
                D3DTEXTUREFILTERTYPE f = (caps.StretchRectFilterCaps & D3DPTFILTERCAPS_MINFLINEAR) ? D3DTEXF_LINEAR : D3DTEXF_NONE;
                if (SUCCEEDED(dev->CreateRenderTarget(ow, oh, D3DFMT_X8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &rt, nullptr)) &&
                    SUCCEEDED(dev->StretchRect(from, nullptr, rt, nullptr, ow != d.Width ? f : D3DTEXF_NONE))) {
                    src = rt;
                    sw = ow;
                    sh = oh;
                    j->gpu_scaled = ow != d.Width;
                } else if (from == res && res && plain) {
                    src = res; // resolved, not scaled: the worker shrinks it
                } else if (d.MultiSampleType != D3DMULTISAMPLE_NONE || !plain) {
                    err = "the back buffer cannot be scaled or read (format " + std::to_string((int)d.Format) + ")";
                    break;
                }
            }
            if (FAILED(dev->CreateOffscreenPlainSurface(sw, sh, src == rt ? D3DFMT_X8R8G8B8 : d.Format, D3DPOOL_SYSTEMMEM,
                                                        &sys, nullptr))) { err = "CreateOffscreenPlainSurface"; break; }
            if (FAILED(dev->GetRenderTargetData(src, sys))) { err = "GetRenderTargetData"; break; }
            D3DLOCKED_RECT lr;
            if (FAILED(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY))) { err = "LockRect"; break; }
            j->w = (int)sw;
            j->h = (int)sh;
            j->bgrx.resize((size_t)sw * sh * 4);
            for (UINT y = 0; y < sh; y++)
                memcpy(&j->bgrx[(size_t)y * sw * 4], (const uint8_t*)lr.pBits + (size_t)y * lr.Pitch, (size_t)sw * 4);
            sys->UnlockRect();
        } while (false);
    } catch (...) {
        err = "out of memory";
    }
    if (sys) sys->Release();
    if (rt) rt->Release();
    if (res) res->Release();
    if (bb) bb->Release();
    if (err.empty() && j) {
        j->frame_ms = (now_seconds() - t0) * 1000;
        j->path = p.path;
        j->name = p.name;
        j->caption = p.caption;
        HANDLE th = CreateThread(nullptr, 0, shot_worker, j, 0, nullptr);
        if (th) {
            CloseHandle(th);
            return;
        }
        err = "no worker thread";
    }
    delete j;
    fail(p.name, err);
}

} // namespace sk
