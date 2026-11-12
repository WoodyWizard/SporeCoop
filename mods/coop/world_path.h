// The file names of a received world (world.cpp): portable, so tests/validate_tests.cpp checks them on Linux.
// A received world comes from another player's machine: only plain relative names inside the save slot.
#pragma once

#include <cstring>
#include <string>

namespace world_path {

// BUG-0785: a path part Windows opens as a device instead of a file: CON, PRN, AUX, NUL, CONIN$, CONOUT$, COM0-9,
// LPT0-9 and COM / LPT with a superscript 1, 2 or 3, case-insensitive, with any extension ("nul.spo", "COM1.txt")
// and with spaces before the extension ("con .txt": Windows drops them). The updater's pack check has the same
// list (updater/pack.cpp device_name, BUG-0242).
inline bool device_name(const std::string& part)
{
    std::string b;
    for (char c : part.substr(0, part.find('.')))
        b += (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    while (!b.empty() && b.back() == ' ')
        b.pop_back();
    static const char* const dev[] = {"con", "prn", "aux", "nul", "conin$", "conout$"};
    for (auto* d : dev)
        if (b == d)
            return true;
    if (b.size() >= 4 && (b.compare(0, 3, "com") == 0 || b.compare(0, 3, "lpt") == 0)) {
        std::string n = b.substr(3);
        if (n.size() == 1 && n[0] >= '0' && n[0] <= '9')
            return true;
        if (n == "\xC2\xB9" || n == "\xC2\xB2" || n == "\xC2\xB3") // superscript one, two, three
            return true;
    }
    return false;
}

// a relative path inside the save slot: at most 4 parts, 200 bytes; no empty part (leading or doubled
// separators), no "." or "..", no trailing dot or space (Windows drops them), no drive or stream (':'),
// wildcards or control characters; with `devices` (world.device_names) no device name either
inline bool safe_rel(const std::string& rel, bool devices)
{
    if (rel.empty() || rel.size() > 200)
        return false;
    size_t start = 0;
    int depth = 0;
    while (start <= rel.size()) {
        size_t end = rel.find_first_of("/\\", start);
        if (end == std::string::npos)
            end = rel.size();
        std::string part = rel.substr(start, end - start);
        if (part.empty() || part.back() == '.' || part.back() == ' ' || ++depth > 4)
            return false;
        // a planet file is named as the player typed it ("Bob's World!.spo"): any character a Windows
        // file name may have, but no ':' (drives, streams), wildcards or control characters
        for (unsigned char c : part)
            if (c < 0x20 || c == 0x7F || strchr("<>:\"|?*", c))
                return false;
        if (devices && device_name(part))
            return false;
        start = end + 1;
    }
    return true;
}

} // namespace world_path
