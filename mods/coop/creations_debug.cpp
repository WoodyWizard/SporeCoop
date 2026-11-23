// coop.creations: the creations module's console command (tests and diagnostics); moved out of creations.cpp (C8 of
// 0.7.15, BUG-1183).
#include "creations_internal.h"

namespace creations {

std::string debug(const std::vector<std::string>& a)
{
    if (a.size() >= 2 && a[0] == "ps_null") { // ps_null <frames>: tests (BUG-1057), the host's evolved species is taken as
        // not known to this game for that many player-species frames (as a species registered a little later)
        g_ps_test_null = (unsigned)atoi(a[1].c_str());
        return "player species unknown for " + std::to_string(g_ps_test_null) + " frames";
    }
    if (a.size() >= 2 && a[0] == "profile") { // tests (BUG-0457): the species profile the game has for a creature key
        uint32_t inst = (uint32_t)strtoul(a[1].c_str(), nullptr, 16);
        void* p = game::species_by_key({inst, kInfo[kCreature].type, kInfo[kCreature].group});
        char b[160];
        snprintf(b, sizeof b, "profile %08X = %p key=%08X version=%u\n", inst, p,
                 p && skg::readable((char*)p + 0x504, 0x40) ? skg::field<uint32_t>(p, 0x504) : 0,
                 p && skg::readable((char*)p + 0x53C, 4) ? skg::field<uint32_t>(p, 0x53C) : 0);
        return b;
    }
    if (a.size() >= 2 && a[0] == "sendmeta") { // tests (BUG-0802): our <kind>'s bundle with its metadata only, as a
        // 0.7.12 guest sent a renamed creation whose model is in another package (to the host, or from the host to all)
        int k = a[1] == "cell" ? kCell : a[1] == "ufo" ? kUfo : kCreature;
        Option o = mine((Kind)k);
        if (!o.key.instance || !C.sess || !C.sess->connected())
            return "no creation of ours / no session";
        Bundle b;
        b.key = o.key;
        if (!make_bundle((Kind)k, o.key, b) || b.recs.empty())
            records_from_game({o.key.instance, kBem, kInfo[k].group}, b.recs);
        std::erase_if(b.recs, [](const dbpf::Record& r) { return r.key.type != kMeta; });
        if (b.recs.empty())
            return "no metadata record";
        if (a.size() >= 3) { // a new name in it: another bundle id
            b.recs[0].data.push_back((uint8_t)atoi(a[2].c_str()));
        }
        std::vector<uint8_t> data = pack(b);
        uint32_t id = fnv(data.data(), data.size());
        send_bundle(is_host() ? -1 : 0, C.sess->my_id(), (Kind)k, id, data);
        char r[96];
        snprintf(r, sizeof r, "sent %s %08X with %u record(s), no model\n", kInfo[k].name, o.key.instance, (unsigned)b.recs.size());
        return r;
    }
    if (a.size() >= 2 && (a[0] == "next" || a[0] == "prev")) {
        int k = a[1] == "cell" ? kCell : a[1] == "ufo" ? kUfo : kCreature;
        cycle((Kind)k, a[0] == "next" ? 1 : -1);
        return "chosen: " + mine((Kind)k).name;
    }
    if (a.size() >= 3 && a[0] == "hideown") { // tests (BUG-1476): hideown <kind> on|off: our My Creations of it read as empty
        int k = a[1] == "cell" ? kCell : a[1] == "ufo" ? kUfo : kCreature;
        test_hide_own(k, a[2] == "on" || a[2] == "1");
        return "own " + a[1] + " creations " + (a[2] == "on" || a[2] == "1" ? "hidden" : "shown") + ": " +
               std::to_string(options((Kind)k).size()) + " choice(s)" + (premades_listed((Kind)k) ? " (game cells)" : "") + "\n";
    }
    if (a.size() >= 2 && a[0] == "choose") { // choose <kind> <hex instance|none>
        int k = a[1] == "cell" ? kCell : a[1] == "ufo" ? kUfo : kCreature;
        uint32_t inst = a.size() > 2 && a[2] != "none" ? (uint32_t)strtoul(a[2].c_str(), nullptr, 16) : 0;
        choose((Kind)k, inst ? Key{inst, kInfo[k].type, kInfo[k].group} : Key{});
        return "chosen";
    }
    if (a.size() >= 2 && a[0] == "unchoose") { // tests (BUG-0345): no explicit choice of <kind> (choices.ini entry removed)
        int k = a[1] == "cell" ? kCell : a[1] == "ufo" ? kUfo : kCreature;
        g_chosen[k] = false;
        g_choice[k] = {};
        if (find_paths())
            WritePrivateProfileStringW(L"creations", wide(kInfo[k].name).c_str(), nullptr, (g_dir + L"\\choices.ini").c_str());
        g_last_check = 0;
        return "no choice";
    }
    if (!a.empty() && a[0] == "worlds") { // tests (BUG-0345): worlds [forget]: the own creation recorded for this world
        uint32_t w = modes::current_world();
        if (a.size() >= 2 && a[1] == "forget") {
            for (int k = 0; k < kKinds; k++)
                world_record_set(k, w, 0);
            g_own_told.clear();
            g_last_check = 0;
        }
        if (a.size() >= 4 && a[1] == "set") { // worlds set <kind> <hex|0>: restore a record (tests leave the profile as it was)
            int k = a[2] == "cell" ? kCell : kCreature;
            world_record_set(k, w, (uint32_t)strtoul(a[3].c_str(), nullptr, 16));
            g_last_check = 0;
        }
        auto ch = [](int k) {
            char c[16];
            if (!g_chosen[k])
                return std::string("-");
            if (!g_choice[k].instance)
                return std::string("none");
            snprintf(c, sizeof c, "%08X", g_choice[k].instance);
            return std::string(c);
        };
        char b[200];
        snprintf(b, sizeof b, "world %08X creature=%08X cell=%08X records=%u switch=%d choice_creature=%s choice_cell=%s\n", w,
                 world_record(kCreature, w), world_record(kCell, w), g_world_records, g_guest_world_fix ? 1 : 0,
                 ch(kCreature).c_str(), ch(kCell).c_str());
        return b;
    }
    if (!a.empty() && a[0] == "sameaddr") { // tests (BUG-0119): the next new avatar looks like the old one
        g_sim_same_addr = true;
        return "the next avatar is taken for the old one (a reused address)";
    }
    if (a.size() >= 2 && a[0] == "worlddrop") { // worlddrop on|off (tests, BUG-0055)
        g_world_drop = a[1] != "off";
        return std::string("the host's world creations are dropped at the session end: ") + (g_world_drop ? "on" : "off") +
               " (dropped " + std::to_string(g_world_dropped) + ", registered " + std::to_string(g_world_pkgs.size()) + ")";
    }
    if (a.size() >= 2 && a[0] == "fakepng") { // fakepng <hex inst> [<hex group>]: tests (BUG-0643), a broken png answers
        // the creature's png key above every package (what a game without the creation's picture has)
        uint32_t inst = (uint32_t)strtoul(a[1].c_str(), nullptr, 16);
        uint32_t grp = a.size() > 2 ? (uint32_t)strtoul(a[2].c_str(), nullptr, 16) : kInfo[kCreature].group;
        if (!find_paths())
            return "no folder";
        dbpf::Record r;
        r.key = {inst, kPng, grp};
        r.data.assign(16, 0xAB);
        std::wstring path = g_dir + L"\\fakepng_" + std::to_wstring(inst) + L"_" + std::to_wstring(grp) + L".package";
        return package_for(path, {r}, kPrioModel) ? "fake png registered" : "cannot register";
    }
    if (a.size() >= 2 && a[0] == "sendown" && is_host()) { // sendown <hex crt inst>: tests (BUG-0643), the host's own
        // creation to every guest as before an NPC of it (host_species_out)
        uint32_t inst = (uint32_t)strtoul(a[1].c_str(), nullptr, 16);
        unsigned n = 0;
        for (auto& p : C.sess->players())
            if (p.id != 0) {
                g_own_sent[p.id].erase(inst);
                host_species_out(p.id, {inst, kInfo[kCreature].type, kInfo[kCreature].group});
                n++;
            }
        return "sent to " + std::to_string(n) + " guest(s)\n";
    }
    if (a.size() >= 2 && a[0] == "local") { // local <hex crt inst>: the key the host's creation has here
        Key k = local_species({(uint32_t)strtoul(a[1].c_str(), nullptr, 16), kInfo[kCreature].type, kInfo[kCreature].group});
        return "local " + hex(k.instance) + "\n";
    }
    if (a.size() >= 2 && a[0] == "png") { // png <hex inst> [<hex group>]: the picture this game answers for a creature key
        Key k{(uint32_t)strtoul(a[1].c_str(), nullptr, 16), kPng,
              a.size() > 2 ? (uint32_t)strtoul(a[2].c_str(), nullptr, 16) : kInfo[kCreature].group};
        std::vector<dbpf::Record> recs;
        void* rm = a_RmPtr ? *(void**)a_RmPtr : nullptr;
        uint32_t key[3] = {k.instance, k.type, k.group}, real[3] = {};
        bool known = rm && vcall<void*>(rm, 0x30 / 4, (const uint32_t*)key, real, (void*)nullptr) != nullptr;
        return "png " + hex(k.instance) + " known=" + std::to_string((int)known) + "\n";
    }
    if (!a.empty() && a[0] == "designs") // BUG-0647: the host's own designs on this guest
        return design_status();
    if (a.size() >= 2 && a[0] == "breakremap") // tests (BUG-1293)
        return test_break_remap((uint32_t)strtoul(a[1].c_str(), nullptr, 16));
    if (a.size() >= 2 && a[0] == "lack") { // lack <hex part id> | clear: tests (BUG-0725): treat a part as one this game lacks
        if (a[1] == "clear")
            g_lacked.clear();
        else
            g_lacked.insert((uint32_t)strtoul(a[1].c_str(), nullptr, 16));
        g_key_cache.clear();
        g_model_use.clear(); // the verdicts are made again
        return "lacked parts " + std::to_string(g_lacked.size()) + ", descriptions received " +
               std::to_string(g_rx_descs.size()) + "\n";
    }
    if (!a.empty() && a[0] == "objmodels") { // tests (BUG-0730 / BUG-0731): the model keys of every placed object
        g_objmodels_scan = a.size() > 1 && a[1] == "scan";
        // (cSpatialObject model key at object +0xC4, as vehicles and buildings), per noun: which package answers each
        // key here and how many part ids of its model this game lacks
        struct Row { unsigned n = 0; };
        std::map<std::tuple<uint32_t, uint32_t, uint32_t, uint32_t>, unsigned> rows; // (noun, inst, type, group) -> count
        game::for_each_object(
            [](void* o, uint32_t noun, void* u) {
                auto* m = (std::map<std::tuple<uint32_t, uint32_t, uint32_t, uint32_t>, unsigned>*)u;
                net::Vec3 p;
                if (!game::object_alive(o) || IsBadReadPtr(o, 0xD0) || !game::object_position(o, p) || m->size() > 400)
                    return true;
                if (noun == 0x018C43E8 && !IsBadReadPtr(o, 0x528 + 0x24)) // a city: its 3 decoration keys (+0x528)
                    for (int i = 0; i < 3; i++) {
                        const uint32_t* d = (const uint32_t*)((uint8_t*)o + 0x528 + 0xC * i);
                        if (d[0] && d[0] != 0xFFFFFFFFu)
                            (*m)[{0xDEC00000u | (uint32_t)i, d[0], d[1], d[2]}]++;
                    }
                const uint32_t* k = (const uint32_t*)((uint8_t*)o + 0xC4);
                if (k[0] && k[0] != 0xFFFFFFFFu && k[1])
                    (*m)[{noun, k[0], k[1], k[2]}]++;
                // every key-like triple in the object's first 0x600 bytes whose type is a model kind's (a model key
                // kept elsewhere than +0xC4: a hut, an ornament), noun | 0x80000000 + its offset in the 'n' column
                if (g_objmodels_scan && noun != 0x018EB45E && noun != 0x018EB4B7 && !IsBadReadPtr(o, 0x600))
                    for (int off = 0; off + 12 <= 0x600; off += 4) {
                        const uint32_t* t = (const uint32_t*)((uint8_t*)o + off);
                        bool model = false;
                        for (auto& mi : kModelInfo)
                            model |= t[1] == mi.type && t[2] == mi.group;
                        model |= t[1] == kPartProp && t[2] && t[2] != 0xFFFFFFFFu; // a noun's property file (tools)
                        if (model && t[0] && t[0] != 0xFFFFFFFFu)
                            (*m)[{noun | 0x80000000u, t[0], t[1], t[2]}] = (unsigned)off;
                    }
                return true;
            },
            &rows);
        std::string out = "objmodels " + std::to_string(rows.size()) + "\n";
        void* rm = a_RmPtr ? *(void**)a_RmPtr : nullptr;
        for (auto& [k, n] : rows) {
            auto [noun, inst, type, group] = k;
            uint32_t key[3] = {inst, type, group}, real[3] = {};
            void* db = rm ? vcall<void*>(rm, 0x30 / 4, (const uint32_t*)key, real, (void*)nullptr) : nullptr;
            std::wstring loc;
            std::string where = !db ? "none" : !file_location(db, loc) ? "folder" : "";
            if (where.empty()) {
                size_t sl = loc.find_last_of(L"\\/");
                std::wstring base = sl == std::wstring::npos ? loc : loc.substr(sl + 1);
                where = std::string(base.begin(), base.end());
            }
            int kind = -1;
            for (auto& mi : kModelInfo)
                if (mi.type == type)
                    kind = mi.kind;
            std::string parts = "-";
            if (kind >= 0) {
                std::vector<dbpf::Record> recs;
                if (records_from_game({inst, type, group}, recs))
                    parts = std::to_string(unknown_part_ids(recs, (ModelKind)kind).size());
                else
                    parts = "unreadable";
            }
            char b[200];
            snprintf(b, sizeof b, "  obj noun=%08X n=%u key=%08X/%08X/%08X from=%s unknown_parts=%s\n", noun, n, inst, type,
                     group, where.c_str(), parts.c_str());
            out += b;
        }
        return out;
    }
    if (!a.empty() && a[0] == "looks") { // tests (BUG-0725): the lookalike choices of this session (group/id=chosen)
        std::string out = "looks";
        for (auto& [k, v] : g_lookalike)
            out += " " + hex(k.first) + "/" + hex(k.second) + "=" + hex(v);
        return out + "\n";
    }
    if (a.size() >= 3 && a[0] == "mparts") { // mparts <kind> <hex instance>: the part ids of the model the game reads (tests)
        int kind = a[1] == "cell" ? kMkCell : a[1] == "ufo" ? kMkUfo : a[1] == "vehicle" ? kMkVehicle
                   : a[1] == "building" ? kMkBuilding : kMkCreature;
        Key k{(uint32_t)strtoul(a[2].c_str(), nullptr, 16), kModelInfo[kind].type, kModelInfo[kind].group};
        std::vector<dbpf::Record> recs;
        if (!records_from_game(k, recs))
            return "mparts " + hex(k.instance) + " none\n";
        std::string out = "mparts " + hex(k.instance);
        std::set<uint32_t> seen;
        for (auto& r : recs)
            if (r.key.type == kBem)
                for (auto& ref : model_part_refs(r.data, part_group((ModelKind)kind)))
                    if (seen.insert(ref.id).second)
                        out += " " + hex(ref.id) + (part_known(ref.id, part_group((ModelKind)kind)) ? "" : "!") +
                               (g_rx_descs.count(ref.id) ? "+d" : "");
        return out + "\n";
    }
    if (a.size() >= 2 && a[0] == "modelhash") { // modelhash <hex inst> [<hex type> <hex group>]: which model answers
        Key k{(uint32_t)strtoul(a[1].c_str(), nullptr, 16),
              a.size() > 2 ? (uint32_t)strtoul(a[2].c_str(), nullptr, 16) : kModelInfo[kMkVehicle].type,
              a.size() > 3 ? (uint32_t)strtoul(a[3].c_str(), nullptr, 16) : kModelInfo[kMkVehicle].group};
        return "model " + hex(k.instance) + " " + model_hash(k) + "\n";
    }
    if (!a.empty() && a[0] == "content") { // BUG-0469: content [<kind> <hex instance>]: counters, or one model
        if (a.size() < 3)
            return content_status();
        int kind = a[1] == "cell" ? kMkCell : a[1] == "ufo" ? kMkUfo : a[1] == "vehicle" ? kMkVehicle
                   : a[1] == "building" ? kMkBuilding : kMkCreature;
        uint32_t inst = (uint32_t)strtoul(a[2].c_str(), nullptr, 16);
        Model m = ensure_model({inst, kModelInfo[kind].type, kModelInfo[kind].group}, (ModelKind)kind);
        char b[200];
        snprintf(b, sizeof b, "content %s %08X status=%s key=%08X/%08X/%08X\n", kModelInfo[kind].name, inst,
                 m.status == Model::kSame ? "same" : m.status == Model::kSubstituted ? "substituted" : "refused",
                 m.key.instance, m.key.type, m.key.group);
        return b;
    }
    if (a.size() >= 2 && a[0] == "parts") { // parts <hex crt instance>: the parts of the model the game reads for that key (tests, BUG-0452)
        Key k{(uint32_t)strtoul(a[1].c_str(), nullptr, 16), kInfo[kCreature].type, kInfo[kCreature].group};
        std::vector<dbpf::Record> recs;
        if (!records_from_game(k, recs))
            return "parts " + hex(k.instance) + " none\n";
        std::vector<uint32_t> unknown = unknown_part_ids(recs, kMkCreature);
        unsigned n = 0, cc = 0;
        for (auto& r : recs)
            if (r.key.type == kBem)
                for (auto& ref : model_part_refs(r.data, kPartGroup)) {
                    n++;
                    cc += is_cc_part(ref.id);
                }
        // the game's own species profile (it builds the model): only when it can (a part it does
        // not have faults in the bake, 0x41F3B)
        int species = unknown.empty() ? game::species_by_key({k.instance, k.type, k.group}) != nullptr : -1;
        char b[300];
        snprintf(b, sizeof b, "parts %s n=%u cc=%u unknown=%u species=%d have_pack=%d substituted=%u refused=%u",
                 hex(k.instance).c_str(), n, cc, (unsigned)unknown.size(), species, (int)have_creepy_cute(),
                 g_parts_substituted, g_parts_refused);
        for (uint32_t id : unknown)
            snprintf(b + strlen(b), sizeof b - strlen(b), " %08X", id);
        snprintf(b + strlen(b), sizeof b - strlen(b), "\n");
        return b;
    }
    if (a.size() >= 2 && a[0] == "partlist") { // partlist <hex crt instance>: each part of the model the game reads, its kind here (tests, BUG-0559)
        Key k{(uint32_t)strtoul(a[1].c_str(), nullptr, 16), kInfo[kCreature].type, kInfo[kCreature].group};
        std::vector<dbpf::Record> recs;
        if (!records_from_game(k, recs))
            return "partlist " + hex(k.instance) + " none\n";
        std::string out = "partlist " + hex(k.instance);
        void* rm = a_RmPtr ? *(void**)a_RmPtr : nullptr;
        uint32_t key[3] = {k.instance, k.type, k.group}, real[3] = {};
        void* db = rm ? vcall<void*>(rm, 0x30 / 4, (const uint32_t*)key, real, (void*)nullptr) : nullptr;
        std::wstring loc;
        if (file_location(db, loc)) {
            size_t sl = loc.find_last_of(L"\\/");
            std::wstring base = sl == std::wstring::npos ? loc : loc.substr(sl + 1);
            out += " from=" + std::string(base.begin(), base.end());
        }
        std::set<uint32_t> seen;
        for (auto& r : recs)
            if (r.key.type == kBem)
                for (auto& ref : model_part_refs(r.data, kPartGroup)) {
                    if (!seen.insert(ref.id).second)
                        continue;
                    std::vector<uint8_t> d;
                    partprops::Part p;
                    const char* kind = "unknown";
                    if (part_prop_bytes(ref.id, kPartGroup, d) && partprops::parse(d.data(), d.size(), ref.id, p)) {
                        kind = p.detail() ? "detail" : "role";
                        for (uint32_t role : p.roles)
                            kind = role == 0xB00F0FE9 ? "eye" : role == 0xB00F0FEC ? "mouth" : role == 0xB00F0FE2 ? "grasper"
                                   : role == 0xB00F0FEF ? "foot" : role == 0xB00F0FDF ? "limb" : kind;
                    }
                    out += " " + hex(ref.id) + "=" + kind;
                }
        // BUG-0559: the species data the game finds for it (creaturedata): the part ids it names, and how
        // many of them this game lacks (each one is drawn as a block)
        std::vector<dbpf::Record> cd;
        unsigned cn = 0, cu = 0;
        if (records_from_game({k.instance, kCreatureData, k.group}, cd))
            for (auto& r : cd)
                if (r.key.type == kCreatureData)
                    for (size_t i = 0; i + 8 <= r.data.size(); i++)
                        if (!r.data[i] && r.data[i + 1] == 0x60 && r.data[i + 2] == 0x62 && r.data[i + 3] == 0x40) {
                            uint32_t id = r.data[i + 4] | r.data[i + 5] << 8 | r.data[i + 6] << 16 | (uint32_t)r.data[i + 7] << 24;
                            cn++;
                            cu += id && !part_known(id, kPartGroup);
                        }
        char b[80];
        snprintf(b, sizeof b, " species_data=%u species_data_unknown=%u", cn, cu);
        return out + b + "\n";
    }
    if (a.size() >= 2 && a[0] == "paints") { // paints <hex crt instance>: the paint layers of the model the game reads (tests, BUG-0452)
        Key k{(uint32_t)strtoul(a[1].c_str(), nullptr, 16), kInfo[kCreature].type, kInfo[kCreature].group};
        std::vector<dbpf::Record> recs;
        if (!records_from_game(k, recs))
            return "paints " + hex(k.instance) + " none\n";
        std::string out = "paints " + hex(k.instance);
        unsigned cc = 0, unknown = 0;
        for (auto& r : recs) {
            uint32_t eff[3];
            size_t at[3], len[3];
            if (r.key.type != kBem || !model_effects(r.data, eff, at, len))
                continue;
            for (int i = 0; i < 3; i++) {
                out += " " + hex(eff[i]);
                cc += is_cc_paint(eff[i]);
                unknown += !paint_known(eff[i]);
            }
        }
        char b[120];
        snprintf(b, sizeof b, " cc=%u unknown=%u have_pack=%d substituted=%u in_run=%u\n", cc, unknown, (int)have_creepy_cute(),
                 g_paints_substituted, g_paints_in_run);
        return out + b;
    }
    if (a.size() >= 2 && a[0] == "speciesof") { // speciesof <hex crt instance>: the species profile the game gives for that key (tests, BUG-0200)
        Key k{(uint32_t)strtoul(a[1].c_str(), nullptr, 16), kInfo[kCreature].type, kInfo[kCreature].group};
        void* p = game::species_by_key({k.instance, k.type, k.group});
        if (!p || !skg::readable(p, 0x510))
            return "profile " + hex(k.instance) + " none\n";
        Key pk{skg::field<uint32_t>(p, 0x504), skg::field<uint32_t>(p, 0x508), skg::field<uint32_t>(p, 0x50C)};
        std::vector<dbpf::Record> recs;
        uint32_t h = 0;
        if (records_from_game(pk, recs))
            for (auto& r : recs)
                if (r.key.type == kBem)
                    h = fnv(r.data.data(), r.data.size());
        auto alias = g_alias.find(k.instance);
        char b[200];
        snprintf(b, sizeof b, "profile %08X key %08X bem %08X aliased %d (set %u restored %u own_world %u)\n", k.instance, pk.instance, h,
                 alias != g_alias.end() && alias->second.prof == p ? 1 : 0, g_alias_set, g_alias_restored, g_alias_own_undone);
        return b;
    }
    if (a.size() >= 2 && a[0] == "bem") { // bem <hex crt instance>: the body the game finds for that key (tests)
        Key k{(uint32_t)strtoul(a[1].c_str(), nullptr, 16), kInfo[kCreature].type, kInfo[kCreature].group};
        std::vector<dbpf::Record> recs;
        if (!records_from_game(k, recs))
            return "bem " + hex(k.instance) + " none\n";
        uint32_t h = 0;
        for (auto& r : recs)
            if (r.key.type == kBem)
                h = fnv(r.data.data(), r.data.size());
        return "bem " + hex(k.instance) + " " + hex(h) + "\n";
    }
    if (a.size() >= 2 && a[0] == "bar" && a[1] == "rebuild") { // tests: the game builds the bar for the avatar it has now
        if (!g_bar_queued) {
            g_bar_queued = true;
            H->run_outside_frame(bar_rebuild_task, nullptr);
        }
        return "the ability bar is built again on the next frame\n";
    }
    if (!a.empty() && a[0] == "bar") { // the creature ability bar against the avatar's abilities (tests, BUG-0358)
        BarState st = bar_state(game::avatar());
        char b[200];
        snprintf(b, sizeof b, "bar abilities=%d used=%d stale=%d max_index=%d rebuilds=%u slots=", st.abilities, st.used,
                 st.stale, st.max_index, g_bar_rebuilds);
        std::string out = b;
        if (uint8_t* bar = bar_table(); bar && game::avatar())
            for (int r = 0; r < *(int*)(bar + 0xC8); r++)
                for (int c = 0; c < *(int*)(bar + 0xCC); c++)
                    if (int idx = *(int*)(bar + r * 0x28 + c * 8); idx != -1)
                        out += std::to_string(r) + "." + std::to_string(c) + ":" + std::to_string(idx) + ",";
        return out + "\n";
    }
    if (!a.empty() && a[0] == "list") {
        std::string s;
        for (int k = 0; k < kKinds; k++)
            for (auto& o : options((Kind)k))
                s += std::string(kInfo[k].name) + " " + hex(o.key.instance) + " " + o.name + "\n";
        return s;
    }
    return status();
}

} // namespace creations
