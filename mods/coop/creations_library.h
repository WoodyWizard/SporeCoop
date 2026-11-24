// Persistent shared creations library (BUG-0873, creations 1.3):
// Keeps creations received from co-op partners across sessions and offline restarts in
// %APPDATA%/Spore/SporeCoop/SharedLibrary with its own Sporepedia tab ("Shared Creations")
// while keeping them out of My Creations.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "net/dbpf.h"

namespace creations {

void library_init();
void library_tick(double now);
void library_shutdown();
bool library_kind_on(uint8_t kind);
void library_message(uint8_t kind, uint8_t from, uint8_t owner, const std::vector<uint8_t>& data);
void library_join(uint8_t id);
void library_leave(uint8_t id);
void library_end();
void library_on_partner_records(const std::string& sender, const std::string& creation_name, uint32_t group,
                                const std::vector<dbpf::Record>& recs, bool origin_own = true);
std::string library_status();
std::string library_debug(const std::vector<std::string>& args);

} // namespace creations
