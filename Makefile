CC      ?= gcc
MUSL    ?= 0
ifeq ($(MUSL),1)
CC      := musl-gcc
EXTRA   := -idirafter /usr/include -idirafter /usr/include/x86_64-linux-gnu
endif
CFLAGS  ?= -O3 -march=x86-64-v3 -std=c11 -Wall -Wextra
CFLAGS  += -Isrc -Isrc/vendor -I/usr/include/libdrm $(EXTRA) -D_GNU_SOURCE
VERSION := $(shell git describe --tags --always --dirty 2>/dev/null || echo v2.1)
CFLAGS  += -DCRAFTBOOT_VERSION_GIT=\"$(VERSION)\"
LDLIBS  := -lpthread -lm
B       := build

CORE    := $(B)/render.o $(B)/assets.o $(B)/menu.o $(B)/efivar.o
SHIP    := $(B)/display_drm.o $(B)/input_evdev.o $(B)/actions.o $(B)/initlib.o $(B)/plat_host.o $(B)/main.o
DEVOBJ  := $(B)/dev_render.o $(B)/dev_assets.o $(B)/dev_menu.o \
           $(B)/dev_display_sdl.o $(B)/dev_input_sdl.o $(B)/dev_actions.o \
           $(B)/dev_initlib.o $(B)/dev_plat_host.o $(B)/dev_main.o

all: $(B)/craftboot
dev: $(B)/craftboot-dev

$(B)/%.o: src/core/%.c ; @mkdir -p $(B); $(CC) $(CFLAGS) -c $< -o $@
$(B)/%.o: src/platform/%.c ; @mkdir -p $(B); $(CC) $(CFLAGS) -c $< -o $@
$(B)/%.o: src/boot/%.c ; @mkdir -p $(B); $(CC) $(CFLAGS) -c $< -o $@
$(B)/%.o: src/init/%.c ; @mkdir -p $(B); $(CC) $(CFLAGS) -c $< -o $@

$(B)/craftboot: $(CORE) $(SHIP)
	$(CC) $(CFLAGS) -static -o $@ $^ $(LDLIBS)

$(B)/dev_%.o: src/core/%.c ; @mkdir -p $(B); $(CC) $(CFLAGS) -DDEV -c $< -o $@
$(B)/dev_%.o: src/platform/%.c ; @mkdir -p $(B); $(CC) $(CFLAGS) -DDEV $(shell sdl2-config --cflags) -c $< -o $@
$(B)/dev_%.o: src/boot/%.c ; @mkdir -p $(B); $(CC) $(CFLAGS) -DDEV -c $< -o $@
$(B)/dev_%.o: src/init/%.c ; @mkdir -p $(B); $(CC) $(CFLAGS) -DDEV -c $< -o $@

$(B)/craftboot-dev: $(DEVOBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(shell sdl2-config --libs) $(LDLIBS)

TESTS := $(patsubst tests/%.c,$(B)/%,$(wildcard tests/test_*.c))
# initlib.o (uuid_parse16 / ext4_uuid_matches) is linked into every test so
# the generic pattern rule covers test_uuid.c too, without a special case.
$(B)/test_%: tests/test_%.c $(CORE) $(B)/actions.o $(B)/display_drm.o $(B)/input_evdev.o $(B)/initlib.o $(B)/plat_host.o
	@mkdir -p $(B); $(CC) $(CFLAGS) -Itests -o $@ $< $(filter %.o,$^) $(LDLIBS)
test: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done; echo "ALL TESTS PASS"

bench: $(B)/bench_pano
$(B)/bench_pano: tests/bench_pano.c $(CORE) $(B)/display_drm.o $(B)/input_evdev.o $(B)/plat_host.o
	@mkdir -p $(B); $(CC) $(CFLAGS) -Itests -o $@ $< $(filter %.o,$^) $(LDLIBS)
	./$@
.PHONY: bench

diff-pano: $(CORE) $(B)/display_drm.o $(B)/input_evdev.o $(B)/plat_host.o
	@mkdir -p $(B)
	$(CC) $(CFLAGS) -DPANO_NO_AVX2 -c src/core/render.c -o $(B)/render_scalar.o
	$(CC) $(CFLAGS) -Itests -o $(B)/diff_scalar tests/diff_pano.c $(B)/render_scalar.o $(B)/assets.o $(B)/menu.o $(B)/display_drm.o $(B)/input_evdev.o $(B)/plat_host.o $(LDLIBS)
	$(CC) $(CFLAGS) -Itests -o $(B)/diff_avx2 tests/diff_pano.c $(CORE) $(B)/display_drm.o $(B)/input_evdev.o $(B)/plat_host.o $(LDLIBS)
	./$(B)/diff_scalar > $(B)/frames_scalar.bin
	./$(B)/diff_avx2   > $(B)/frames_avx2.bin
	cmp $(B)/frames_scalar.bin $(B)/frames_avx2.bin && echo "DIFF-PANO: byte-identical"
.PHONY: diff-pano

# ---- sanitizer + fuzz builds --------------------------------------------
# Separate object namespace (asan_*) built straight from source with
# -fsanitize=address,undefined, so every translation unit under test carries
# sanitizer instrumentation (stack/global bounds checks included, not just
# the heap redzones an uninstrumented .o would still get for free via the
# intercepted malloc). -O1 keeps ASan's stack-use-after-return checks and
# backtraces meaningful without the -O3 vectorized codegen path diverging
# too far from what's actually shipped.
# -fno-sanitize-recover=undefined is load-bearing for CI: UBSan defaults to
# "recoverable" mode -- on a UB event it prints a "runtime error:" line and
# KEEPS GOING with exit code 0, so `make test-asan` / `make fuzz` would stay
# GREEN through a real undefined-behavior regression. Baking no-recover into
# the binary makes any UB trip abort with a nonzero exit (can't be bypassed by
# a missing env var), so the sanitizer gate is actually blocking. (ASan
# already aborts nonzero on its own errors; this only fixes UBSan.)
ASAN_CFLAGS := $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined
ASAN_CORE   := $(B)/asan_render.o $(B)/asan_assets.o $(B)/asan_menu.o $(B)/asan_efivar.o

$(B)/asan_%.o: src/core/%.c ; @mkdir -p $(B); $(CC) $(ASAN_CFLAGS) -c $< -o $@
$(B)/asan_%.o: src/platform/%.c ; @mkdir -p $(B); $(CC) $(ASAN_CFLAGS) -c $< -o $@
$(B)/asan_%.o: src/boot/%.c ; @mkdir -p $(B); $(CC) $(ASAN_CFLAGS) -c $< -o $@
$(B)/asan_%.o: src/init/%.c ; @mkdir -p $(B); $(CC) $(ASAN_CFLAGS) -c $< -o $@

ASAN_TESTS := $(patsubst tests/%.c,$(B)/asan_%,$(wildcard tests/test_*.c))
$(B)/asan_test_%: tests/test_%.c $(ASAN_CORE) $(B)/asan_actions.o $(B)/asan_display_drm.o $(B)/asan_input_evdev.o $(B)/asan_initlib.o $(B)/asan_plat_host.o
	@mkdir -p $(B); $(CC) $(ASAN_CFLAGS) -Itests -o $@ $< $(filter %.o,$^) $(LDLIBS)

# detect_leaks=0: the test harness loads font/image atlases (see
# tests/test_text.c, tests/test_menu.c) that live for the process lifetime
# and are never explicitly freed -- a known, harmless non-defect in test
# code, not a real leak in shipped code. Everything else (heap/stack/global
# overflow, UB) stays fully enforced.
test-asan: $(ASAN_TESTS)
	@for t in $(ASAN_TESTS); do ASAN_OPTIONS=detect_leaks=0 ./$$t || exit 1; done; echo "ALL ASAN TESTS PASS"
.PHONY: test-asan

fuzz: $(B)/fuzz_parse
$(B)/fuzz_parse: tests/fuzz_parse.c $(B)/asan_assets.o $(B)/asan_actions.o $(B)/asan_initlib.o $(B)/asan_plat_host.o
	@mkdir -p $(B); $(CC) $(ASAN_CFLAGS) -Itests -o $@ $< $(filter %.o,$^) $(LDLIBS)
	ASAN_OPTIONS=detect_leaks=0 ./$@
.PHONY: fuzz

# ---- EFI scaffolding (v3.0) ---------------------------------------------
# Freestanding UEFI app: no libc, no libm -- only efi.h + mini_libc + compiler
# builtins. Cross-compiled PE32+ via mingw's ms_abi support.
MINGW := x86_64-w64-mingw32-gcc
# Version consistency: reuse the same $(VERSION) (git describe --tags) as the
# host CFLAGS above, rather than a second hardcoded literal, so the EFI menu
# footer and the host binary's footer always agree. Pre-tag this prints the
# describe string (e.g. v2.1-NN-gSHA); it reads v3.0 once the v3.0 tag lands.
EFI_CFLAGS := -ffreestanding -fno-stack-protector -fno-stack-check -fshort-wchar \
              -mno-red-zone -mno-stack-arg-probe -O2 -mavx2 -Wall -Wextra -Isrc -Isrc/vendor \
              -DPANO_MP_SERVICES -DEFI -DCRAFTBOOT_VERSION_GIT=\"$(VERSION)\" \
              -D__USE_MINGW_ANSI_STDIO=0 -DNDEBUG \
              -DSTBI_NO_STDIO -DSTBI_NO_LINEAR $(EFI_EXTRA)
EFI_LDFLAGS := -nostdlib -Wl,-dll -shared -Wl,--subsystem,10 -e efi_main
EFI_SRC := src/efi/main.c src/efi/mini_libc.c src/efi/display_efi.c src/efi/input_efi.c \
           src/efi/fs.c src/efi/sys.c src/efi/actions_efi.c src/efi/plat_efi.c src/efi/sbat.c \
           src/core/efivar.c src/core/render.c src/core/menu.c src/core/assets.c
# Two things are required for Ubuntu's shim (ships 15.8) to load the MOK-signed
# image:
#   1. .sbat section: shim 15.8 REJECTS any second stage without a valid .sbat
#      section (Security Violation), regardless of signature. src/efi/sbat.c
#      defines it so the LINKER places it at a real RVA inside SizeOfImage
#      (objcopy --add-section lands it OUTSIDE the mapped image -> shim can't
#      see it). ukify-built UKIs carry one; a hand-linked mingw PE otherwise not.
#   2. --strip-all: drop mingw's trailing COFF symbol/string-table overlay (past
#      the last PE section) that otherwise trips shim's Authenticode hashing.
MINGW_OBJCOPY := x86_64-w64-mingw32-objcopy
efi: ; @mkdir -p build; $(MINGW) $(EFI_CFLAGS) $(EFI_SRC) -o build/craftboot.efi $(EFI_LDFLAGS); \
	$(MINGW_OBJCOPY) --strip-all build/craftboot.efi
.PHONY: efi
# (Core/EFI source list grows in later tasks.)

# Task 9: trivial chainload TARGET .efi (distinct solid-color fill + serial
# marker, see tests/efi/chainload_target.c) used to prove the zero-reboot
# LoadImage/StartImage handoff in QEMU. It is a test fixture, not part of
# craftboot.efi, so it is built by its own rule and kept out of EFI_SRC.
chainload-target: ; @mkdir -p build; $(MINGW) $(EFI_CFLAGS) tests/efi/chainload_target.c -o build/chainload_target.efi $(EFI_LDFLAGS)
.PHONY: chainload-target

clean: ; -rm -rf $(B)
.PHONY: all dev test clean bench
