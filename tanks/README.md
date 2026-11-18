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

The title screen offers three modes:

- **1 player**
- **2 players - PC ally:** a computer-driven partner (the green tank) hunts the
  nearest enemy, shoots its way through brick and never fires towards your flag.
- **2 players - 2 gamepads:** two people, one gamepad each. The game goes on
  until both players are out of tanks.

Each player has five tanks. Shells from your own side pass through each
other's tanks.

## Controls

Keyboard (always player 1):

| Key | Action |
|---|---|
| arrows or `W` `A` `S` `D` | turn and drive |
| `Space` | fire |
| up / down, `Enter` | choose the mode on the title screen, start |
| `P` | pause |
| `M` | sound on / off |
| `Esc` | quit |

Gamepad (Xbox layout through XInput, for example the Thunderobot G50):

| Control | Action |
|---|---|
| d-pad | turn the tank that way and fire |
| right stick, any direction | drive forward along the tank's heading |
| `LT`, `RT` | fire |
| `X` | take control of the menu |
| `A` or Start | start; Start also pauses |

Every fire control works the same way: one press is one shot, and holding it
fires four times a second.

With two gamepads connected, the one that leads the menu is player 1 and the
other is player 2. The first gamepad leads to begin with; whichever gamepad
pressed `X` last takes over. With only one gamepad in two-gamepad mode, the
keyboard is player 1 and the gamepad is player 2. If the game does not see
your gamepad, switch it to its XInput (Xbox) mode.

## What is in it

- **Terrain:** brick chips away a slab at a time, steel stops ordinary shells,
  water blocks tanks but not shells, trees hide whatever drives under them.
- **Four enemy kinds:** basic (silver), fast (teal), power with quick shells
  (violet) and armoured, which takes four hits (green).
- **Pickups** dropped by the blinking red tanks: `S` shield, `+` extra tank,
  `F` freeze the enemies, `B` destroy every enemy on the field, `U` upgrade
  the gun (faster shells, then double damage, then steel-piercing).
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
confirm that the ally never shoots the flag. The gamepad rules are tested in
the game core: one shot per press, exactly four shots a second when held,
turning on the spot, driving along the heading, and two-human play.

Reading the gamepad itself (XInput in `main.c`) cannot be exercised by these
tests or by the automated Windows launch, which has no controller attached.
