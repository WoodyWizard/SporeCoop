// Portable recent servers list (BUG-0867): stores up to 8 recently joined server addresses,
// most recent first, deduplicated. Persisted in mods/coop.ini under [recent] (server0..server7).
#pragma once

#include <algorithm>
#include <cctype>
#include <functional>
#include <string>
#include <vector>

#include "net/udp.h"

namespace recent {

constexpr size_t kMaxRecent = 8;

inline std::string clean_address(const std::string& raw)
{
    std::string s = net::Endpoint::clean(raw);
    while (!s.empty() && std::isspace((unsigned char)s.front()))
        s.erase(s.begin());
    while (!s.empty() && std::isspace((unsigned char)s.back()))
        s.pop_back();
    if (s.size() > 128)
        s.resize(128);
    // Reject control characters / newlines (never break INI lines)
    s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char c) { return c < 0x20 || c == 0x7F; }), s.end());
    return s;
}

inline bool same_address(const std::string& a, const std::string& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); i++)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
            return false;
    return true;
}

class List {
public:
    bool add(const std::string& raw)
    {
        std::string addr = clean_address(raw);
        if (addr.empty())
            return false;
        items_.erase(std::remove_if(items_.begin(), items_.end(),
                                    [&](const std::string& x) { return same_address(x, addr); }),
                     items_.end());
        items_.insert(items_.begin(), addr);
        if (items_.size() > kMaxRecent)
            items_.resize(kMaxRecent);
        return true;
    }

    bool remove(const std::string& raw)
    {
        std::string addr = clean_address(raw);
        size_t before = items_.size();
        items_.erase(std::remove_if(items_.begin(), items_.end(),
                                    [&](const std::string& x) { return same_address(x, addr); }),
                     items_.end());
        return items_.size() != before;
    }

    void clear() { items_.clear(); }
    const std::vector<std::string>& items() const { return items_; }
    size_t size() const { return items_.size(); }
    bool empty() const { return items_.empty(); }

    // Load server0..server7 via reader(key) -> string
    void load(const std::function<std::string(const char* key)>& read_key)
    {
        items_.clear();
        for (size_t i = 0; i < kMaxRecent; i++) {
            char key[16];
            snprintf(key, sizeof key, "server%u", (unsigned)i);
            std::string addr = clean_address(read_key(key));
            if (addr.empty())
                continue;
            bool dup = false;
            for (const auto& existing : items_)
                if (same_address(existing, addr)) {
                    dup = true;
                    break;
                }
            if (!dup)
                items_.push_back(addr);
        }
    }

    // Save server0..server7 via writer(key, value); clears unused slots with ""
    void save(const std::function<void(const char* key, const std::string& value)>& write_key) const
    {
        for (size_t i = 0; i < kMaxRecent; i++) {
            char key[16];
            snprintf(key, sizeof key, "server%u", (unsigned)i);
            write_key(key, i < items_.size() ? items_[i] : std::string());
        }
    }

private:
    std::vector<std::string> items_;
};

} // namespace recent
