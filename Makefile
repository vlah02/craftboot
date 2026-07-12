CC      ?= gcc
MUSL    ?= 0
ifeq ($(MUSL),1)
CC      := musl-gcc
EXTRA   := -idirafter /usr/include -idirafter /usr/include/x86_64-linux-gnu
endif
CFLAGS  ?= -O3 -march=x86-64-v3 -std=c11 -Wall -Wextra
CFLAGS  += -Isrc -Isrc/vendor -I/usr/include/libdrm $(EXTRA) -D_GNU_SOURCE
LDLIBS  := -lpthread -lm
B       := build

CORE    := $(B)/render.o $(B)/assets.o $(B)/menu.o
SHIP    := $(B)/display_drm.o $(B)/input_evdev.o $(B)/actions.o $(B)/main.o
DEVOBJ  := $(B)/dev_render.o $(B)/dev_assets.o $(B)/dev_menu.o \
           $(B)/dev_display_sdl.o $(B)/dev_input_sdl.o $(B)/dev_actions.o $(B)/dev_main.o

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
$(B)/test_%: tests/test_%.c $(CORE) $(B)/actions.o
	@mkdir -p $(B); $(CC) $(CFLAGS) -Itests -o $@ $< $(filter %.o,$^) $(LDLIBS)
test: $(TESTS)
	@for t in $(TESTS); do ./$$t || exit 1; done; echo "ALL TESTS PASS"

bench: $(B)/bench_pano
$(B)/bench_pano: tests/bench_pano.c $(CORE)
	@mkdir -p $(B); $(CC) $(CFLAGS) -Itests -o $@ $< $(filter %.o,$^) $(LDLIBS)
	./$@
.PHONY: bench

diff-pano: $(CORE)
	@mkdir -p $(B)
	$(CC) $(CFLAGS) -DPANO_NO_AVX2 -c src/core/render.c -o $(B)/render_scalar.o
	$(CC) $(CFLAGS) -Itests -o $(B)/diff_scalar tests/diff_pano.c $(B)/render_scalar.o $(B)/assets.o $(B)/menu.o $(LDLIBS)
	$(CC) $(CFLAGS) -Itests -o $(B)/diff_avx2 tests/diff_pano.c $(CORE) $(LDLIBS)
	./$(B)/diff_scalar > $(B)/frames_scalar.bin
	./$(B)/diff_avx2   > $(B)/frames_avx2.bin
	cmp $(B)/frames_scalar.bin $(B)/frames_avx2.bin && echo "DIFF-PANO: byte-identical"
.PHONY: diff-pano

clean: ; rm -rf $(B)
.PHONY: all dev test clean bench
