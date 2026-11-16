# Tank Defender

A top-down tank game in the classic arcade style, for Windows 11. One small
executable with no installer and no data files: download it and run it.

| Your computer | File |
|---|---|
| ARM64 (Snapdragon and other Windows-on-ARM) | `release/tanks-arm64.exe` |
| AMD64 / x86-64 (ordinary Intel and AMD) | `release/tanks-amd64.exe` |

Defend your flag. Twenty enemy tanks arrive in each stage through three gates
along the top; destroy them all to move on. You have five tanks per game.
Lose your flag or your last tank and the game is over.

On the title screen choose **1 player**, or **2 players** to fight alongside
a computer-driven ally (the green tank). The ally hunts the nearest enemy,
shoots its way through brick, never fires towards your flag, and has five
tanks of its own. Shells from your own side pass through each other's tanks.

## Controls

| Key | Action |
|---|---|
| arrows or `W` `A` `S` `D` | drive |
| `Space` | fire |
| up / down on the title screen | choose 1 or 2 players |
| `Enter` | start |
| `P` | pause |
| `M` | sound on / off |
| `Esc` | quit |

## What is in it

- **Terrain:** brick chips away a slab at a time, steel stops ordinary shells,
  water blocks tanks but not shells, trees hide whatever drives under them.
- **Four enemy kinds:** basic (silver), fast (teal), power with quick shells
  (violet) and armoured, which takes four hits (green).
- **Pickups** dropped by the blinking red tanks: `S` shield, `+` extra tank,
  `F` freeze the enemies, `B` destroy every enemy on the field, `U` upgrade
  the gun (faster shells, then two at once, then steel-piercing).
- **Five maps**, cycling with a tougher enemy mix each stage.
- The window can be resized; the picture scales in whole steps to stay sharp.

The game, its maps and its graphics are original to this project. Everything
on screen is drawn from rectangles in code.

## Layout

| File | Role |
|---|---|
| `src/game.c`, `game.h` | rules and simulation; portable C11, no I/O |
| `src/render.c` | draws a frame into a 512x448 pixel buffer; portable C11 |
| `src/main.c` | Windows shell: window, keys, 60 Hz loop, synthesised sound |
| `tests/test.c` | tests, and a tool that writes sample frames |
| `release/tanks-arm64.exe`, `tanks-amd64.exe` | compiled release builds |

## Build and test

    make test       # native tests, any platform with a C compiler
    make shots      # also writes sample frames to build/shots
    make release    # cross-compiles both executables

The releases are cross-compiled from Linux with Zig's C compiler
(`pip install ziglang`).

The game is a pure function of its seed and the keys pressed, so the tests
drive it with scripted players, checking on every tick that no tank is ever
inside a wall or off the field, that the counts of spawned and destroyed
enemies add up, and that stages can be cleared one after another. They also
check shell-against-wall behaviour cell by cell. For two-player mode they
let the ally clear five stages by itself, and play 60 unattended games to
confirm that the ally never shoots the flag.
