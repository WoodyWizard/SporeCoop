#include "founders.h"

#include <windows.h>
#include <array>
#include <algorithm>
#include <cstdio>

#include "net/buffer.h"
#include "../../updater/sha256.h"
#include "slotfs.h"

namespace founders {
namespace {
uint32_t format(Stage stage) { return stage == Stage::Tribe ? 0x314F5544u : 0x31564943u; } // DUO1 / CIV1
const wchar_t* suffix(Stage stage) { return stage == Stage::Tribe ? L".duofounders" : L".duonations"; }
constexpr size_t kBytes = 5 * sizeof(uint32_t);

bool valid(const Roster& v)
{
    // The current on-disk duo formats cannot represent extra founders. Refuse
    // them rather than silently discarding a future caller's campaign data.
    for (size_t i = 2; i < v.entries.size(); ++i)
        if (v.entries[i] != Founder{}) return false;
    return v.home().species && v.guest().species && v.home().species != v.guest().species &&
           v.home().faction && v.guest().faction && v.home().faction != v.guest().faction;
}

std::wstring path_for(const std::string& key, const wchar_t* suffix = L".duofounders")
{
    if (!key.ends_with(".spo")) return {};
    std::wstring planet = slotfs::widen(key);
    DWORD attr = GetFileAttributesW(planet.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) return {};
    return planet + suffix;
}

bool publish(const std::wstring& path, const std::vector<uint8_t>& data, bool replace = false)
{
    std::wstring temp = path + L".duo-tmp-" + std::to_wstring(GetCurrentProcessId());
    HANDLE f = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD wrote = 0;
    bool ok = WriteFile(f, data.data(), (DWORD)data.size(), &wrote, nullptr) && wrote == data.size() && FlushFileBuffers(f);
    CloseHandle(f);
    if (ok && MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH | (replace ? MOVEFILE_REPLACE_EXISTING : 0))) return true;
    DeleteFileW(temp.c_str());
    return false;
}
} // namespace

// The native SaveGame call writes a temporary file and renames it. Bind
// fractional credit to its bytes so reloading a backup cannot import newer meals.
static std::string planet_digest(const std::string& key)
{
    const auto path = path_for(key,L"");
    if (path.empty()) return {};
    HANDLE f = CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (f == INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER size{};
    std::vector<uint8_t> data;
    DWORD got = 0;
    if (GetFileSizeEx(f,&size) && size.QuadPart > 0 && size.QuadPart <= (128u << 20)) {
        data.resize(size_t(size.QuadPart));
        if (!ReadFile(f,data.data(),DWORD(data.size()),&got,nullptr) || got != data.size()) data.clear();
    }
    CloseHandle(f);
    return data.empty() ? "" : upd::sha256_hex(data);
}

bool save_cell_credit(const std::string& key, const CampaignRules& rules, const CellCredit& credit)
{
    const auto path = path_for(key,L".duo-food");
    if (path.empty() || !rules.seats || rules.seats > 4 || credit.health_carry >= 100 ||
        credit.damage_carry >= 100) return false;
    auto digest = planet_digest(key);
    if (digest.empty()) return false;
    net::Writer w;
    w.u32(0x32444643u); // CFD2
    w.u32(rules.workload_percent());
    w.u32(credit.remainder[0]);
    w.u32(credit.remainder[1]);
    w.u32(rules.enemy_health_percent());
    w.u32(rules.enemy_damage_percent());
    w.u8(credit.health_carry);
    w.u8(credit.damage_carry);
    w.raw(digest.data(),digest.size());
    return publish(path,w.data,true);
}

bool read_cell_credit(const std::string& key, const CampaignRules& rules, CellCredit& credit)
{
    const auto path = path_for(key,L".duo-food");
    if (path.empty()) return false;
    HANDLE f = CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ | FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    std::array<uint8_t,91> data{};
    DWORD got = 0;
    bool ok = ReadFile(f,data.data(),DWORD(data.size()),&got,nullptr) && (got == 80 || got == 90);
    CloseHandle(f);
    if (!ok) return false;
    net::Reader r(data.data(),got);
    const auto version = r.u32();
    if (!((got == 80 && version == 0x31444643u) || (got == 90 && version == 0x32444643u)) ||
        r.u32() != rules.workload_percent()) return false;
    CellCredit saved{{r.u32(),r.u32()}};
    if (got == 90) {
        if (r.u32() != rules.enemy_health_percent() || r.u32() != rules.enemy_damage_percent()) return false;
        saved.health_carry = r.u8();
        saved.damage_carry = r.u8();
        if (saved.health_carry >= 100 || saved.damage_carry >= 100) return false;
    }
    if (saved.remainder[0] >= rules.workload_percent() || saved.remainder[1] >= rules.workload_percent() ||
        std::string((const char*)r.cur(),r.left()) != planet_digest(key)) return false;
    credit = saved;
    return true;
}

RulesFile read_rules(const std::string& key, CampaignRules& value, SeatPlan* plan)
{
    auto path = path_for(key,L".duo-seats");
    if (path.empty()) return RulesFile::Missing;
    HANDLE file = CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ | FILE_SHARE_DELETE,nullptr,
                              OPEN_EXISTING,0,nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return GetLastError() == ERROR_FILE_NOT_FOUND ? RulesFile::Missing : RulesFile::Unreadable;
    std::array<uint8_t,22> data{}; // largest format plus one byte to detect overlong files
    DWORD count = 0;
    bool ok = ReadFile(file,data.data(),DWORD(data.size()),&count,nullptr) && (count == 7 || count == 11 || count == 14 || count == 16 || count == 21);
    CloseHandle(file);
    if (!ok) return RulesFile::Unreadable;
    net::Reader r(data.data(),count);
    uint32_t format = r.u32();
    if (!((format == 0x31545346u && count == 7) || (format == 0x32545346u && count == 11) ||
          (format == 0x33545346u && count == 14) || (format == 0x34545346u && count == 16) ||
          (format == 0x35545346u && count == 21)))
        return RulesFile::Unreadable;
    CampaignRules saved{r.u8(),r.u16()};
    // FST1 campaigns used fixed combat bonuses before the fields were persisted.
    if (count == 7) {
        saved.extra_health_percent = 25;
        saved.extra_damage_percent = 10;
    } else {
        saved.extra_health_percent = r.u16();
        saved.extra_damage_percent = r.u16();
    }
    if (!r.ok() || saved.seats < 1 || saved.seats > 4) return RulesFile::Unreadable;
    // Older saves did not record which seats had been claimed while offline.
    SeatPlan saved_plan{saved.seats};
    if (count >= 14) {
        saved_plan = {r.u8(),r.u8(),r.u8()};
        if (!r.ok() || !saved_plan.protected_seats || saved_plan.protected_seats > saved.seats ||
            saved_plan.next_seats > 4 || saved_plan.queued_stage > 5 ||
            bool(saved_plan.next_seats) != bool(saved_plan.queued_stage) ||
            (saved_plan.next_seats && saved_plan.next_seats < saved_plan.protected_seats))
            return RulesFile::Unreadable;
    }
    saved.extra_hunger_basis_points = count >= 16 ? r.u16() : 0;
    if (count == 21) {
        for (auto& seats : saved_plan.previous_stage_seats) {
            seats = r.u8();
            if (seats > 4) return RulesFile::Unreadable;
        }
    }
    if (!r.ok() || r.left()) return RulesFile::Unreadable;
    if (plan) *plan = saved_plan;
    value = saved;
    return RulesFile::Saved;
}

// The native file is replaced atomically, but the read/modify/write sequence
// also needs to exclude simultaneous hosts or UI and admission updates.
class RuleWriteLock {
    HANDLE mutex = CreateMutexW(nullptr,FALSE,L"SporeCoop.CampaignRules");
public:
    bool held = false;
    RuleWriteLock() {
        if (mutex) {
            const auto result = WaitForSingleObject(mutex,1000);
            held = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
        }
    }
    ~RuleWriteLock() {
        if (held) ReleaseMutex(mutex);
        if (mutex) CloseHandle(mutex);
    }
    RuleWriteLock(const RuleWriteLock&) = delete;
    RuleWriteLock& operator=(const RuleWriteLock&) = delete;
};

static bool write_rules(const std::wstring& path, const CampaignRules& rules, const SeatPlan& plan, bool replace)
{
    net::Writer w;
    w.u32(0x35545346u); // FST5
    w.u8(rules.seats);
    w.u16(rules.difficulty_percent);
    w.u16(rules.extra_health_percent);
    w.u16(rules.extra_damage_percent);
    w.u8(plan.protected_seats);
    w.u8(plan.next_seats);
    w.u8(plan.queued_stage);
    w.u16(rules.extra_hunger_basis_points);
    for (auto seats : plan.previous_stage_seats) w.u8(seats);
    return publish(path,w.data,replace);
}

bool queue_seats(const std::string& key, uint8_t seats, uint8_t stage, std::string& error)
{
    RuleWriteLock lock;
    if (!lock.held) { error = "Campaign seat settings are busy. Try again."; return false; }
    CampaignRules rules;
    SeatPlan plan;
    if (!stage || stage > 5 || read_rules(key,rules,&plan) != RulesFile::Saved) {
        error = "Open a saved, reserved campaign in a regular stage.";
        return false;
    }
    if (seats < plan.protected_seats || seats > 4) {
        error = "This campaign protects " + std::to_string(plan.protected_seats) + " seat(s). Choose up to four.";
        return false;
    }
    const auto previous = plan;
    plan.next_seats = seats == plan.seats_in(stage,rules.seats) ? 0 : seats;
    plan.queued_stage = plan.next_seats ? stage : 0;
    if (plan == previous || write_rules(path_for(key,L".duo-seats"),rules,plan,true)) return true;
    error = "Could not save the queued seat change.";
    return false;
}

bool update_seats(const std::string& key, uint8_t occupied, uint8_t stage, bool space_reloaded,
                  CampaignRules& rules, SeatPlan& plan, std::string& error)
{
    RuleWriteLock lock;
    if (!lock.held) { error = "Campaign seat settings are busy. Try again."; return false; }
    if (read_rules(key,rules,&plan) != RulesFile::Saved || !occupied || occupied > plan.seats_in(stage,rules.seats) ||
        !stage || stage > 5) {
        error = "Cannot match the current lobby to this saved campaign.";
        return false;
    }
    const auto previous = plan;
    plan.protected_seats = std::max(plan.protected_seats,occupied);
    rules.seats = std::max(rules.seats,plan.protected_seats);
    if (plan.next_seats) {
        if (plan.next_seats < plan.protected_seats) {
            plan.next_seats = plan.queued_stage = 0;
        } else if (plan.due(stage,space_reloaded)) {
            for (uint8_t i = 1; i <= plan.previous_stage_seats.size(); ++i)
                plan.previous_stage_seats[i-1] = i < stage ? plan.seats_in(i,rules.seats) : 0;
            rules.seats = plan.next_seats;
            plan.next_seats = plan.queued_stage = 0;
        }
    }
    if (plan != previous && !write_rules(path_for(key,L".duo-seats"),rules,plan,true)) {
        error = "Could not persist campaign seat settings.";
        return false;
    }
    rules.seats = plan.seats_in(stage,rules.seats);
    return true;
}

bool reserve(const std::string& key, const CampaignRules& requested, std::string& err)
{
    RuleWriteLock lock;
    if (!lock.held) { err = "Campaign seat settings are busy. Try again."; return false; }
    auto path = path_for(key,L".duo-seats");
    if (path.empty() || !requested.seats || requested.seats > 4) {
        err = "A saved planet and one to four reserved founders are required.";
        return false;
    }
    CampaignRules saved;
    auto state = read_rules(key,saved);
    if (state == RulesFile::Saved && saved == requested) return true;
    if (state == RulesFile::Missing) {
        if (write_rules(path,requested,{},false) || (read_rules(key,saved) == RulesFile::Saved && saved == requested)) return true;
    }
    err = "The campaign rules already differ or could not be saved.";
    return false;
}

bool read(const std::string& key, Roster& out, Stage stage)
{
    std::wstring path = path_for(key, suffix(stage));
    if (path.empty()) return false;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    std::array<uint8_t, kBytes + 1> bytes{};
    DWORD got = 0;
    bool ok = ReadFile(f, bytes.data(), (DWORD)bytes.size(), &got, nullptr) && got == kBytes;
    CloseHandle(f);
    if (!ok) return false;
    net::Reader r(bytes.data(), got);
    if (r.u32() != format(stage)) return false;
    Roster value{r.u32(), r.u32(), r.u32(), r.u32()};
    if (!r.ok() || !valid(value)) return false;
    out = value;
    return true;
}

bool can_record(const std::string& key, const Roster& value, std::string& err, Stage stage)
{
    std::wstring path = path_for(key, suffix(stage));
    if (path.empty() || !valid(value)) {
        err = "A saved planet and two distinct native factions are required.";
        return false;
    }
    if (stage == Stage::Civilization) {
        Roster tribe;
        Outfits outfits;
        auto previous = path_for(key, suffix(Stage::Tribe));
        if (GetFileAttributesW(previous.c_str()) != INVALID_FILE_ATTRIBUTES &&
            (!read(key, tribe) || !read_outfits(key, outfits, err) ||
             !outfits.contains(0, value.home().species) || !outfits.contains(1, value.guest().species))) {
            err = "The selected species do not match the saved tribal founders.";
            return false;
        }
    }
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND) return true;
    Roster prior;
    if (read(key, prior, stage) && prior == value) return true;
    err = "This planet has a different or unreadable founding roster.";
    return false;
}

bool record(const std::string& key, const Roster& value, std::string& err, Stage stage)
{
    if (!can_record(key, value, err, stage)) return false;
    std::wstring path = path_for(key, suffix(stage));
    Roster prior;
    if (read(key, prior, stage) && prior == value) return true;
    net::Writer w;
    for (uint32_t n : {format(stage), value.home().species, value.guest().species, value.home().faction, value.guest().faction})
        w.u32(n);
    if (publish(path, w.data)) return true;
    if (read(key, prior, stage) && prior == value) return true;
    err = "Could not save the two founding species. Check write access to the co-op save.";
    return false;
}

std::string describe(const Roster& v)
{
    char line[120];
    snprintf(line, sizeof line, "Founding species: host %08X, guest %08X", v.home().species, v.guest().species);
    return line;
}

// A missing or unreadable setting always reserves an absent nation. Bind
// the permission to the saved political identities as well as the planet.
bool offline_control(const std::string& planet, const Roster& nations)
{
    auto path = path_for(planet, L".duo-command");
    if (path.empty() || !valid(nations)) return false;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    std::array<uint8_t, 17> bytes{};
    DWORD size = 0;
    bool ok = ReadFile(file, bytes.data(), DWORD(bytes.size()), &size, nullptr) && size == 16;
    CloseHandle(file);
    if (!ok) return false;
    net::Reader r(bytes.data(), size);
    return r.u32() == 0x31444D43 && r.u32() == nations.home().faction &&
           r.u32() == nations.guest().faction && r.u32() == 1;
}

bool set_offline_control(const std::string& planet, const Roster& nations, bool enabled)
{
    Roster saved;
    auto path = path_for(planet, L".duo-command");
    if (path.empty() || !read(planet, saved, Stage::Civilization) || saved != nations) return false;
    net::Writer w;
    w.u32(0x31444D43); // CMD1
    w.u32(saved.home().faction); w.u32(saved.guest().faction); w.u32(enabled ? 1 : 0);
    return publish(path, w.data, true);
}

bool read_six_rivals(const std::string& planet, const Roster& founders, SixRivals& trial)
{
    auto path = path_for(planet, L".duo-six");
    if (path.empty() || !valid(founders)) return false;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    std::array<uint8_t, 25> bytes{};
    DWORD size = 0;
    bool ok = ReadFile(file, bytes.data(), DWORD(bytes.size()), &size, nullptr) && size == 24;
    CloseHandle(file);
    if (!ok) return false;
    net::Reader r(bytes.data(), size);
    if (r.u32() != 0x31584953 || r.u32() != founders.home().faction || r.u32() != founders.guest().faction) return false;
    SixRivals value{r.u32(), r.u32(), r.u32()};
    if (!value.extra || founders.contains_faction(value.extra) || founders.contains_faction(value.deferred) ||
        value.outcome > 2 || bool(value.deferred) != bool(value.outcome)) return false;
    trial = value;
    return true;
}

bool save_six_rivals(const std::string& planet, const Roster& founders, const SixRivals& trial, bool create)
{
    auto path = path_for(planet, L".duo-six");
    if (path.empty() || !valid(founders) || !trial.extra || founders.contains_faction(trial.extra)) return false;
    SixRivals prior;
    if (!create && (!read_six_rivals(planet, founders, prior) || prior.extra != trial.extra)) return false;
    net::Writer w;
    for (uint32_t value : {0x31584953u, founders.home().faction, founders.guest().faction,
                           trial.extra, trial.deferred, trial.outcome}) w.u32(value);
    return publish(path, w.data, !create);
}

bool Outfits::contains(size_t seat, uint32_t key) const
{
    return key && seat < species.size() &&
        std::find(species[seat].begin(), species[seat].end(), key) != species[seat].end();
}

bool read_outfits(const std::string& key, Outfits& out, std::string& err)
{
    Roster roster;
    if (!read(key, roster)) { err = "No readable original founders for this outfit."; return false; }
    Outfits value;
    value.species[0] = {roster.home().species};
    value.species[1] = {roster.guest().species};
    auto path = path_for(key, L".duooutfits");
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        if (GetLastError() != ERROR_FILE_NOT_FOUND) { err = "Could not read founder outfit history."; return false; }
        out = std::move(value); return true;
    }
    // OFT1, four original identity words, two counts, at most16 keys per seat.
    std::array<uint8_t, (7 + 32) * 4 + 1> bytes{};
    DWORD got = 0;
    bool ok = ReadFile(file, bytes.data(), (DWORD)bytes.size(), &got, nullptr) && got < bytes.size();
    CloseHandle(file);
    net::Reader r(bytes.data(), got);
    ok = ok && r.u32() == 0x3154464Fu && r.u32() == roster.home().species &&
        r.u32() == roster.guest().species && r.u32() == roster.home().faction && r.u32() == roster.guest().faction;
    for (size_t seat = 0; seat < 2 && ok; ++seat) {
        unsigned count = r.u32();
        if (!count || count > 16 || r.left() < count * 4) { ok = false; break; }
        value.species[seat].clear();
        for (unsigned i = 0; i < count; ++i) {
            uint32_t id = r.u32();
            if (!id || value.contains(seat, id) || (seat && value.contains(0, id))) { ok = false; break; }
            value.species[seat].push_back(id);
        }
        if (value.species[seat].empty() || value.species[seat][0] != roster.entries[seat].species) ok = false;
    }
    if (!ok || !r.ok() || r.left()) { err = "Invalid founder outfit history; the original roster was retained."; return false; }
    out = std::move(value); return true;
}

bool record_outfit(const std::string& key, size_t seat, uint32_t expected, uint32_t model, std::string& err)
{
    if (seat >= 2 || !expected || !model) { err = "Invalid founder outfit change."; return false; }
    struct Guard {
        HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\SporeCoopDuoOutfits");
        DWORD state = mutex ? WaitForSingleObject(mutex, 0) : WAIT_FAILED;
        bool acquired() const { return state == WAIT_OBJECT_0 || state == WAIT_ABANDONED; }
        ~Guard() { if (acquired()) ReleaseMutex(mutex); if (mutex) CloseHandle(mutex); }
    } guard;
    if (!guard.acquired()) { err = "Another outfit is being saved. Retry in a moment."; return false; }
    Outfits value;
    Roster roster;
    if (!read(key, roster) || !read_outfits(key, value, err)) return false;
    if (value.current(seat) != expected) { err = "This outfit result is older than the current founder."; return false; }
    if (model == expected) return true;
    if (value.contains(1 - seat, model) || value.contains(seat, model) || value.species[seat].size() >= 16) {
        err = "Outfit history is full, repeated, or belongs to the other founder."; return false;
    }
    value.species[seat].push_back(model);
    net::Writer w;
    for (uint32_t n : {0x3154464Fu, roster.home().species, roster.guest().species, roster.home().faction, roster.guest().faction}) w.u32(n);
    for (const auto& history : value.species) {
        w.u32((uint32_t)history.size());
        for (uint32_t n : history) w.u32(n);
    }
    if (publish(path_for(key, L".duooutfits"), w.data, true)) return true;
    err = "Could not save founder outfit history. The previous history was retained.";
    return false;
}

constexpr unsigned kMaxModel = 8u << 20;
constexpr size_t kMaxModels = 16;

static std::wstring model_path(const std::string& key, uint32_t id)
{
    if ((id >> 24) != 0x7C) return {};
    wchar_t suffix[32];
    swprintf(suffix, 32, L".duoguest-%08X", id);
    return path_for(key, suffix);
}

bool model_ids(const std::string& key, std::vector<uint32_t>& ids, std::string& err)
{
    ids.clear();
    std::wstring prefix = path_for(key, L".duoguest-");
    if (prefix.empty()) return true;
    WIN32_FIND_DATAW entry{};
    HANDLE h = FindFirstFileW((prefix + L"????????").c_str(), &entry);
    if (h == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) return true;
        err = "Could not list this planet's archived creatures.";
        return false;
    }
    bool ok = true;
    do {
        std::wstring name = entry.cFileName;
        auto suffix = name.substr(name.size() >= 8 ? name.size() - 8 : 0);
        if (suffix.size() != 8 || suffix.find_first_not_of(L"0123456789abcdefABCDEF") != std::wstring::npos) continue;
        uint32_t id = wcstoul(suffix.c_str(), nullptr, 16);
        if ((id >> 24) != 0x7C || (entry.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) ||
            entry.nFileSizeHigh || !entry.nFileSizeLow || entry.nFileSizeLow > kMaxModel || ids.size() >= kMaxModels) { ok = false; break; }
        ids.push_back(id);
    } while (FindNextFileW(h, &entry));
    if (ok && GetLastError() != ERROR_NO_MORE_FILES) ok = false;
    FindClose(h);
    if (!ok) err = "Invalid or excessive guest creature archives in this save.";
    return ok;
}

bool load_guest_model(const std::string& key, uint32_t id, std::vector<uint8_t>& data, std::string& err)
{
    data.clear();
    std::wstring path = model_path(key, id);
    if (path.empty()) { err = "Invalid saved model key."; return false; }
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        err = "Could not read the saved guest creature.";
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
    if (!ok) { data.clear(); err = "Invalid or incomplete guest creature archive."; }
    return ok;
}

bool store_guest_model(const std::string& key, uint32_t id, const std::vector<uint8_t>& data, std::string& err)
{
    std::wstring path = model_path(key, id);
    if (path.empty() || data.empty() || data.size() > kMaxModel) {
        err = "A saved planet and a valid guest creature are required.";
        return false;
    }
    // All instances share the directory-count check and atomic publication. Never stall the game thread.
    struct Guard {
        HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\SporeCoopDuoFounderArchive");
        DWORD state = mutex ? WaitForSingleObject(mutex, 0) : WAIT_FAILED;
        bool acquired() const { return state == WAIT_OBJECT_0 || state == WAIT_ABANDONED; }
        ~Guard() { if (acquired()) ReleaseMutex(mutex); if (mutex) CloseHandle(mutex); }
    } guard;
    if (!guard.acquired()) { err = "Another founder archive is being written. Retry in a moment."; return false; }
    std::vector<uint32_t> ids;
    if (!model_ids(key, ids, err)) return false;
    if (std::find(ids.begin(), ids.end(), id) == ids.end()) {
        if (ids.size() >= kMaxModels) { err = "This planet has reached its archived model limit."; return false; }
        if (publish(path, data)) return true;
    }
    std::vector<uint8_t> existing;
    if (!load_guest_model(key, id, existing, err)) return false;
    if (data == existing) return true;
    err = existing.empty() ? "Could not archive the guest creature in the host save." : "This planet already archives a different guest creature.";
    return false;
}
} // namespace founders
