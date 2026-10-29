// Settings files (mods\*.ini) through the Windows profile API, in Unicode (BUG-0142).
//
// The profile API reads and writes a file without a byte order mark in the system ANSI codepage:
// a name outside it ("Вася" on a Western Windows or Wine) was saved as "????", and a file edited
// by hand in UTF-8 was read garbled ("JÃ¶rg"). A file that holds anything but ASCII, or gets a
// value that is not ASCII, is rewritten once as UTF-16LE with a BOM (from UTF-8 when its bytes are
// valid UTF-8, else from the ANSI codepage); the profile API then keeps it in Unicode. ASCII-only
// files stay as they are (tools edit them with sed). Standalone: tests/win_tests.cpp tests it.
#pragma once
#include <string>

namespace skini {

extern bool g_unicode_fix; // false: the old behaviour (kit.fix iniutf 0, win_tests --old)
// BUG-0245: UTF-16 BE is converted losslessly, a file with NUL bytes (UTF-16 without a BOM, or not an
// ini file) is left as it is; false = only FF FE counts as Unicode (kit.fix iniencode 0)
extern bool g_encode_fix;
// BUG-0245: a named mutex per settings file around the conversion and the writes (two instances on
// one folder), and a non-ASCII value is not written when the file could not be made Unicode (it
// became "????"); false = no mutex, written anyway (kit.fix inilock 0)
extern bool g_lock_fix;
extern void (*g_log)(const std::string&); // problems worth one log line (set by SporeKit; may be null)

// rewrites `file` as UTF-16LE with a BOM when it is not already; `force`: also when it is ASCII
// only (or missing: then an empty Unicode file is made). true when the file is Unicode afterwards.
bool make_unicode(const std::wstring& file, bool force);

std::string read_str(const std::wstring& file, const char* section, const char* key, const char* def);
int read_int(const std::wstring& file, const char* section, const char* key, int def);
bool write_str(const std::wstring& file, const char* section, const char* key, const char* value);

// the text of an ini file as UTF-8 (a UTF-16 file converted; others as they are): report archives
std::string to_utf8_text(const std::string& bytes);

} // namespace skini
