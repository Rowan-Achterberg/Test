# Minesweeper for Windows on ARM64, cross-compiled with Zig's C compiler.
#
#   make test      run the engine and solver tests natively
#   make release   build release/mines-arm64.exe
#
# Zig is only used as a C cross-compiler (pip install ziglang).
# ARCH=x86_64 builds the same program for Intel/AMD Windows.

ZIG    ?= python3 -m ziglang
ARCH   ?= aarch64
CFLAGS ?= -std=c11 -O2 -Wall -Wextra
SRC     = src/core.c src/solver.c
OUT     = release/mines-$(subst aarch64,arm64,$(ARCH)).exe

.PHONY: release test clean

release: $(OUT)

$(OUT): $(SRC) src/main.c src/core.h src/solver.h
	@mkdir -p release
	$(ZIG) cc -target $(ARCH)-windows-gnu $(CFLAGS) -s -Wl,--subsystem,windows \
		$(SRC) src/main.c -o $@ -luser32 -lgdi32 -ldwmapi

test:
	@mkdir -p build
	$(CC) $(CFLAGS) $(SRC) tests/test.c -lm -o build/test
	./build/test

clean:
	rm -rf build release
