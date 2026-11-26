// Native unit tests for the persistent shared creations library pure core (library_core.h, BUG-0873, BUG-1583).
#include "library_core.h"
#include "species_line_core.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace library;

static int g_fail = 0;

static void check(bool cond, const char* msg, int line)
{
    if (!cond) {
        fprintf(stderr, "FAIL line %d: %s\n", line, msg);
        g_fail++;
    }
}
#define CK(c) check((c), #c, __LINE__)

static std::vector<uint8_t> make_fake_png(uint8_t seed, size_t payload_len = 64)
{
    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A,
                                0x00, 0x00, 0x00, 0x0D, 'I',  'H',  'D',  'R'};
    out.resize(16 + payload_len);
    for (size_t i = 16; i < out.size(); ++i)
        out[i] = (uint8_t)((seed * 31u + i * 7u) & 0xFFu);
    return out;
}

static void test_png_validation_and_filenames()
{
    auto p = make_fake_png(1);
    CK(is_valid_png(p.data(), p.size()));
    CK(!is_valid_png(p.data(), 10));
    p[0] = 0x00;
    CK(!is_valid_png(p.data(), p.size()));

    auto p2 = make_fake_png(2);
    p2[12] = 'X';
    CK(!is_valid_png(p2.data(), p2.size()));

    CK(is_creation_group(0x40616200u)); // cell
    CK(is_creation_group(0x40626200u)); // creature
    CK(is_creation_group(0x40636200u)); // building
    CK(is_creation_group(0x40646200u)); // vehicle
    CK(is_creation_group(0x40656200u)); // ufo
    CK(is_creation_group(0x40666200u)); // flora
    CK(!is_creation_group(0x40626201u)); // thumbnail group +1 is not model group
    CK(!is_creation_group(0u));

    uint64_t h = 0x0123456789abcdefull;
    std::string fn = png_filename(h);
    CK(fn == "0123456789abcdef.png");
    uint64_t parsed = 0;
    CK(parse_png_filename(fn, parsed) && parsed == h);
    CK(!parse_png_filename("../0123456789abcdef.png", parsed));
    CK(!parse_png_filename("0000000000000000.png", parsed));
    CK(!parse_png_filename("zzzzzzzzzzzzzzzz.png", parsed));
    CK(!parse_png_filename("short.png", parsed));
}

static void test_index_roundtrip_and_corruption()
{
    std::vector<IndexEntry> in = {
        {0x1122334455667788ull, {0x29010001u, 0x2B978C46u, 0x40626200u}, 0x40626200u, kFlagImported | kFlagOriginOwn,
         "Bob", "Chitis"},
        {0x99AABBCCDDEEFF01ull, {}, 0x40646200u, kFlagForeign, "Carol", "Rover"},
    };
    auto raw = encode_index(in);
    std::vector<IndexEntry> out;
    CK(decode_index(raw.data(), raw.size(), out));
    CK(out.size() == 2);
    CK(out[0].hash == in[0].hash);
    CK(out[0].imported == in[0].imported);
    CK(out[0].sender == "Bob");
    CK(out[0].name == "Chitis");
    CK(out[1].hash == in[1].hash);
    CK(!out[1].imported.valid());
    CK(out[1].flags == kFlagForeign);

    // Truncation / bad magic / bad version must fail safely
    std::vector<IndexEntry> bad;
    CK(!decode_index(raw.data(), 4, bad));
    CK(!decode_index(raw.data(), raw.size() - 3, bad));
    auto bad_magic = raw;
    bad_magic[0] = 'X';
    CK(!decode_index(bad_magic.data(), bad_magic.size(), bad));
    auto bad_ver = raw;
    bad_ver[4] = 99;
    CK(!decode_index(bad_ver.data(), bad_ver.size(), bad));
}

static void test_wire_codecs()
{
    ListMsg have;
    have.subkind = kListHave;
    have.have.push_back({0xAABBCCDD11223344ull, 0x40626200u, 1u});
    have.have.push_back({0x11223344AABBCCDDull, 0x40656200u, 0u});
    auto w1 = encode_list(have);
    ListMsg d1;
    CK(decode_list(w1.data(), w1.size(), d1));
    CK(d1.subkind == kListHave && d1.have.size() == 2);
    CK(d1.have[0].hash == have.have[0].hash && d1.have[0].flags == 1u);
    CK(d1.have[1].group == 0x40656200u && d1.have[1].flags == 0u);

    ListMsg want;
    want.subkind = kListWant;
    want.want = {0xAABBCCDD11223344ull, 0x5566778899AABBCCull};
    auto w2 = encode_list(want);
    ListMsg d2;
    CK(decode_list(w2.data(), w2.size(), d2));
    CK(d2.subkind == kListWant && d2.want.size() == 2);
    CK(d2.want[1] == 0x5566778899AABBCCull);

    // PNG transfer message: hash must match PNG payload
    PngMsg pm;
    pm.png = make_fake_png(42, 128);
    pm.hash = content_hash64(pm.png.data(), pm.png.size());
    pm.group = 0x40626200u;
    pm.flags = 1u;
    pm.sender = "Bob";
    pm.name = "Sporeling";
    auto w3 = encode_png_msg(pm);
    PngMsg d3;
    CK(decode_png_msg(w3.data(), w3.size(), d3));
    CK(d3.hash == pm.hash && d3.group == pm.group && d3.sender == "Bob" && d3.name == "Sporeling");
    CK(d3.png == pm.png);

    // Tampered byte must fail hash check
    auto bad_png = w3;
    bad_png.back() ^= 0xFFu;
    CK(!decode_png_msg(bad_png.data(), bad_png.size(), d3));
}

static void test_state_dedup_foreign_retries_and_my_filter()
{
    LibraryState st;
    AssetKey own_key{0x29000001u, 0x2B978C46u, 0x40626200u};
    AssetKey partner_key{0x29000002u, 0x2B978C46u, 0x40626200u};
    uint64_t own_hash = 0x1001ull;
    uint64_t partner_hash = 0x2002ull;
    uint64_t foreign_hash = 0x3003ull;

    st.set_own({own_hash}, {own_key});

    // Dedup against own creations
    CK(!st.should_want(own_hash, true, true));
    CK(!st.ingest(own_hash, 0x40626200u, true, "Bob", "OwnCopy", true));

    // Foreign creation policy: receiver decides
    CK(!st.should_want(foreign_hash, false, false));
    CK(st.should_want(foreign_hash, false, true));

    // Partner's own creation accepted once, deduped on second ingest
    CK(st.should_want(partner_hash, true, false));
    CK(st.ingest(partner_hash, 0x40626200u, true, "Bob", "Sporeling", true));
    CK(!st.should_want(partner_hash, true, true));
    CK(!st.ingest(partner_hash, 0x40626200u, true, "Bob", "Sporeling", true));

    // Retry policy: 3 attempts per run, 60 s apart
    CK(st.due_imports(10.0).size() == 1);
    st.record_import_failure(partner_hash, 10.0, true);
    CK(st.due_imports(50.0).empty()); // < 60 s
    CK(st.due_imports(70.0).size() == 1); // >= 60 s

    // Second failure -> another 60 s wait
    st.record_import_failure(partner_hash, 70.0, true);
    CK(st.due_imports(129.0).empty());
    CK(st.due_imports(130.0).size() == 1);

    // Import succeeds on 3rd attempt
    CK(st.mark_imported(partner_hash, partner_key));
    CK(st.due_imports(200.0).empty());

    // Sporepedia tab contains partner key; My Creations hides partner key but NEVER hides own_key
    auto feed = st.feed_keys();
    CK(feed.size() == 1 && feed[0] == partner_key);
    CK(st.should_hide_from_my_creations(partner_key));
    CK(!st.should_hide_from_my_creations(own_key));

    // Retry disabled: first failure gives up immediately
    uint64_t fail_hash = 0x4004ull;
    CK(st.ingest(fail_hash, 0x40626200u, true, "Bob", "Broken", true));
    st.record_import_failure(fail_hash, 10.0, false);
    CK(st.due_imports(1000.0).empty());
    CK(st.entries[fail_hash].gave_up);

    // Persisted index survives reload
    auto saved = encode_index(st.save_index());
    std::vector<IndexEntry> reloaded_idx;
    CK(decode_index(saved.data(), saved.size(), reloaded_idx));
    LibraryState st2;
    st2.set_own({own_hash}, {own_key});
    st2.load_index(reloaded_idx);
    CK(st2.entries.count(partner_hash) && st2.entries[partner_hash].is_imported());
    CK(st2.feed_keys().size() == 1 && st2.feed_keys()[0] == partner_key);
    CK(st2.should_hide_from_my_creations(partner_key));
    // Next run resets gave_up on unimported entries so they get fresh retries
    CK(st2.entries.count(fail_hash) && !st2.entries[fail_hash].gave_up);
}

static std::vector<uint8_t> make_pollen_meta(uint32_t version, uint64_t author_id, const std::vector<uint16_t>& author_u16,
                                             const std::vector<uint16_t>& name_u16)
{
    std::vector<uint8_t> d(0x38, 0);
    auto put_be32 = [&](size_t off, uint32_t v) {
        d[off + 0] = (uint8_t)(v >> 24);
        d[off + 1] = (uint8_t)(v >> 16);
        d[off + 2] = (uint8_t)(v >> 8);
        d[off + 3] = (uint8_t)(v);
    };
    auto push_be32 = [&](uint32_t v) {
        d.push_back((uint8_t)(v >> 24));
        d.push_back((uint8_t)(v >> 16));
        d.push_back((uint8_t)(v >> 8));
        d.push_back((uint8_t)(v));
    };
    put_be32(0x00, version);
    for (int i = 0; i < 8; i++)
        d[0x30 + i] = (uint8_t)(author_id >> (56 - 8 * i));
    push_be32((uint32_t)author_u16.size());
    for (uint16_t c : author_u16) {
        d.push_back((uint8_t)(c & 0xFFu));
        d.push_back((uint8_t)(c >> 8));
    }
    push_be32((uint32_t)name_u16.size());
    for (uint16_t c : name_u16) {
        d.push_back((uint8_t)(c & 0xFFu));
        d.push_back((uint8_t)(c >> 8));
    }
    return d;
}

static void test_metadata_names_and_nonempty_fallback()
{
    // 1. Offline author (-2 = 0xFFFFFFFFFFFFFFFE) with Cyrillic name "Дед" (U+0414 U+0435 U+0434)
    auto m_offline = make_pollen_meta(13, 0xFFFFFFFFFFFFFFFEull, {'V', 'l', 'a', 'i', 'd'}, {0x0414, 0x0435, 0x0434});
    std::string author, name;
    CK(species::extract_metadata_name(m_offline.data(), m_offline.size(), author, name));
    CK(author == "Vlaid");
    CK(name == "Дед");

    // 2. Online EA / Maxis author ID (non -2, previously caused empty '' name in Shared Library, BUG-1583)
    auto m_online = make_pollen_meta(13, 0x0000000012345678ull, {'M', 'a', 'x', 'i', 's'}, {'K', 'o', 'o', 'k'});
    CK(species::extract_metadata_name(m_online.data(), m_online.size(), author, name));
    CK(author == "Maxis");
    CK(name == "Kook");

    // 3. Fallback creation name is NEVER empty for empty/blank/control-only inputs
    std::string fb_crt = species::sanitize_creation_name("", 0x40626200u, 0xAABBCCDD11223344ull);
    CK(!fb_crt.empty() && fb_crt.rfind("Creature #", 0) == 0);
    std::string fb_ws = species::sanitize_creation_name("   \t\n ", 0x40636200u, 0x1234ull);
    CK(!fb_ws.empty() && fb_ws.rfind("Building #", 0) == 0);
    std::string fb_unk = species::sanitize_creation_name("", 0u, 0ull);
    CK(!fb_unk.empty() && fb_unk == "Creation #1");

    // 4. Clean names (ASCII & Cyrillic) are preserved verbatim (trimmed)
    CK(species::sanitize_creation_name("  Дед  ", 0x40626200u, 99ull) == "Дед");
    CK(species::sanitize_creation_name("Sporeling", 0x40626200u, 99ull) == "Sporeling");

    // 5. Ingested library entries with sanitized names survive index roundtrip non-empty
    LibraryState st;
    uint64_t h1 = 0xCAFEBABE12345678ull;
    uint64_t h2 = 0xDEADBEEF87654321ull;
    CK(st.ingest(h1, 0x40626200u, true, "DERMOGEN", species::sanitize_creation_name("", 0x40626200u, h1), true));
    CK(st.ingest(h2, 0x40626200u, true, "DERMOGEN", species::sanitize_creation_name("Дед", 0x40626200u, h2), true));
    CK(!st.entries[h1].name.empty());
    CK(st.entries[h2].name == "Дед");

    auto raw = encode_index(st.save_index());
    std::vector<IndexEntry> out;
    CK(decode_index(raw.data(), raw.size(), out) && out.size() == 2);
    for (const auto& ie : out)
        CK(!ie.name.empty());
}

int main()
{
    test_png_validation_and_filenames();
    test_index_roundtrip_and_corruption();
    test_wire_codecs();
    test_state_dedup_foreign_retries_and_my_filter();
    test_metadata_names_and_nonempty_fallback();
    if (g_fail) {
        fprintf(stderr, "library_tests: %d FAILED\n", g_fail);
        return 1;
    }
    printf("library_tests: all passed\n");
    return 0;
}
