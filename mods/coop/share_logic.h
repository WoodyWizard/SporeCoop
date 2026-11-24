// BUG-1434 (WP-W1 of 0.7.17, docs/plan-0.7.17.md item 8): which files of the host's save slot a world share sends.
// Portable (no game, no Win32): tests/core_tests.cpp.
//
// A save slot (Games\GameN) is a galaxy: every game started on a free disc of the galaxy menu is one more
// "<planet>.spo" next to the galaxy's own files (stars.db, planetRecords.pkp/.pkt, PlanetScripts.pld, GGEUserData.dat,
// lastSave.pld, snapshot.spo, the dbread markers). Up to 0.7.16.1 a share sent the whole slot, so a guest's co-op copy
// held the host's old planets too; after a role swap the new host started one of them instead of the session's world
// (a player report, 2026-11-22). Now a share sends the played planet's file and the galaxy's own files only: the other
// planets' files are left out. The galaxy's files still list those planets (the galaxy menu shows them; the game marks a
// missing save and its Play step says "Bad or Missing Saved Game" instead of loading one: docs/re/space.md
// #space-share-one-planet). Only to a guest whose world family is 1.1 or newer (an older mod gets the whole slot as
// before, its rules for a copy count the planet files), only when the played planet's file is in the list (a share
// from a menu, no game played, sends the slot as before). Switch `coop.fix world.share_planet off`: the whole slot
// (0.7.16.1).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace share_logic {

// the world family's minor from which a guest takes a share of one planet (messages.def world 1.1, 0.7.17)
constexpr uint16_t kMinor = 1;

inline std::string lower_ascii(std::string s)
{
    for (char& c : s)
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
    return s;
}

// "<name>.spo" at the top of the slot, not the game's temporary "snapshot.spo" (any case: Windows names)
inline bool is_planet(const std::string& rel)
{
    std::string l = lower_ascii(rel);
    return l.size() > 4 && l.compare(l.size() - 4, 4, ".spo") == 0 && l != "snapshot.spo" &&
           l.find_first_of("/\\") == std::string::npos;
}

// a file the share of the played planet `played_spo` ("Name.spo") leaves out: another planet's save
inline bool left_out(const std::string& rel, const std::string& played_spo)
{
    return is_planet(rel) && lower_ascii(rel) != lower_ascii(played_spo);
}

// host: guest families -> the guest takes a share of one planet (its world family has the same rev, minor kMinor+)
inline bool peer_takes_one(bool has_families, bool found, uint16_t rev, uint16_t minor, uint16_t my_rev)
{
    return has_families && found && rev == my_rev && minor >= kMinor;
}

// drops the other planets' files from `files` (each has a `rel`); nothing is dropped when `played_spo` is not a planet
// file or not among them. Returns how many were dropped.
template <class File> size_t keep_played(std::vector<File>& files, const std::string& played_spo)
{
    if (!is_planet(played_spo))
        return 0;
    bool have = false;
    for (auto& f : files)
        if (lower_ascii(f.rel) == lower_ascii(played_spo))
            have = true;
    if (!have)
        return 0;
    size_t n = 0;
    for (size_t i = 0; i < files.size();)
        if (left_out(files[i].rel, played_spo)) {
            files.erase(files.begin() + (long)i);
            n++;
        } else
            i++;
    return n;
}

// planet files among `names` other than the planet `planet` (no ".spo"): a co-op copy's old planets (the copy notice)
inline int other_planets(const std::vector<std::string>& names, const std::string& planet)
{
    int n = 0;
    for (auto& s : names)
        if (left_out(s, planet + ".spo"))
            n++;
    return n;
}

// BUG-1537: a queued world load waits while the window is minimized or the D3D9 device is lost / settling after a
// Reset (Terrain::cTerrainSphere builds heightmaps and nest stamps on D3D9 render targets; resets mid-load dropped
// guest nest terrain by 1.3-3.0 m). Returns a short reason string while blocked, nullptr when safe to load.
constexpr double kDeviceResetSettle = 1.5;
inline const char* load_device_blocker(bool fix, bool test_busy, bool iconic, bool device_lost, bool coop_ok,
                                       double now, double reset_at)
{
    if (!fix)
        return nullptr;
    if (test_busy)
        return "D3D9 device held (test)";
    if (iconic)
        return "game window is minimized";
    if (device_lost)
        return "D3D9 device is lost";
    if (!coop_ok)
        return "D3D9 device is not cooperative";
    if (reset_at > 0.0 && now >= reset_at && now - reset_at < kDeviceResetSettle)
        return "D3D9 device settling after Reset";
    return nullptr;
}

} // namespace share_logic
