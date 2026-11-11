# mines

Minesweeper for Windows 11 on ARM64. One small native executable, no installer,
no dependencies: download `release/mines-arm64.exe` and run it.

## What is in it

- **Deterministic boards.** A game is a pure function of size, seed and the
  first cell you open. The seed is shown in the header; copy it (Ctrl+C) and
  paste it (Ctrl+V) to replay or share a board.
- **Safe start.** Mines are laid on the first probe by a seeded Fisher-Yates
  shuffle that keeps the probed cell and its neighbours clear.
- **Exact solver.** It sees only what the player sees. It splits the frontier
  into independent components, enumerates each by backtracking, and combines
  them by polynomial convolution weighted by the ways to scatter the remaining
  mines. `P` overlays the exact chance that each cell is a mine, `?` gives a
  hint, `S` applies every deduction that is certain.
- **3BV** (the minimum number of clicks for the board) next to your click count.
- Hexadecimal axes, keyboard play with arrows or `h j k l`.

## Controls

| Input | Action |
|---|---|
| left click, `space`, `enter` | open a cell; on a number, chord |
| right click, `f` | flag |
| middle click | chord |
| `n`, `F2` | new game |
| `r` | retry the same seed |
| `1` `2` `3` | beginner 9x9/10, intermediate 16x16/40, expert 30x16/99 |
| `?` | hint |
| `p` | show mine probabilities |
| `s` | solver step |
| `Ctrl+C`, `Ctrl+V` | copy / paste the seed |
| `F1` | help |
| `q`, `Esc` | quit |

## Layout

| File | Role |
|---|---|
| `src/core.c`, `core.h` | rules, board, PRNG; portable C11, no allocation |
| `src/solver.c`, `solver.h` | exact probability solver; portable C11 |
| `src/main.c` | Win32 window, GDI drawing, input |
| `tests/test.c` | engine and solver tests |
| `release/mines-arm64.exe` | compiled release build |

## Build and test

    make test       # native tests, any platform with a C compiler
    make release    # cross-compiles release/mines-arm64.exe

The release is cross-compiled from Linux with Zig's C compiler
(`pip install ziglang`). `make release ARCH=x86_64` builds for Intel/AMD.

The tests check the solver against brute-force enumeration of every mine
layout on small boards, then let it play 3,500 full games while asserting that
a cell it proves safe is never a mine. In that run it won 96% of beginner,
87% of intermediate and 51% of expert games. On about 3% of expert games a
position exceeds the search budget; the program then reports that the solver
is unavailable for that position.
