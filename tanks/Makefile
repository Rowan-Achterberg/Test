# Tank Defender for Windows on ARM64, cross-compiled with Zig's C compiler.
#
#   make test      run the game logic tests natively
#   make shots     also write sample frames to build/shots as .ppm
#   make release   build release/tanks-arm64.exe
#
# Zig is only used as a C cross-compiler (pip install ziglang).
# ARCH=x86_64 builds the same program for Intel/AMD Windows.

ZIG    ?= python3 -m ziglang
ARCH   ?= aarch64
CFLAGS ?= -std=c11 -O2 -Wall -Wextra
SRC     = src/game.c src/render.c
OUT     = release/tanks-$(subst aarch64,arm64,$(ARCH)).exe

.PHONY: release test shots clean

release: $(OUT)

$(OUT): $(SRC) src/main.c src/game.h
	@mkdir -p release
	$(ZIG) cc -target $(ARCH)-windows-gnu $(CFLAGS) -s -Wl,--subsystem,windows \
		$(SRC) src/main.c -o $@ -luser32 -lgdi32 -lwinmm

build/test: $(SRC) tests/test.c src/game.h
	@mkdir -p build
	$(CC) $(CFLAGS) $(SRC) tests/test.c -o build/test

test: build/test
	./build/test

shots: build/test
	@mkdir -p build/shots
	./build/test shots build/shots

clean:
	rm -rf build release
