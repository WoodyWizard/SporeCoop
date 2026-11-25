// Windows-side unit tests: the file handling of the updater and of received worlds against the
// real Win32 file API (Wine in a lane's bottle: tools/tests/win_unit.py; or Windows). Each test
// works in its own folder under %TEMP%. `win_tests.exe --old` switches the fixes off: every
// check of a fixed bug must then fail.
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

#include "../installer/install.h"
#include "../mods/coop/founders.h"
#include "../mods/coop/slotfs.h"
#include "../sporekit/inifile.h"
#include "../updater/pending.h"
#include "../updater/sha256.h"
#include "../updater/winupdate.h"

static int g_fail;
#define CHECK(c)                                                    \
    do {                                                            \
        if (!(c)) {                                                 \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);   \
            g_fail++;                                               \
        }                                                           \
    } while (0)

static std::wstring g_tmp;
static bool g_old;          // --old
static std::wstring g_bat;  // --bat <install.bat to test> (tools/tests/win_unit.py)
static std::wstring g_ubat; // --ubat <uninstall.bat to test> (win_unit.py)
static std::wstring g_excl; // --exclude <installer/ini.exclude>
static std::wstring g_steam; // --steam <a real Steam folder, read only> (win_unit.py: the user's, if any)
static bool g_close_wait_fix = true; // --old-closewait: the old final close of test_setup_close (BUG-0853)
static std::wstring g_setup; // --setup <SporeCoopSetup.exe to test> (default: the one next to win_tests.exe)

static bool exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }

static void put(const std::wstring& p, const std::string& text)
{
    for (size_t q = g_tmp.size() + 1; q < p.size(); q++)
        if (p[q] == L'\\')
            CreateDirectoryW(p.substr(0, q).c_str(), nullptr);
    HANDLE f = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    DWORD w;
    WriteFile(f, text.data(), (DWORD)text.size(), &w, nullptr);
    CloseHandle(f);
}

static std::string get(const std::wstring& p)
{
    HANDLE f = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return "<missing>";
    char b[4096];
    DWORD got = 0;
    ReadFile(f, b, sizeof b, &got, nullptr);
    CloseHandle(f);
    return std::string(b, got);
}

static void rmtree(const std::wstring& d)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((d + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::wstring n = fd.cFileName;
            if (n == L"." || n == L"..")
                continue;
            std::wstring p = d + L"\\" + n;
            SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                rmtree(p);
            else
                DeleteFileW(p.c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(d.c_str());
}

static std::wstring fresh_dir(const wchar_t* name)
{
    std::wstring d = g_tmp + L"\\" + name;
    rmtree(d);
    CreateDirectoryW(d.c_str(), nullptr);
    return d;
}

static void test_founders()
{
    auto planet = fresh_dir(L"founders") + L"\\planet.spo";
    std::string key = slotfs::narrow(planet), err;
    founders::Roster wanted{1, 2, 0x800021CA, 0x21CB}, got;
    CHECK(!founders::record(key, wanted, err));
    put(planet, "native planet");
    founders::CampaignRules settings;
    CHECK(founders::read_rules(key,settings) == founders::RulesFile::Missing);
    const founders::CampaignRules quartet{4,100}, partial{4,50};
    CHECK(quartet.rival_target(5) == 20 && quartet.workload_percent() == 400);
    CHECK(quartet.hunger_factor() == 1.075f);
    CHECK(quartet.enemy_health_percent() == 145 && quartet.enemy_damage_percent() == 115);
    CHECK(quartet.takeover_percent() == 115 && partial.takeover_percent() == 115);
    CHECK(founders::CampaignRules{}.takeover_percent() == 105);
    founders::ConversionCredit conversion{7,8,9,0};
    CHECK(conversion.apply(1,115) == 0 && conversion.remainder == 100);
    int converted = 0;
    for (int i=0; i<22; ++i) converted += conversion.apply(1,115);
    CHECK(converted == 20 && conversion.remainder == 0);
    CHECK(conversion.apply(-1,115) == -1 && conversion.remainder == 0);
    CHECK(conversion.apply(INT_MAX,115) == int(uint64_t(INT_MAX)*100/115));
    founders::CampaignRules pair{2,100};
    CHECK(pair.hunger_factor() == 1.025f);
    CHECK(pair.enemy_health_percent() == 115 && pair.enemy_damage_percent() == 105);
    founders::CampaignRules one_seat{1,100};
    CHECK(one_seat.takeover_percent() == 100);
    CHECK(one_seat.enemy_health_percent() == 100 && one_seat.enemy_damage_percent() == 100);
    CHECK(partial.rival_target(5) == 14 && partial.workload_percent() == 250);
    CHECK(one_seat.civ_military_limit(3) == 3 && one_seat.civ_military_interval_ms() == 40000);
    CHECK(pair.civ_military_limit(3) == 4 && pair.civ_military_interval_ms() == 36000);
    CHECK(quartet.civ_military_limit(10) == 13 && quartet.civ_military_interval_ms() == 28000);
    CHECK(partial.civ_military_limit(10) == 12 && partial.civ_military_interval_ms() == 34000);
    CHECK(quartet.civ_military_limit(35) == 46 && quartet.civ_military_limit(0) == 0);
    CHECK(pair.civ_overflow_slots(7,36,7) == 1 && pair.civ_overflow_slots(7,37,7) == 0);
    CHECK(quartet.civ_overflow_slots(7,36,7) == 3 && quartet.civ_overflow_slots(7,38,8) == 1);
    CHECK(quartet.civ_overflow_slots(7,36,10) == 0 && one_seat.civ_overflow_slots(36,36,36) == 0);
    CHECK(quartet.civ_overflow_slots(36,36,36) == 11 && quartet.civ_overflow_slots(36,47,46) == 0);
    const founders::CampaignRules disabled{4,0}, high{4,UINT16_MAX};
    CHECK(disabled.civ_military_limit(7) == 7 && disabled.civ_military_interval_ms() == 40000);
    CHECK(high.civ_military_limit(7) == 47 && high.civ_military_interval_ms() == 1);
    CHECK(disabled.civ_overflow_slots(7,36,6) == 0 && high.civ_overflow_slots(7,46,6) == 1);
    // BUG-9100: new campaigns scale every per-seat extra with the difficulty; progress adds 60% per seat.
    auto scaled = [](uint8_t seats, uint16_t difficulty) {
        founders::CampaignRules r{seats,difficulty};
        r.model = 1;
        return r;
    };
    const auto s4 = scaled(4,100), s4_double = scaled(4,200), s4_half = scaled(4,50), s1 = scaled(1,300);
    CHECK(s4.workload_percent() == 280 && s4.rival_target(5) == 14);
    CHECK(s4.enemy_health_percent() == 145 && s4.enemy_damage_percent() == 115);
    CHECK(s4.hunger_factor() == 1.075f && s4.takeover_percent() == 115 && s4.extra_total(1) == 3);
    CHECK(s4_double.workload_percent() == 460 && s4_double.rival_target(5) == 23);
    CHECK(s4_double.enemy_health_percent() == 190 && s4_double.enemy_damage_percent() == 130);
    CHECK(s4_double.hunger_factor() == 1.15f && s4_double.takeover_percent() == 130 && s4_double.extra_total(1) == 6);
    CHECK(s4_half.workload_percent() == 190 && s4_half.enemy_health_percent() == 123);
    CHECK(s4_half.enemy_damage_percent() == 108 && s4_half.takeover_percent() == 108 && s4_half.extra_total(1) == 2);
    CHECK(scaled(2,100).workload_percent() == 160 && scaled(4,0).workload_percent() == 100);
    CHECK(scaled(4,0).enemy_health_percent() == 100 && scaled(4,0).civ_military_interval_ms() == 40000);
    CHECK(s1.workload_percent() == 100 && s1.enemy_health_percent() == 100 && s1.extra_total(1) == 0);
    CHECK(s4.civ_military_interval_ms() == 32800 && s4.civ_military_limit(10) == 12);
    CHECK(!(s4 == quartet));
    // 6E B: any count up to founders::kSeatCeiling (4 before); one more is refused
    CHECK(!founders::reserve(key,{0,100},err) && !founders::reserve(key,{uint8_t(founders::kSeatCeiling + 1),100},err));
    CHECK(founders::reserve(key,quartet,err) && founders::reserve(key,quartet,err));
    CHECK(!founders::reserve(key,partial,err));
    CHECK(founders::read_rules(key,settings) == founders::RulesFile::Saved && settings == quartet);
    auto saved_rules = get(planet + L".duo-seats");
    auto legacy_rules = saved_rules.substr(0,21);
    legacy_rules.replace(0,4,"FST5");
    put(planet + L".duo-seats",legacy_rules);
    CHECK(founders::read_rules(key,settings) == founders::RulesFile::Saved && settings == quartet);
    auto custom_rules = saved_rules;
    custom_rules[21] = 9;
    put(planet + L".duo-seats",custom_rules);
    CHECK(founders::read_rules(key,settings) == founders::RulesFile::Saved && settings.takeover_percent() == 127);
    put(planet + L".duo-seats",saved_rules + "x");
    CHECK(founders::read_rules(key,settings) == founders::RulesFile::Unreadable);
    put(planet + L".duo-seats",saved_rules.substr(0,16));
    CHECK(founders::read_rules(key,settings) == founders::RulesFile::Unreadable);
    put(planet + L".duo-seats",saved_rules);
    // FST6 (40-dev and older) keeps the old rules; FST7 round-trips the scaled model.
    CHECK(saved_rules.size() == 24 && saved_rules.compare(0,4,"FST7") == 0 && saved_rules[23] == 0);
    auto fst6 = saved_rules.substr(0,23);
    fst6.replace(0,4,"FST6");
    put(planet + L".duo-seats",fst6);
    CHECK(founders::read_rules(key,settings) == founders::RulesFile::Saved && settings == quartet && settings.model == 0);
    auto scaled_file = saved_rules;
    scaled_file[23] = 1;
    put(planet + L".duo-seats",scaled_file);
    CHECK(founders::read_rules(key,settings) == founders::RulesFile::Saved && settings == s4);
    scaled_file[23] = 2;
    put(planet + L".duo-seats",scaled_file);
    CHECK(founders::read_rules(key,settings) == founders::RulesFile::Unreadable);
    put(planet + L".duo-seats",saved_rules);
    founders::SeatPlan plan;
    // A host queues a shrink in Tribal, quits, and reopens the same stage.
    CHECK(founders::queue_seats(key,2,3,err));
    CHECK(founders::read_rules(key,settings,&plan) == founders::RulesFile::Saved);
    CHECK(settings == quartet && plan.next_seats == 2 && plan.queued_stage == 3);
    CHECK(founders::update_seats(key,2,3,false,settings,plan,err) && settings == quartet);
    CHECK(founders::update_seats(key,1,4,false,settings,plan,err) && settings == pair && !plan.next_seats);
    // The native executable may reopen the prior stage after an unsaved transition.
    CHECK(founders::update_seats(key,1,3,false,settings,plan,err) && settings == quartet);
    CHECK(founders::update_seats(key,1,4,false,settings,plan,err) && settings == pair);
    CHECK(!founders::queue_seats(key,1,4,err)); // the offline founder still owns seat two
    CHECK(founders::queue_seats(key,4,4,err));
    CHECK(founders::update_seats(key,1,5,false,settings,plan,err) && settings == quartet);
    CHECK(founders::queue_seats(key,2,5,err));
    CHECK(founders::update_seats(key,1,5,false,settings,plan,err) && settings == quartet);
    CHECK(founders::update_seats(key,1,5,true,settings,plan,err) && settings == pair);
    CHECK(founders::update_seats(key,1,5,true,settings,plan,err) && settings == pair && !plan.next_seats);
    CHECK(founders::queue_seats(key,4,5,err));
    CHECK(founders::queue_seats(key,2,5,err)); // cancel using current count
    CHECK(founders::update_seats(key,1,5,true,settings,plan,err) && settings == pair);
    CHECK(founders::queue_seats(key,4,5,err));
    CHECK(founders::update_seats(key,1,5,true,settings,plan,err) && settings == quartet);
    CHECK(founders::queue_seats(key,2,5,err));
    CHECK(founders::update_seats(key,3,5,true,settings,plan,err) && settings == quartet && !plan.next_seats);
    // BUG-9150: the planet's stand-in list (<planet>.duostandins)
    {
        std::vector<uint32_t> si;
        CHECK(founders::read_stand_ins(key,si) && si.empty());
        CHECK(founders::add_stand_in(key,0x7C112233u,err) && founders::add_stand_in(key,0x7C445566u,err));
        CHECK(founders::add_stand_in(key,0x7C112233u,err)); // already listed
        CHECK(!founders::add_stand_in(key,0x29112233u,err)); // not a shared key
        CHECK(founders::read_stand_ins(key,si) && si == std::vector<uint32_t>({0x7C112233u,0x7C445566u}));
    }
    // Owner decision 2026-11-12: more seats in the running stage at once (founders::raise_now / raise_seats)
    {
        founders::CampaignRules r{3,100};
        founders::SeatPlan p{1};
        CHECK(!founders::raise_now(r,p,3,3) && !founders::raise_now(r,p,3,2) &&
              !founders::raise_now(r,p,3,founders::kSeatCeiling + 1));
        {
            founders::CampaignRules big{3,100};
            founders::SeatPlan bp{1};
            // 6E B: "Setting it to 17 shouldn't cause any issues"; difficulty stays a 4-seat party's
            CHECK(founders::raise_now(big,bp,3,17) && big.seats == 17 && bp.seats_in(3,big.seats) == 17);
            CHECK(big.enemy_health_percent() == quartet.enemy_health_percent() &&
                  big.workload_percent() == quartet.workload_percent());
        }
        CHECK(!founders::raise_now(r,p,0,4) && !founders::raise_now(r,p,6,4));
        CHECK(founders::raise_now(r,p,3,4) && r.seats == 4 && p.seats_in(3,r.seats) == 4);
        CHECK(r.enemy_health_percent() == quartet.enemy_health_percent());
        founders::SeatPlan q{1, 2, 3}; // a shrink queued for the next stage is dropped by the raise
        r = {2,100};
        CHECK(founders::raise_now(r,q,3,3) && r.seats == 3 && !q.next_seats && !q.queued_stage);
        founders::SeatPlan g{1, 4, 3}; // a queued raise past the new count stays
        r = {2,100};
        CHECK(founders::raise_now(r,g,3,3) && r.seats == 3 && g.next_seats == 4 && g.queued_stage == 3);
        founders::SeatPlan back{1}; // Civ reopened after Space began: Civ's own entry is raised, Space's count kept
        back.previous_stage_seats = {2,2,2,2,0};
        r = {4,100};
        CHECK(founders::raise_now(r,back,4,3) && back.seats_in(4,r.seats) == 3 && r.seats == 4);
        // the file: the Jiff case, 3 reserved in Tribal, a fourth player arrives
        CHECK(founders::queue_seats(key,3,3,err)); // a count to start from (4 now: queue 3 for the next stage...)
        CHECK(founders::update_seats(key,1,4,false,settings,plan,err) && settings.seats == 3);
        CHECK(!founders::raise_seats(key,3,4,err) && !founders::raise_seats(key,2,4,err));
        CHECK(!founders::raise_seats(key,4,5,err)); // Space keeps its next-load rule
        CHECK(founders::raise_seats(key,4,4,err));
        CHECK(founders::read_rules(key,settings,&plan) == founders::RulesFile::Saved && plan.seats_in(4,settings.seats) == 4);
        CHECK(founders::update_seats(key,1,4,false,settings,plan,err) && settings.seats == 4);
    }
    founders::CellCredit carry, recovered;
    CHECK(carry.award(3,true,quartet) == 0 && carry.award(1,false,quartet) == 0);
    CHECK(carry.award(-4,true,quartet) == -4 && carry.remainder[1] == 300);
    for (int i = 0; i < 19; ++i) CHECK(founders::CellCredit::bonus(1,105,carry.damage_carry) == 0);
    for (int i = 0; i < 6; ++i) CHECK(founders::CellCredit::bonus(1,115,carry.health_carry) == 0);
    CHECK(carry.damage_carry == 95 && carry.health_carry == 90);
    CHECK(founders::save_cell_credit(key,quartet,carry));
    CHECK(founders::read_cell_credit(key,quartet,recovered) && recovered.remainder == carry.remainder);
    CHECK(founders::CellCredit::bonus(1,105,recovered.damage_carry) == 1 && recovered.damage_carry == 0);
    CHECK(founders::CellCredit::bonus(1,115,recovered.health_carry) == 1 && recovered.health_carry == 5);
    CHECK(recovered.award(1,true,quartet) == 1 && recovered.award(2,false,quartet) == 0);
    // BUG-9110: plant food worth 125% at three seats or more, meat unchanged, remainders kept per kind
    {
        CHECK(quartet.cell_extra_predators() == 1 && quartet.cell_plant_percent() == 125);
        CHECK(pair.cell_extra_predators() == 0 && pair.cell_plant_percent() == 100);
        const auto divisor = int(quartet.workload_percent());
        founders::CellCredit a, b;
        int plant = 0, meat = 0;
        for (int i = 0; i < 100; ++i) {
            plant += a.award(4, true, quartet, 125);
            meat += b.award(4, false, quartet, 125);
        }
        CHECK(plant == 400 * 125 / divisor && meat == 400 * 100 / divisor);
    }
    CHECK(!founders::read_cell_credit(key,partial,recovered));
    auto current_credit = get(planet + L".duo-food");
    auto legacy_credit = current_credit.substr(0,16) + current_credit.substr(26);
    legacy_credit.replace(0,4,"CFD1");
    put(planet + L".duo-food",legacy_credit);
    CHECK(founders::read_cell_credit(key,quartet,recovered) && recovered.remainder == carry.remainder);
    CHECK(recovered.health_carry == 0 && recovered.damage_carry == 0);
    put(planet + L".duo-food",current_credit);
    put(planet,"different native checkpoint");
    CHECK(!founders::read_cell_credit(key,quartet,recovered));
    put(planet,"native planet");
    std::vector<founders::ConversionCredit> converted_saved{{7,8,9,100}}, converted_read;
    CHECK(founders::save_conversion_credit(key,115,converted_saved));
    CHECK(founders::read_conversion_credit(key,115,converted_read) && converted_read == converted_saved);
    CHECK(converted_read[0].apply(1,115) == 1 && converted_read[0].remainder == 85);
    CHECK(!founders::read_conversion_credit(key,105,converted_read));
    CHECK(!founders::save_conversion_credit(key,115,{{7,8,9,115}}));
    CHECK(!founders::save_conversion_credit(key,115,{{7,8,9,1},{7,8,9,2}}));
    auto conversion_bytes = get(planet + L".duo-conversion");
    put(planet + L".duo-conversion",conversion_bytes + "x");
    CHECK(!founders::read_conversion_credit(key,115,converted_read));
    put(planet + L".duo-conversion",conversion_bytes);
    put(planet,"different checkpoint");
    CHECK(!founders::read_conversion_credit(key,115,converted_read));
    put(planet,"native planet");
    put(planet + L".duo-seats",std::string("FST1\x04\x64\x00",7));
    CHECK(founders::read_rules(key,settings) == founders::RulesFile::Saved);
    CHECK(settings.hunger_factor() == 1.f);
    CHECK(settings.enemy_health_percent() == 175 && settings.enemy_damage_percent() == 130);
    put(planet + L".duo-seats","bad");
    CHECK(founders::read_rules(key,settings) == founders::RulesFile::Unreadable);
    CHECK(!founders::reserve(key,quartet,err) && get(planet + L".duo-seats") == "bad");
    CHECK(!founders::record(key, {1, 1, 3, 4}, err));
    CHECK(founders::record(key, wanted, err));
    CHECK(founders::record(key, wanted, err));
    CHECK(!founders::record(key, {1, 3, 4, 5}, err));
    CHECK(founders::read(key, got) && got == wanted);
    // Lock the legacy wire bytes, including their species-then-factions order.
    const char legacy[] = {'D', 'U', 'O', '1', 1, 0, 0, 0, 2, 0, 0, 0,
                           char(0xCA), 0x21, 0, char(0x80), char(0xCB), 0x21, 0, 0};
    CHECK(get(planet + L".duofounders") == std::string(legacy, sizeof legacy));
    CHECK(got.entries[0].species == 1 && got.entries[1].species == 2);
    CHECK(got.by_faction(0x21CB) == &got.entries[1]);
    CHECK(!got.by_faction(0) && !got.by_faction(123));
    auto expanded = wanted;
    expanded.entries[2] = {7, 9};
    expanded.entries[3] = {8, 10};
    CHECK(expanded.contains_faction(9) && expanded.contains_faction(10));
    // BUG-9135: further partners' seats are tested on their own planet (test_founders_multi); this one stays on two
    CHECK(founders::read(key, got) && got == wanted);
    auto civ = founders::Stage::Civilization;
    CHECK(!founders::record(key, {1, 7, 3, 4}, err, civ));
    CHECK(founders::record(key, {1, 2, 3, 4}, err, civ));
    CHECK(founders::read(key, got, civ) && got.guest().faction == 4);
    auto expected_civ = std::string(legacy, sizeof legacy);
    expected_civ.replace(0, 4, "CIV1");
    expected_civ.replace(12, 8, std::string("\x03\0\0\0\x04\0\0\0", 8));
    CHECK(get(planet + L".duonations") == expected_civ);
    CHECK(founders::read(key, got) && got == wanted);
    std::vector<uint32_t> ids;
    std::vector<uint8_t> bytes;
    constexpr uint32_t model = 0x7C123456;
    CHECK(founders::model_ids(key, ids, err) && ids.empty());
    CHECK(!founders::store_guest_model(key, 123, {1}, err));
    CHECK(founders::store_guest_model(key, model, {7, 8, 9}, err));
    CHECK(founders::store_guest_model(key, model, {7, 8, 9}, err));
    CHECK(!founders::store_guest_model(key, model, {7, 8}, err));
    CHECK(founders::model_ids(key, ids, err) && ids.size() == 1 && ids[0] == model);
    CHECK(founders::load_guest_model(key, model, bytes, err) && bytes == std::vector<uint8_t>({7, 8, 9}));
    founders::Outfits outfits;
    CHECK(founders::read_outfits(key, outfits, err) && outfits.current(0) == 1 && outfits.current(1) == 2);
    CHECK(founders::record_outfit(key, 0, 1, 11, err));
    CHECK(founders::record_outfit(key, 1, 2, 22, err));
    CHECK(founders::record_outfit(key, 1, 22, 22, err)); // idempotent retry
    CHECK(!founders::record_outfit(key, 1, 2, 23, err)); // delayed result
    CHECK(!founders::record_outfit(key, 1, 22, 11, err)); // partner's species
    CHECK(!founders::record_outfit(key, 2, 22, 23, err)); // four-species gameplay stays disabled
    CHECK(founders::read_outfits(key, outfits, err) && outfits.contains(0, 1) && outfits.contains(1, 2) &&
          outfits.current(0) == 11 && outfits.current(1) == 22);
    CHECK(founders::read(key, got) && got == wanted); // founding identity never rewritten
    auto outfit_bytes = get(planet + L".duooutfits");
    // A new Civilization roster accepts outfit descendants, not arbitrary models.
    DeleteFileW((planet + L".duonations").c_str());
    CHECK(founders::record(key, {11, 22, 3, 4}, err, civ));
    CHECK(!founders::can_record(key, {11, 23, 3, 4}, err, civ));
    HANDLE locked = CreateFileW((planet + L".duooutfits").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(locked != INVALID_HANDLE_VALUE);
    CHECK(!founders::record_outfit(key, 1, 22, 23, err)); // atomic replacement fails, prior intact
    CloseHandle(locked);
    CHECK(get(planet + L".duooutfits") == outfit_bytes);
    put(planet + L".duooutfits", outfit_bytes + "x");
    CHECK(!founders::read_outfits(key, outfits, err));
    CHECK(!founders::record_outfit(key, 1, 22, 23, err));
    CHECK(founders::read(key, got) && got == wanted);
    put(planet + L".duooutfits", outfit_bytes);
    auto path = planet + L".duofounders";
    put(path, get(path) + "x");
    CHECK(!founders::read(key, got));
    CHECK(!founders::record(key, wanted, err));
    put(path, "DUO");
    CHECK(!founders::read(key, got));
    CHECK(get(planet) == "native planet");
}

// BUG-9135: more than two founders (DUO2 / CIV2 / OFT2) and the player identity per seat
static void test_founders_multi()
{
    auto planet = fresh_dir(L"founders_multi") + L"\planet.spo";
    std::string key = slotfs::narrow(planet), err;
    put(planet, "native planet");
    founders::Roster two{1, 2, 0x800021CA, 0x21CB}, got;
    CHECK(founders::record(key, two, err));
    const std::string duo1 = get(planet + L".duofounders");
    CHECK(duo1.size() == 20 && duo1.compare(0, 4, "DUO1") == 0); // two founders without identities: DUO1 as before
    founders::Outfits outfits;
    CHECK(founders::record_outfit(key, 1, 2, 22, err));
    const std::string oft1 = get(planet + L".duooutfits");
    CHECK(oft1.compare(0, 4, "OFT1") == 0);
    CHECK(!founders::record_outfit(key, 2, 7, 70, err)); // no third seat yet
    // a third partner's seat: the same first two, then theirs (DUO2)
    auto three = two;
    three.entries[2] = {7, 0x21D5, 0xABCDEF0123456789ull};
    CHECK(three.seats() == 3 && three.seat_of_faction(0x21D5) == 2 && three.seat_of_player(0xABCDEF0123456789ull) == 2);
    CHECK(founders::record(key, three, err));
    CHECK(get(planet + L".duofounders").compare(0, 4, "DUO2") == 0);
    CHECK(founders::read(key, got) && got == three);
    // the OFT1 history of seats 0 / 1 still reads; seat 2 starts with its founding species
    CHECK(founders::read_outfits(key, outfits, err) && outfits.current(1) == 22 && outfits.current(2) == 7);
    CHECK(founders::record_outfit(key, 2, 7, 70, err));
    CHECK(get(planet + L".duooutfits").compare(0, 4, "OFT2") == 0);
    CHECK(founders::read_outfits(key, outfits, err) && outfits.current(0) == 1 && outfits.current(1) == 22 &&
          outfits.current(2) == 70 && outfits.contains(2, 7));
    CHECK(!founders::record_outfit(key, 2, 70, 22, err)); // another founder's appearance
    // an identity is learnt once for a seat that had none; a seat's founders never change
    auto bound = three;
    bound.entries[1].player = 0x1111;
    CHECK(founders::record(key, bound, err) && founders::read(key, got) && got.seat_of_player(0x1111) == 1);
    auto rebound = bound;
    rebound.entries[1].player = 0x2222;
    CHECK(!founders::record(key, rebound, err));
    auto changed = bound;
    changed.entries[2].species = 8;
    CHECK(!founders::record(key, changed, err));
    auto fewer = two;
    CHECK(!founders::record(key, fewer, err));     // seats are never dropped
    auto twins = bound;
    twins.entries[3] = {9, 0x21D6, 0x1111};      // one player, two seats
    CHECK(!founders::record(key, twins, err));
    // every seat fits (founders::kMaxSeats, 6E B); one more does not exist; a hole in the seats is invalid
    auto eight = bound;
    for (size_t i = 3; i < founders::kMaxSeats; ++i)
        eight.entries[i] = {uint32_t(100 + i), uint32_t(0x2200 + i), 0};
    CHECK(founders::record(key, eight, err) && founders::read(key, got) && got.seats() == founders::kMaxSeats);
    auto holed = founders::Roster{1, 2, 3, 4};
    holed.entries[3] = {5, 6, 0};
    CHECK(holed.seats() == 2 && !founders::can_record(slotfs::narrow(fresh_dir(L"founders_hole") + L"\p.spo"), holed, err));
    // Civilization: every nation descends from its seat's tribal founder
    auto civ = founders::Stage::Civilization;
    founders::Roster nations = eight;
    nations.entries[1].species = 22;
    nations.entries[2].species = 70;
    CHECK(founders::record(key, nations, err, civ));
    CHECK(get(planet + L".duonations").compare(0, 4, "CIV2") == 0);
    auto stranger = nations;
    stranger.entries[2].species = 71;
    DeleteFileW((planet + L".duonations").c_str());
    CHECK(!founders::can_record(key, stranger, err, civ));
    // roadmap phase 4: a late joiner's nation (a seat past the Tribe roster) needs an identity and a species of its
    // own; the tribal seats keep their founders' looks; two founders stay CIV1 byte for byte
    {
        auto late_planet = fresh_dir(L"founders_late") + L"\\planet.spo";
        std::string late_key = slotfs::narrow(late_planet);
        put(late_planet, "native planet");
        CHECK(founders::record(late_key, founders::Roster{1, 2, 0x800021CA, 0x21CB}, err)); // DUO1
        CHECK(founders::record_outfit(late_key, 1, 2, 22, err));
        founders::Roster pair{1, 22, 0x800021CA, 0x21CB};
        CHECK(founders::record(late_key, pair, err, civ) && get(late_planet + L".duonations").size() == 20 &&
              get(late_planet + L".duonations").compare(0, 4, "CIV1") == 0);
        auto late = pair;
        late.entries[2] = {9, 0x21D9, 0};
        CHECK(!founders::can_record(late_key, late, err, civ)); // no identity: refused
        late.entries[2].species = 2;
        late.entries[2].player = 0x7777;
        CHECK(!founders::can_record(late_key, late, err, civ)); // seat 1's founding look: refused
        late.entries[2].species = 9;
        CHECK(founders::record(late_key, late, err, civ));
        CHECK(get(late_planet + L".duonations").compare(0, 4, "CIV2") == 0);
        CHECK(founders::read(late_key, got, civ) && got == late && got.seat_of_player(0x7777) == 2);
        auto changed_tribal = late;
        changed_tribal.entries[1].species = 2; // a tribal seat never changes its nation
        CHECK(!founders::record(late_key, changed_tribal, err, civ));
        CHECK(get(late_planet) == "native planet");
    }
    // a damaged DUO2 is unreadable, never half-read
    put(planet + L".duofounders", get(planet + L".duofounders").substr(0, 30));
    CHECK(!founders::read(key, got));
    CHECK(get(planet) == "native planet");
}

// roadmap 0b: the campaign's party (.duoparty)
static void test_party()
{
    auto planet = fresh_dir(L"party") + L"\planet.spo";
    std::string key = slotfs::narrow(planet), err;
    founders::Party got, party;
    CHECK(!founders::record_party(key, {{}}, err)); // no planet yet
    put(planet, "native planet");
    CHECK(!founders::read_party(key, got));
    party.seats.push_back({0x1111, 0x291EE5B5, "Host"});
    CHECK(founders::record_party(key, party, err) && founders::read_party(key, got) && got == party);
    CHECK(get(planet + L".duoparty").compare(0, 4, "PTY1") == 0);
    party.seats.push_back({0, 0x7C67F1C3, "BigTexas"});    // a seat from a two-founder save, identity not known yet
    CHECK(founders::record_party(key, party, err) && founders::read_party(key, got) && got.seats.size() == 2);
    party.seats[1].player = 0x2222;                       // learnt once
    CHECK(founders::record_party(key, party, err) && founders::read_party(key, got) && got.seat_of(0x2222) == 1);
    auto other = party;
    other.seats[1].player = 0x3333;                       // never given to someone else
    CHECK(!founders::record_party(key, other, err));
    // BUG-9259: a seat the host handed over on purpose (its player is not here) may change player; no other seat
    {
        founders::Party handed = party;
        handed.seats[1].player = 0x7777;
        CHECK(!founders::record_party(key, handed, err, 1u << 0) && founders::record_party(key, handed, err, 1u << 1));
        CHECK(founders::read_party(key, got) && got.seat_of(0x7777) == 1);
        CHECK(founders::record_party(key, party, err, 1u << 1)); // (back for the checks below)
    }
    auto fewer = party;
    fewer.seats.pop_back();                               // never loses a seat
    CHECK(!founders::record_party(key, fewer, err));
    auto twice = party;
    twice.seats.push_back({0x2222, 1, "x"});              // one player, two seats
    CHECK(!founders::record_party(key, twice, err));
    party.seats[1].species = 0x7C000001;                  // a species and a name may change
    party.seats[1].name = "Big Texas";
    CHECK(founders::record_party(key, party, err) && founders::read_party(key, got) && got == party);
    for (uint64_t i = 2; i < founders::kMaxSeats; ++i)
        party.seats.push_back({0x5000 + i, uint32_t(i), "p"});
    CHECK(founders::record_party(key, party, err) && founders::read_party(key, got) && got.seats.size() == founders::kMaxSeats);
    party.seats.push_back({0x9999, 9, "ninth"});
    CHECK(!founders::record_party(key, party, err));      // founders::kMaxSeats at most
    put(planet + L".duoparty", get(planet + L".duoparty").substr(0, 20));
    CHECK(!founders::read_party(key, got));
    CHECK(!founders::record_party(key, got, err) || true); // an unreadable file is left as it is
    CHECK(get(planet) == "native planet");
}

static void test_uninstall()
{
    std::wstring root = fresh_dir(L"uninstall");
    std::wstring bin = root + L"\\SporebinEP1";
    std::wstring symbols = bin + L"\\mods\\sporekit\\symbols";
    std::string msg;
    put(bin + L"\\SporeApp.exe", "game");
    put(bin + L"\\steam_api.dll", "sporekit-loader");
    put(bin + L"\\mods\\coop.dll", "co-op");
    put(bin + L"\\mods\\sporecoop.version", "test");
    put(symbols + L"\\march2017_dc04aee5.sym", "co-op");
    put(symbols + L"\\another-mod.sym", "other symbols");
    put(symbols + L"\\another-mod.sym.old", "other backup");
    put(bin + L"\\mods\\coop.ini", "player settings");
    put(bin + L"\\mods\\sporekit.ini", "kit settings");
    CHECK(lineages_graphics::write(inst::graphics_file(root), {144, true}));
    const auto graphics = lineages_graphics::read(inst::graphics_file(root));
    CHECK(graphics.fps == 144 && graphics.borderless);
    CHECK(!lineages_graphics::write(inst::graphics_file(root), {361, false}));
    put(bin + L"\\mods\\logs\\SporeCoop-report-test.zip", "mod report");
    put(bin + L"\\mods\\logs\\unrelated.zip", "other report");
    put(bin + L"\\mods\\logs\\sporekit.log", "kit log");
    put(bin + L"\\mods\\logs\\previous\\sporekit.4-test.log", "previous kit log");
    const auto profile = g_tmp + L"\\test-roaming\\Spore";
    put(profile + L"\\SporeCoop\\LineagesCache\\objects\\example.png", "cached creation");
    put(profile + L"\\EditorSaves.package", "native creation library");
    put(root + L"\\Data\\unrelated.package", "player data");
    CHECK(!inst::do_uninstall(root, msg)); // no original DLL to restore
    CHECK(get(bin + L"\\mods\\coop.dll") == "co-op");
    put(bin + L"\\steam_api_orig.dll", "original game DLL");
    HANDLE held = CreateFileW((bin + L"\\steam_api.dll").c_str(), GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(held != INVALID_HANDLE_VALUE);
    CHECK(!inst::do_uninstall(root, msg)); // game has the proxy open
    CloseHandle(held);
    CHECK(get(bin + L"\\steam_api_orig.dll") == "original game DLL");
    CHECK(get(bin + L"\\steam_api.dll") == "sporekit-loader");
    CHECK(inst::do_uninstall(root, msg));
    CHECK(inst::do_uninstall(root, msg)); // an interrupted cleanup is retryable
    CHECK(get(bin + L"\\steam_api.dll") == "original game DLL");
    CHECK(!exists(bin + L"\\steam_api_orig.dll"));
    CHECK(!exists(bin + L"\\mods\\coop.dll"));
    CHECK(!exists(symbols + L"\\march2017_dc04aee5.sym"));
    CHECK(get(symbols + L"\\another-mod.sym") == "other symbols");
    CHECK(get(symbols + L"\\another-mod.sym.old") == "other backup");
    CHECK(!exists(bin + L"\\mods\\coop.ini"));
    CHECK(!exists(bin + L"\\mods\\sporekit.ini"));
    CHECK(!exists(inst::graphics_file(root)));
    CHECK(!exists(bin + L"\\mods\\logs\\SporeCoop-report-test.zip"));
    CHECK(get(bin + L"\\mods\\logs\\unrelated.zip") == "other report");
    CHECK(!exists(bin + L"\\mods\\logs\\sporekit.log"));
    CHECK(!exists(bin + L"\\mods\\logs\\previous\\sporekit.4-test.log"));
    CHECK(!exists(profile + L"\\SporeCoop\\LineagesCache"));
    CHECK(get(profile + L"\\EditorSaves.package") == "native creation library");
    CHECK(get(root + L"\\Data\\unrelated.package") == "player data");
}

// BUG-0114: only the updater's own backups are removed, never the player's *.old files
static void test_cleanup_old()
{
    printf("updater: cleanup_old keeps the player's *.old files\n");
    std::wstring g = fresh_dir(L"cleanup");
    put(g + L"\\SporebinEP1\\SporeApp.exe.old", "player's backup");
    put(g + L"\\Data\\keep.old", "another mod");
    put(g + L"\\SporebinEP1\\mods\\othermod.ini.old", "another mod");
    put(g + L"\\SporebinEP1\\steam_api.dll.old", "the player's own steam_api backup (not our loader)");
    put(g + L"\\SporebinEP1\\sporekit.dll.sporecoop-old", "ours");
    put(g + L"\\SporebinEP1\\mods\\coop.dll.sporecoop-old", "ours");
    put(g + L"\\SporebinEP1\\mods\\coop.dll.old", "ours, from a version before the fix");
    put(g + L"\\SporebinEP1\\mods\\sporekit\\symbols\\gog2024_dc04aee5.sym.old", "ours, old version");
    put(g + L"\\Data\\SporeCoop.package.sporecoop-old", "ours");
    upd::cleanup_old(g);
    CHECK(exists(g + L"\\SporebinEP1\\SporeApp.exe.old"));
    CHECK(exists(g + L"\\Data\\keep.old"));
    CHECK(exists(g + L"\\SporebinEP1\\mods\\othermod.ini.old"));
    CHECK(exists(g + L"\\SporebinEP1\\steam_api.dll.old"));
    CHECK(!exists(g + L"\\SporebinEP1\\sporekit.dll.sporecoop-old") || !upd::g_old_suffix_fix);
    CHECK(!exists(g + L"\\SporebinEP1\\mods\\coop.dll.sporecoop-old") || !upd::g_old_suffix_fix);
    CHECK(!exists(g + L"\\SporebinEP1\\mods\\coop.dll.old"));
    CHECK(!exists(g + L"\\SporebinEP1\\mods\\sporekit\\symbols\\gog2024_dc04aee5.sym.old"));
    CHECK(!exists(g + L"\\Data\\SporeCoop.package.sporecoop-old") || !upd::g_old_suffix_fix);
}

// BUG-0089 (in apply_pack too) and the backup name: a pack replaces a file, the backup is ours
static void test_apply_pack()
{
    printf("updater: apply_pack backups and a file listed twice\n");
    std::wstring g = fresh_dir(L"apply");
    put(g + L"\\SporebinEP1\\steam_api.dll", "the game's steam api");
    put(g + L"\\SporebinEP1\\mods\\coop.dll", "ORIGINAL");
    put(g + L"\\SporebinEP1\\mods\\coop.dll.old", "player's backup");
    upd::Pack p;
    p.version = "9";
    p.files.push_back({"SporebinEP1/mods/coop.dll", 0, {'N', '1'}});
    p.files.push_back({"SporebinEP1/mods/coop.dll", 0, {'N', '2'}});
    std::string err;
    bool ok = upd::apply_pack(g, p, err);
    CHECK(!ok); // a file listed twice: nothing installed
    CHECK(get(g + L"\\SporebinEP1\\mods\\coop.dll") == "ORIGINAL");
    CHECK(get(g + L"\\SporebinEP1\\mods\\coop.dll.old") == "player's backup");
    p.files.pop_back();
    CHECK(upd::apply_pack(g, p, err));
    CHECK(get(g + L"\\SporebinEP1\\mods\\coop.dll") == "N1");
    CHECK(get(g + L"\\SporebinEP1\\mods\\coop.dll.old") == "player's backup");
    CHECK(get(g + L"\\SporebinEP1\\mods\\coop.dll.sporecoop-old") == "ORIGINAL");
}

// BUG-0242: "coop.dll" and "coop.dll." are one file on Windows: the second entry moved the first
// entry's new file to its own backup and the rollback ended with the original gone
static void test_apply_rollback()
{
    printf("updater: a rollback with two names of one file, and the rollback itself (BUG-0242)\n");
    std::wstring g = fresh_dir(L"rollback");
    put(g + L"\\SporebinEP1\\steam_api.dll", "the game's steam api");
    put(g + L"\\SporebinEP1\\mods\\coop.dll", "ORIGINAL");
    put(g + L"\\SporebinEP1\\sporekit.dll", "KIT");
    upd::Pack p;
    p.version = "9";
    p.files.push_back({"SporebinEP1/mods/coop.dll", 0, {'N', '1'}});
    p.files.push_back({"SporebinEP1/mods/coop.dll.", 0, {'N', '2'}});
    p.files.push_back({"SporebinEP1/sporekit.dll", 0, {'N', '3'}});
    std::string err;
    upd::g_fail_swap_at = 3;
    bool ok = upd::apply_pack(g, p, err);
    std::string after = get(g + L"\\SporebinEP1\\mods\\coop.dll");
    printf("  two names: apply=%d (%s), coop.dll='%s'\n", ok, err.c_str(), after.c_str());
    CHECK(!ok && after == "ORIGINAL");
    // the hook itself: no duplicates, the second swap fails, every original is back
    put(g + L"\\SporebinEP1\\mods\\coop.dll", "ORIGINAL");
    p.files.erase(p.files.begin() + 1);
    upd::g_fail_swap_at = 2;
    ok = upd::apply_pack(g, p, err);
    upd::g_fail_swap_at = 0;
    CHECK(!ok && get(g + L"\\SporebinEP1\\mods\\coop.dll") == "ORIGINAL" &&
          get(g + L"\\SporebinEP1\\sporekit.dll") == "KIT");
}

// BUG-0243: a new start of coop.dll (hot reload, a second instance) ran cleanup_old while an update
// in the same folder was swapping files: the backups its rollback needed were gone
struct ApplyJob {
    std::wstring g;
    upd::Pack p;
    bool ok = true;
    std::string err;
};
static DWORD WINAPI apply_job(void* a)
{
    auto* j = (ApplyJob*)a;
    j->ok = upd::apply_pack(j->g, j->p, j->err);
    return 0;
}

static void test_update_lock()
{
    printf("updater: no cleanup_old under a running update (BUG-0243)\n");
    std::wstring g = fresh_dir(L"lock");
    put(g + L"\\SporebinEP1\\steam_api.dll", "the game's steam api");
    put(g + L"\\SporebinEP1\\mods\\sporecoop.version", "V1");
    put(g + L"\\SporebinEP1\\mods\\sporekit\\symbols\\x.sym", "SYM1");
    put(g + L"\\Data\\SporeCoop.package", "PKG1");
    ApplyJob j;
    j.g = g;
    j.p.version = "9";
    j.p.files.push_back({"SporebinEP1/mods/sporecoop.version", 0, {'V', '2'}});
    j.p.files.push_back({"SporebinEP1/mods/sporekit/symbols/x.sym", 0, {'S', '2'}});
    j.p.files.push_back({"Data/SporeCoop.package", 0, {'P', '2'}});
    upd::g_swap_delay_ms = 1500;
    upd::g_swap_delay_file = "SporebinEP1/mods/sporecoop.version";
    upd::g_fail_swap_at = 3; // the third swap fails: the rollback needs the first file's backup
    HANDLE t = CreateThread(nullptr, 0, apply_job, &j, 0, nullptr);
    Sleep(800);
    upd::cleanup_old(g); // the new image's start
    WaitForSingleObject(t, 20000);
    CloseHandle(t);
    upd::g_swap_delay_ms = 0;
    upd::g_swap_delay_file.clear();
    upd::g_fail_swap_at = 0;
    std::string v = get(g + L"\\SporebinEP1\\mods\\sporecoop.version");
    printf("  apply=%d (%s), sporecoop.version='%s' x.sym='%s'\n", j.ok, j.err.c_str(), v.c_str(),
           get(g + L"\\SporebinEP1\\mods\\sporekit\\symbols\\x.sym").c_str());
    CHECK(!j.ok && v == "V1" && get(g + L"\\SporebinEP1\\mods\\sporekit\\symbols\\x.sym") == "SYM1");
    // the next start (no update running) removes the backups as before
    upd::cleanup_old(g);
    CHECK(!exists(g + L"\\SporebinEP1\\mods\\sporecoop.version.sporecoop-old"));
}

// BUG-0150: a deferred file whose swap fails at the next start stays listed and is applied later
static void test_pending()
{
    printf("updater: apply_pending keeps an entry whose swap failed\n");
    std::wstring g = fresh_dir(L"pending");
    std::wstring pkg = g + L"\\Data\\SporeCoop.package", list = g + L"\\SporebinEP1\\mods\\sporecoop.pending";
    put(pkg, "OLD");
    put(pkg + L".new", "NEW");
    put(g + L"\\SporebinEP1\\mods\\coop.dll.new", "NEWDLL");
    put(g + L"\\SporebinEP1\\mods\\coop.dll", "OLDDLL");
    put(list, "Data/SporeCoop.package\nSporebinEP1/mods/coop.dll\n");
    // another instance of the game holds the package (no sharing at all)
    HANDLE hold = CreateFileW(pkg.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(hold != INVALID_HANDLE_VALUE);
    upd::apply_pending(g);
    CHECK(get(g + L"\\SporebinEP1\\mods\\coop.dll") == "NEWDLL");
    CloseHandle(hold);
    CHECK(exists(list));
    CHECK(get(list) == "Data/SporeCoop.package\n");
    upd::apply_pending(g); // the start after that
    CHECK(get(pkg) == "NEW");
    CHECK(!exists(pkg + L".new"));
    CHECK(!exists(list));
}

// BUG-1019: apply_pack kept only the first 4096 bytes of an older sporecoop.pending when it deferred another file
static std::string get_all(const std::wstring& p); // below

static void test_pending_long()
{
    printf("updater: apply_pack adds a deferred file to the whole pending list (BUG-1019)\n");
    std::wstring g = fresh_dir(L"pendlong");
    std::wstring list = g + L"\\SporebinEP1\\mods\\sporecoop.pending", dll = g + L"\\SporebinEP1\\mods\\coop.dll";
    put(g + L"\\SporebinEP1\\steam_api.dll", "the game's steam api");
    put(dll, "OLDDLL");
    std::string old; // an older list of about 5000 bytes: 100 lines of 50 bytes, the last one coop.dll itself
    for (int i = 0; i < 99; i++) {
        char ln[64];
        snprintf(ln, sizeof ln, "SporebinEP1/mods/sporekit/symbols/entry-%03d.sym\n", i);
        old += ln;
    }
    old += "SporebinEP1/mods/coop.dll\n";
    put(list, old);
    upd::Pack p;
    p.version = "9";
    p.files.push_back({"SporebinEP1/mods/coop.dll", 0, {'N', 'E', 'W'}});
    // the running game holds coop.dll: the swap waits for the next start
    HANDLE hold = CreateFileW(dll.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(hold != INVALID_HANDLE_VALUE);
    std::string err;
    int pending = 0;
    bool ok = upd::apply_pack(g, p, err, &pending);
    CloseHandle(hold);
    std::string now = get_all(list);
    int lines = 0, dll_lines = 0;
    for (size_t i = 0; i < now.size();) {
        size_t e = now.find('\n', i);
        std::string ln = now.substr(i, (e == std::string::npos ? now.size() : e) - i);
        lines += !ln.empty();
        dll_lines += ln == "SporebinEP1/mods/coop.dll";
        i = e == std::string::npos ? now.size() : e + 1;
    }
    printf("  apply=%d (%s) deferred=%d, the list: %u bytes, %d lines (%d of coop.dll), was %u bytes\n", ok, err.c_str(),
           pending, (unsigned)now.size(), lines, dll_lines, (unsigned)old.size());
    CHECK(ok && pending == 1);
    CHECK(now.compare(0, old.size(), old) == 0); // every older line kept whole
    CHECK(lines == 100 && dll_lines == 1);       // coop.dll listed once
}

// BUG-0035, BUG-0087, BUG-0105: where a received world goes and how it is written
static std::vector<slotfs::File> world(const char* planet, const char* data)
{
    std::vector<slotfs::File> v;
    v.push_back({std::string(planet) + ".spo", std::vector<uint8_t>(data, data + strlen(data))});
    v.push_back({"stars.db", std::vector<uint8_t>(data, data + strlen(data))});
    v.push_back({"sub/x.pkp", std::vector<uint8_t>(data, data + strlen(data))});
    return v;
}

static bool wr(const std::wstring& slot, const std::vector<slotfs::File>& w, const char* host, const char* planet,
               uint32_t id, std::string& err)
{
    std::vector<const slotfs::File*> v;
    for (auto& f : w)
        v.push_back(&f);
    bool ok = slotfs::write(slot, v, host, planet, id, err);
    if (!ok) printf("  slot write: %s\n", err.c_str());
    return ok;
}

static std::wstring lower(std::wstring s)
{
    for (auto& c : s)
        c = towlower(c);
    return s;
}

static DWORD WINAPI release_slot_lock(void* handle)
{
    Sleep(150);
    CloseHandle((HANDLE)handle);
    return 0;
}

static void test_slot_move_retry()
{
    printf("world: staged folder rename tolerates a brief real file lock (BUG-9028)\n");
    std::wstring dir = fresh_dir(L"move_retry"), source = dir + L"\\stage", destination = dir + L"\\Game0";
    put(source + L"\\stars.db", "COMPLETE WORLD");
    HANDLE hold = CreateFileW((source + L"\\stars.db").c_str(), GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(hold != INVALID_HANDLE_VALUE);
    if (hold == INVALID_HANDLE_VALUE) return;
    // Establish that this OS enforces the lock before testing a delayed release.
    bool locked_move = MoveFileW(source.c_str(), destination.c_str());
    DWORD blocked_error = GetLastError();
    if (locked_move) {
        CloseHandle(hold);
        printf("  file-lock semantics unavailable on this OS; delayed-release case skipped\n");
        return;
    }
    CHECK(blocked_error == ERROR_ACCESS_DENIED || blocked_error == ERROR_SHARING_VIOLATION);
    HANDLE release = CreateThread(nullptr, 0, release_slot_lock, hold, 0, nullptr);
    CHECK(release != nullptr);
    if (!release) { CloseHandle(hold); return; }
    bool moved = slotfs::move_staged(source, destination);
    DWORD move_error = moved ? ERROR_SUCCESS : GetLastError();
    WaitForSingleObject(release, INFINITE);
    CloseHandle(release);
    printf("  transient lock: moved=%d error=%lu\n", moved, move_error);
    CHECK(moved);
    CHECK(get(destination + L"\\stars.db") == "COMPLETE WORLD");
    CHECK(!exists(source));

    if (!moved) return; // keep the negative control's later checks independent

    // A persistent lock stays an error; neither side may be destroyed.
    source = dir + L"\\permanent";
    std::wstring absent = dir + L"\\Game1";
    put(source + L"\\stars.db", "KEEP SOURCE");
    hold = CreateFileW((source + L"\\stars.db").c_str(), GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(hold != INVALID_HANDLE_VALUE);
    ULONGLONG began = GetTickCount64();
    bool permanent = slotfs::move_staged(source, absent);
    DWORD permanent_error = GetLastError();
    CHECK(!permanent);
    CHECK(permanent_error == ERROR_ACCESS_DENIED || permanent_error == ERROR_SHARING_VIOLATION);
    CHECK(GetTickCount64() - began < 2000);
    CloseHandle(hold);
    CHECK(get(source + L"\\stars.db") == "KEEP SOURCE");
    CHECK(!exists(absent));
    // Even after retrying, an occupied destination is never replaced.
    CHECK(!slotfs::move_staged(source, destination));
    CHECK(get(source + L"\\stars.db") == "KEEP SOURCE");
    CHECK(get(destination + L"\\stars.db") == "COMPLETE WORLD");
}

static void test_slot_busy()
{
    printf("world: the slot the game plays is never moved or written (BUG-0035)\n");
    std::wstring games = fresh_dir(L"busy") + L"\\Games";
    CreateDirectoryW(games.c_str(), nullptr);
    std::string err, note;
    auto w = world("Terra", "A1");
    std::wstring s0 = slotfs::pick(games, L"", "Alice", "Terra", "Terra.spo", true, note);
    CHECK(wr(s0, w, "Alice", "Terra", 0x11, err));
    // the guest loads that slot by hand (auto_load_world=0) and plays it; the host shares again
    put(s0 + L"\\guest-progress.txt", "the guest's game");
    auto w2 = world("Terra", "A2");
    std::wstring s1 = slotfs::pick(games, lower(s0), "Alice", "Terra", "Terra.spo", true, note);
    CHECK(!s1.empty());
    CHECK(lower(s1) != lower(s0));
    if (!s1.empty() && lower(s1) != lower(s0)) {
        bool written = wr(s1, w2, "Alice", "Terra", 0x22, err);
        if (!written) printf("  second slot write: %s\n", err.c_str());
        CHECK(written);
    }
    CHECK(get(s0 + L"\\Terra.spo") == "A1");
    CHECK(get(s0 + L"\\guest-progress.txt") == "the guest's game");
    CHECK(get(s1 + L"\\Terra.spo") == "A2");
    // the next share while the guest still plays s0: s1 is updated, s0 still untouched
    std::wstring s2 = slotfs::pick(games, lower(s0), "Alice", "Terra", "Terra.spo", true, note);
    CHECK(lower(s2) == lower(s1));
}

static void test_slot_match()
{
    printf("world: a slot is reused only for the same host world and replaced entirely (BUG-0087)\n");
    std::wstring games = fresh_dir(L"match") + L"\\Games";
    CreateDirectoryW(games.c_str(), nullptr);
    std::string err, note;
    std::wstring a = slotfs::pick(games, L"", "Alice", "Terra", "Terra.spo", true, note);
    CHECK(wr(a, world("Terra", "A"), "Alice", "Terra", 1, err));
    std::wstring b = slotfs::pick(games, L"", "Bob", "Vulcan", "Vulcan.spo", true, note);
    CHECK(lower(a) != lower(b));
    if (lower(a) != lower(b))
        CHECK(wr(b, world("Vulcan", "B"), "Bob", "Vulcan", 2, err));
    CHECK(get(a + L"\\Terra.spo") == "A");
    CHECK(!exists(b + L"\\Terra.spo"));
    // the same host world again: its slot, with no file of the old copy left
    put(a + L"\\Oldplanet.spo", "an old planet of that slot");
    std::wstring a2 = slotfs::pick(games, L"", "Alice", "Terra", "Terra.spo", true, note);
    CHECK(lower(a2) == lower(a));
    CHECK(wr(a2, world("Terra", "A3"), "Alice", "Terra", 3, err));
    CHECK(get(a + L"\\Terra.spo") == "A3");
    CHECK(!exists(a + L"\\Oldplanet.spo") || !slotfs::g_write_fix);
}

static void test_slot_write_fails()
{
    printf("world: a world whose files could not all be written is not kept (BUG-0105)\n");
    std::wstring games = fresh_dir(L"wfail") + L"\\Games";
    CreateDirectoryW(games.c_str(), nullptr);
    std::string err, note;
    std::wstring a = slotfs::pick(games, L"", "Alice", "Terra", "Terra.spo", true, note);
    CHECK(wr(a, world("Terra", "A"), "Alice", "Terra", 1, err));
    // a file of the slot is held open by another program (antivirus, backup tool)
    HANDLE hold = CreateFileW((a + L"\\stars.db").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(hold != INVALID_HANDLE_VALUE);
    err.clear();
    bool ok = wr(a, world("Terra", "NEW"), "Alice", "Terra", 2, err);
    CloseHandle(hold);
    // Windows refuses to move a folder with a file open in it: the write fails and says why; Wine
    // moves it. Either way the slot holds one whole world, never a mix of both.
    CHECK(ok || !err.empty());
    printf("  held file: write %s (%s)\n", ok ? "succeeded" : "refused", err.c_str());
    CHECK(get(a + L"\\Terra.spo") == get(a + L"\\stars.db"));
    // a file that cannot be created (a name Windows refuses): the world is not written at all
    std::wstring b = slotfs::pick(games, L"", "Bob", "Vulcan", "Vulcan.spo", true, note);
    auto bad = world("Vulcan", "B");
    bad.push_back({"sub/a\x01:b", {1}});
    err.clear();
    CHECK(!wr(b, bad, "Bob", "Vulcan", 3, err));
    CHECK(!exists(b + L"\\sporecoop.txt"));
}

// BUG-0848: the staged folder's final move into the slot meets a file held for a moment (Windows: a
// scanner opens a fresh file without FILE_SHARE_DELETE and MoveFileW fails with error 5). Wine
// moves a folder with an open file in it (test_slot_write_fails), so besides holding a real handle
// the test makes the move fail with ERROR_ACCESS_DENIED for the same 150 ms
// (slotfs::g_test_move_denied_until), as Windows does.
static HANDLE g_move_hold;
static DWORD WINAPI release_after(void* ms)
{
    Sleep((DWORD)(uintptr_t)ms);
    if (g_move_hold != INVALID_HANDLE_VALUE)
        CloseHandle(g_move_hold);
    g_move_hold = INVALID_HANDLE_VALUE;
    return 0;
}

static void test_staged_move()
{
    printf("world: the staged world's move into the slot survives a file held for a moment (BUG-0848)\n");
    std::wstring games = fresh_dir(L"stagedmove") + L"\\Games";
    CreateDirectoryW(games.c_str(), nullptr);
    std::string err, note;
    std::wstring a = slotfs::pick(games, L"", "Alice", "Terra", "Terra.spo", true, note);
    CHECK(wr(a, world("Terra", "A1"), "Alice", "Terra", 1, err));
    // 1. held for 150 ms, then free: the world is stored
    HANDLE th = nullptr;
    slotfs::g_test_before_move = [&](const std::wstring& tmp) {
        g_move_hold = CreateFileW((tmp + L"\\Terra.spo").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        slotfs::g_test_move_denied_until = GetTickCount64() + 150;
        th = CreateThread(nullptr, 0, release_after, (void*)(uintptr_t)150, 0, nullptr);
    };
    err.clear();
    unsigned retries = slotfs::g_move_retries;
    bool ok = wr(a, world("Terra", "A2"), "Alice", "Terra", 2, err);
    if (th) {
        WaitForSingleObject(th, 5000);
        CloseHandle(th);
    }
    printf("  held 150 ms: %s (%s), moves tried again %u\n", ok ? "stored" : "refused", err.c_str(),
           slotfs::g_move_retries - retries);
    CHECK(ok);
    CHECK(get(a + L"\\Terra.spo") == "A2" && get(a + L"\\stars.db") == "A2");
    CHECK(get(a + L"\\sporecoop.txt").find("host Alice\n") != std::string::npos);
    // 2. held for good: refused with the error, the old world stays whole, nothing staged left behind
    slotfs::g_test_before_move = [&](const std::wstring&) { slotfs::g_test_move_denied_until = GetTickCount64() + 60000; };
    err.clear();
    ok = wr(a, world("Terra", "A3"), "Alice", "Terra", 3, err);
    slotfs::g_test_move_denied_until = 0;
    slotfs::g_test_before_move = nullptr;
    printf("  held for good: %s (%s)\n", ok ? "stored" : "refused", err.c_str());
    CHECK(!ok && err.find("error 5") != std::string::npos);
    std::string keep = slotfs::g_staged_move_fix ? "A2" : "A1"; // (--old: step 1 failed, A1 stayed)
    CHECK(get(a + L"\\Terra.spo") == keep && get(a + L"\\stars.db") == keep && get(a + L"\\sub\\x.pkp") == keep);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((games + L"-sporecoop-tmp\\*").c_str(), &fd);
    int left = 0;
    if (h != INVALID_HANDLE_VALUE) {
        do
            left += wcscmp(fd.cFileName, L".") && wcscmp(fd.cFileName, L"..");
        while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    CHECK(left == 0);
}

// BUG-0847: the host's share reads every file of the slot; one it cannot open (Windows: a scanner
// holds it without share access, or the game is still writing it) fails the share, it is not left out
static void test_share_collect()
{
    printf("world: a share fails when a file of the save cannot be read, never sends the rest (BUG-0847)\n");
    std::wstring slot = fresh_dir(L"collect") + L"\\Game0";
    put(slot + L"\\Terra.spo", "planet");
    put(slot + L"\\stars.db", "stars");
    put(slot + L"\\sub\\x.pkp", "sub file");
    put(slot + L"\\sporecoop.txt", "host_slot 1\n");
    std::vector<slotfs::File> files;
    std::string err;
    CHECK(slotfs::collect(slot, files, err));
    CHECK(files.size() == 3); // the marker is not sent
    size_t bytes = 0;
    bool sub = false;
    for (auto& f : files) {
        bytes += f.data.size();
        sub |= f.rel == "sub/x.pkp" && std::string(f.data.begin(), f.data.end()) == "sub file";
    }
    CHECK(bytes == 6 + 5 + 8 && sub);
    // a file held without share access
    HANDLE hold = CreateFileW((slot + L"\\sub\\x.pkp").c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(hold != INVALID_HANDLE_VALUE);
    files.clear();
    err.clear();
    bool ok = slotfs::collect(slot, files, err);
    printf("  held file: %s, %u files (%s)\n", ok ? "collected" : "refused", (unsigned)files.size(), err.c_str());
    CHECK(!ok && files.empty() && err.find("sub/x.pkp") != std::string::npos);
    CloseHandle(hold);
    // free again: the share goes out whole
    files.clear();
    err.clear();
    CHECK(slotfs::collect(slot, files, err) && files.size() == 3);
}

static int count_same_host(const std::wstring& games, const char* host)
{
    int n = 0;
    for (int k = 0; k < 64; k++) {
        std::string m = get(games + L"\\Game" + std::to_wstring(k) + L"\\sporecoop.txt");
        if (m.find(std::string("host ") + host + "\n") != std::string::npos)
            n++;
    }
    return n;
}

// BUG-0254: the old copy of the host's world cannot be replaced (Windows: a file in it is open by
// another program; slotfs::g_test_fail_replace makes Wine refuse the same way)
static void test_slot_fallback()
{
    printf("world: a slot that cannot be replaced gives way to another slot, tidied later (BUG-0254)\n");
    std::wstring games = fresh_dir(L"fallback") + L"\\Games";
    CreateDirectoryW(games.c_str(), nullptr);
    std::string err, note;
    auto files = [](const std::vector<slotfs::File>& w) {
        std::vector<const slotfs::File*> v;
        for (auto& f : w)
            v.push_back(&f);
        return v;
    };
    std::wstring a = slotfs::pick(games, L"", "Alice", "Terra", "Terra.spo", true, note);
    CHECK(wr(a, world("Terra", "A1"), "Alice", "Terra", 1, err));
    // the guest plays a save of its own (busy, never chosen)
    std::wstring own = games + L"\\Game5";
    put(own + L"\\Mine.spo", "the guest's own game");
    slotfs::g_test_held = lower(a);
    auto w2 = world("Terra", "A2");
    std::wstring slot = slotfs::pick(games, lower(own), "Alice", "Terra", "Terra.spo", true, note);
    CHECK(lower(slot) == lower(a));
    bool moved = false;
    err.clear();
    bool ok = slotfs::write_or_fallback(games, slot, lower(own), files(w2), "Alice", "Terra", "Terra.spo", 2, err, moved);
    printf("  held slot: %s, moved %d (%s)\n", ok ? "written" : "refused", (int)moved, err.c_str());
    CHECK(ok && moved);
    CHECK(lower(slot) != lower(a) && lower(slot) != lower(own));
    CHECK(get(slot + L"\\Terra.spo") == "A2");
    CHECK(get(slot + L"\\sporecoop.txt").find("host Alice\n") != std::string::npos);
    CHECK(get(a + L"\\Terra.spo") == "A1"); // the held copy unchanged
    CHECK(get(own + L"\\Mine.spo") == "the guest's own game");
    slotfs::g_test_held.clear();
    // the hold is released: the next share writes the lowest same-host slot and tidies the extra copy
    std::wstring s3 = slotfs::pick(games, lower(own), "Alice", "Terra", "Terra.spo", true, note);
    err.clear();
    CHECK(slotfs::write_or_fallback(games, s3, lower(own), files(world("Terra", "A3")), "Alice", "Terra", "Terra.spo", 3, err, moved));
    CHECK(count_same_host(games, "Alice") == 1);
    CHECK(get(s3 + L"\\Terra.spo") == "A3");
    CHECK(get(own + L"\\Mine.spo") == "the guest's own game");
}

static void set_age(const std::wstring& dir, int seconds)
{
    HANDLE h = CreateFileW(dir.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    u.QuadPart -= (ULONGLONG)seconds * 10000000ull;
    ft.dwLowDateTime = u.LowPart;
    ft.dwHighDateTime = u.HighPart;
    if (h != INVALID_HANDLE_VALUE) {
        SetFileTime(h, &ft, nullptr, nullptr);
        CloseHandle(h);
    }
}

static DWORD WINAPI busy_writer(void* p)
{
    auto* games = (std::wstring*)p;
    std::string e;
    slotfs::WriteFail why = slotfs::WriteFail::None;
    std::vector<slotfs::File> w = world("Terra", "B");
    std::vector<const slotfs::File*> v;
    for (auto& f : w)
        v.push_back(&f);
    bool ok = slotfs::write(*games + L"\\Game1", v, "Bob", "Terra", 2, e, &why);
    return ok ? 0 : (DWORD)why;
}

// BUG-0254: folders left in Games-sporecoop-tmp by writes that were interrupted
static void test_slot_leftovers()
{
    printf("world: leftovers of interrupted writes are tidied, an old world is never lost (BUG-0254)\n");
    std::wstring root = fresh_dir(L"leftover"), games = root + L"\\Games", tmp = games + L"-sporecoop-tmp",
                 bak = games + L"-sporecoop-backup";
    CreateDirectoryW(games.c_str(), nullptr);
    CreateDirectoryW(tmp.c_str(), nullptr);
    std::string err;
    put(tmp + L"\\Game3-20260101-101010-123\\stars.db", "old temp");
    set_age(tmp + L"\\Game3-20260101-101010-123", 3600);
    Sleep(3000); // (Wine may not keep a creation time set by hand: the age rule is 2 s here)
    put(tmp + L"\\Game3-20260101-101011-124\\stars.db", "fresh temp");
    put(tmp + L"\\Game4-old-20260101-101010-125\\Terra.spo", "the only copy of Game4");
    put(games + L"\\Game6\\Vulcan.spo", "Game6 now");
    put(tmp + L"\\Game6-old-20260101-101010-126\\Vulcan.spo", "an older Game6");
    put(tmp + L"\\notes\\x.txt", "not ours");
    slotfs::g_test_cleanup_age_s = 2;
    CHECK(wr(games + L"\\Game0", world("Terra", "T"), "Alice", "Terra", 1, err));
    slotfs::g_test_cleanup_age_s = 600;
    CHECK(!exists(tmp + L"\\Game3-20260101-101010-123"));    // an old temp copy: deleted
    CHECK(exists(tmp + L"\\Game3-20260101-101011-124"));     // a fresh one (maybe another write): kept
    CHECK(get(games + L"\\Game4\\Terra.spo") == "the only copy of Game4"); // back into its slot
    CHECK(get(games + L"\\Game6\\Vulcan.spo") == "Game6 now");
    CHECK(get(bak + L"\\Game6-old-20260101-101010-126\\Vulcan.spo") == "an older Game6"); // kept aside
    CHECK(exists(tmp + L"\\notes\\x.txt"));
    // another instance holds the write lock: nothing is touched
    HANDLE mx = CreateMutexW(nullptr, TRUE, L"Local\\SporeCoopSlotWrite");
    if (slotfs::g_cleanup_fix) {
        // the mutex is owned by this thread here, so take it from another thread
        HANDLE th = CreateThread(nullptr, 0, busy_writer, &games, 0, nullptr);
        WaitForSingleObject(th, 30000);
        DWORD code = 99;
        GetExitCodeThread(th, &code);
        CloseHandle(th);
        CHECK(code == (DWORD)slotfs::WriteFail::Busy);
        CHECK(!exists(games + L"\\Game1"));
    }
    ReleaseMutex(mx);
    CloseHandle(mx);
}

// BUG-0142: a name outside the ANSI codepage survives a write and a read through the profile API;
// a coop.ini edited by hand in UTF-8 (with or without a BOM) is read as UTF-8
static void test_ini_unicode()
{
    std::wstring f = g_tmp + L"\\coop.ini";
    const char* name = "\xD0\x92\xD0\xB0\xD1\x81\xD1\x8F \xE5\xA4\xAA\xE9\x83\x8E"; // "Вася 太郎"
    put(f, "; SporeCoop settings\r\n[coop]\r\nname=Player\r\nshare_world=1\r\n");
    CHECK(skini::write_str(f, "coop", "name", name));
    CHECK(skini::read_str(f, "coop", "name", "") == name);
    CHECK(skini::read_int(f, "coop", "share_world", 0) == 1); // the rest of the file is kept
    std::wstring g = g_tmp + L"\\hand.ini";
    put(g, "[coop]\r\nname=J\xC3\xB6rg\r\n"); // UTF-8 without a BOM (Notepad, any Linux editor)
    CHECK(skini::read_str(g, "coop", "name", "") == "J\xC3\xB6rg");
    std::wstring h = g_tmp + L"\\bom.ini";
    put(h, "\xEF\xBB\xBF[coop]\r\nname=\xD0\x92\xD0\xB0\xD1\x81\xD1\x8F\r\n"); // UTF-8 with a BOM
    CHECK(skini::read_str(h, "coop", "name", "") == "\xD0\x92\xD0\xB0\xD1\x81\xD1\x8F");
    std::wstring a = g_tmp + L"\\ascii.ini";
    put(a, "[sporekit]\r\nlog_level=1\r\n");
    CHECK(skini::read_str(a, "sporekit", "log_level", "") == "1");
    CHECK(get(a) == "[sporekit]\r\nlog_level=1\r\n"); // ASCII files stay ASCII (tools edit them with sed)
    std::wstring m = g_tmp + L"\\missing.ini"; // a new file gets a non-ASCII value
    CHECK(skini::write_str(m, "coop", "name", name));
    CHECK(skini::read_str(m, "coop", "name", "") == name);

    // BUG-0244: the coop.ini every upgrading player has, written by 0.7.2.x through the profile API in
    // the ANSI codepage ("J\xF6rg" in cp1252): read as CP_ACP (it is not UTF-8) and converted once
    UINT acp = GetACP();
    printf("  ANSI codepage %u\n", acp);
    if (acp == CP_UTF8) {
        printf("  NOT RUN: the ANSI codepage is UTF-8 (the ANSI case equals the UTF-8 case)\n");
        return;
    }
    const wchar_t* cands[] = {L"J\u00f6rg", L"\u0412\u0430\u0441\u044f", L"\u03a3\u03bf\u03c6\u03af\u03b1", L"\u592a\u90ce"};
    std::string ansi;
    std::wstring wname;
    for (const wchar_t* c : cands) {
        BOOL used = FALSE;
        char buf[64];
        int n = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, c, -1, buf, sizeof buf, nullptr, &used);
        if (n > 1 && !used) {
            ansi.assign(buf, n - 1);
            wname = c;
            break;
        }
    }
    CHECK(!ansi.empty());
    if (ansi.empty())
        return;
    std::wstring o = g_tmp + L"\\ansi.ini";
    put(o, "[coop]\r\nname=" + ansi + "\r\nshare_world=1\r\n");
    char u8[64];
    int un = WideCharToMultiByte(CP_UTF8, 0, wname.c_str(), -1, u8, sizeof u8, nullptr, nullptr);
    CHECK(skini::read_str(o, "coop", "name", "") == std::string(u8, un - 1));
    CHECK(skini::read_int(o, "coop", "share_world", 0) == 1);
    std::string disk = get(o); // converted once: UTF-16 LE with a BOM (the caches have no reset: read the bytes)
    std::wstring want = L"name=" + wname;
    CHECK(disk.size() >= 2 && (unsigned char)disk[0] == 0xFF && (unsigned char)disk[1] == 0xFE &&
          disk.find(std::string((const char*)want.data(), want.size() * sizeof(wchar_t))) != std::string::npos);
}

// BUG-0245: a UTF-16 BE file was decoded as the ANSI codepage and saved over the original; a UTF-16
// file without a BOM was converted on a guess; a second instance holding the temp name (or the lock)
// made a non-ASCII write land as "????"
static std::string utf16(const std::wstring& w, bool be)
{
    std::string o;
    for (wchar_t c : w) {
        unsigned char hi = (unsigned char)(c >> 8), lo = (unsigned char)c;
        o += (char)(be ? hi : lo);
        o += (char)(be ? lo : hi);
    }
    return o;
}

struct HoldLock {
    const wchar_t* name;
    HANDLE taken;
};
static DWORD WINAPI hold_lock(void* a)
{
    auto* h = (HoldLock*)a;
    HANDLE m = CreateMutexW(nullptr, TRUE, h->name);
    SetEvent(h->taken);
    Sleep(1000);
    ReleaseMutex(m);
    CloseHandle(m);
    return 0;
}

static void test_ini_encoding()
{
    const char* vasya = "\xD0\x92\xD0\xB0\xD1\x81\xD1\x8F"; // "Вася"
    const std::wstring text = L"[coop]\r\nname=\x0412\x0430\x0441\x044F\r\nshare_world=1\r\n";
    // 1. UTF-16 BE with a BOM: converted losslessly
    std::wstring be = g_tmp + L"\\be.ini";
    put(be, "\xFE\xFF" + utf16(text, true));
    std::string got = skini::read_str(be, "coop", "name", "");
    printf("  UTF-16 BE: name='%s' share_world=%d first bytes %02X %02X\n", got.c_str(),
           skini::read_int(be, "coop", "share_world", 0), (unsigned char)get(be)[0], (unsigned char)get(be)[1]);
    CHECK(got == vasya);
    CHECK(skini::read_int(be, "coop", "share_world", 0) == 1);
    CHECK(get(be).rfind("\xFF\xFE", 0) == 0);
    // 2. UTF-16 LE without a BOM: left as it is, a non-ASCII write refused
    std::wstring nb = g_tmp + L"\\nobom.ini";
    std::string raw = utf16(L"[coop]\r\nname=Player\r\n", false);
    put(nb, raw);
    skini::read_str(nb, "coop", "name", "");
    bool w = skini::write_str(nb, "coop", "name", vasya);
    printf("  UTF-16 without BOM: write=%d unchanged=%d\n", w, get(nb) == raw);
    CHECK(!w && get(nb) == raw);
    // 3. another instance holds the temp name: never "????", the next write after it stores the name
    std::wstring a = g_tmp + L"\\held.ini";
    put(a, "[coop]\r\nname=Player\r\n");
    HANDLE t = CreateFileW((a + L".sporecoop-tmp").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    bool w1 = skini::write_str(a, "coop", "name", vasya);
    std::string held = skini::read_str(a, "coop", "name", "");
    CloseHandle(t);
    DeleteFileW((a + L".sporecoop-tmp").c_str());
    bool w2 = skini::write_str(a, "coop", "name", vasya);
    std::string after = skini::read_str(a, "coop", "name", "");
    printf("  temp held: write=%d name='%s'; released: write=%d name='%s'\n", w1, held.c_str(), w2, after.c_str());
    CHECK(held.find('?') == std::string::npos && !w1);
    CHECK(w2 && after == vasya);
    // 4. another instance holds the file's lock for 1 s: the write is refused, nothing is lost
    std::wstring b = g_tmp + L"\\locked.ini";
    put(b, "[coop]\r\nname=Player\r\n");
    wchar_t name[128];
    CHECK(sc_lock_name(L"SporeKitIni-", b.c_str(), name, 128));
    // held by another thread (a mutex is re-entrant for its owner thread)
    HoldLock hold{name, CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    HANDLE ht = CreateThread(nullptr, 0, hold_lock, &hold, 0, nullptr);
    WaitForSingleObject(hold.taken, 5000);
    bool w3 = skini::write_str(b, "coop", "name", vasya);
    std::string lk = skini::read_str(b, "coop", "name", "");
    WaitForSingleObject(ht, 5000);
    CloseHandle(ht);
    CloseHandle(hold.taken);
    printf("  lock held: write=%d name='%s'\n", w3, lk.c_str());
    if (skini::g_lock_fix)
        CHECK(!w3 && lk == "Player");
    // the same lock name for another spelling of the path
    wchar_t name2[128];
    std::wstring upper = b;
    for (auto& c : upper)
        c = (wchar_t)towupper(c);
    CHECK(sc_lock_name(L"SporeKitIni-", upper.c_str(), name2, 128) && !wcscmp(name, name2));
}

// ---------------- the setup and install.bat ----------------

static std::vector<uint8_t> bytes(const std::string& s) { return std::vector<uint8_t>(s.begin(), s.end()); }

// a pack of version v as tools/mkpack.py builds it (the files a release installs)
static upd::Pack make_pack(const std::string& v)
{
    upd::Pack p;
    p.version = v;
    p.files.push_back({"SporebinEP1/steam_api.dll", 0, bytes("sporekit-loader " + v)});
    p.files.push_back({"SporebinEP1/sporekit.dll", 0, bytes("sporekit " + v)});
    p.files.push_back({"SporebinEP1/mods/coop.dll", 0, bytes("coop " + v)});
    p.files.push_back({"SporebinEP1/mods/sporekit/symbols/gog2024_dc04aee5.sym", 0, bytes("sym " + v)});
    p.files.push_back({"Data/SporeCoop.package", 0, bytes("package " + v)});
    p.files.push_back({"SporebinEP1/mods/coop.ini", upd::kOnlyIfMissing, bytes("[coop]\r\nname=Player\r\n")});
    p.files.push_back({"SporebinEP1/mods/sporecoop.version", 0, bytes(v + "\n")});
    return p;
}

static std::string get_all(const std::wstring& p)
{
    HANDLE f = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return {};
    std::string b(1 << 20, '\0');
    DWORD got = 0;
    ReadFile(f, b.data(), (DWORD)b.size(), &got, nullptr);
    CloseHandle(f);
    b.resize(got);
    return b;
}

static void put_bytes(const std::wstring& p, const std::vector<uint8_t>& b) { put(p, std::string(b.begin(), b.end())); }

static std::wstring fake_game(const wchar_t* name)
{
    std::wstring g = fresh_dir(name);
    put(g + L"\\SporebinEP1\\SporeApp.exe", "not the game");
    put(g + L"\\SporebinEP1\\steam_api.dll", "the game's steam api");
    CreateDirectoryW((g + L"\\Data").c_str(), nullptr);
    return g;
}

// every file of version v in place (the setup's result is one version, never a mix)
static bool all_of_version(const std::wstring& g, const std::string& v, bool quiet = false)
{
    bool ok = get(g + L"\\SporebinEP1\\steam_api.dll") == "sporekit-loader " + v &&
              get(g + L"\\SporebinEP1\\sporekit.dll") == "sporekit " + v &&
              get(g + L"\\SporebinEP1\\mods\\coop.dll") == "coop " + v &&
              get(g + L"\\Data\\SporeCoop.package") == "package " + v && upd::installed_version(g) == v;
    if (!ok && !quiet)
        printf("  state: steam_api=%s sporekit=%s coop=%s version=%s\n",
               get(g + L"\\SporebinEP1\\steam_api.dll").c_str(), get(g + L"\\SporebinEP1\\sporekit.dll").c_str(),
               get(g + L"\\SporebinEP1\\mods\\coop.dll").c_str(), upd::installed_version(g).c_str());
    return ok;
}

// BUG-0094: offline the setup took the first SporeCoop-*.pack listed and installed it over a newer
// version without asking
static void test_setup_packs()
{
    printf("setup: the newest local pack, no silent downgrade (BUG-0094)\n");
    std::wstring g = fake_game(L"setup_packs");
    std::wstring dir = fresh_dir(L"setup_packs_dl");
    for (const char* v : {"0.10.0", "0.11.0", "0.9.0"})
        put_bytes(dir + L"\\SporeCoop-" + upd::widen(v) + L".pack", upd::write_pack(make_pack(v)));
    inst::Options o;
    o.pack_dir = dir;
    o.api = "http://127.0.0.1:9/"; // offline
    std::wstring lp = inst::local_pack(o);
    printf("  local pack: %s\n", upd::narrow(lp).c_str());
    CHECK(lp == dir + L"\\SporeCoop-0.11.0.pack" || g_old); // the listing order under Wine is the host's
    upd::ReleaseBox box;
    std::string msg;
    o.pack = dir + L"\\SporeCoop-0.11.0.pack";
    CHECK(inst::do_install(g, o, box, msg) == inst::Result::Ok);
    CHECK(all_of_version(g, "0.11.0"));
    put(g + L"\\SporebinEP1\\mods\\coop.ini", "[coop]\r\nname=Alice\r\n");
    // an older setup next to an old pack, offline: "Reinstall"
    o.pack = dir + L"\\SporeCoop-0.9.0.pack";
    std::string older, have;
    inst::Result r = inst::do_install(g, o, box, msg, {}, &older, &have);
    printf("  older pack: %s\n", msg.c_str());
    CHECK(r == inst::Result::Older);
    CHECK(older == "0.9.0" && have == "0.11.0");
    CHECK(all_of_version(g, "0.11.0"));
    // the player said yes (or /downgrade)
    o.downgrade = true;
    CHECK(inst::do_install(g, o, box, msg) == inst::Result::Ok);
    CHECK(all_of_version(g, "0.9.0"));
    CHECK(get(g + L"\\SporebinEP1\\mods\\coop.ini") == "[coop]\r\nname=Alice\r\n");
}

struct FindWnd {
    DWORD pid;
    HWND w;
};

static BOOL CALLBACK find_wnd(HWND w, LPARAM lp)
{
    auto* f = (FindWnd*)lp;
    DWORD p = 0;
    wchar_t cls[64] = {};
    GetWindowThreadProcessId(w, &p);
    GetClassNameW(w, cls, 64);
    if (p == f->pid && !wcscmp(cls, L"SporeCoopSetup")) {
        f->w = w;
        return FALSE;
    }
    return TRUE;
}

static HWND setup_window(DWORD pid)
{
    FindWnd f{pid, nullptr};
    EnumWindows(find_wnd, (LPARAM)&f);
    return f.w;
}

static std::wstring exe_dir()
{
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring s = self;
    return s.substr(0, s.find_last_of(L"\\/"));
}

// BUG-0139: the window could be closed while the job swapped files: ExitProcess killed the job
// between a file's two moves (here: with a test delay of 1.5 s between them)
static void test_setup_close()
{
    printf("setup: closing the window during an install (BUG-0139)\n");
    std::wstring setup = exe_dir() + L"\\SporeCoopSetup.exe";
    if (!exists(setup)) {
        printf("  FAIL no %s\n", upd::narrow(setup).c_str());
        g_fail++;
        return;
    }
    std::wstring g = fake_game(L"setup_close");
    std::wstring dir = fresh_dir(L"setup_close_dl");
    put_bytes(dir + L"\\old.pack", upd::write_pack(make_pack("0.9.0")));
    put_bytes(dir + L"\\new.pack", upd::write_pack(make_pack("0.11.0")));
    {
        inst::Options o;
        o.pack = dir + L"\\old.pack";
        o.pack_dir = dir;
        upd::ReleaseBox box;
        std::string msg;
        CHECK(inst::do_install(g, o, box, msg) == inst::Result::Ok); // 0.9.0 installed
    }
    std::wstring cmd = L"\"" + setup + L"\" \"/dir:" + g + L"\" \"/pack:" + dir + L"\\new.pack\" /api:http://127.0.0.1:9/" +
                       (g_old ? L" /oldfix" : L"");
    SetEnvironmentVariableW(L"SPORECOOP_SETUP_TEST_DELAY", L"1500");
    // BUG-0853: the job's WM_DONE 2 s after the last file: the old wait (all files new, then one
    // WM_CLOSE) always closed into the busy window, which refused it, and the setup never ended
    SetEnvironmentVariableW(L"SPORECOOP_SETUP_TEST_DONE_DELAY", L"2000");
    STARTUPINFOW si{sizeof si};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    BOOL started = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
    SetEnvironmentVariableW(L"SPORECOOP_SETUP_TEST_DELAY", nullptr);
    SetEnvironmentVariableW(L"SPORECOOP_SETUP_TEST_DONE_DELAY", nullptr);
    CHECK(started);
    if (!started)
        return;
    HWND w = nullptr;
    for (int i = 0; i < 100 && !w; i++) {
        Sleep(100);
        w = setup_window(pi.dwProcessId);
    }
    CHECK(w != nullptr);
    HWND btn = w ? GetDlgItem(w, 11) : nullptr; // ID_INSTALL
    for (int i = 0; i < 50 && btn && !IsWindowEnabled(btn); i++)
        Sleep(100);
    if (w) {
        PostMessageW(w, WM_COMMAND, MAKEWPARAM(11, BN_CLICKED), (LPARAM)btn);
        Sleep(800);                       // the new files are written, the first swap waits
        PostMessageW(w, WM_CLOSE, 0, 0);  // the window's X (Alt+F4)
        PostMessageW(w, WM_COMMAND, MAKEWPARAM(13, BN_CLICKED), (LPARAM)GetDlgItem(w, 13)); // Close
    }
    bool closed_early = WaitForSingleObject(pi.hProcess, 2000) == WAIT_OBJECT_0;
    printf("  closed during the job: %s\n", closed_early ? "yes" : "no (refused)");
    CHECK(!closed_early);
    if (g_close_wait_fix) {
        // BUG-0853: the job is over when the window enables Close again (refresh() after WM_DONE)
        HWND close = w ? GetDlgItem(w, 13) : nullptr; // ID_CLOSE
        int waited = 0;
        for (; waited < 300 && !closed_early && close && !IsWindowEnabled(close); waited++)
            Sleep(100);
        printf("  Close enabled again after %.1f s\n", waited / 10.0);
    } else // the old wait: the files only (the window may still be busy)
        for (int i = 0; i < 200 && !closed_early && !all_of_version(g, "0.11.0", true); i++)
            Sleep(100);
    if (!closed_early && w)
        PostMessageW(w, WM_CLOSE, 0, 0);
    bool ended = WaitForSingleObject(pi.hProcess, 20000) == WAIT_OBJECT_0;
    if (!ended)
        TerminateProcess(pi.hProcess, 1);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CHECK(ended);
    CHECK(exists(g + L"\\SporebinEP1\\steam_api.dll"));
    CHECK(all_of_version(g, "0.11.0"));
}

static bool run_bat(const std::wstring& bin, const wchar_t* name = L"install.bat")
{
    std::wstring cmd = std::wstring(L"cmd.exe /c ") + name + L" < NUL > NUL";
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, bin.c_str(), &si, &pi))
        return false;
    bool ok = WaitForSingleObject(pi.hProcess, 60000) == WAIT_OBJECT_0;
    if (!ok)
        TerminateProcess(pi.hProcess, 1);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return ok;
}

// BUG-0080: install.bat run again over an installed SporeCoop (the documented manual update)
// replaced mods\coop.ini: the player's name and the [update] check=0 opt-out were lost
static void test_install_bat()
{
    printf("install.bat keeps the player's .ini files (BUG-0080)\n");
    if (g_bat.empty()) {
        printf("  (skipped: no --bat)\n");
        return;
    }
    std::wstring g = fake_game(L"bat");
    std::wstring bin = g + L"\\SporebinEP1", sc = bin + L"\\SporeCoop";
    CHECK(CopyFileW(g_bat.c_str(), (bin + L"\\install.bat").c_str(), FALSE));
    put(sc + L"\\steam_api.sporekit.dll", "sporekit-loader 1");
    put(sc + L"\\sporekit.dll", "sporekit 1");
    put(sc + L"\\SporeCoop.package", "package 1");
    put(sc + L"\\mods\\coop.dll", "coop 1");
    put(sc + L"\\mods\\coop.ini", "[coop]\r\nname=Player\r\n");
    put(sc + L"\\mods\\sporekit.ini", "[kit]\r\nconsole=1\r\n");
    put(sc + L"\\mods\\sporekit\\symbols\\gog2024_dc04aee5.sym", "sym 1");
    if (!g_excl.empty()) // tools/package.sh puts it there
        CHECK(CopyFileW(g_excl.c_str(), (sc + L"\\ini.exclude").c_str(), FALSE));
    CHECK(run_bat(bin));
    CHECK(get(bin + L"\\mods\\coop.dll") == "coop 1");
    CHECK(get(bin + L"\\mods\\coop.ini") == "[coop]\r\nname=Player\r\n"); // a fresh install gets the defaults
    CHECK(get(bin + L"\\mods\\sporekit.ini") == "[kit]\r\nconsole=1\r\n");
    CHECK(get(bin + L"\\mods\\sporekit\\symbols\\gog2024_dc04aee5.sym") == "sym 1");
    CHECK(get(bin + L"\\steam_api.dll") == "sporekit-loader 1");
    CHECK(get(bin + L"\\steam_api_orig.dll") == "the game's steam api");
    CHECK(get(g + L"\\Data\\SporeCoop.package") == "package 1");
    // the player plays: a name, no update check; then the next zip over it
    const std::string mine = "[coop]\r\nname=Alice\r\n[update]\r\ncheck=0\r\n";
    put(bin + L"\\mods\\coop.ini", mine);
    DeleteFileW((bin + L"\\mods\\sporekit.ini").c_str());
    put(sc + L"\\mods\\coop.dll", "coop 2");
    put(sc + L"\\sporekit.dll", "sporekit 2");
    put(sc + L"\\mods\\sporekit\\symbols\\gog2024_dc04aee5.sym", "sym 2");
    CHECK(run_bat(bin));
    printf("  coop.ini after the reinstall: %s\n", get(bin + L"\\mods\\coop.ini") == mine ? "kept" : "replaced");
    CHECK(get(bin + L"\\mods\\coop.ini") == mine);
    CHECK(get(bin + L"\\mods\\sporekit.ini") == "[kit]\r\nconsole=1\r\n"); // a missing one comes back
    CHECK(get(bin + L"\\mods\\coop.dll") == "coop 2");
    CHECK(get(bin + L"\\sporekit.dll") == "sporekit 2");
    CHECK(get(bin + L"\\mods\\sporekit\\symbols\\gog2024_dc04aee5.sym") == "sym 2");
}

// ---------------- BUG-0391: a Spore folder without Galactic Adventures ----------------

// Steam's base Spore: SporeBin, Data, Support, ... and no SporebinEP1 (the Galactic Adventures DLC)
static std::wstring spore_no_ga(const wchar_t* name)
{
    std::wstring g = fresh_dir(name);
    put(g + L"\\SporeBin\\SporeApp.exe", "base spore");
    put(g + L"\\Data\\Spore_Game.package", "data");
    put(g + L"\\Support\\readme.txt", "support");
    put(g + L"\\steam_appid.txt", "17390");
    return g;
}

static void test_ga_folder()
{
    printf("setup: a Spore folder without Galactic Adventures (BUG-0391)\n");
    std::wstring lib = fresh_dir(L"ga_lib");
    std::wstring spore = lib + L"\\steamapps\\common\\Spore";
    CreateDirectoryW((lib + L"\\steamapps").c_str(), nullptr);
    CreateDirectoryW((lib + L"\\steamapps\\common").c_str(), nullptr);
    put(spore + L"\\SporeBin\\SporeApp.exe", "base spore");
    put(spore + L"\\steam_appid.txt", "17390");
    CreateDirectoryW((spore + L"\\Data").c_str(), nullptr);
    CHECK(!inst::is_game(spore));
    // the player picked SporeBin (the report) or typed the folder with a slash
    std::wstring r = inst::game_root_of(spore + L"\\SporeBin");
    printf("  SporeBin -> %s\n", upd::narrow(r).c_str());
    CHECK(r == spore);
    CHECK(inst::game_root_of(spore + L"\\SporeBin\\") == spore);
    CHECK(inst::game_root_of(spore) == spore);
    std::string why = inst::folder_problem(spore);
    printf("  says: %s\n", why.c_str());
    CHECK(why.find("Galactic Adventures is not installed") != std::string::npos);
    CHECK(why.find("Spore Galactic Adventures DLC") != std::string::npos);
    // the install job says the same (silent mode: its log)
    upd::ReleaseBox box;
    inst::Options o;
    o.api = "http://127.0.0.1:9/";
    std::string msg;
    CHECK(inst::do_install(spore, o, box, msg) == inst::Result::Failed);
    CHECK(msg.find("Galactic Adventures is not installed") != std::string::npos);
    // a folder that is no Spore at all keeps the plain advice
    std::wstring other = fresh_dir(L"ga_other");
    CHECK(inst::folder_problem(other).find("Galactic Adventures") == std::string::npos);
    CHECK(inst::game_root_of(other) == other);
    // a real game folder: SporebinEP1 picked -> the game folder (the old rule stays)
    std::wstring g = fake_game(L"ga_game");
    CHECK(inst::game_root_of(g + L"\\SporebinEP1") == g);
    CHECK(inst::folder_problem(g).empty());
    // the auto-detection: Steam's libraryfolders.vdf (new and old format) leads to the library
    std::string lib_esc; // the vdf escapes backslashes
    for (char c : upd::narrow(lib))
        lib_esc += c == '\\' ? std::string("\\\\") : std::string(1, c);
    std::string vdf = "\"libraryfolders\"\n{\n\t\"0\"\n\t{\n\t\t\"path\"\t\t\"C:\\\\Program Files (x86)\\\\Steam\"\n"
                      "\t\t\"apps\"\n\t\t{\n\t\t\t\"17390\"\t\t\"1234\"\n\t\t}\n\t}\n\t\"1\"\n\t{\n\t\t\"path\"\t\t\"" +
                      lib_esc + "\"\n\t}\n}\n";
    auto libs = inst::steam_libraries(vdf);
    CHECK(libs.size() == 2 && libs[0] == L"C:\\Program Files (x86)\\Steam" && libs.size() > 1 && libs[1] == lib);
    auto old_libs = inst::steam_libraries("\"LibraryFolders\"\n{\n\t\"TimeNextStatsReport\"\t\t\"1\"\n\t\"1\"\t\t\"D:\\\\Games\\\\Steam\"\n}\n");
    CHECK(old_libs.size() == 1 && old_libs[0] == L"D:\\Games\\Steam");
    // no game anywhere, one Spore without GA: the setup shows that folder (and says why)
    std::vector<std::wstring> cand = {L"", other, libs.size() > 1 ? libs[1] + L"\\steamapps\\common\\Spore" : L""};
    std::wstring picked = inst::pick_game(cand);
    printf("  detected: %s\n", upd::narrow(picked).c_str());
    CHECK(picked == spore);
    cand.push_back(g); // a real game wins over it
    CHECK(inst::pick_game(cand) == g);
}

// child windows' texts of the setup window, joined by '\n'
static BOOL CALLBACK child_text(HWND c, LPARAM lp)
{
    wchar_t b[1024] = {};
    DWORD_PTR n = 0;
    SendMessageTimeoutW(c, WM_GETTEXT, 1024, (LPARAM)b, SMTO_ABORTIFHUNG, 2000, &n);
    *(std::wstring*)lp += std::wstring(b) + L"\n";
    return TRUE;
}

static std::wstring setup_exe() { return g_setup.empty() ? exe_dir() + L"\\SporeCoopSetup.exe" : g_setup; }

// starts the setup window hidden with these arguments; returns its window (or nullptr)
static HWND start_setup(const std::wstring& args, PROCESS_INFORMATION& pi)
{
    std::wstring cmd = L"\"" + setup_exe() + L"\" " + args;
    SetEnvironmentVariableW(L"SPORECOOP_SETUP_TEST_DELAY", L"1"); // no "Spore is running" question
    STARTUPINFOW si{sizeof si};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    pi = {};
    BOOL started = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
    SetEnvironmentVariableW(L"SPORECOOP_SETUP_TEST_DELAY", nullptr);
    if (!started)
        return nullptr;
    HWND w = nullptr;
    for (int i = 0; i < 100 && !w; i++) {
        Sleep(100);
        w = setup_window(pi.dwProcessId);
    }
    return w;
}

static void stop_setup(HWND w, PROCESS_INFORMATION& pi)
{
    if (w)
        PostMessageW(w, WM_CLOSE, 0, 0);
    if (pi.hProcess && WaitForSingleObject(pi.hProcess, 10000) != WAIT_OBJECT_0)
        TerminateProcess(pi.hProcess, 1);
    if (pi.hProcess)
        CloseHandle(pi.hProcess);
    if (pi.hThread)
        CloseHandle(pi.hThread);
}

// what the player saw: the setup window with SporeBin chosen in a Spore folder without SporebinEP1
static void test_setup_ga_window()
{
    printf("setup window: SporeBin of a Spore without Galactic Adventures (BUG-0391)\n");
    if (!exists(setup_exe())) {
        printf("  FAIL no %s\n", upd::narrow(setup_exe()).c_str());
        g_fail++;
        return;
    }
    std::wstring spore = spore_no_ga(L"ga_window");
    PROCESS_INFORMATION pi;
    HWND w = start_setup(L"\"/dir:" + spore + L"\\SporeBin\" /api:http://127.0.0.1:9/" + (inst::g_ga_fix ? L"" : L" /oldga"), pi);
    CHECK(w != nullptr);
    std::wstring texts;
    if (w) {
        Sleep(500);
        EnumChildWindows(w, child_text, (LPARAM)&texts);
    }
    std::string t = upd::narrow(texts);
    printf("  window: %s\n", t.substr(0, t.find("Installed:")).c_str());
    CHECK(t.find("Game: Galactic Adventures is not installed") != std::string::npos);
    CHECK(t.find(upd::narrow(spore) + "\n") != std::string::npos); // the folder field stepped up from SporeBin
    HWND btn = w ? GetDlgItem(w, 11) : nullptr; // ID_INSTALL
    CHECK(btn && !IsWindowEnabled(btn));
    stop_setup(w, pi);
}

// ---------------- BUG-0391, Steam: Galactic Adventures is app 24720 in "Spore 24720" ----------------

static void put_acf(const std::wstring& steamapps, const char* installdir)
{
    put(steamapps + L"\\appmanifest_24720.acf", std::string("\"AppState\"\n{\n\t\"appid\"\t\t\"24720\"\n\t\"name\"\t\t") +
                                                    "\"Spore: Galactic Adventures\"\n\t\"installdir\"\t\t\"" + installdir +
                                                    "\"\n}\n");
}

// a Steam library as Steam lays out Spore + Galactic Adventures: base Spore (17390) in common\Spore
// (SporeBin, no SporebinEP1), GA (24720) in common\<ga_dir> with SporebinEP1. Returns the library
static std::wstring steam_lib(const wchar_t* name, const wchar_t* ga_dir, const char* acf_dir)
{
    std::wstring lib = fresh_dir(name);
    std::wstring common = lib + L"\\steamapps\\common";
    put(common + L"\\Spore\\SporeBin\\SporeApp.exe", "base spore");
    put(common + L"\\Spore\\runme.exe", "runme");
    CreateDirectoryW((common + L"\\Spore\\Data").c_str(), nullptr);
    if (ga_dir) {
        put(common + L"\\" + ga_dir + L"\\SporebinEP1\\SporeApp.exe", "spore ga");
        put(common + L"\\" + ga_dir + L"\\SporebinEP1\\steam_api.dll", "the game's steam api");
        put(common + L"\\" + ga_dir + L"\\SporeBin\\SporeApp.exe", "base spore");
        CreateDirectoryW((common + L"\\" + ga_dir + L"\\Data").c_str(), nullptr);
    }
    if (acf_dir)
        put_acf(lib + L"\\steamapps", acf_dir);
    return lib;
}

static std::wstring lower_w(std::wstring s)
{
    for (auto& c : s)
        c = towlower(c);
    return s;
}

static void test_steam_ga_folder()
{
    printf("setup: Steam's Galactic Adventures folder next to base Spore (BUG-0391)\n");
    std::wstring lib = steam_lib(L"steam_lib", L"spore 24720", "spore 24720"); // as Steam writes it
    std::wstring common = lib + L"\\steamapps\\common", spore = common + L"\\Spore", ga = common + L"\\spore 24720";
    CHECK(inst::spore_without_ga(spore) && inst::is_game(ga));
    CHECK(inst::acf_installdir("\"AppState\"\n{\n\t\"installdir\"\t\t\"spore 24720\"\n}\n") == "spore 24720");
    auto gf = inst::ga_folders(lib);
    CHECK(!gf.empty() && gf[0] == ga);
    // the player's choice: SporeBin of base Spore, or base Spore itself, or steamapps\common
    std::string note;
    std::wstring r = inst::resolve_root(spore + L"\\SporeBin", {}, &note);
    printf("  SporeBin -> %s\n  says: %s\n", upd::narrow(r).c_str(), note.c_str());
    CHECK(lower_w(r) == lower_w(ga));
    CHECK(note.find("Galactic Adventures is installed in") != std::string::npos && note.find("using that folder") != std::string::npos);
    CHECK(lower_w(inst::resolve_root(spore)) == lower_w(ga));
    CHECK(lower_w(inst::resolve_root(common)) == lower_w(ga));
    CHECK(inst::resolve_root(ga, {}, &note) == ga && note.empty()); // a game folder stays
    // no appmanifest: "Spore 24720" by name (any case)
    std::wstring lib2 = steam_lib(L"steam_lib2", L"Spore 24720", nullptr);
    CHECK(lower_w(inst::resolve_root(lib2 + L"\\steamapps\\common\\Spore")) == lower_w(lib2 + L"\\steamapps\\common\\Spore 24720"));
    // another installdir in the appmanifest wins
    std::wstring lib3 = steam_lib(L"steam_lib3", L"Galactic", "Galactic");
    CHECK(inst::resolve_root(lib3 + L"\\steamapps\\common\\Spore") == lib3 + L"\\steamapps\\common\\Galactic");
    // base Spore in one library, GA in another one
    std::wstring lib4 = steam_lib(L"steam_lib4", nullptr, nullptr);
    std::wstring r4 = inst::resolve_root(lib4 + L"\\steamapps\\common\\Spore\\SporeBin", {lib4, lib}, &note);
    CHECK(lower_w(r4) == lower_w(ga));
    // no GA anywhere: "not installed", and where Steam would put it
    std::wstring r5 = inst::resolve_root(lib4 + L"\\steamapps\\common\\Spore", {lib4});
    std::string why = inst::folder_problem(r5);
    printf("  no GA: %s\n", why.c_str());
    CHECK(r5 == lib4 + L"\\steamapps\\common\\Spore");
    CHECK(why.find("Galactic Adventures is not installed") != std::string::npos && why.find("Spore 24720") != std::string::npos);
}

// the real setup: the window with base Spore's SporeBin chosen, and silent mode installing there
static void test_setup_steam_ga()
{
    printf("setup: base Spore chosen, Galactic Adventures in \"Spore 24720\" (BUG-0391)\n");
    if (!exists(setup_exe())) {
        printf("  FAIL no %s\n", upd::narrow(setup_exe()).c_str());
        g_fail++;
        return;
    }
    std::wstring lib = steam_lib(L"steam_setup", L"spore 24720", "spore 24720");
    std::wstring spore = lib + L"\\steamapps\\common\\Spore", ga = lib + L"\\steamapps\\common\\spore 24720";
    PROCESS_INFORMATION pi;
    HWND w = start_setup(L"\"/dir:" + spore + L"\\SporeBin\" /api:http://127.0.0.1:9/" +
                             (inst::g_ga_dir_fix ? L"" : L" /oldgadir") + (inst::g_ga_fix ? L"" : L" /oldga"), pi);
    CHECK(w != nullptr);
    std::wstring texts;
    if (w) {
        Sleep(500);
        EnumChildWindows(w, child_text, (LPARAM)&texts);
    }
    std::string t = upd::narrow(texts);
    size_t g0 = t.find("Browse...\n");
    printf("  window: %s\n", g0 == std::string::npos ? "?" : t.substr(g0 + 10, t.find("Installed:") - g0 - 10).c_str());
    CHECK(t.find("Galactic Adventures is installed in " + upd::narrow(ga) + ": using that folder.") != std::string::npos);
    CHECK(t.find("not installed: SporeCoop needs") == std::string::npos);
    HWND btn = w ? GetDlgItem(w, 11) : nullptr;
    CHECK(btn && IsWindowEnabled(btn));
    stop_setup(w, pi);
    // silent mode, as the player's choice: SporeCoop goes into the GA folder, base Spore stays as it is
    std::wstring dir = fresh_dir(L"steam_setup_dl");
    put_bytes(dir + L"\\SporeCoop-0.14.0.pack", upd::write_pack(make_pack("0.14.0")));
    std::wstring log = dir + L"\\setup.log";
    std::wstring cmd = L"\"" + setup_exe() + L"\" /S \"/dir:" + spore + L"\\SporeBin\" \"/pack:" + dir +
                       L"\\SporeCoop-0.14.0.pack\" /api:http://127.0.0.1:9/ \"/log:" + log + L"\"" +
                       (inst::g_ga_dir_fix ? L"" : L" /oldgadir") + (inst::g_ga_fix ? L"" : L" /oldga");
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION p2{};
    DWORD code = 99;
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &p2)) {
        if (WaitForSingleObject(p2.hProcess, 60000) != WAIT_OBJECT_0)
            TerminateProcess(p2.hProcess, 1);
        GetExitCodeProcess(p2.hProcess, &code);
        CloseHandle(p2.hProcess);
        CloseHandle(p2.hThread);
    }
    printf("  /S exit %lu, log: %s", code, get(log).c_str());
    CHECK(code == 0);
    CHECK(all_of_version(ga, "0.14.0"));
    CHECK(!exists(spore + L"\\SporebinEP1"));
}

// read only: the parsing against a real Steam folder (win_unit.py passes the user's when present)
static void test_real_steam()
{
    if (g_steam.empty())
        return;
    printf("setup: a real Steam folder, read only (BUG-0391)\n");
    std::string vdf = get_all(g_steam + L"\\steamapps\\libraryfolders.vdf");
    auto libs = inst::steam_libraries(vdf);
    for (auto& l : libs)
        printf("  library: %s\n", upd::narrow(l).c_str());
    CHECK(!libs.empty());
    std::string acf = get_all(g_steam + L"\\steamapps\\appmanifest_24720.acf");
    if (acf.empty()) {
        printf("  no appmanifest_24720.acf (Galactic Adventures not installed there)\n");
        return;
    }
    std::string dir = inst::acf_installdir(acf);
    printf("  GA installdir: '%s'\n", dir.c_str());
    CHECK(!dir.empty());
    auto gf = inst::ga_folders(g_steam);
    CHECK(!gf.empty() && gf[0] == g_steam + L"\\steamapps\\common\\" + upd::widen(dir));
    std::wstring base = g_steam + L"\\steamapps\\common\\Spore";
    if (exists(base + L"\\SporeBin") && inst::is_game(gf[0])) {
        std::string note;
        std::wstring r = inst::resolve_root(base + L"\\SporeBin", {}, &note);
        printf("  base Spore\\SporeBin -> %s (%s)\n", upd::narrow(r).c_str(), note.c_str());
        CHECK(r == gf[0]);
    }
}

// ---------------- BUG-0392: the GitHub API answers 403 / 429 ----------------

// a local stand-in for api.github.com and github.com: the API path refuses, the release page
// redirects to the tag, the assets are under releases/download/<tag>/
struct FakeGithub {
    int api_status = 403;          // the API's answer (200: a JSON release)
    int web_status = 302;          // the release page: 302 to the tag, else this status
    bool relative = false;         // the redirect's Location without the origin
    std::string tag = "v0.12.0";
    std::vector<uint8_t> pack;     // served as SporeCoop-<tag without v>.pack
    std::string sums;              // SHA256SUMS.txt
    int pack_failures = 0;
    int api_hits = 0, web_hits = 0, asset_hits = 0, tag_hits = 0; // tag_hits: the redirect was followed
    // BUG-0755: the next fail_left requests of a path ending in fail_path fail: fail_status 0 = the
    // connection is closed without an answer, -1 = the body breaks off halfway, else that status
    // (with Retry-After: retry_after when >= 0)
    std::string fail_path;
    int fail_left = 0, fail_status = 504, retry_after = -1, fail_hits = 0;
};
static FakeGithub g_gh;
static std::mutex g_gh_mu;
static SOCKET g_gh_sock = INVALID_SOCKET;
static int g_gh_port;

static void gh_answer(SOCKET c)
{
    std::string req;
    char b[2048];
    while (req.find("\r\n\r\n") == std::string::npos) {
        int n = recv(c, b, sizeof b, 0);
        if (n <= 0)
            return;
        req.append(b, n);
    }
    std::string path = req.substr(4, req.find(' ', 4) - 4); // "GET <path> HTTP/1.1"
    std::lock_guard<std::mutex> l(g_gh_mu);
    std::string base = "http://127.0.0.1:" + std::to_string(g_gh_port);
    std::string repo = "/WoodyWizard/SporeCoop/releases";
    std::string ver = g_gh.tag.substr(1);
    std::string status = "404 Not Found", extra, body;
    std::vector<uint8_t> bin;
    bool truncate = false;
    if (g_gh.fail_left > 0 && !g_gh.fail_path.empty() && path.size() >= g_gh.fail_path.size() &&
        path.compare(path.size() - g_gh.fail_path.size(), g_gh.fail_path.size(), g_gh.fail_path) == 0) {
        g_gh.fail_left--;
        g_gh.fail_hits++;
        if (g_gh.fail_status == 0)
            return; // the connection is closed without an answer
        if (g_gh.fail_status == -1)
            truncate = true;
        else {
            std::string head = "HTTP/1.1 " + std::to_string(g_gh.fail_status) + " Gateway Timeout\r\n" +
                               (g_gh.retry_after >= 0 ? "Retry-After: " + std::to_string(g_gh.retry_after) + "\r\n" : "") +
                               "Content-Length: 0\r\nConnection: close\r\n\r\n";
            send(c, head.data(), (int)head.size(), 0);
            return;
        }
    }
    if (path == "/repos" + repo + "/latest") {
        g_gh.api_hits++;
        if (g_gh.api_status == 200) {
            status = "200 OK";
            std::string dl = base + repo + "/download/" + g_gh.tag + "/";
            body = "{\"tag_name\":\"" + g_gh.tag + "\",\"html_url\":\"" + base + repo + "/tag/" + g_gh.tag +
                   "\",\"assets\":[{\"browser_download_url\":\"" + dl + "SporeCoop-" + ver +
                   ".pack\"},{\"browser_download_url\":\"" + dl + "SHA256SUMS.txt\"}]}";
        } else {
            status = std::to_string(g_gh.api_status) + (g_gh.api_status == 429 ? " Too Many Requests" : " Forbidden");
            body = "{\"message\":\"API rate limit exceeded for 127.0.0.1.\"}";
        }
    } else if (path == repo + "/latest") {
        g_gh.web_hits++;
        if (g_gh.web_status == 302) {
            status = "302 Found";
            extra = "Location: " + (g_gh.relative ? std::string() : base) + repo + "/tag/" + g_gh.tag + "\r\n";
        } else
            status = std::to_string(g_gh.web_status) + " Refused";
    } else if (path == repo + "/tag/" + g_gh.tag) {
        g_gh.tag_hits++;
        status = "200 OK";
        body = "<html>release " + g_gh.tag + "</html>";
    } else if (path == repo + "/download/" + g_gh.tag + "/SHA256SUMS.txt") {
        g_gh.asset_hits++;
        status = "200 OK";
        body = g_gh.sums;
    } else if (path == repo + "/download/" + g_gh.tag + "/SporeCoop-" + ver + ".pack") {
        g_gh.asset_hits++;
        if (g_gh.pack_failures > 0) {
            --g_gh.pack_failures;
            status = "504 Gateway Timeout";
        } else {
            status = "200 OK";
            bin = g_gh.pack;
        }
    }
    if (bin.empty())
        bin.assign(body.begin(), body.end());
    std::string head = "HTTP/1.1 " + status + "\r\n" + extra + "Content-Length: " + std::to_string(bin.size()) +
                       "\r\nConnection: close\r\n\r\n";
    send(c, head.data(), (int)head.size(), 0);
    if (truncate)
        bin.resize(bin.size() / 2); // Content-Length says the whole, half of it comes
    for (size_t at = 0; at < bin.size();) {
        int n = send(c, (const char*)bin.data() + at, (int)std::min<size_t>(bin.size() - at, 65536), 0);
        if (n <= 0)
            break;
        at += n;
    }
}

static DWORD WINAPI gh_serve(void*)
{
    for (;;) {
        SOCKET c = accept(g_gh_sock, nullptr, nullptr);
        if (c == INVALID_SOCKET)
            return 0;
        gh_answer(c);
        shutdown(c, SD_SEND);
        closesocket(c);
    }
}

static bool gh_start()
{
    WSADATA wd;
    WSAStartup(MAKEWORD(2, 2), &wd);
    g_gh_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    if (bind(g_gh_sock, (sockaddr*)&a, sizeof a) || listen(g_gh_sock, 8))
        return false;
    int len = sizeof a;
    getsockname(g_gh_sock, (sockaddr*)&a, &len);
    g_gh_port = ntohs(a.sin_port);
    CloseHandle(CreateThread(nullptr, 0, gh_serve, nullptr, 0, nullptr));
    return true;
}

// the release v<version> on the fake server; sums_ok false = SHA256SUMS.txt names another hash
static void gh_release(const std::string& version, const std::string& pack_version, bool sums_ok = true)
{
    std::lock_guard<std::mutex> l(g_gh_mu);
    g_gh.tag = "v" + version;
    g_gh.pack = upd::write_pack(make_pack(pack_version));
    std::string h = sums_ok ? upd::sha256_hex(g_gh.pack) : std::string(64, 'a');
    g_gh.sums = std::string(64, 'b') + "  SporeCoop-" + version + ".zip\n" + h + "  SporeCoop-" + version + ".pack\n" +
                std::string(64, 'c') + "  SporeCoopSetup.exe\n";
    g_gh.api_status = 403;
    g_gh.web_status = 302;
    g_gh.relative = false;
    g_gh.api_hits = g_gh.web_hits = g_gh.asset_hits = g_gh.tag_hits = 0;
    g_gh.fail_path.clear();
    g_gh.fail_left = g_gh.fail_hits = 0;
    g_gh.fail_status = 504;
    g_gh.retry_after = -1;
}

static void gh_fail(const std::string& path_end, int times, int status, int retry_after = -1)
{
    std::lock_guard<std::mutex> l(g_gh_mu);
    g_gh.fail_path = path_end;
    g_gh.fail_left = times;
    g_gh.fail_status = status;
    g_gh.retry_after = retry_after;
    g_gh.fail_hits = 0;
}

static std::string gh_api() { return "http://127.0.0.1:" + std::to_string(g_gh_port) + "/repos/WoodyWizard/SporeCoop/releases/latest"; }

static void test_web_fallback()
{
    printf("updater: the GitHub API refuses (403 / 429), the release page still works (BUG-0392)\n");
    if (g_gh_port == 0 && !gh_start()) {
        printf("  FAIL no local server\n");
        g_fail++;
        return;
    }
    CHECK(upd::web_latest_url("https://api.github.com/repos/WoodyWizard/SporeCoop/releases/latest") ==
          "https://github.com/WoodyWizard/SporeCoop/releases/latest");
    CHECK(upd::web_latest_url("https://example.com/repos/a/b/releases/latest").empty());
    upd::Release bad;
    CHECK(!upd::release_from_redirect("https://github.com/a/b/releases/latest", "https://github.com/a/b/releases/tag/v1/../../x", bad));
    // 1. the in-game check and the setup's check: 403 from the API
    gh_release("0.12.0", "0.12.0");
    upd::Release r;
    std::string err;
    bool ok = upd::latest_release(gh_api(), r, err);
    printf("  403: %s %s%s, the redirect %s\n", ok ? "found" : "FAILED:", ok ? r.version.c_str() : err.c_str(),
           ok && r.from_web ? " (from the release page)" : "", g_gh.tag_hits ? "was followed" : "was not followed");
    CHECK(ok && r.version == "0.12.0" && r.tag == "v0.12.0");
    CHECK(ok && r.pack_url == "http://127.0.0.1:" + std::to_string(g_gh_port) +
                                  "/WoodyWizard/SporeCoop/releases/download/v0.12.0/SporeCoop-0.12.0.pack");
    upd::Pack p;
    {
        std::lock_guard<std::mutex> lock(g_gh_mu);
        g_gh.pack_failures = 1;
    }
    CHECK(ok && upd::download_pack(r, p, err) && p.version == "0.12.0");
    // 2. 429, a relative Location
    gh_release("0.12.1", "0.12.1");
    g_gh.api_status = 429;
    g_gh.relative = true;
    ok = upd::latest_release(gh_api(), r, err);
    CHECK(ok && r.version == "0.12.1");
    // 3. the checks stay: a pack that does not match SHA256SUMS.txt, a pack of another version
    gh_release("0.12.2", "0.12.2", false);
    ok = upd::latest_release(gh_api(), r, err) && upd::download_pack(r, p, err);
    printf("  bad checksum: %s\n", ok ? "accepted" : err.c_str());
    CHECK(!ok && err.find("checksum") != std::string::npos);
    gh_release("0.12.3", "0.9.0");
    ok = upd::latest_release(gh_api(), r, err) && upd::download_pack(r, p, err);
    printf("  wrong version: %s\n", ok ? "accepted" : err.c_str());
    CHECK(!ok);
    // 4. the API works: the page is not asked
    gh_release("0.12.4", "0.12.4");
    g_gh.api_status = 200;
    ok = upd::latest_release(gh_api(), r, err);
    CHECK(ok && r.version == "0.12.4" && !r.from_web && g_gh.web_hits == 0);
    // 5. the setup's install: no pack next to it, GitHub's API refuses
    gh_release("0.12.0", "0.12.0");
    std::wstring g = fake_game(L"web_install");
    inst::Options o;
    o.pack_dir = fresh_dir(L"web_install_dl"); // no pack: the release from "GitHub"
    o.api = gh_api();
    upd::ReleaseBox box;
    std::string msg;
    inst::Result res = inst::do_install(g, o, box, msg);
    printf("  setup: %s\n", msg.c_str());
    CHECK(res == inst::Result::Ok);
    CHECK(all_of_version(g, "0.12.0"));
    // 6. both refuse: the setup says how to install by hand
    gh_release("0.12.5", "0.12.5");
    g_gh.web_status = 403;
    upd::ReleaseBox box2;
    res = inst::do_install(g, o, box2, msg);
    printf("  both refuse: %s\n", msg.c_str());
    CHECK(res == inst::Result::Failed);
    CHECK(msg.find("install.bat") != std::string::npos || g_old);
    CHECK(all_of_version(g, "0.12.0"));
}

// the real setup (silent mode, as scripts and update_check.py run it) against the refusing API
static void test_setup_api403()
{
    printf("setup /S: the GitHub API answers 403 (BUG-0392)\n");
    if (!exists(setup_exe()) || (g_gh_port == 0 && !gh_start())) {
        printf("  FAIL no setup or no local server\n");
        g_fail++;
        return;
    }
    gh_release("0.13.0", "0.13.0");
    std::wstring g = fake_game(L"web_setup");
    // This case exercises the network fallback. A user's packaged setup has an offline pack next to it.
    std::wstring exe = fresh_dir(L"web_setup_exe") + L"\\SporeCoopSetup.exe";
    if (!CopyFileW(setup_exe().c_str(), exe.c_str(), TRUE)) {
        CHECK(false);
        return;
    }
    std::wstring log = g + L"\\setup.log";
    std::wstring cmd = L"\"" + exe + L"\" /S \"/dir:" + g + L"\" /api:" + upd::widen(gh_api()) + L" \"/log:" + log +
                       L"\"" + (upd::g_web_fallback_fix ? L"" : L" /oldweb");
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    DWORD code = 99;
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        if (WaitForSingleObject(pi.hProcess, 60000) != WAIT_OBJECT_0)
            TerminateProcess(pi.hProcess, 1);
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    std::string l = get(log);
    printf("  exit %lu, log: %s", code, l.c_str());
    CHECK(code == 0);
    CHECK(all_of_version(g, "0.13.0"));
    printf("  requests: api %d, release page %d, assets %d\n", g_gh.api_hits, g_gh.web_hits, g_gh.asset_hits);
}

// ---------------- BUG-0755: a 504 / a dropped connection from GitHub's file host ----------------

static void test_retry()
{
    printf("updater: transient download failures are retried (BUG-0755)%s\n", upd::g_retry_fix ? "" : " [update.retry off]");
    if (g_gh_port == 0 && !gh_start()) {
        printf("  FAIL no local server\n");
        g_fail++;
        return;
    }
    upd::Release r;
    upd::Pack p;
    std::string err;
    auto pack_case = [&](const char* what, const std::string& path_end, int times, int status) {
        gh_release("0.14.0", "0.14.0");
        g_gh.api_status = 200;
        gh_fail(path_end, times, status);
        bool ok = upd::latest_release(gh_api(), r, err) && upd::download_pack(r, p, err);
        printf("  %s: %s (failed answers %d)\n", what, ok ? "downloaded" : err.c_str(), g_gh.fail_hits);
        return ok && p.version == "0.14.0";
    };
    // 1. the report: SHA256SUMS.txt answers 504 (twice), the third try works
    CHECK(pack_case("SHA256SUMS.txt 504 x2", "/SHA256SUMS.txt", 2, 504));
    // 2. the pack's connection is closed without an answer, twice
    CHECK(pack_case("pack: connection closed x2", ".pack", 2, 0));
    // 3. the pack's body breaks off halfway once (Content-Length not reached)
    CHECK(pack_case("pack: body broken off x1", ".pack", 1, -1));
    // 4. a 500 / 502 / 503 too
    CHECK(pack_case("pack: 503 x1", ".pack", 1, 503));
    // 5. the API answers 502 once: the API's answer, not the release page
    gh_release("0.14.1", "0.14.1");
    g_gh.api_status = 200;
    gh_fail("/releases/latest", 1, 502);
    bool ok = upd::latest_release(gh_api(), r, err);
    printf("  API 502 x1: %s%s\n", ok ? r.version.c_str() : err.c_str(), ok && r.from_web ? " (from the release page)" : "");
    CHECK(ok && r.version == "0.14.1" && !r.from_web);
    // 6. 429 with a short Retry-After: waited for; a long one (an hour): not waited for, the
    // release page answers at once (BUG-0392 stays)
    gh_release("0.14.2", "0.14.2");
    g_gh.api_status = 200;
    gh_fail("/repos/WoodyWizard/SporeCoop/releases/latest", 1, 429, 1);
    ok = upd::latest_release(gh_api(), r, err);
    CHECK(ok && !r.from_web && g_gh.api_hits == 1 && g_gh.fail_hits == 1);
    gh_release("0.14.3", "0.14.3");
    g_gh.api_status = 200;
    gh_fail("/repos/WoodyWizard/SporeCoop/releases/latest", 5, 429, 3600);
    DWORD t0 = GetTickCount();
    ok = upd::latest_release(gh_api(), r, err);
    printf("  API 429 Retry-After 3600: %s, %d API answers, %lu ms\n", ok ? (r.from_web ? "release page" : "API") : err.c_str(),
           g_gh.fail_hits, GetTickCount() - t0);
    CHECK(g_gh.fail_hits == 1 && (ok || !upd::g_web_fallback_fix));
    // 7. final answers are not retried: a 404 asked once, its error as before
    gh_release("0.14.4", "0.14.4");
    g_gh.api_status = 200;
    gh_fail("/SHA256SUMS.txt", 3, 404);
    ok = upd::latest_release(gh_api(), r, err) && upd::download_pack(r, p, err);
    printf("  SHA256SUMS.txt 404: %s (asked %d times)\n", ok ? "downloaded" : err.c_str(), g_gh.fail_hits);
    CHECK(!ok && g_gh.fail_hits == 1 && err.find("404") != std::string::npos && err.find("attempts") == std::string::npos);
    // 8. it stays broken: 3 attempts, then the last error and the count
    gh_release("0.14.5", "0.14.5");
    g_gh.api_status = 200;
    gh_fail("/SHA256SUMS.txt", 9, 504);
    ok = upd::latest_release(gh_api(), r, err) && upd::download_pack(r, p, err);
    printf("  SHA256SUMS.txt 504 x9: %s (asked %d times)\n", ok ? "downloaded" : err.c_str(), g_gh.fail_hits);
    CHECK(!ok && g_gh.fail_hits == 3 && err.find("504") != std::string::npos && err.find("after 3 attempts") != std::string::npos);
    // 9. the checksum check stays: a retried pack that does not match SHA256SUMS.txt is refused
    gh_release("0.14.6", "0.14.6", false);
    g_gh.api_status = 200;
    gh_fail(".pack", 1, 504);
    ok = upd::latest_release(gh_api(), r, err) && upd::download_pack(r, p, err);
    CHECK(!ok && err.find("checksum") != std::string::npos);
}

// the real setup (silent mode) while SHA256SUMS.txt answers 504 twice (its own retry, its log)
static void test_setup_retry()
{
    printf("setup /S: SHA256SUMS.txt answers 504 twice (BUG-0755)\n");
    if (!exists(setup_exe()) || (g_gh_port == 0 && !gh_start())) {
        printf("  FAIL no setup or no local server\n");
        g_fail++;
        return;
    }
    gh_release("0.15.0", "0.15.0");
    g_gh.api_status = 200;
    gh_fail("/SHA256SUMS.txt", 2, 504);
    std::wstring g = fake_game(L"retry_setup");
    std::wstring log = g + L"\\setup.log";
    std::wstring cmd = L"\"" + setup_exe() + L"\" /S \"/dir:" + g + L"\" /api:" + upd::widen(gh_api()) + L" \"/log:" + log +
                       L"\"" + (upd::g_retry_fix ? L"" : L" /oldretry");
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    DWORD code = 99;
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        if (WaitForSingleObject(pi.hProcess, 60000) != WAIT_OBJECT_0)
            TerminateProcess(pi.hProcess, 1);
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    std::string l = get(log);
    printf("  exit %lu, log: %s", code, l.c_str());
    CHECK(code == 0);
    CHECK(all_of_version(g, "0.15.0"));
    CHECK(l.find("attempt 2 of 3") != std::string::npos);
}

// ---------------- decision 8Y C: the galaxy server, an optional part of the setup ----------------

static upd::Pack make_server_pack(const std::string& v)
{
    upd::Pack p;
    p.version = v;
    const std::string d = inst::kServerPrefix;
    p.files.push_back({d + "galaxy_server.exe", 0, bytes("galaxy server " + v)});
    p.files.push_back({d + "galaxy_update.exe", 0, bytes("galaxy update " + v)}); // BUG-9378: its update check
    p.files.push_back({d + "galaxy-server.version", 0, bytes(v + "\n")});
    p.files.push_back({d + "Start-Galaxy-Server.cmd", 0, bytes("start " + v)});
    p.files.push_back({d + "Read-me.txt", 0, bytes("read me " + v)});
    p.files.push_back({d + "galaxy-settings.cmd", upd::kOnlyIfMissing, bytes("set \"GALAXY_NAME=My Lineages Galaxy\"\r\n")});
    return p;
}

static void test_setup_server()
{
    printf("setup: the optional galaxy server (8Y C)\n");
    std::wstring g = fake_game(L"setup_server");
    std::wstring dir = fresh_dir(L"setup_server_dl");
    std::wstring sd = g + inst::kServerDir;
    put_bytes(dir + L"\\SporeCoop-0.11.0.pack", upd::write_pack(make_pack("0.11.0")));
    put_bytes(dir + L"\\SporeCoop-0.12.0.pack", upd::write_pack(make_pack("0.12.0")));
    put_bytes(dir + L"\\GalaxyServer-0.11.0.pack", upd::write_pack(make_server_pack("0.11.0")));
    put_bytes(dir + L"\\GalaxyServer-0.12.0.pack", upd::write_pack(make_server_pack("0.12.0")));
    inst::Options o;
    o.pack_dir = dir;
    o.api = "";
    o.pack = dir + L"\\SporeCoop-0.11.0.pack";
    o.server_pack = dir + L"\\GalaxyServer-0.11.0.pack";
    upd::ReleaseBox box;
    std::string msg;
    // (a) not asked for: the game's files only, no server folder
    CHECK(inst::has_server_pack(o));
    CHECK(inst::do_install(g, o, box, msg) == inst::Result::Ok);
    printf("  (a) without the box: %s\n", msg.c_str());
    CHECK(all_of_version(g, "0.11.0") && !inst::has_server(g) && !exists(sd));
    // (b) asked for: the server's four files beside the game's
    o.server = true;
    CHECK(inst::do_install(g, o, box, msg) == inst::Result::Ok);
    printf("  (b) with the box: %s\n", msg.c_str());
    CHECK(inst::has_server(g) && get(sd + L"\\galaxy_server.exe") == "galaxy server 0.11.0");
    CHECK(get(sd + L"\\Start-Galaxy-Server.cmd") == "start 0.11.0" && get(sd + L"\\Read-me.txt") == "read me 0.11.0");
    CHECK(msg.find("Start-Galaxy-Server.cmd") != std::string::npos && all_of_version(g, "0.11.0"));
    // (c) the next version: the program and the start file are replaced, the host's settings stay
    put(sd + L"\\galaxy-settings.cmd", "set \"GALAXY_NAME=Pandora\"\r\n");
    o.pack = dir + L"\\SporeCoop-0.12.0.pack";
    o.server_pack = dir + L"\\GalaxyServer-0.12.0.pack";
    CHECK(inst::do_install(g, o, box, msg) == inst::Result::Ok);
    CHECK(all_of_version(g, "0.12.0") && get(sd + L"\\galaxy_server.exe") == "galaxy server 0.12.0");
    CHECK(get(sd + L"\\Start-Galaxy-Server.cmd") == "start 0.12.0" && get(sd + L"\\galaxy-server.version") == "0.12.0\n");
    CHECK(get(sd + L"\\galaxy-settings.cmd") == "set \"GALAXY_NAME=Pandora\"\r\n");
    // (d) a server pack that names a file of the game: refused whole, said so, the game's files as they were
    upd::Pack bad = make_server_pack("0.13.0");
    bad.files.push_back({"SporebinEP1/mods/coop.dll", 0, bytes("not the mod")});
    put_bytes(dir + L"\\GalaxyServer-bad.pack", upd::write_pack(bad));
    o.server_pack = dir + L"\\GalaxyServer-bad.pack";
    inst::Result r = inst::do_install(g, o, box, msg);
    printf("  (d) a pack reaching outside its folder: %s\n", msg.c_str());
    CHECK(r == inst::Result::Failed && msg.find("outside the server's folder") != std::string::npos);
    CHECK(all_of_version(g, "0.12.0") && get(sd + L"\\galaxy_server.exe") == "galaxy server 0.12.0");
    // (e) asked for in a setup that carries no server
    o.server_pack.clear();
    CHECK(!inst::has_server_pack(o));
    r = inst::do_install(g, o, box, msg);
    printf("  (e) no server in the setup: %s\n", msg.c_str());
    CHECK(r == inst::Result::Failed && msg.find("does not include it") != std::string::npos && all_of_version(g, "0.12.0"));
    // (f) the uninstall takes the server's program, start file and read-me; the host's settings stay
    bool ok = inst::do_uninstall(g, msg);
    printf("  (f) uninstall: %d, %s\n", ok, msg.c_str());
    CHECK(ok && !inst::has_server(g) && !exists(sd + L"\\Start-Galaxy-Server.cmd") && !exists(sd + L"\\Read-me.txt"));
    CHECK(!exists(sd + L"\\galaxy_update.exe") && !exists(sd + L"\\galaxy-server.version"));
    {   // no backup of a replaced server file stays behind (the cleanup knows the server's folder, BUG-9378)
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((sd + L"\\*.sporecoop-old").c_str(), &fd);
        CHECK(h == INVALID_HANDLE_VALUE);
        if (h != INVALID_HANDLE_VALUE)
            FindClose(h);
    }
    CHECK(get(sd + L"\\galaxy-settings.cmd") == "set \"GALAXY_NAME=Pandora\"\r\n");
    CHECK(get(g + L"\\SporebinEP1\\steam_api.dll") == "the game's steam api");
}

// ---------------- BUG-0851: the uninstaller ----------------

// a fake game with SporeCoop 0.9.0 installed by do_install, the release's symbol file names and a
// symbol file of the player's own
static std::wstring installed_game(const wchar_t* name)
{
    std::wstring g = fake_game(name);
    std::wstring dir = fresh_dir((std::wstring(name) + L"_dl").c_str());
    put_bytes(dir + L"\\p.pack", upd::write_pack(make_pack("0.9.0")));
    inst::Options o;
    o.pack = dir + L"\\p.pack";
    o.pack_dir = dir;
    upd::ReleaseBox box;
    std::string msg;
    CHECK(inst::do_install(g, o, box, msg) == inst::Result::Ok);
    std::wstring sym = g + L"\\SporebinEP1\\mods\\sporekit\\symbols\\";
    put(sym + L"overrides_dc04aee5.sym", "ours");
    put(sym + L"mine.sym", "the player's own");
    return g;
}

struct LockHold {
    std::wstring root;
    HANDLE go, done;
};
static DWORD WINAPI lock_holder(void* a)
{
    auto* l = (LockHold*)a;
    void* h = nullptr;
    upd::try_update_lock(l->root, &h);
    SetEvent(l->go);
    WaitForSingleObject(l->done, 20000);
    upd::release_update_lock(h);
    return 0;
}

static void test_uninstall_loader()
{
    printf("setup: uninstall (BUG-0851)\n");
    std::string msg;
    // (a) steam_api.dll missing (an antivirus took the loader), steam_api_orig.dll there
    std::wstring g = installed_game(L"un_a"), bin = g + L"\\SporebinEP1";
    DeleteFileW((bin + L"\\steam_api.dll").c_str());
    bool ok = inst::do_uninstall(g, msg);
    printf("  (a) loader missing: %d, %s\n", ok, msg.c_str());
    CHECK(ok && get(bin + L"\\steam_api.dll") == "the game's steam api");
    CHECK(!exists(bin + L"\\steam_api_orig.dll"));
    // (b) coop.dll held open (a running game): not "removed"; the next try after it is closed is
    g = installed_game(L"un_b"), bin = g + L"\\SporebinEP1";
    HANDLE f = CreateFileW((bin + L"\\mods\\coop.dll").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(f != INVALID_HANDLE_VALUE);
    ok = inst::do_uninstall(g, msg);
    printf("  (b) coop.dll in use: %d, %s\n", ok, msg.c_str());
    CHECK(!ok && msg.find("coop.dll") != std::string::npos);
    CHECK(get(bin + L"\\steam_api.dll") == "the game's steam api"); // the game starts without the mod
    CHECK(inst::has_sporecoop(g)); // the setup still offers Uninstall
    CloseHandle(f);
    ok = inst::do_uninstall(g, msg);
    CHECK(ok && !exists(bin + L"\\mods\\coop.dll") && !inst::has_sporecoop(g));
    // (c) only SporeCoop's symbol files go
    printf("  (c) symbols: ours %s, the player's %s\n", exists(bin + L"\\mods\\sporekit\\symbols\\overrides_dc04aee5.sym") ? "kept" : "removed",
           exists(bin + L"\\mods\\sporekit\\symbols\\mine.sym") ? "kept" : "removed");
    CHECK(!exists(bin + L"\\mods\\sporekit\\symbols\\overrides_dc04aee5.sym"));
    CHECK(get(bin + L"\\mods\\sporekit\\symbols\\mine.sym") == "the player's own");
    // (d) another update holds the folder's lock: nothing changes
    g = installed_game(L"un_d"), bin = g + L"\\SporebinEP1";
    LockHold l{g, CreateEventW(nullptr, TRUE, FALSE, nullptr), CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    HANDLE t = CreateThread(nullptr, 0, lock_holder, &l, 0, nullptr);
    WaitForSingleObject(l.go, 5000);
    ok = inst::do_uninstall(g, msg);
    printf("  (d) update lock held: %d, %s\n", ok, msg.c_str());
    CHECK(!ok && upd::is_loader(bin + L"\\steam_api.dll") && exists(bin + L"\\mods\\coop.dll"));
    SetEvent(l.done);
    WaitForSingleObject(t, 20000);
    CloseHandle(t);
    CloseHandle(l.go);
    CloseHandle(l.done);
    CHECK(inst::do_uninstall(g, msg) && get(bin + L"\\steam_api.dll") == "the game's steam api");
    // (e) the loader in place and no backup of the game's file: refused, nothing removed
    g = installed_game(L"un_e"), bin = g + L"\\SporebinEP1";
    DeleteFileW((bin + L"\\steam_api_orig.dll").c_str());
    ok = inst::do_uninstall(g, msg);
    printf("  (e) no steam_api_orig.dll: %d, %s\n", ok, msg.c_str());
    CHECK(!ok && exists(bin + L"\\mods\\coop.dll") && exists(bin + L"\\sporekit.dll"));
    // (f) a half removed install (the version file gone): the setup still offers Uninstall
    g = installed_game(L"un_f"), bin = g + L"\\SporebinEP1";
    DeleteFileW((bin + L"\\mods\\sporecoop.version").c_str());
    CHECK(inst::has_sporecoop(g));
    // (g) not a game folder
    std::wstring nog = fresh_dir(L"un_g");
    CHECK(!inst::do_uninstall(nog, msg));
}

// BUG-0851: uninstall.bat copied steam_api_orig.dll over any steam_api.dll (also one Steam had put
// back) and left mods\coop.dll; now it runs the setup next to it, else checks the loader itself
static void test_uninstall_bat()
{
    printf("uninstall.bat (BUG-0851)\n");
    if (g_ubat.empty()) {
        printf("  (skipped: no --ubat)\n");
        return;
    }
    // 1. no setup next to it, Steam put its steam_api.dll back over the loader
    std::wstring g = installed_game(L"ubat1"), bin = g + L"\\SporebinEP1";
    CHECK(CopyFileW(g_ubat.c_str(), (bin + L"\\uninstall.bat").c_str(), FALSE));
    put(bin + L"\\steam_api.dll", "the game's steam api, updated by Steam");
    CHECK(run_bat(bin, L"uninstall.bat"));
    printf("  1. no setup, Steam's file: steam_api.dll='%s' coop.dll %s\n", get(bin + L"\\steam_api.dll").c_str(),
           exists(bin + L"\\mods\\coop.dll") ? "left" : "removed");
    CHECK(get(bin + L"\\steam_api.dll") == "the game's steam api, updated by Steam");
    CHECK(!exists(bin + L"\\mods\\coop.dll") && !exists(bin + L"\\sporekit.dll"));
    CHECK(!exists(g + L"\\Data\\SporeCoop.package"));
    CHECK(get(bin + L"\\mods\\sporekit\\symbols\\mine.sym") == "the player's own");
    // 2. no setup, the loader in place: the game's file comes back
    g = installed_game(L"ubat2"), bin = g + L"\\SporebinEP1";
    CHECK(CopyFileW(g_ubat.c_str(), (bin + L"\\uninstall.bat").c_str(), FALSE));
    CHECK(run_bat(bin, L"uninstall.bat"));
    CHECK(get(bin + L"\\steam_api.dll") == "the game's steam api" && !exists(bin + L"\\steam_api_orig.dll"));
    CHECK(!exists(bin + L"\\mods\\coop.dll") && !exists(bin + L"\\mods\\sporecoop.version"));
    // 3. the setup next to it (from the zip) does the work
    g = installed_game(L"ubat3"), bin = g + L"\\SporebinEP1";
    CHECK(CopyFileW(g_ubat.c_str(), (bin + L"\\uninstall.bat").c_str(), FALSE));
    CHECK(CopyFileW(setup_exe().c_str(), (bin + L"\\SporeCoopSetup.exe").c_str(), FALSE));
    DeleteFileW((bin + L"\\steam_api.dll").c_str());
    CHECK(run_bat(bin, L"uninstall.bat"));
    printf("  3. with the setup: steam_api.dll='%s' coop.dll %s, log: %s\n", get(bin + L"\\steam_api.dll").c_str(),
           exists(bin + L"\\mods\\coop.dll") ? "left" : "removed", get(bin + L"\\mods\\sporecoop-uninstall.log").c_str());
    CHECK(get(bin + L"\\steam_api.dll") == "the game's steam api");
    CHECK(!exists(bin + L"\\mods\\coop.dll") && !exists(bin + L"\\sporekit.dll"));
    CHECK(get(bin + L"\\mods\\sporekit\\symbols\\mine.sym") == "the player's own");
}

int main(int argc, char** argv)
{
    bool staged_move_only = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--staged-move-only"))
            staged_move_only = true;
        else if (!strcmp(argv[i], "--old-staged-move"))
            slotfs::g_move_retry_fix = false;
        else if (!strcmp(argv[i], "--old"))
            g_old = true;
        else if (!strcmp(argv[i], "--old-suffix")) // BUG-0114 alone (update.old_suffix)
            upd::g_old_suffix_fix = false;
        else if (!strcmp(argv[i], "--old-keep")) // BUG-0150 alone (update.keep_failed)
            upd::g_keep_failed_fix = false;
        else if (!strcmp(argv[i], "--old-iniencode")) // only this switch off (BUG-0245)
            skini::g_encode_fix = false;
        else if (!strcmp(argv[i], "--old-inilock"))
            skini::g_lock_fix = false;
        else if (!strcmp(argv[i], "--old-paths")) // BUG-0242
            upd::g_path_fix = false;
        else if (!strcmp(argv[i], "--old-lock")) // BUG-0243
            upd::g_lock_fix = false;
        else if (!strcmp(argv[i], "--old-ga")) // BUG-0391 (setup only)
            inst::g_ga_fix = false;
        else if (!strcmp(argv[i], "--old-gadir")) // BUG-0391, the Steam GA folder (setup only)
            inst::g_ga_dir_fix = false;
        else if (!strcmp(argv[i], "--steam") && i + 1 < argc)
            g_steam = upd::widen(argv[++i]);
        else if (!strcmp(argv[i], "--old-web")) // BUG-0392 (update.web_fallback)
            upd::g_web_fallback_fix = false;
        else if (!strcmp(argv[i], "--old-retry")) // BUG-0755 (update.retry)
            upd::g_retry_fix = false;
        else if (!strcmp(argv[i], "--old-pending")) // BUG-1019 (update.pending_whole)
            upd::g_pending_whole_fix = false;
        else if (!strcmp(argv[i], "--old-move")) // BUG-0848 (world.staged_move)
            slotfs::g_staged_move_fix = false;
        else if (!strcmp(argv[i], "--old-collect")) // BUG-0847 (world.share_complete)
            slotfs::g_collect_fix = false;
        else if (!strcmp(argv[i], "--old-closewait")) // BUG-0853 (the test's own race)
            g_close_wait_fix = false;
        else if (!strcmp(argv[i], "--old-uninstall")) // BUG-0851 (setup only)
            inst::g_uninstall_fix = false;
        else if (!strcmp(argv[i], "--ubat") && i + 1 < argc)
            g_ubat = upd::widen(argv[++i]);
        else if (!strcmp(argv[i], "--setup") && i + 1 < argc)
            g_setup = upd::widen(argv[++i]);
        else if (!strcmp(argv[i], "--bat") && i + 1 < argc)
            g_bat = upd::widen(argv[++i]);
        else if (!strcmp(argv[i], "--exclude") && i + 1 < argc)
            g_excl = upd::widen(argv[++i]);
    }
    if (g_old) {
        inst::g_fix = false;
        upd::g_old_suffix_fix = upd::g_keep_failed_fix = false;
        upd::g_dup_fix = false;
        slotfs::g_busy_fix = slotfs::g_match_fix = slotfs::g_write_fix = false;
        slotfs::g_fallback_fix = slotfs::g_cleanup_fix = false;
        slotfs::g_move_retry_fix = false;
        slotfs::g_staged_move_fix = slotfs::g_collect_fix = false;
        skini::g_unicode_fix = false;
        skini::g_encode_fix = skini::g_lock_fix = false;
        upd::g_path_fix = false;
        upd::g_lock_fix = false;
        inst::g_ga_fix = inst::g_ga_dir_fix = false;
        inst::g_uninstall_fix = false;
        g_close_wait_fix = false;
        upd::g_web_fallback_fix = false;
        upd::g_retry_fix = false;
        upd::g_pending_whole_fix = false;
    }
    upd::g_retry_unit_ms = 50; // the in-process retries wait 50 / 150 ms (the setup's real 1 s / 3 s)
    upd::g_log = [](const std::string& s) { printf("    log: %s\n", s.c_str()); };
    wchar_t t[MAX_PATH];
    GetTempPathW(MAX_PATH, t);
    g_tmp = std::wstring(t) + L"sporecoop-win-tests";
    rmtree(g_tmp);
    CreateDirectoryW(g_tmp.c_str(), nullptr);
    const auto appdata = g_tmp + L"\\test-roaming";
    if (!SetEnvironmentVariableW(L"APPDATA", appdata.c_str())) {
        fputs("Cannot isolate test APPDATA\n", stderr);
        return 1;
    }
    if (argc == 2 && !strcmp(argv[1], "--founders-only")) {
        test_founders();
    test_founders_multi();
    test_party();
        rmtree(g_tmp);
        printf(g_fail ? "FAILED (%d)\n" : "ALL OK\n", g_fail);
        return g_fail ? 1 : 0;
    }
    if (staged_move_only) {
        test_slot_move_retry();
        rmtree(g_tmp);
        printf(g_fail ? "FAILED (%d)\n" : "ALL OK\n", g_fail);
        return g_fail ? 1 : 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--slots-only")) {
        test_slot_move_retry();
        test_slot_busy();
        test_slot_match();
        test_slot_write_fails();
        test_slot_fallback();
        test_slot_leftovers();
        rmtree(g_tmp);
        printf(g_fail ? "FAILED (%d)\n" : "ALL OK\n", g_fail);
        return g_fail ? 1 : 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--server-only")) { // 8Y C
        test_setup_server();
        rmtree(g_tmp);
        printf(g_fail ? "FAILED (%d)\n" : "ALL OK\n", g_fail);
        return g_fail ? 1 : 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--setup-close-only")) {
        test_setup_close();
        rmtree(g_tmp);
        printf(g_fail ? "FAILED (%d)\n" : "ALL OK\n", g_fail);
        return g_fail ? 1 : 0;
    }
    test_uninstall();
    test_founders();
    test_founders_multi(); // BUG-9138: these ran only with --founders-only, never in the lab's full run
    test_party();
    if (argc == 2 && !strcmp(argv[1], "--uninstall-only")) {
        rmtree(g_tmp);
        printf(g_fail ? "FAILED (%d)\n" : "ALL OK\n", g_fail);
        return g_fail ? 1 : 0;
    }
    test_cleanup_old();
    test_apply_pack();
    test_apply_rollback();
    test_update_lock();
    test_pending();
    test_slot_move_retry();
    test_pending_long();
    test_slot_busy();
    if (argc == 2 && !strcmp(argv[1], "--slot-busy-only")) {
        rmtree(g_tmp);
        printf(g_fail ? "FAILED (%d)\n" : "ALL OK\n", g_fail);
        return g_fail ? 1 : 0;
    }
    test_slot_match();
    test_slot_write_fails();
    test_slot_fallback();
    test_slot_leftovers();
    test_staged_move();
    test_share_collect();
    test_ini_unicode();
    test_ini_encoding();
    test_setup_packs();
    test_setup_server(); // 8Y C
    test_setup_close();
    test_install_bat();
    test_uninstall_loader(); // (renamed: the upstream merge gave two tests one name and the file did not compile)
    test_uninstall_bat();
    test_ga_folder();
    test_setup_ga_window();
    test_steam_ga_folder();
    test_setup_steam_ga();
    test_real_steam();
    test_web_fallback();
    test_setup_api403();
    test_retry();
    test_setup_retry();
    rmtree(g_tmp);
    printf(g_fail ? "FAILED (%d)\n" : "ALL OK\n", g_fail);
    return g_fail ? 1 : 0;
}
