#include "pack.h"

#include <cctype>
#include <cstring>

#include "sha256.h"

namespace upd {

bool g_path_fix = true;

static std::string lower(const std::string& s)
{
    std::string o = s;
    for (auto& c : o)
        c = c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
    return o;
}

static bool ends_with(const std::string& s, const char* t)
{
    size_t k = strlen(t);
    return s.size() >= k && s.compare(s.size() - k, k, t) == 0;
}

// a DOS device name ("con", "nul.txt", "com1", ...): opens a device, not a file
static bool device_name(const std::string& part_lower)
{
    std::string b = part_lower.substr(0, part_lower.find('.'));
    static const char* const dev[] = {"con", "prn", "aux", "nul", "conin$", "conout$"};
    for (auto* d : dev)
        if (b == d)
            return true;
    if (b.size() == 4 && (b.rfind("com", 0) == 0 || b.rfind("lpt", 0) == 0) && b[3] >= '0' && b[3] <= '9')
        return true;
    return false;
}

static bool allowed_path_old(const std::string& path);

std::string canonical_path(const std::string& path)
{
    if (!allowed_path_old(path))
        return {};
    std::string l = lower(path);
    // BUG-0242: Windows strips a trailing dot of a name ("coop.dll." is coop.dll): refused, no real
    // pack has one. Device names and the updater's own working names are refused too.
    size_t start = 0;
    while (start <= l.size()) {
        size_t end = l.find('/', start);
        if (end == std::string::npos)
            end = l.size();
        std::string part = l.substr(start, end - start);
        if (part.back() == '.' || device_name(part))
            return {};
        start = end + 1;
    }
    if (ends_with(l, ".new") || ends_with(l, ".sporecoop-old") || ends_with(l, ".old") ||
        l.find(".sporecoop-tmp") != std::string::npos || l == "sporebinep1/mods/sporecoop.pending")
        return {};
    return l;
}

bool allowed_path(const std::string& path)
{
    if (g_path_fix)
        return !canonical_path(path).empty();
    return allowed_path_old(path);
}

static bool allowed_path_old(const std::string& path)
{
    if (path.empty() || path.size() > 200 || path.find('\\') != std::string::npos ||
        path.find(':') != std::string::npos || path[0] == '/')
        return false;
    size_t start = 0;
    while (start <= path.size()) {
        size_t end = path.find('/', start);
        if (end == std::string::npos)
            end = path.size();
        std::string part = path.substr(start, end - start);
        if (part.empty() || part == "." || part == "..")
            return false;
        for (unsigned char c : part)
            if (!(isalnum(c) || c == '_' || c == '-' || c == '.'))
                return false;
        start = end + 1;
    }
    return path == "SporebinEP1/steam_api.dll" || path == "SporebinEP1/sporekit.dll" ||
           path == "Data/SporeCoop.package" || path.rfind("SporebinEP1/mods/", 0) == 0;
}

bool g_dup_fix = true;

static std::string fold(const std::string& s)
{
    std::string o = s;
    for (auto& c : o)
        c = c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c == '\\' ? '/' : c;
    return o;
}

std::string duplicate_path(const std::vector<PackFile>& files)
{
    for (size_t i = 0; i < files.size(); i++)
        for (size_t k = 0; k < i; k++)
            if (g_path_fix ? canonical_path(files[i].path) == canonical_path(files[k].path) &&
                                 !canonical_path(files[i].path).empty()
                           : fold(files[i].path) == fold(files[k].path))
                return files[i].path;
    return {};
}

namespace {

struct In {
    const std::vector<uint8_t>& b;
    size_t o = 0;
    bool ok = true;
    bool need(size_t n)
    {
        if (!ok || o + n > b.size() || o + n < o)
            ok = false;
        return ok;
    }
    uint32_t u32()
    {
        if (!need(4))
            return 0;
        uint32_t v = b[o] | b[o + 1] << 8 | b[o + 2] << 16 | (uint32_t)b[o + 3] << 24;
        o += 4;
        return v;
    }
    uint16_t u16()
    {
        if (!need(2))
            return 0;
        uint16_t v = (uint16_t)(b[o] | b[o + 1] << 8);
        o += 2;
        return v;
    }
    std::string str16()
    {
        uint16_t n = u16();
        if (!need(n))
            return {};
        std::string s((const char*)b.data() + o, n);
        o += n;
        return s;
    }
};

void put_u32(std::vector<uint8_t>& o, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        o.push_back((uint8_t)(v >> (8 * i)));
}

void put_str16(std::vector<uint8_t>& o, const std::string& s)
{
    o.push_back((uint8_t)s.size());
    o.push_back((uint8_t)(s.size() >> 8));
    o.insert(o.end(), s.begin(), s.end());
}

} // namespace

bool parse_pack(const std::vector<uint8_t>& bytes, Pack& out, std::string& err)
{
    out = Pack{};
    In in{bytes};
    if (!in.need(4) || memcmp(bytes.data(), "SCUP", 4) != 0) {
        err = "not a SporeCoop update pack";
        return false;
    }
    in.o = 4;
    if (in.u32() != 1) {
        err = "unknown pack format";
        return false;
    }
    out.version = in.str16();
    uint32_t count = in.u32();
    if (!in.ok || count == 0 || count > 256) {
        err = "bad pack header";
        return false;
    }
    struct Head {
        std::string path;
        uint8_t flags;
        uint32_t size;
        std::string sha;
    };
    std::vector<Head> heads;
    uint64_t total = 0;
    for (uint32_t i = 0; i < count && in.ok; i++) {
        Head h;
        h.path = in.str16();
        h.flags = in.need(1) ? bytes[in.o++] : 0;
        h.size = in.u32();
        if (in.need(32)) {
            static const char* hx = "0123456789abcdef";
            for (int k = 0; k < 32; k++) {
                h.sha += hx[bytes[in.o + k] >> 4];
                h.sha += hx[bytes[in.o + k] & 15];
            }
            in.o += 32;
        }
        total += h.size;
        if (!allowed_path(h.path)) {
            err = "pack writes outside the mod's files: " + h.path;
            return false;
        }
        heads.push_back(h);
    }
    if (!in.ok || total > (64u << 20) || in.o + total != bytes.size()) {
        err = "pack is truncated or too big";
        return false;
    }
    for (auto& h : heads) {
        PackFile f;
        f.path = h.path;
        f.flags = h.flags;
        f.data.assign(bytes.begin() + in.o, bytes.begin() + in.o + h.size);
        in.o += h.size;
        if (sha256_hex(f.data) != h.sha) {
            err = "damaged file in the pack: " + h.path;
            return false;
        }
        out.files.push_back(std::move(f));
    }
    if (std::string d = g_dup_fix ? duplicate_path(out.files) : std::string(); !d.empty()) {
        err = "the pack lists a file twice: " + d;
        out = Pack{};
        return false;
    }
    return true;
}

std::vector<uint8_t> write_pack(const Pack& p)
{
    std::vector<uint8_t> o = {'S', 'C', 'U', 'P'};
    put_u32(o, 1);
    put_str16(o, p.version);
    put_u32(o, (uint32_t)p.files.size());
    for (auto& f : p.files) {
        put_str16(o, f.path);
        o.push_back(f.flags);
        put_u32(o, (uint32_t)f.data.size());
        std::string h = sha256_hex(f.data);
        for (int k = 0; k < 32; k++)
            o.push_back((uint8_t)std::stoi(h.substr(k * 2, 2), nullptr, 16));
    }
    for (auto& f : p.files)
        o.insert(o.end(), f.data.begin(), f.data.end());
    return o;
}

bool g_pick_fix = true;
bool g_release_lock_fix = true;

std::string pack_version_of(const std::string& path)
{
    size_t slash = path.find_last_of("\\/");
    std::string n = slash == std::string::npos ? path : path.substr(slash + 1);
    for (auto& c : n)
        c = (char)tolower((unsigned char)c);
    const std::string pre = "sporecoop-", suf = ".pack";
    if (n.size() <= pre.size() + suf.size() || n.compare(0, pre.size(), pre) != 0 ||
        n.compare(n.size() - suf.size(), suf.size(), suf) != 0)
        return {};
    std::string v = n.substr(pre.size(), n.size() - pre.size() - suf.size());
    if (v.empty() || !isdigit((unsigned char)(v[0] == 'v' ? (v.size() > 1 ? v[1] : 'x') : v[0])))
        return {};
    return v;
}

std::string pick_pack(const std::vector<std::string>& names)
{
    std::string best, best_v;
    for (auto& n : names) {
        std::string v = pack_version_of(n);
        if (v.empty())
            continue;
        if (!g_pick_fix) // the old choice: the first one listed
            return n;
        if (best.empty() || compare_versions(v, best_v) > 0) {
            best = n;
            best_v = v;
        }
    }
    return best;
}

int compare_versions(const std::string& a, const std::string& b)
{
    auto parts = [](std::string s) {
        if (!s.empty() && (s[0] == 'v' || s[0] == 'V'))
            s.erase(0, 1);
        std::vector<long> v;
        size_t i = 0;
        while (i < s.size() && v.size() < 4) {
            long n = 0;
            bool any = false;
            while (i < s.size() && isdigit((unsigned char)s[i])) {
                n = n * 10 + (s[i++] - '0');
                any = true;
                if (n > 1000000)
                    break;
            }
            v.push_back(any ? n : 0);
            if (i < s.size() && s[i] == '.')
                i++;
            else
                break;
        }
        while (v.size() < 4)
            v.push_back(0);
        return v;
    };
    auto x = parts(a), y = parts(b);
    for (int i = 0; i < 4; i++)
        if (x[i] != y[i])
            return x[i] < y[i] ? -1 : 1;
    return 0;
}

namespace {

// the string value after "key": in a JSON text, starting at `from` (enough for the GitHub answer:
// the values we read never hold escaped quotes)
std::string json_str(const std::string& j, const std::string& key, size_t from, size_t* at = nullptr)
{
    std::string k = "\"" + key + "\"";
    size_t p = j.find(k, from);
    if (p == std::string::npos)
        return {};
    p = j.find(':', p + k.size());
    if (p == std::string::npos)
        return {};
    p = j.find('"', p);
    if (p == std::string::npos)
        return {};
    size_t e = j.find('"', p + 1);
    if (e == std::string::npos)
        return {};
    if (at)
        *at = e;
    return j.substr(p + 1, e - p - 1);
}

bool github_download(const std::string& url)
{
    return url.rfind("https://github.com/", 0) == 0 || url.rfind("http://127.0.0.1:", 0) == 0;
}

} // namespace

bool parse_release(const std::string& json, Release& out)
{
    out = Release{};
    out.tag = json_str(json, "tag_name", 0);
    out.page_url = json_str(json, "html_url", 0);
    if (out.tag.empty())
        return false;
    out.version = out.tag[0] == 'v' || out.tag[0] == 'V' ? out.tag.substr(1) : out.tag;
    size_t pos = 0;
    for (;;) {
        size_t at = 0;
        std::string url = json_str(json, "browser_download_url", pos, &at);
        if (url.empty())
            break;
        pos = at;
        if (!github_download(url)) // assets come from github.com (or the local test server)
            continue;
        std::string name = url.substr(url.find_last_of('/') + 1);
        if (name.size() > 5 && name.compare(name.size() - 5, 5, ".pack") == 0)
            out.pack_url = url;
        else if (name == "SHA256SUMS.txt")
            out.sums_url = url;
        else if (name == "SporeCoopSetup.exe")
            out.setup_url = url;
    }
    return !out.version.empty();
}

std::string sums_lookup(const std::string& sums, const std::string& file)
{
    size_t p = 0;
    while (p < sums.size()) {
        size_t e = sums.find('\n', p);
        if (e == std::string::npos)
            e = sums.size();
        std::string line = sums.substr(p, e - p);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        size_t sp = line.find(' ');
        if (sp == 64) {
            std::string name = line.substr(sp);
            size_t k = name.find_first_not_of(" *");
            if (k != std::string::npos && name.substr(k) == file)
                return line.substr(0, 64);
        }
        p = e + 1;
    }
    return {};
}

} // namespace upd
