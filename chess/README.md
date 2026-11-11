# Offline Chess

Play chess against the computer with no internet connection. The whole game is
one file, [`chess.html`](chess.html): download it, keep it on your laptop, and
open it in Chrome. It works on any CPU, ARM included, because it runs in the
browser.

## How to get it

1. Open [`chess.html`](chess.html) on GitHub and use the **Download raw file** button.
2. Double-click the downloaded file (or drag it into a Chrome window).

Nothing is installed and nothing is fetched from the network.

## Playing

- **New game** picks a strength from 1 (beginner) to 8 (strongest) and your side.
- Move by dragging a piece, or click the piece and then the square. Legal
  squares are marked.
- **Takeback** undoes your last move; **Resign** ends the game.
- Click any move in the list, or use the arrow keys, to step through the game.
  `F` flips the board.
- **Copy PGN** puts the game on the clipboard for later analysis.
- The game in progress is saved in the browser, so closing the tab does not lose it.

All the rules are implemented: castling, en passant, promotion with a choice of
piece, check, checkmate, stalemate, threefold repetition, the fifty-move rule
and insufficient material.

## The computer opponent

The opponent is **Stockfish 19** (the lite, single-threaded WebAssembly build
from [stockfish.js](https://github.com/nmrugg/stockfish.js)). It is embedded
inside `chess.html` itself, so it runs with no network; the file is about
2.5 MB because of it. Levels 1 to 7 use Stockfish's own Skill Level setting
with a limited search depth, and the two lowest also play an occasional random
move; level 8 is Stockfish at full strength, thinking two seconds a move.

Stockfish also drives the display:

- the **evaluation bar** left of the board shows who is ahead (light = White);
- **Computer's score** in the side panel is the same number from the
  computer's point of view, in pawns, or the moves to mate;
- **captured pieces** are lined up above and below the board, with the
  material lead.

- **move scores** in the move list give the computer's score after every
  move, so you can see which move changed the game.

`Eval bar: on/off` and `Move scores: on/off` hide these if you would rather
not see them while playing.

A small engine written for this project (`src/engine.js`) supplies the rules
and notation, and takes over as the opponent if Stockfish cannot start.

## Source

| File | Role |
|---|---|
| `src/engine.js` | rules, notation, search, evaluation, levels |
| `src/ui.js` | board, input, move list, saving |
| `src/style.css`, `src/index.html` | appearance and page skeleton |
| `src/pieces.svg` | piece artwork |
| `vendor/stockfish/` | the Stockfish build that gets embedded, with its licence |
| `build.js` | joins the sources into `chess.html` |
| `test/test.js` | engine tests |

    node chess/test/test.js   # run the tests
    node chess/build.js       # rebuild chess.html

The tests cover the project's own engine. They check move generation by perft against the published node counts for
six standard positions (over 16 million positions in total), plus hashing,
notation, every draw rule, tactical test positions and self-play at each level.

## Credits and licence

- **Stockfish** by the Stockfish developers, and **Stockfish.js** by Nathan
  Rugg and Chess.com, are licensed under the GNU GPL version 3
  (`vendor/stockfish/COPYING.txt`). Because `chess.html` contains Stockfish,
  that file as a whole is distributed under the GPL v3. Source:
  [official-stockfish/Stockfish](https://github.com/official-stockfish/Stockfish)
  and [nmrugg/stockfish.js](https://github.com/nmrugg/stockfish.js).
- The **chess pieces** are the standard Wikimedia Commons set by Colin M.L.
  Burnett (User:Cburnett) and User:Rfc1394, licensed
  [CC BY-SA 3.0](https://creativecommons.org/licenses/by-sa/3.0/), in the
  sprite form prepared by Stefan Haack for
  [cm-chessboard](https://github.com/shaack/cm-chessboard).
- The piece-square tables in `src/engine.js` are from Tomasz Michniewski's
  "Simplified Evaluation Function".
