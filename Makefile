# SporeCoop build (Linux host, llvm-mingw cross toolchain, 32-bit Windows targets)
LLVM_MINGW ?= $(HOME)/.local/opt/llvm-mingw-20261006-ucrt-ubuntu-22.04-x86_64
CC  := $(LLVM_MINGW)/bin/i686-w64-mingw32-clang
CXX := $(LLVM_MINGW)/bin/i686-w64-mingw32-clang++
HOSTCXX ?= clang++

B := build
COMMON  := -O2 -g -Wall -Wno-unused-function -Wno-missing-braces -D_WIN32_WINNT=0x0601 -DUNICODE -D_UNICODE
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
all: loader sporekit coop res setup

# game resources (icons, ...) for SporeCoop: Data/SporeCoop.package
UI_PNG := $(wildcard data/ui/*.png)
res: $(B)/SporeCoop.package
$(B)/SporeCoop.package: tools/mkpackage.py $(UI_PNG)
	@mkdir -p $(B)
	python3 tools/mkpackage.py $@ $(foreach f,$(UI_PNG),sporecoop!$(basename $(notdir $(f))).png=$(f))

loader: $(B)/steam_api.dll
sporekit: $(B)/sporekit.dll
coop: $(B)/coop.dll

$(B)/steam_api.dll: loader/loader.c loader/steam_api.def
	@mkdir -p $(B)
	$(CC) $(CFLAGS) -shared -o $@ loader/loader.c loader/steam_api.def -Wl,--kill-at

$(B)/obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(MINHOOK)/include -c $< -o $@

$(B)/obj/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -I$(MINHOOK)/include -Isdk/include -c $< -o $@ -MMD -MP

$(B)/sporekit.dll: $(KIT_OBJ) $(MH_OBJ)
	$(CXX) $(LDDLL) -o $@ $^ -lws2_32 -luser32 -lshell32 -ld3d9 -Wl,--out-implib,$(B)/libsporekit.a

$(B)/coop.dll: $(COOP_OBJ)
	$(CXX) $(LDDLL) -o $@ $^ -lws2_32 -luser32 -liphlpapi -lwinhttp -lshell32

# SporeCoopSetup.exe: install / update / uninstall (installer/, updater/)
WINDRES := $(LLVM_MINGW)/bin/i686-w64-mingw32-windres
COOP_VERSION = $(shell sed -n 's/^#define COOP_VERSION "\(.*\)"/\1/p' mods/coop/coop.cpp)
setup: $(B)/SporeCoopSetup.exe
$(B)/setup.ico: data/ui/coop_icon.png tools/make_ico.py
	@mkdir -p $(B)
	python3 tools/make_ico.py $< $@
$(B)/obj/installer/setup_rc.o: installer/setup.rc installer/setup.manifest $(B)/setup.ico
	@mkdir -p $(dir $@)
	$(WINDRES) -i installer/setup.rc -o $@ --include-dir installer
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

# native Linux unit tests for portable netcode
test: sanitizer-probe
	@mkdir -p $(B)/host
ifeq ($(SKIP_SANITIZERS),1)
	@echo "WARNING: SKIP_SANITIZERS=1: the ASan/UBSan/TSan tests are NOT RUN"
endif
	$(HOSTCXX) -std=c++20 -O1 -g -Wall -Imods/coop -o $(B)/host/net_tests mods/coop/net/*.cpp updater/pack.cpp updater/sha256.cpp tests/net_tests.cpp
	$(B)/host/net_tests
	python3 tools/tests/fakepeer_check.py $(B)/host/net_tests
	python3 tools/tests/lab_selftest.py
ifneq ($(SKIP_SANITIZERS),1)
	$(MAKE) --no-print-directory test-asan
	$(MAKE) --no-print-directory test-tsan
	$(HOSTCXX) -std=c++20 -O1 -g -Wall -fsanitize=address,undefined -fno-omit-frame-pointer -Imods/coop -o $(B)/host/fuzz_decoders mods/coop/net/*.cpp updater/pack.cpp updater/sha256.cpp tests/fuzz_decoders.cpp
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 $(B)/host/fuzz_decoders
endif
	$(HOSTCXX) -std=c++20 -O1 -g -Wall $(SAN) -Imods/coop -o $(B)/host/validate_tests tests/validate_tests.cpp
	$(B)/host/validate_tests
	$(HOSTCXX) -std=c++20 -O1 -g -Wall $(SAN) -Imods/coop -o $(B)/host/cell_tests tests/cell_tests.cpp
	$(B)/host/cell_tests
	$(HOSTCXX) -std=c++20 -O1 -g -Wall $(SAN) -Imods/coop -o $(B)/host/npc_tests tests/npc_tests.cpp
	$(B)/host/npc_tests
	$(HOSTCXX) -std=c++20 -O1 -g -Wall $(SAN) -Imods/coop -o $(B)/host/space_tests tests/space_tests.cpp
	$(B)/host/space_tests
	$(HOSTCXX) -std=c++20 -O1 -g -Wall $(SAN) -Imods/coop -o $(B)/host/text_tests tests/text_tests.cpp
	$(B)/host/text_tests
	$(HOSTCXX) -std=c++20 -O1 -g -Wall $(SAN) -Imods/coop -o $(B)/host/fixes_tests tests/fixes_tests.cpp mods/coop/fixes.cpp
	$(B)/host/fixes_tests $(wildcard mods/coop/*.cpp mods/coop/*.h mods/coop/net/*.cpp mods/coop/net/*.h updater/*.cpp updater/*.h)
	python3 tools/re_lint.py
	python3 tools/docs_lint.py
	python3 tools/glyph_lint.py
	python3 tools/tickets.py check
	python3 tools/tests/tools_selftest.py
ifeq ($(SKIP_SANITIZERS),1)
	@echo "WARNING: SANITIZER TESTS NOT RUN (SKIP_SANITIZERS=1)"
endif

# the same unit tests under AddressSanitizer: a freed object used later fails here (BUG-0029)
test-asan: sanitizer-probe
	@mkdir -p $(B)/host
	$(HOSTCXX) -std=c++20 -O1 -g -Wall -fsanitize=address,undefined -fno-omit-frame-pointer -Imods/coop -o $(B)/host/net_tests_asan mods/coop/net/*.cpp updater/pack.cpp updater/sha256.cpp tests/net_tests.cpp
	ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 $(B)/host/net_tests_asan
	@# --nofix: every fix test runs to the end and fails (BUG-0214: it used to abort at test_dbbf_wrap)
	@ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 $(B)/host/net_tests_asan --nofix > $(B)/host/net_tests_nofix.txt 2>&1; \
	  rc=$$?; tail -n 1 $(B)/host/net_tests_nofix.txt | grep -q '^FAILED (' && [ $$rc = 1 ] \
	  || { tail -n 20 $(B)/host/net_tests_nofix.txt; echo "net_tests_asan --nofix: expected FAILs to the end (exit $$rc)"; exit 1; }
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
	@! TSAN_OPTIONS=halt_on_error=1 $(TSAN_RUN)$(B)/host/release_tsan --old-merge > $(B)/host/release_tsan_old_merge.txt 2>&1 \
	  || { cat $(B)/host/release_tsan_old_merge.txt; echo "release_tsan --old-merge must fail (BUG-0244)"; exit 1; }

# Windows-side unit tests (file handling of the updater and of received worlds), run under Wine in
# a lane's bottle by tools/tests/win_unit.py (lab test win_unit)
test-win: $(B)/win_tests.exe $(B)/SporeCoopSetup.exe
$(B)/win_tests.exe: tests/win_tests.cpp installer/install.cpp installer/install.h mods/coop/slotfs.cpp mods/coop/slotfs.h updater/winupdate.cpp updater/pending.h updater/pack.cpp updater/sha256.cpp sporekit/inifile.cpp sporekit/inifile.h
	@mkdir -p $(B)
	$(CXX) $(CXXFLAGS) -static -o $@ tests/win_tests.cpp installer/install.cpp mods/coop/slotfs.cpp updater/winupdate.cpp updater/pack.cpp updater/sha256.cpp sporekit/inifile.cpp -lwinhttp

clean:
	rm -rf $(B)

-include $(KIT_OBJ:.o=.d) $(COOP_OBJ:.o=.d)
