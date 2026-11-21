// The campaign's founder seats next to the planet's save (founders.h; Lineages port, BUG-9260).
#include "founders.h"

#include <windows.h>
#include <array>

#include "net/buffer.h"
#include "slotfs.h"

namespace founders {
namespace {

constexpr uint32_t kPartyMagic = 0x31595450u; // PTY1
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

bool planet_saved(const std::string& planet_file) { return !path_for(planet_file, L"").empty(); }

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
    if (!read_side(key, L".duoparty", bytes, 5 + kMaxSeats * (8 + 4 + 1 + kNameMax)))
        return false;
    net::Reader r(bytes.data(), bytes.size());
    if (r.u32() != kPartyMagic)
        return false;
    Party value;
    const unsigned n = r.u8();
    for (unsigned i = 0; i < n && r.ok(); ++i) {
        PartySeat s;
        s.player = r.u64();
        s.species = r.u32();
        s.name = r.str8();
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
    w.u32(kPartyMagic);
    w.u8((uint8_t)value.seats.size());
    for (const auto& s : value.seats) {
        w.u64(s.player);
        w.u32(s.species);
        w.str8(s.name);
    }
    if (publish(path, w.data, had))
        return true;
    err = "could not save the party (write access to the planet's folder?)";
    return false;
}

} // namespace founders
