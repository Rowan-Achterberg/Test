# Tank Defender for Windows, cross-compiled with Zig's C compiler.
#
#   make test      run the game logic tests natively
#   make shots     also write sample frames to build/shots as .ppm
#   make release   build release/tanks-arm64.exe and release/tanks-amd64.exe
#
# Zig is only used as a C cross-compiler (pip install ziglang).

ZIG    ?= python3 -m ziglang
CFLAGS ?= -std=c11 -O2 -Wall -Wextra
SRC     = src/game.c src/render.c
WINSRC  = $(SRC) src/main.c
WINFLAGS = $(CFLAGS) -s -Wl,--subsystem,windows
WINLIBS = -luser32 -lgdi32 -lwinmm

.PHONY: release test shots clean

release: release/tanks-arm64.exe release/tanks-amd64.exe

# ARM64: Snapdragon and other Windows-on-ARM machines.
release/tanks-arm64.exe: $(WINSRC) src/game.h
	@mkdir -p release
	$(ZIG) cc -target aarch64-windows-gnu $(WINFLAGS) $(WINSRC) -o $@ $(WINLIBS)

# AMD64 (x86-64): ordinary Intel and AMD machines.
release/tanks-amd64.exe: $(WINSRC) src/game.h
	@mkdir -p release
	$(ZIG) cc -target x86_64-windows-gnu $(WINFLAGS) $(WINSRC) -o $@ $(WINLIBS)

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
