// The registry of the bug fixes' test switches (fixes.h, BUG-0116).
#include "fixes.h"

#include <cstring>

namespace fix {

// function-local statics: modules register from their static initialisers, in any order
static std::vector<Info>& table()
{
    static std::vector<Info> t;
    return t;
}
static int& dup_count()
{
    static int n = 0;
    return n;
}

bool reg(const char* name, const char* ticket, bool* on, const char* what, bool unsafe_off)
{
    // R2: one name per bool too (a second name for the same bool would switch two "fixes" at once)
    bool same_bool = false;
    for (const Info& f : table())
        same_bool |= f.on == on;
    if (!name || !*name || !on || find(name) || same_bool) {
        dup_count()++;
        return false;
    }
    table().push_back({name, ticket ? ticket : "", on, what ? what : "", unsafe_off});
    return true;
}

const std::vector<Info>& all() { return table(); }
int duplicates() { return dup_count(); }

const Info* find(const std::string& name)
{
    for (const Info& f : table())
        if (name == f.name)
            return &f;
    return nullptr;
}

bool set(const std::string& name, bool on)
{
    const Info* f = find(name);
    if (!f)
        return false;
    *f->on = on;
    return true;
}

size_t set_all(bool on, bool unsafe, size_t* skipped)
{
    size_t n = 0, skip = 0;
    for (const Info& f : table()) {
        if (!on && f.unsafe_off && !unsafe) {
            skip++;
            continue;
        }
        *f.on = on;
        n++;
    }
    if (skipped)
        *skipped = skip;
    return n;
}

static std::string fold(std::string s)
{
    for (char& c : s)
        if (c == '_' || c == '.' || c == '-')
            c = '_';
    return s;
}

std::vector<std::string> suggest(const std::string& tail)
{
    std::vector<std::string> out;
    std::string t = fold(tail);
    if (t.empty())
        return out;
    for (const Info& f : table()) {
        std::string n = fold(f.name);
        // the whole name or its end after a separator: "session_end" and "end" find "session.end"
        bool ends = n.size() >= t.size() && n.compare(n.size() - t.size(), t.size(), t) == 0 &&
                    (n.size() == t.size() || n[n.size() - t.size() - 1] == '_');
        if (ends)
            out.push_back(f.name);
    }
    return out;
}

static std::string line(const Info& f)
{
    return std::string(f.name) + (*f.on ? " on " : " off ") + f.ticket + (f.unsafe_off ? " unsafe" : "") + "  " +
           f.what + "\n";
}

std::string list()
{
    size_t off = 0;
    for (const Info& f : table())
        off += !*f.on;
    std::string s = std::to_string(table().size()) + " fixes (" + std::to_string(off) + " off)";
    if (dup_count())
        s += ", " + std::to_string(dup_count()) + " refused registrations (duplicate name or bool)";
    s += "; a reload of coop.dll sets every switch on again\n";
    for (const Info& f : table())
        s += line(f);
    return s;
}

static int parse_state(const std::string& v)
{
    if (v == "on" || v == "1")
        return 1;
    if (v == "off" || v == "0")
        return 0;
    return -1;
}

std::string command(const std::vector<std::string>& a, bool* ok)
{
    if (ok)
        *ok = true;
    const char* usage = "usage: coop.fix [list] | <name> [on|off] | all on|off [--unsafe]  (0 / off = the code before "
                        "the fix, tests; `all off` leaves the unsafe ones on without --unsafe)\n";
    if (a.empty() || a[0] == "list")
        return list();
    int v = a.size() >= 2 ? parse_state(a[1]) : -2;
    bool unsafe = a.size() == 3 && a[0] == "all" && a[2] == "--unsafe";
    if ((a.size() > 2 && !unsafe) || v == -1) {
        if (ok)
            *ok = false;
        return usage;
    }
    if (a[0] == "all") {
        if (v < 0) {
            if (ok)
                *ok = false;
            return usage;
        }
        size_t skipped = 0;
        size_t n = set_all(v == 1, unsafe, &skipped);
        std::string r = "all " + std::to_string(n) + " fixes " + (v ? "on" : "off");
        if (!v)
            r += ", " + std::to_string(skipped) + " unsafe skipped";
        return r + "\n";
    }
    const Info* f = find(a[0]);
    if (!f) {
        if (ok)
            *ok = false;
        std::string r = "unknown fix '" + a[0] + "'";
        std::vector<std::string> s = suggest(a[0]);
        for (size_t i = 0; i < s.size(); i++)
            r += (i ? ", " : " (did you mean ") + s[i];
        return r + (s.empty() ? " (coop.fix list)\n" : ")\n");
    }
    if (v >= 0)
        *f->on = v == 1;
    return line(*f);
}

} // namespace fix
