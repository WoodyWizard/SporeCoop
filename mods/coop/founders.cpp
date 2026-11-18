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
// BUG-9135: DUO2 / CIV2: u8 seats (2..kMaxSeats), per seat u32 species, u32 faction, u64 player identity
uint32_t format2(Stage stage) { return stage == Stage::Tribe ? 0x324F5544u : 0x32564943u; }
const wchar_t* suffix(Stage stage) { return stage == Stage::Tribe ? L".duofounders" : L".duonations"; }
constexpr size_t kBytes = 5 * sizeof(uint32_t);
constexpr size_t kBytes2Max = 4 + 1 + kMaxSeats * 16;

bool valid(const Roster& v)
{
    // Seats are contiguous from seat 0; at least the host's and one partner's; nothing after the last one;
    // every seat a distinct species and faction.
    const size_t n = v.seats();
    if (n < 2) return false;
    for (size_t i = n; i < v.entries.size(); ++i)
        if (v.entries[i] != Founder{}) return false;
    for (size_t i = 0; i < n; ++i)
        for (size_t j = i + 1; j < n; ++j)
            if (v.entries[i].species == v.entries[j].species || v.entries[i].faction == v.entries[j].faction ||
                (v.entries[i].player && v.entries[i].player == v.entries[j].player)) return false;
    return true;
}
// the legacy two-seat formats hold exactly two founders and no identities
bool legacy(const Roster& v)
{
    return v.seats() == 2 && !v.entries[0].player && !v.entries[1].player;
}
// `prior` is an earlier state of `value`: the same founders in the same seats (an identity may be learnt once),
// perhaps followed by new seats
bool extends(const Roster& prior, const Roster& value)
{
    const size_t n = prior.seats();
    if (value.seats() < n) return false;
    for (size_t i = 0; i < n; ++i) {
        const Founder &a = prior.entries[i], &b = value.entries[i];
        if (a.species != b.species || a.faction != b.faction || (a.player && a.player != b.player)) return false;
    }
    return true;
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

uint64_t checkpoint_stamp(const std::string& key)
{
    WIN32_FILE_ATTRIBUTE_DATA data{};
    const auto path = path_for(key,L"");
    if (path.empty() || !GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&data)) return 0;
    return (uint64_t(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime;
}

static bool valid_conversion(uint32_t percent, const std::vector<ConversionCredit>& credit)
{
    if (percent < 100 || percent > 100u+3u*UINT16_MAX || credit.size() > 1024) return false;
    std::vector<std::pair<uint32_t,uint32_t>> ids;
    for (const auto& c : credit) {
        if (!c.city || !c.owner || !c.attacker || c.owner == c.attacker || c.remainder >= percent) return false;
        const auto id = std::pair(c.city,c.attacker);
        if (std::find(ids.begin(),ids.end(),id) != ids.end()) return false;
        ids.push_back(id);
    }
    return true;
}

bool save_conversion_credit(const std::string& key, uint32_t percent, const std::vector<ConversionCredit>& credit)
{
    auto path = path_for(key,L".duo-conversion");
    if (path.empty() || !valid_conversion(percent,credit)) return false;
    auto digest = planet_digest(key);
    if (digest.empty()) return false;
    net::Writer w;
    w.u32(0x31564343u); // CCV1
    w.u32(percent); w.u16(uint16_t(credit.size()));
    for (const auto& c : credit) { w.u32(c.city); w.u32(c.owner); w.u32(c.attacker); w.u32(c.remainder); }
    w.raw(digest.data(),digest.size());
    return publish(path,w.data,true);
}

bool read_conversion_credit(const std::string& key, uint32_t percent, std::vector<ConversionCredit>& credit)
{
    auto path = path_for(key,L".duo-conversion");
    if (path.empty()) return false;
    HANDLE f = CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ | FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    std::array<uint8_t,75+1024*16> data{};
    DWORD got = 0;
    bool ok = ReadFile(f,data.data(),DWORD(data.size()),&got,nullptr);
    CloseHandle(f);
    if (!ok || got < 74) return false;
    net::Reader r(data.data(),got);
    if (r.u32() != 0x31564343u || r.u32() != percent) return false;
    const auto count = r.u16();
    if (count > 1024 || r.left() != size_t(count)*16+64) return false;
    std::vector<ConversionCredit> saved(count);
    for (auto& c : saved) c = {r.u32(),r.u32(),r.u32(),r.u32()};
    if (!r.ok() || !valid_conversion(percent,saved) ||
        std::string((const char*)r.cur(),r.left()) != planet_digest(key)) return false;
    credit = std::move(saved);
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
    std::array<uint8_t,25> data{}; // largest format plus one byte to detect overlong files
    DWORD count = 0;
    bool ok = ReadFile(file,data.data(),DWORD(data.size()),&count,nullptr) && (count == 7 || count == 11 || count == 14 || count == 16 || count == 21 || count == 23 || count == 24);
    CloseHandle(file);
    if (!ok) return RulesFile::Unreadable;
    net::Reader r(data.data(),count);
    uint32_t format = r.u32();
    if (!((format == 0x31545346u && count == 7) || (format == 0x32545346u && count == 11) ||
          (format == 0x33545346u && count == 14) || (format == 0x34545346u && count == 16) ||
          (format == 0x35545346u && count == 21) || (format == 0x36545346u && count == 23) ||
          (format == 0x37545346u && count == 24)))
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
    if (count >= 21) {
        for (auto& seats : saved_plan.previous_stage_seats) {
            seats = r.u8();
            if (seats > 4) return RulesFile::Unreadable;
        }
    }
    if (count >= 23) saved.extra_takeover_percent = r.u16();
    // FST7 (41-dev) records whether the difficulty scales every extra (BUG-9100).
    if (count == 24) {
        saved.model = r.u8();
        if (saved.model > 1) return RulesFile::Unreadable;
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
    w.u32(0x37545346u); // FST7
    w.u8(rules.seats);
    w.u16(rules.difficulty_percent);
    w.u16(rules.extra_health_percent);
    w.u16(rules.extra_damage_percent);
    w.u8(plan.protected_seats);
    w.u8(plan.next_seats);
    w.u8(plan.queued_stage);
    w.u16(rules.extra_hunger_basis_points);
    for (auto seats : plan.previous_stage_seats) w.u8(seats);
    w.u16(rules.extra_takeover_percent);
    w.u8(rules.model);
    return publish(path,w.data,replace);
}

bool queue_seats(const std::string& key, uint8_t seats, uint8_t stage, std::string& error)
{
    RuleWriteLock lock;
    if (!lock.held) { error = "Player slot settings are busy. Try again."; return false; }
    CampaignRules rules;
    SeatPlan plan;
    if (!stage || stage > 5 || read_rules(key,rules,&plan) != RulesFile::Saved) {
        error = "Open a saved, reserved campaign in a regular stage.";
        return false;
    }
    if (seats < plan.protected_seats || seats > 4) {
        error = "This campaign keeps " + std::to_string(plan.protected_seats) + " player slot(s) for players who joined. Choose up to four.";
        return false;
    }
    const auto previous = plan;
    plan.next_seats = seats == plan.seats_in(stage,rules.seats) ? 0 : seats;
    plan.queued_stage = plan.next_seats ? stage : 0;
    if (plan == previous || write_rules(path_for(key,L".duo-seats"),rules,plan,true)) return true;
    error = "Could not save the player slot change.";
    return false;
}

bool raise_seats(const std::string& key, uint8_t seats, uint8_t stage, std::string& error)
{
    RuleWriteLock lock;
    if (!lock.held) { error = "Player slot settings are busy. Try again."; return false; }
    CampaignRules rules;
    SeatPlan plan;
    if (!stage || stage > 4 || read_rules(key,rules,&plan) != RulesFile::Saved) {
        error = "Open a saved, reserved campaign in Cell, Creature, Tribal or Civilization.";
        return false;
    }
    if (!raise_now(rules,plan,stage,seats)) {
        error = "This campaign already has " + std::to_string(plan.seats_in(stage,rules.seats)) +
                " player slot(s). More slots apply at once; fewer apply from the next stage.";
        return false;
    }
    if (write_rules(path_for(key,L".duo-seats"),rules,plan,true)) return true;
    error = "Could not save the player slot change.";
    return false;
}

bool update_seats(const std::string& key, uint8_t occupied, uint8_t stage, bool space_reloaded,
                  CampaignRules& rules, SeatPlan& plan, std::string& error)
{
    RuleWriteLock lock;
    if (!lock.held) { error = "Player slot settings are busy. Try again."; return false; }
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
        error = "Could not save the player slots.";
        return false;
    }
    rules.seats = plan.seats_in(stage,rules.seats);
    return true;
}

bool reserve(const std::string& key, const CampaignRules& requested, std::string& err)
{
    RuleWriteLock lock;
    if (!lock.held) { err = "Player slot settings are busy. Try again."; return false; }
    auto path = path_for(key,L".duo-seats");
    if (path.empty() || !requested.seats || requested.seats > 4) {
        err = "A saved planet and one to four player slots are required.";
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
    std::array<uint8_t, kBytes2Max + 1> bytes{};
    DWORD got = 0;
    bool ok = ReadFile(f, bytes.data(), (DWORD)bytes.size(), &got, nullptr) && got <= kBytes2Max;
    CloseHandle(f);
    if (!ok || got < 4) return false;
    net::Reader r(bytes.data(), got);
    const uint32_t magic = r.u32();
    Roster value;
    if (magic == format(stage) && got == kBytes) {
        value = Roster{r.u32(), r.u32(), r.u32(), r.u32()};
    } else if (magic == format2(stage)) { // BUG-9135
        const unsigned n = r.u8();
        if (n < 2 || n > kMaxSeats || got != 5 + n * 16) return false;
        for (unsigned i = 0; i < n; ++i) {
            value.entries[i].species = r.u32();
            value.entries[i].faction = r.u32();
            value.entries[i].player = r.u64();
        }
        if (value.seats() != n) return false;
    } else {
        return false;
    }
    if (!r.ok() || r.left() || !valid(value)) return false;
    out = value;
    return true;
}

bool can_record(const std::string& key, const Roster& value, std::string& err, Stage stage)
{
    std::wstring path = path_for(key, suffix(stage));
    if (path.empty() || !valid(value)) {
        err = "A saved planet and distinct native factions for every founder are required.";
        return false;
    }
    if (stage == Stage::Civilization) {
        Roster tribe;
        Outfits outfits;
        auto previous = path_for(key, suffix(Stage::Tribe));
        bool match = GetFileAttributesW(previous.c_str()) == INVALID_FILE_ATTRIBUTES;
        // every nation's species descends from its seat's tribal founder; a seat past the Tribe roster is a late
        // joiner's, with its player's identity and a species of its own (BUG-9135, roadmap phase 4)
        if (!match && read(key, tribe) && read_outfits(key, outfits, err))
            match = nations_descend(tribe, outfits, value);
        if (!match) {
            err = "The selected species do not match the saved tribal founders.";
            return false;
        }
    }
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND) return true;
    Roster prior;
    // BUG-9135: the same founders, perhaps with an identity learnt or further partners' seats added
    if (read(key, prior, stage) && extends(prior, value)) return true;
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
    if (legacy(value)) { // byte for byte as before: an older Lineages keeps reading a two-founder campaign
        for (uint32_t n : {format(stage), value.home().species, value.guest().species, value.home().faction, value.guest().faction})
            w.u32(n);
    } else { // BUG-9135
        const size_t n = value.seats();
        w.u32(format2(stage));
        w.u8((uint8_t)n);
        for (size_t i = 0; i < n; ++i) {
            w.u32(value.entries[i].species);
            w.u32(value.entries[i].faction);
            w.u64(value.entries[i].player);
        }
    }
    if (publish(path, w.data, prior.seats() != 0)) return true;
    if (read(key, prior, stage) && prior == value) return true;
    err = "Could not save the founding species. Check write access to the co-op save.";
    return false;
}

std::string describe(const Roster& v)
{
    char line[96];
    snprintf(line, sizeof line, "Founding species: host %08X", v.home().species);
    std::string out = line;
    for (size_t i = 1; i < v.seats(); ++i) {
        snprintf(line, sizeof line, ", partner %zu %08X", i, v.entries[i].species);
        out += line;
    }
    return out;
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
    const size_t seats = roster.seats();
    Outfits value;
    for (size_t i = 0; i < seats; ++i)
        value.species[i] = {roster.entries[i].species};
    auto path = path_for(key, L".duooutfits");
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        if (GetLastError() != ERROR_FILE_NOT_FOUND) { err = "Could not read founder outfit history."; return false; }
        out = std::move(value); return true;
    }
    // OFT1: four original identity words (seats 0 / 1), two counts, at most 16 keys per seat.
    // OFT2 (BUG-9135): u8 seats, per seat its species and faction, then per seat a count and its keys. A file written
    // before further partners were founded covers fewer seats: theirs start with the founding species.
    std::array<uint8_t, 4 + 1 + kMaxSeats * 8 + kMaxSeats * (4 + 16 * 4) + 1> bytes{};
    DWORD got = 0;
    bool ok = ReadFile(file, bytes.data(), (DWORD)bytes.size(), &got, nullptr) && got < bytes.size();
    CloseHandle(file);
    net::Reader r(bytes.data(), got);
    const uint32_t magic = ok ? r.u32() : 0;
    size_t stored = 0;
    if (magic == 0x3154464Fu) {
        stored = 2;
        ok = r.u32() == roster.home().species && r.u32() == roster.guest().species && r.u32() == roster.home().faction &&
             r.u32() == roster.guest().faction;
    } else if (magic == 0x3254464Fu) {
        stored = r.u8();
        ok = stored >= 2 && stored <= seats;
        for (size_t i = 0; i < stored && ok; ++i)
            ok = r.u32() == roster.entries[i].species && r.u32() == roster.entries[i].faction;
    } else {
        ok = false;
    }
    for (size_t seat = 0; seat < stored && ok; ++seat) {
        unsigned count = r.u32();
        if (!count || count > 16 || r.left() < count * 4) { ok = false; break; }
        value.species[seat].clear();
        for (unsigned i = 0; i < count && ok; ++i) {
            uint32_t id = r.u32();
            bool clash = !id || value.contains(seat, id);
            for (size_t other = 0; other < seat && !clash; ++other)
                clash = value.contains(other, id);
            if (clash) { ok = false; break; }
            value.species[seat].push_back(id);
        }
        if (value.species[seat].empty() || value.species[seat][0] != roster.entries[seat].species) ok = false;
    }
    if (!ok || !r.ok() || r.left()) { err = "Invalid founder outfit history; the original roster was retained."; return false; }
    out = std::move(value); return true;
}

bool record_outfit(const std::string& key, size_t seat, uint32_t expected, uint32_t model, std::string& err)
{
    if (seat >= kMaxSeats || !expected || !model) { err = "Invalid founder outfit change."; return false; }
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
    const size_t seats = roster.seats();
    if (seat >= seats) { err = "Invalid founder outfit change."; return false; }
    if (value.current(seat) != expected) { err = "This outfit result is older than the current founder."; return false; }
    if (model == expected) return true;
    bool other = false;
    for (size_t i = 0; i < seats && !other; ++i)
        other = i != seat && value.contains(i, model);
    if (other || value.contains(seat, model) || value.species[seat].size() >= 16) {
        err = "Outfit history is full, repeated, or belongs to another founder."; return false;
    }
    value.species[seat].push_back(model);
    net::Writer w;
    if (seats == 2) { // OFT1, byte for byte as before
        for (uint32_t n : {0x3154464Fu, roster.home().species, roster.guest().species, roster.home().faction, roster.guest().faction})
            w.u32(n);
    } else { // OFT2 (BUG-9135)
        w.u32(0x3254464Fu);
        w.u8((uint8_t)seats);
        for (size_t i = 0; i < seats; ++i) {
            w.u32(roster.entries[i].species);
            w.u32(roster.entries[i].faction);
        }
    }
    for (size_t i = 0; i < seats; ++i) {
        w.u32((uint32_t)value.species[i].size());
        for (uint32_t n : value.species[i]) w.u32(n);
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

bool read_stand_ins(const std::string& key, std::vector<uint32_t>& keys)
{
    keys.clear();
    std::wstring path = path_for(key, L".duostandins");
    if (path.empty()) return false;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND;
    uint8_t data[4 * kMaxSeats + 1];
    DWORD got = 0;
    const bool ok = ReadFile(f, data, sizeof data, &got, nullptr) && got % 4 == 0 && got <= 4 * kMaxSeats;
    CloseHandle(f);
    if (!ok) return false;
    for (DWORD i = 0; i + 4 <= got; i += 4) {
        const uint32_t k = data[i] | (data[i + 1] << 8) | (data[i + 2] << 16) | ((uint32_t)data[i + 3] << 24);
        if ((k >> 24) != 0x7C) { keys.clear(); return false; }
        keys.push_back(k);
    }
    return true;
}

bool add_stand_in(const std::string& key, uint32_t id, std::string& err)
{
    std::vector<uint32_t> keys;
    std::wstring path = path_for(key, L".duostandins");
    if (path.empty() || (id >> 24) != 0x7C || !read_stand_ins(key, keys)) {
        err = "Could not read this planet's stand-in creatures.";
        return false;
    }
    if (std::find(keys.begin(), keys.end(), id) != keys.end()) return true;
    if (keys.size() >= kMaxSeats) { err = "This planet has too many stand-in creatures."; return false; }
    keys.push_back(id);
    std::vector<uint8_t> data;
    for (uint32_t k : keys)
        for (int b = 0; b < 4; ++b) data.push_back((uint8_t)(k >> (8 * b)));
    if (publish(path, data, true)) return true;
    err = "Could not save this planet's stand-in creatures.";
    return false;
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
// BUG-9135 / roadmap 0b
static bool party_valid(const Party& v)
{
    if (v.seats.empty() || v.seats.size() > kMaxSeats) return false;
    for (size_t i = 0; i < v.seats.size(); ++i) {
        if (v.seats[i].name.size() > 64) return false;
        for (size_t j = i + 1; j < v.seats.size(); ++j)
            if (v.seats[i].player && v.seats[i].player == v.seats[j].player) return false;
    }
    return true;
}

bool read_side(const std::string& key, const wchar_t* suffix, std::vector<uint8_t>& out, size_t max_bytes)
{
    auto path = path_for(key, suffix);
    if (path.empty()) return false;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
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
    auto path = path_for(key, L".duoparty");
    if (path.empty()) return false;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    std::array<uint8_t, 5 + kMaxSeats * (8 + 4 + 1 + 64) + 1> bytes{};
    DWORD got = 0;
    bool ok = ReadFile(f, bytes.data(), (DWORD)bytes.size(), &got, nullptr) && got < bytes.size();
    CloseHandle(f);
    if (!ok) return false;
    net::Reader r(bytes.data(), got);
    if (r.u32() != 0x31595450u) return false; // PTY1
    Party value;
    const unsigned n = r.u8();
    for (unsigned i = 0; i < n && r.ok(); ++i) {
        PartySeat s;
        s.player = r.u64();
        s.species = r.u32();
        s.name = r.str8();
        value.seats.push_back(std::move(s));
    }
    if (!r.ok() || r.left() || value.seats.size() != n || !party_valid(value)) return false;
    out = std::move(value);
    return true;
}

bool record_party(const std::string& key, const Party& value, std::string& err)
{
    auto path = path_for(key, L".duoparty");
    if (path.empty() || !party_valid(value)) { err = "A saved planet and a valid party are required."; return false; }
    Party prior;
    const bool had = read_party(key, prior);
    if (!had && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) {
        err = "This planet's party file is unreadable; it was left as it is."; return false;
    }
    if (had) {
        if (prior == value) return true;
        if (value.seats.size() < prior.seats.size()) { err = "A party never loses a seat."; return false; }
        for (size_t i = 0; i < prior.seats.size(); ++i)
            if (prior.seats[i].player && prior.seats[i].player != value.seats[i].player) {
                err = "A party seat belongs to another player."; return false;
            }
    }
    net::Writer w;
    w.u32(0x31595450u);
    w.u8((uint8_t)value.seats.size());
    for (const auto& s : value.seats) {
        w.u64(s.player);
        w.u32(s.species);
        w.str8(s.name);
    }
    if (publish(path, w.data, had)) return true;
    err = "Could not save the party. Check write access to the co-op save.";
    return false;
}

} // namespace founders
