# SporeCoop build (Linux host, llvm-mingw cross toolchain, 32-bit Windows targets)
# the pinned toolchain version (and its tarball's SHA-256 for CI): tools/toolchain.env (A3, BUG-1121)
include tools/toolchain.env
LLVM_MINGW ?= $(HOME)/.local/opt/llvm-mingw-$(LLVM_MINGW_VERSION)-ucrt-ubuntu-22.04-x86_64
CC  := $(LLVM_MINGW)/bin/i686-w64-mingw32-clang
CXX := $(LLVM_MINGW)/bin/i686-w64-mingw32-clang++
HOSTCXX ?= clang++

B := build
comma := ,
# A4 (BUG-1122): the code reads and writes game objects through casts at fixed offsets and SporeKit lets code go on
# after a repaired fault: no type-based aliasing assumptions, no null checks dropped after a dereference. Warnings
# are errors (the toolchain is pinned, tools/toolchain.env); printf-style functions carry format attributes (SK_PRINTF).
COMMON  := -O2 -g -fno-strict-aliasing -fno-delete-null-pointer-checks -Wall -Wimplicit-fallthrough -Werror -Wno-unused-function -Wno-missing-braces -D_WIN32_WINNT=0x0601 -DUNICODE -D_UNICODE
CFLAGS  := $(COMMON)
CXXFLAGS:= $(COMMON) -std=c++20
LDDLL   := -shared -static -Wl,--kill-at -Wl,--enable-stdcall-fixup

MINHOOK := third_party/minhook
MH_SRC  := $(MINHOOK)/src/hook.c $(MINHOOK)/src/buffer.c $(MINHOOK)/src/trampoline.c $(MINHOOK)/src/hde/hde32.c
MH_OBJ  := $(patsubst %.c,$(B)/obj/%.o,$(MH_SRC))

KIT_SRC := $(wildcard sporekit/*.cpp)
KIT_OBJ := $(patsubst %.cpp,$(B)/obj/%.o,$(KIT_SRC))

COOP_SRC := $(wildcard mods/coop/*.cpp) $(wildcard mods/coop/net/*.cpp) $(wildcard updater/*.cpp)
COOP_OBJ := $(patsubst %.cpp,$(B)/obj/%.o,$(COOP_SRC))

.PHONY: test-win test-asan test-tsan all loader sporekit coop res setup clean test sanitizer-probe
all: loader sporekit coop res setup $(B)/test/rtsshooks.dll

# game resources (icons, Sporepedia Shared Creations tab, ...) for SporeCoop: Data/SporeCoop.package
UI_PNG := $(wildcard data/ui/*.png)
res: $(B)/SporeCoop.package
$(B)/SporeCoop.package: tools/mkpackage.py tools/gen_library_res.py $(UI_PNG)
	@mkdir -p $(B)/libres
	python3 tools/gen_library_res.py $(B)/libres
	python3 tools/mkpackage.py $@ \
		$(foreach f,$(UI_PNG),sporecoop!$(basename $(notdir $(f))).png=$(f)) \
		0x02FABF01!0xA7C9F2B2.0x02FAC0B6=$(B)/libres/library.locale \
		0x06B38241!0x11C77D4C.prop=$(B)/libres/category.prop \
		0x4E5892EB!0x0BA43291.prop=$(B)/libres/item.prop

# tests: a do-nothing DLL under another program's name (BUG-0782, coop_rtss_note.py)
$(B)/test/rtsshooks.dll: tests/dll/stand_in.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -shared -o $@ $<

loader: $(B)/steam_api.dll
sporekit: $(B)/sporekit.dll
coop: $(B)/coop.dll

$(B)/steam_api.dll: loader/loader.c loader/steam_api.def updater/pending.h $(B)/obj/loader/loader_rc.o
	@mkdir -p $(B)
	$(CC) $(CFLAGS) -shared -o $@ loader/loader.c loader/steam_api.def $(B)/obj/loader/loader_rc.o -Wl,--kill-at

# BUG-0784: a VERSIONINFO resource in every binary (installer/version.rc), its version from
# COOP_VERSION: build/version.h is rewritten only when the version changes
WINDRES := $(LLVM_MINGW)/bin/i686-w64-mingw32-windres
COOP_VERSION = $(shell sed -n 's/^\#define COOP_VERSION "\(.*\)"/\1/p' mods/coop/coop.cpp)
VER_NUM = $(shell echo "$(COOP_VERSION)" | awk -F. '{printf "%d,%d,%d,%d", $$1, $$2, $$3, $$4}')
$(B)/version.h: mods/coop/coop.cpp
	@mkdir -p $(B)
	@printf '#define SC_VER_NUM %s\n#define SC_VER_STR "%s"\n' "$(VER_NUM)" "$(COOP_VERSION)" > $@.tmp
	@if cmp -s $@.tmp $@; then rm $@.tmp; touch $@; else mv $@.tmp $@; echo "version.h: $(COOP_VERSION) ($(VER_NUM))"; fi
$(B)/obj/%_rc.o: %.rc installer/version.rc $(B)/version.h
	@mkdir -p $(dir $@)
	$(WINDRES) -i $< -o $@ -I$(B) -Iinstaller

$(B)/obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(MINHOOK)/include -c $< -o $@

$(B)/obj/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -I$(MINHOOK)/include -Isdk/include -c $< -o $@ -MMD -MP

$(B)/sporekit.dll: $(KIT_OBJ) $(MH_OBJ) $(B)/obj/sporekit/sporekit_rc.o
	$(CXX) $(LDDLL) -o $@ $^ -lws2_32 -luser32 -lshell32 -ld3d9 -Wl,--out-implib,$(B)/libsporekit.a

# session_glue.cpp takes COOP_VERSION from build/version.h (it is defined in coop.cpp, where the tools read it)
$(B)/obj/mods/coop/session_glue.o: $(B)/version.h
$(B)/obj/mods/coop/session_glue.o: CXXFLAGS += -I$(B)

$(B)/coop.dll: $(COOP_OBJ) $(B)/obj/mods/coop/coop_rc.o
	$(CXX) $(LDDLL) -o $@ $^ -lws2_32 -luser32 -liphlpapi -lwinhttp -lshell32

# SporeCoopSetup.exe: install / update / uninstall (installer/, updater/)
setup: $(B)/SporeCoopSetup.exe
$(B)/setup.ico: data/ui/coop_icon.png tools/make_ico.py
	@mkdir -p $(B)
	python3 tools/make_ico.py $< $@
# the manifest's assemblyIdentity version is COOP_VERSION too (BUG-0784; it was 1.0.0.0)
$(B)/setup.manifest: installer/setup.manifest $(B)/version.h
	sed 's/name="SporeCoop.Setup" version="[0-9.]*"/name="SporeCoop.Setup" version="$(subst $(comma),.,$(VER_NUM))"/' $< > $@
	grep -q 'version="$(subst $(comma),.,$(VER_NUM))"' $@
$(B)/obj/installer/setup_rc.o: installer/setup.rc installer/version.rc $(B)/setup.manifest $(B)/setup.ico $(B)/version.h
	@mkdir -p $(dir $@)
	$(WINDRES) -i installer/setup.rc -o $@ -I$(B) -Iinstaller
$(B)/SporeCoopSetup.exe: installer/setup.cpp installer/install.cpp installer/install.h updater/winupdate.cpp updater/pack.cpp updater/sha256.cpp $(B)/obj/installer/setup_rc.o
	$(CXX) $(CXXFLAGS) -DSETUP_VERSION='"$(COOP_VERSION)"' -municode -mwindows -static -o $@ $(filter-out %.h,$^) \
		-lwinhttp -lcomctl32 -lshell32 -lole32 -ladvapi32 -luser32 -lgdi32

# The sanitizer builds need the compiler's runtimes: one probe (a stamp per HOSTCXX) says what is
# missing before any test runs (BUG-0252). SKIP_SANITIZERS=1: a local machine skips them on purpose,
# loudly; refused in CI (GitHub sets CI=true).
SAN_STAMP := $(B)/host/sanitizer-probe.$(subst /,_,$(HOSTCXX)).ok
ifeq ($(SKIP_SANITIZERS),1)
ifneq ($(CI),)
$(error SKIP_SANITIZERS=1 is not allowed in CI: the sanitizer tests must run there)
endif
SAN :=
TSAN_RUN :=
else
SAN := -fsanitize=address,undefined
TSAN_RUN = $(if $(shell grep -s setarch $(SAN_STAMP)),setarch $(shell uname -m) -R )
endif

sanitizer-probe:
ifneq ($(SKIP_SANITIZERS),1)
	@test -f $(SAN_STAMP) || tools/sanitizer_probe.sh "$(HOSTCXX)" $(B)/host $(SAN_STAMP)
endif

# native Linux unit tests for portable netcode. The parts below are independent and mostly wait (sessions over
# localhost UDP, the lab self-test's fake runs): `make test` runs them at once (TEST_JOBS, output per part as it ends;
# docs/06-testing.md#testing-lab-speed). `make test TEST_JOBS=1` runs them one after the other.
TEST_JOBS ?= 8
NET_SHARDS ?= 8
UNIT_PARTS := unit-net unit-lab unit-cpp unit-lint unit-tools unit-symbols unit-verdict
ifneq ($(SKIP_SANITIZERS),1)
UNIT_PARTS += test-asan test-tsan unit-fuzz
endif
.PHONY: $(UNIT_PARTS) unit-parts
test: sanitizer-probe
	@mkdir -p $(B)/host
ifeq ($(SKIP_SANITIZERS),1)
	@echo "WARNING: SKIP_SANITIZERS=1: the ASan/UBSan/TSan tests are NOT RUN"
endif
	@$(MAKE) --no-print-directory -j$(TEST_JOBS) -Otarget unit-parts
ifeq ($(SKIP_SANITIZERS),1)
	@echo "WARNING: SANITIZER TESTS NOT RUN (SKIP_SANITIZERS=1)"
endif
	@echo "make test: all parts passed ($(UNIT_PARTS))"

unit-parts: $(UNIT_PARTS)

unit-net:
	@mkdir -p $(B)/host
	$(HOSTCXX) -std=c++20 -O1 -g -Wall -Imods/coop -o $(B)/host/net_tests mods/coop/net/*.cpp updater/pack.cpp updater/sha256.cpp tests/net_tests.cpp
	python3 tools/tests/shards.py $(NET_SHARDS) $(B)/host/net_tests
	python3 tools/tests/fakepeer_check.py $(B)/host/net_tests

unit-lab:
	python3 tools/tests/lab_selftest.py
	python3 tools/tests/gates_selftest.py
	python3 tools/tests/walk_selftest.py
	python3 tools/tests/savekeys_selftest.py
	python3 tools/tests/dash_selftest.py
	python3 tools/tests/labstart_selftest.py

# the lab's verdicts, reasons, flaky ledger, repeat rule and impact selection (0.7.15 A6-A8, A15, D5): its own part (its
# fake lab runs wait ~40 s, beside unit-lab's)
unit-verdict:
	python3 tools/tests/labverdict_selftest.py

unit-fuzz: sanitizer-probe
	@mkdir -p $(B)/host
	$(HOSTCXX) -std=c++20 -O1 -g -Wall -fsanitize=address,undefined -fno-omit-frame-pointer -Imods/coop -o $(B)/host/fuzz_decoders mods/coop/net/*.cpp updater/pack.cpp updater/sha256.cpp tests/fuzz_decoders.cpp
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 $(B)/host/fuzz_decoders

unit-cpp: sanitizer-probe
	@mkdir -p $(B)/host/jpeg
	$(HOSTCXX) -std=c++20 -O1 -g -Wall $(SAN) -Imods/coop -o $(B)/host/validate_tests tests/validate_tests.cpp
	$(B)/host/validate_tests
	$(HOSTCXX) -std=c++20 -O1 -g -Wall $(SAN) -Imods/coop -o $(B)/host/cell_tests tests/cell_tests.cpp
	$(B)/host/cell_tests
	@# WP-R2 of 0.8.0: Cell stage Rivals mode (cell_rivals_logic.h, BUG-1546)
	$(HOSTCXX) -std=c++20 -O1 -g -Wall $(SAN) -Imods/coop -o $(B)/host/cell_rivals_tests tests/cell_rivals_tests.cpp
	$(B)/host/cell_rivals_tests
	$(HOSTCXX) -std=c++20 -O1 -g -Wall $(SAN) -Imods/coop -o $(B)/host/npc_tests tests/npc_tests.cpp
	$(B)/host/npc_tests
	$(HOSTCXX) -std=c++20 -O1 -g -Wall $(SAN) -Imods/coop -o $(B)/host/space_tests tests/space_tests.cpp
	$(B)/host/space_tests
	@# D3 of 0.7.15: the Civ rules (civ_logic.h, BUG-1163)
	$(HOSTCXX) -std=c++20 -O1 -g -Wall $(SAN) -Imods/coop -o $(B)/host/civ_tests tests/civ_tests.cpp
	$(B)/host/civ_tests
	@# E1 of 0.7.15: the per-player store on the host's disk (BUG-1149)
	$(HOSTCXX) -std=c++20 -O1 -g -Wall $(SAN) -Imods/coop -o $(B)/host/store_tests tests/store_tests.cpp
	$(B)/host/store_tests
	@# WP-0 of 0.7.16: the species line's rules, wire and store format (species_line_core.h, BUG-1270)
	$(HOSTCXX) -std=c++20 -O1 -g -Wall $(SAN) -Imods/coop -o $(B)/host/species_tests tests/species_tests.cpp
	$(B)/host/species_tests
	@# wave C of 0.7.15: mirror registry, snapshot receiver, world id and lifecycle observers, host-busy model
	$(HOSTCXX) -std=c++20 -O1 -g -Wall $(SAN) -Imods/coop -o $(B)/host/core_tests tests/core_tests.cpp
	$(B)/host/core_tests
	@# WP-D of 0.7.16: own DNA per player (dna_own.h, BUG-1291)
	$(HOSTCXX) -std=c++20 -O1 -g -Wall $(SAN) -Imods/coop -o $(B)/host/dna_tests tests/dna_tests.cpp
	$(B)/host/dna_tests
	@# WP-R1 of 0.7.17: revive a downed player (revive_logic.h, BUG-1472)
	$(HOSTCXX) -std=c++20 -O1 -g -Wall $(SAN) -Imods/coop -o $(B)/host/revive_tests tests/revive_tests.cpp
	$(B)/host/revive_tests
	@# WP-D1 of 0.7.17: the difficulty for 3-4 players (party_difficulty_logic.h, BUG-1473)
	$(HOSTCXX) -std=c++20 -O1 -g -Wall $(SAN) -Imods/coop -o $(B)/host/party_tests tests/party_tests.cpp
	$(B)/host/party_tests
	@# WP-Q1 of 0.7.99: frame rate target, borderless window, wider third-person FOV & picking ray (BUG-0865, BUG-0866)
	$(HOSTCXX) -std=c++20 -O1 -g -Wall $(SAN) -Imods/coop -o $(B)/host/display_fov_tests tests/display_fov_tests.cpp
	$(B)/host/display_fov_tests
	@# WP-A of 0.7.99: co-op Galactic Adventures (adventure_logic.h, wire/adventure.h, BUG-1474 / BUG-0793)
	$(HOSTCXX) -std=c++20 -O1 -g -Wall $(SAN) -Imods/coop -o $(B)/host/adventure_tests tests/adventure_tests.cpp
	$(B)/host/adventure_tests
	@# WP-R1 of 0.8.0: Rivals shared systems (rivals_logic.h, wire/rivals.h, BUG-0794)
	$(HOSTCXX) -std=c++20 -O1 -g -Wall $(SAN) -Imods/coop -o $(B)/host/rivals_tests tests/rivals_tests.cpp
	$(B)/host/rivals_tests
	@# WP-AE of 0.8.0: shared Adventure Editor (adv_editor_logic.h, wire/adv_editor.h, BUG-0863)
	$(HOSTCXX) -std=c++20 -O1 -g -Wall $(SAN) -Imods/coop -o $(B)/host/adv_editor_tests tests/adv_editor_tests.cpp
	$(B)/host/adv_editor_tests
	@# WP-R3 of 0.8.0: Creature Rivals portable logic (creature_rivals_logic.h, BUG-1547)
	$(HOSTCXX) -std=c++20 -O1 -g -Wall $(SAN) -Imods/coop -o $(B)/host/creature_rivals_tests tests/creature_rivals_tests.cpp
	$(B)/host/creature_rivals_tests
	@# WP-R5 of 0.8.0: Civilization Rivals portable logic (civ_rivals_logic.h, BUG-1549)
	$(HOSTCXX) -std=c++20 -O1 -g -Wall $(SAN) -Imods/coop -o $(B)/host/civ_rivals_tests tests/civ_rivals_tests.cpp
	$(B)/host/civ_rivals_tests
	@# WP-R6 of 0.8.0: Space Rivals (space_rivals_logic.h, BUG-1550)
	$(HOSTCXX) -std=c++20 -O1 -g -Wall $(SAN) -Imods/coop -o $(B)/host/space_rivals_tests tests/space_rivals_tests.cpp
	$(B)/host/space_rivals_tests
	@# BUG-1557: Rivals per-player Tribe/Civ consequence cards & Space philosophy (history_logic.h)
	$(HOSTCXX) -std=c++20 -O1 -g -Wall $(SAN) -Imods/coop -o $(B)/host/history_rivals_tests tests/history_rivals_tests.cpp
	$(B)/host/history_rivals_tests
	@# WP-Q2 of 0.7.99: Creature editor depth handle (editor_depth_logic.h, BUG-0871) and shared creations library (library_core.h, BUG-0873)
	$(HOSTCXX) -std=c++20 -O1 -g -Wall $(SAN) -Imods/coop -o $(B)/host/editor_depth_tests tests/editor_depth_tests.cpp
	$(B)/host/editor_depth_tests
	$(HOSTCXX) -std=c++20 -O1 -g -Wall $(SAN) -Imods/coop -o $(B)/host/library_tests tests/library_tests.cpp
	$(B)/host/library_tests
	@# D3 of 0.7.15: the Tribe rules (tribe_logic.h, BUG-1161) and the member animations' message type (C7, BUG-1160)
	$(HOSTCXX) -std=c++20 -O1 -g -Wall $(SAN) -Imods/coop -o $(B)/host/tribe_tests tests/tribe_tests.cpp
	$(B)/host/tribe_tests
	@# WP-R4 of 0.8.0: Tribe Rivals logic & wire payloads (tribe_rivals_logic.h, BUG-1548)
	$(HOSTCXX) -std=c++20 -O1 -g -Wall $(SAN) -Imods/coop -o $(B)/host/tribe_rivals_tests tests/tribe_rivals_tests.cpp
	$(B)/host/tribe_rivals_tests
	$(HOSTCXX) -std=c++20 -O1 -g -Wall $(SAN) -Imods/coop -o $(B)/host/wire_tests tests/wire_tests.cpp
	$(B)/host/wire_tests
	$(HOSTCXX) -std=c++20 -O1 -g -Wall $(SAN) -Imods/coop -o $(B)/host/text_tests tests/text_tests.cpp
	$(B)/host/text_tests
	$(HOSTCXX) -std=c++20 -O1 -g -Wall $(SAN) -o $(B)/host/path_mask_tests tests/path_mask_tests.cpp
	$(B)/host/path_mask_tests
	@! $(B)/host/path_mask_tests --old > $(B)/host/path_mask_tests_old.txt || { echo "path_mask_tests --old must fail (BUG-1124)"; exit 1; }
	$(HOSTCXX) -std=c++20 -O1 -g -Wall $(SAN) -o $(B)/host/jpeg_tests tests/jpeg_tests.cpp sporekit/jpeg.cpp
	$(B)/host/jpeg_tests $(B)/host/jpeg
	python3 tools/tests/jpeg_check.py $(B)/host/jpeg
	$(HOSTCXX) -std=c++20 -O1 -g -Wall $(SAN) -Imods/coop -o $(B)/host/fixes_tests tests/fixes_tests.cpp mods/coop/fixes.cpp
	$(B)/host/fixes_tests $(wildcard mods/coop/*.cpp mods/coop/*.h mods/coop/net/*.cpp mods/coop/net/*.h updater/*.cpp updater/*.h)

unit-lint:
	python3 tools/re_lint.py
	python3 tools/docs_lint.py
	python3 tools/glyph_lint.py
	python3 tools/tickets.py check
	python3 tools/ci_lint.py
	python3 tools/ratchet_lint.py
	python3 tools/conflict_lint.py
	python3 tools/ratchet_lint.py --self-test
	python3 tools/fix_retire.py check
	python3 tools/fix_retire.py check --self-test
	python3 tools/msg_manifest.py check
	python3 tools/msg_manifest.py --self-test > $(B)/host/msg_manifest_selftest.txt || { cat $(B)/host/msg_manifest_selftest.txt; exit 1; }

unit-tools:
	python3 tools/tests/tools_selftest.py
	python3 tools/tests/visual_selftest.py
	python3 tools/tests/state_fields_check.py
	python3 tools/tests/test_lint.py
	python3 tools/tests/harness_selftest.py

# B5 (BUG-1123): crash_triage.py symbolises coop.dll / sporekit.dll frames with the debug symbols of the
# report's version (symbols_pack.sh of the local build, a fake gh for the download; no network)
unit-symbols: $(B)/coop.dll $(B)/sporekit.dll $(B)/steam_api.dll
	python3 tools/tests/triage_symbols_check.py $(B) $(LLVM_MINGW)

# the same unit tests under AddressSanitizer: a freed object used later fails here (BUG-0029)
test-asan: sanitizer-probe
	@mkdir -p $(B)/host
	$(HOSTCXX) -std=c++20 -O1 -g -Wall -fsanitize=address,undefined -fno-omit-frame-pointer -Imods/coop -o $(B)/host/net_tests_asan mods/coop/net/*.cpp updater/pack.cpp updater/sha256.cpp tests/net_tests.cpp
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 python3 tools/tests/shards.py $(NET_SHARDS) $(B)/host/net_tests_asan
	@# --nofix: every fix test runs to the end and fails (BUG-0214: it used to abort at test_dbbf_wrap); every shard
	@# must reach its end, the FAILs counted over all of them
	@ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 python3 tools/tests/shards.py $(NET_SHARDS) \
	  $(B)/host/net_tests_asan --nofix --expect-fail --out $(B)/host/net_tests_nofix.txt \
	  || { echo "net_tests_asan --nofix: expected FAILs to the end"; exit 1; }
	@# the decoder bounds fixes' off side reads out of bounds: ASan must stop it (BUG-0208, BUG-0047)
	@! ASAN_OPTIONS=detect_leaks=0 $(B)/host/net_tests_asan --old-dbbf > $(B)/host/net_tests_old_dbbf.txt 2>&1 \
	  && grep -q 'AddressSanitizer' $(B)/host/net_tests_old_dbbf.txt || { echo "net_tests_asan --old-dbbf: ASan did not report"; exit 1; }
	@! ASAN_OPTIONS=detect_leaks=0 $(B)/host/net_tests_asan --old-refpack > $(B)/host/net_tests_old_refpack.txt 2>&1 \
	  && grep -q 'AddressSanitizer' $(B)/host/net_tests_old_refpack.txt || { echo "net_tests_asan --old-refpack: ASan did not report"; exit 1; }
	@echo "net_tests_asan: --nofix ran to the end, --old-dbbf and --old-refpack died under ASan"

# the setup's shared release state under ThreadSanitizer (BUG-0077; `release_tsan --old` must fail)
test-tsan: sanitizer-probe
	@mkdir -p $(B)/host
	$(HOSTCXX) -std=c++20 -O1 -g -Wall -fsanitize=thread -o $(B)/host/release_tsan tests/release_tsan.cpp updater/pack.cpp updater/sha256.cpp
	TSAN_OPTIONS=halt_on_error=1 $(TSAN_RUN)$(B)/host/release_tsan
	$(HOSTCXX) -std=c++20 -O1 -g -Wall -fsanitize=thread -Imods/coop -o $(B)/host/core_tests_tsan tests/core_tests.cpp
	TSAN_OPTIONS=halt_on_error=1 $(TSAN_RUN)$(B)/host/core_tests_tsan
	@! TSAN_OPTIONS=halt_on_error=1 $(TSAN_RUN)$(B)/host/release_tsan --old-merge > $(B)/host/release_tsan_old_merge.txt 2>&1 \
	  || { cat $(B)/host/release_tsan_old_merge.txt; echo "release_tsan --old-merge must fail (BUG-0244)"; exit 1; }

# Windows-side unit tests (file handling of the updater and of received worlds), run under Wine in
# a lane's bottle by tools/tests/win_unit.py (lab test win_unit)
test-win: $(B)/win_tests.exe $(B)/SporeCoopSetup.exe
$(B)/win_tests.exe: tests/win_tests.cpp installer/install.cpp installer/install.h mods/coop/slotfs.cpp mods/coop/slotfs.h updater/winupdate.cpp updater/pending.h updater/pack.cpp updater/sha256.cpp sporekit/inifile.cpp sporekit/inifile.h
	@mkdir -p $(B)
	$(CXX) $(CXXFLAGS) -static -o $@ tests/win_tests.cpp installer/install.cpp mods/coop/slotfs.cpp updater/winupdate.cpp updater/pack.cpp updater/sha256.cpp sporekit/inifile.cpp -lwinhttp -lws2_32

clean:
	rm -rf $(B)

# A2 (BUG-1120): the release build of HEAD as CI makes it (a git archive copy, CI's Python and toolchain,
# every step of the tag workflow that runs locally); before every tag: docs/10-dev-pipeline.md#pipeline-release-dry
.PHONY: release-dry
release-dry:
	tools/release_dry.sh

# C6 (NET-10, BUG-1133): tests/fuzz_decoders.cpp as a libFuzzer target (clang): coverage-guided, every decoder of the
# protocol and of mods/coop/wire, under ASan + UBSan. Not part of `make test` (that runs the fixed-seed build); CI runs it
# on every push for FUZZ_SECONDS and weekly for longer (.github/workflows/fuzz.yml). Corpus: $(B)/host/fuzz-corpus (kept
# between local runs; CI starts empty). A finding is a crash-* file in $(B)/host: `$(B)/host/fuzz_lib <file>` replays it.
FUZZ_CXX ?= clang++
FUZZ_SECONDS ?= 60
.PHONY: fuzz
fuzz:
	@mkdir -p $(B)/host/fuzz-corpus
	$(FUZZ_CXX) -std=c++20 -O1 -g -Wall -Wno-unused-function -DFUZZ_LIBFUZZER -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -Imods/coop -o $(B)/host/fuzz_lib mods/coop/net/*.cpp updater/pack.cpp updater/sha256.cpp tests/fuzz_decoders.cpp
	cd $(B)/host && ASAN_OPTIONS=detect_leaks=0 ./fuzz_lib -max_total_time=$(FUZZ_SECONDS) -max_len=4096 -timeout=10 -rss_limit_mb=2048 -print_final_stats=1 fuzz-corpus

# the toolchain folder this Makefile uses (tools/package.sh, tools/tests/update_check.py ask here)
.PHONY: toolchain-path
toolchain-path:
	@echo $(LLVM_MINGW)

-include $(KIT_OBJ:.o=.d) $(COOP_OBJ:.o=.d)
