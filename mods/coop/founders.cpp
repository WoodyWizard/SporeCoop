// The campaign's founder seats next to the planet's save (founders.h; Lineages port, BUG-9260).
#include "founders.h"

#include <windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cwchar>

#include "net/buffer.h"
#include "slotfs.h"

namespace founders {
namespace {

constexpr uint32_t kPartyMagic = 0x31595450u;  // PTY1 (read only)
constexpr uint32_t kPartyMagic2 = 0x32595450u; // PTY2 (BUG-9263: + the seat's own stage history)
constexpr size_t kSeatExtra = 1 + 4 * kCards + 8;
constexpr size_t kNameMax = 64;

// the side file of a saved planet; empty when the key is no .spo file that exists
std::wstring path_for(const std::string& key, const wchar_t* suffix)
{
    if (!key.ends_with(".spo"))
        return {};
    std::wstring planet = slotfs::widen(key);
    DWORD attr = GetFileAttributesW(planet.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY))
        return {};
    return planet + suffix;
}

// written whole to a temporary file, then moved over the old one (a crash leaves the old file or the new one)
bool publish(const std::wstring& path, const std::vector<uint8_t>& data, bool replace)
{
    std::wstring temp = path + L".duo-tmp-" + std::to_wstring(GetCurrentProcessId());
    HANDLE f = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    DWORD wrote = 0;
    bool ok = WriteFile(f, data.data(), (DWORD)data.size(), &wrote, nullptr) && wrote == data.size() && FlushFileBuffers(f);
    CloseHandle(f);
    if (ok && MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH | (replace ? MOVEFILE_REPLACE_EXISTING : 0)))
        return true;
    DeleteFileW(temp.c_str());
    return false;
}

bool party_valid(const Party& v)
{
    if (v.seats.empty() || v.seats.size() > kMaxSeats)
        return false;
    for (size_t i = 0; i < v.seats.size(); ++i) {
        if (v.seats[i].name.size() > kNameMax)
            return false;
        for (size_t j = i + 1; j < v.seats.size(); ++j)
            if (v.seats[i].player && v.seats[i].player == v.seats[j].player)
                return false;
    }
    return true;
}

} // namespace

bool read_side(const std::string& key, const wchar_t* suffix, std::vector<uint8_t>& out, size_t max_bytes)
{
    auto path = path_for(key, suffix);
    if (path.empty())
        return false;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(f, &size) && size.QuadPart >= 0 && (uint64_t)size.QuadPart <= max_bytes;
    if (ok) {
        out.resize(size_t(size.QuadPart));
        DWORD got = 0;
        ok = out.empty() || (ReadFile(f, out.data(), DWORD(out.size()), &got, nullptr) && got == out.size());
    }
    CloseHandle(f);
    return ok;
}

bool write_side(const std::string& key, const wchar_t* suffix, const std::vector<uint8_t>& data)
{
    auto path = path_for(key, suffix);
    return !path.empty() && publish(path, data, true);
}

bool read_party(const std::string& key, Party& out)
{
    std::vector<uint8_t> bytes;
    if (!read_side(key, L".duoparty", bytes, 5 + kMaxSeats * (8 + 4 + 1 + kNameMax + kSeatExtra)))
        return false;
    net::Reader r(bytes.data(), bytes.size());
    const uint32_t magic = r.u32();
    if (magic != kPartyMagic && magic != kPartyMagic2)
        return false;
    Party value;
    const unsigned n = r.u8();
    for (unsigned i = 0; i < n && r.ok(); ++i) {
        PartySeat s;
        s.player = r.u64();
        s.species = r.u32();
        s.name = r.str8();
        if (magic == kPartyMagic2) {
            s.own_cards = (r.u8() & 1) != 0;
            for (auto& c : s.cards)
                c = r.u32();
            s.social = r.f32();
            s.combat = r.f32();
            if (!std::isfinite(s.social) || !std::isfinite(s.combat)) // a NaN would never compare equal
                s.social = s.combat = 0;
        }
        value.seats.push_back(std::move(s));
    }
    if (!r.ok() || r.left() || value.seats.size() != n || !party_valid(value))
        return false;
    out = std::move(value);
    return true;
}

bool record_party(const std::string& key, const Party& value, std::string& err)
{
    auto path = path_for(key, L".duoparty");
    if (path.empty() || !party_valid(value)) {
        err = "a saved planet and a valid party are required";
        return false;
    }
    Party prior;
    const bool had = read_party(key, prior);
    if (!had && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        err = "this planet's party file is unreadable; it was left as it is";
        return false;
    }
    if (had) {
        if (prior == value)
            return true;
        if (value.seats.size() < prior.seats.size()) {
            err = "a party never loses a seat";
            return false;
        }
        for (size_t i = 0; i < prior.seats.size(); ++i)
            if (prior.seats[i].player && prior.seats[i].player != value.seats[i].player) {
                err = "a party seat belongs to another player";
                return false;
            }
    }
    net::Writer w;
    w.u32(kPartyMagic2);
    w.u8((uint8_t)value.seats.size());
    for (const auto& s : value.seats) {
        w.u64(s.player);
        w.u32(s.species);
        w.str8(s.name);
        w.u8(s.own_cards ? 1 : 0);
        for (uint32_t c : s.cards)
            w.u32(c);
        w.f32(s.social);
        w.f32(s.combat);
    }
    if (publish(path, w.data, had))
        return true;
    err = "could not save the party (write access to the planet's folder?)";
    return false;
}

// ---- BUG-9261: the partners' creatures archived with the planet (from PR #196's founders.cpp) ----
constexpr unsigned kMaxModel = 8u << 20;
constexpr size_t kMaxModels = 16;

static std::wstring model_path(const std::string& key, uint32_t id)
{
    if ((id >> 24) != 0x7C)
        return {};
    wchar_t suffix[32];
    swprintf(suffix, 32, L".duoguest-%08X", id);
    return path_for(key, suffix);
}

bool model_ids(const std::string& key, std::vector<uint32_t>& ids, std::string& err)
{
    ids.clear();
    std::wstring prefix = path_for(key, L".duoguest-");
    if (prefix.empty())
        return true;
    WIN32_FIND_DATAW entry{};
    HANDLE h = FindFirstFileW((prefix + L"????????").c_str(), &entry);
    if (h == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND)
            return true;
        err = "could not list this planet's archived creatures";
        return false;
    }
    bool ok = true;
    do {
        std::wstring name = entry.cFileName;
        auto suffix = name.substr(name.size() >= 8 ? name.size() - 8 : 0);
        if (suffix.size() != 8 || suffix.find_first_not_of(L"0123456789abcdefABCDEF") != std::wstring::npos)
            continue; // (a temporary file of publish)
        uint32_t id = wcstoul(suffix.c_str(), nullptr, 16);
        if ((id >> 24) != 0x7C || (entry.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
            entry.nFileSizeHigh || !entry.nFileSizeLow || entry.nFileSizeLow > kMaxModel || ids.size() >= kMaxModels) {
            ok = false;
            break;
        }
        ids.push_back(id);
    } while (FindNextFileW(h, &entry));
    if (ok && GetLastError() != ERROR_NO_MORE_FILES)
        ok = false;
    FindClose(h);
    if (!ok)
        err = "invalid or too many archived creatures next to this planet";
    return ok;
}

bool load_guest_model(const std::string& key, uint32_t id, std::vector<uint8_t>& data, std::string& err)
{
    data.clear();
    std::wstring path = model_path(key, id);
    if (path.empty()) {
        err = "invalid archived creature key";
        return false;
    }
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        err = "could not read the archived creature";
        return false;
    }
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(f, &size) && size.QuadPart > 0 && size.QuadPart <= kMaxModel;
    DWORD got = 0;
    if (ok) {
        data.resize((size_t)size.QuadPart);
        ok = ReadFile(f, data.data(), (DWORD)data.size(), &got, nullptr) && got == data.size();
    }
    CloseHandle(f);
    if (!ok) {
        data.clear();
        err = "invalid or incomplete archived creature";
    }
    return ok;
}

bool store_guest_model(const std::string& key, uint32_t id, const std::vector<uint8_t>& data, std::string& err)
{
    std::wstring path = model_path(key, id);
    if (path.empty() || data.empty() || data.size() > kMaxModel) {
        err = "a saved planet and a valid creature are required";
        return false;
    }
    // two game instances on one PC may archive at once: one at a time, never waiting in the game thread
    struct Guard {
        HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\SporeCoopDuoFounderArchive");
        DWORD state = mutex ? WaitForSingleObject(mutex, 0) : WAIT_FAILED;
        bool acquired() const { return state == WAIT_OBJECT_0 || state == WAIT_ABANDONED; }
        ~Guard()
        {
            if (acquired())
                ReleaseMutex(mutex);
            if (mutex)
                CloseHandle(mutex);
        }
    } guard;
    if (!guard.acquired()) {
        err = "another creature archive is being written: retried later";
        return false;
    }
    std::vector<uint32_t> ids;
    if (!model_ids(key, ids, err))
        return false;
    if (std::find(ids.begin(), ids.end(), id) == ids.end()) {
        if (ids.size() >= kMaxModels) {
            err = "this planet holds the most archived creatures it can";
            return false;
        }
        if (publish(path, data, false))
            return true;
    }
    std::vector<uint8_t> existing;
    if (!load_guest_model(key, id, existing, err))
        return false;
    if (data == existing)
        return true;
    err = "this planet already archives a different creature under that key";
    return false;
}

// ---- BUG-9268: the founders of a stage (from PR #196's founders.cpp, indexed by party seat) ----
static const wchar_t* roster_suffix(Stage stage) { return stage == Stage::Tribe ? L".duofounders" : L".duonations"; }
constexpr uint32_t kRosterMagic = 0x334F5544u; // DUO3
constexpr size_t kRosterMax = 5 + kMaxSeats * 16;

bool read(const std::string& key, Roster& out, Stage stage)
{
    std::vector<uint8_t> bytes;
    if (!read_side(key, roster_suffix(stage), bytes, kRosterMax))
        return false;
    net::Reader r(bytes.data(), bytes.size());
    if (r.u32() != kRosterMagic)
        return false;
    const unsigned n = r.u8();
    if (n < 1 || n > kMaxSeats || bytes.size() != 5 + n * 16)
        return false;
    Roster value;
    for (unsigned i = 0; i < n; ++i) {
        value.entries[i].species = r.u32();
        value.entries[i].faction = r.u32();
        value.entries[i].player = r.u64();
    }
    if (!r.ok() || r.left() || !roster_valid(value))
        return false;
    out = value;
    return true;
}

bool record(const std::string& key, const Roster& value, std::string& err, Stage stage)
{
    auto path = path_for(key, roster_suffix(stage));
    if (path.empty() || !roster_valid(value) || !value.seats()) {
        err = "a saved planet and distinct species and factions for every founder are required";
        return false;
    }
    Roster prior;
    if (read(key, prior, stage) && prior == value)
        return true;
    net::Writer w;
    w.u32(kRosterMagic);
    const size_t n = value.seats();
    w.u8((uint8_t)n);
    for (size_t i = 0; i < n; ++i) {
        w.u32(value.entries[i].species);
        w.u32(value.entries[i].faction);
        w.u64(value.entries[i].player);
    }
    if (publish(path, w.data, GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES))
        return true;
    err = "could not save the founders (write access to the planet's folder?)";
    return false;
}

std::string describe(const Roster& v)
{
    std::string out;
    char line[96];
    for (size_t i = 0; i < v.seats(); ++i) {
        if (!v.entries[i].founded())
            continue;
        snprintf(line, sizeof line, "%sseat %zu %08X/%08X", out.empty() ? "" : ", ", i, v.entries[i].species, v.entries[i].faction);
        out += line;
    }
    return out.empty() ? "no founders" : out;
}

} // namespace founders
