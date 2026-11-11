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

The engine is written for this project in plain JavaScript and runs in a
background worker so the board never freezes. It is an iterative-deepening
principal variation search with a transposition table, null-move pruning, late
move reductions, killer and history ordering, check extensions and a
quiescence search, over a 0x88 board with Zobrist hashing. Evaluation is
material plus piece-square tables, passed pawns, the bishop pair and an
endgame mop-up term.

Levels 1 to 5 search 1 to 4 moves deep and add deliberate error; levels 6 to 8
play at full strength with more thinking time (up to about 4 seconds a move).
It is a capable club-level opponent, not Stockfish.

## Source

| File | Role |
|---|---|
| `src/engine.js` | rules, notation, search, evaluation, levels |
| `src/ui.js` | board, input, move list, saving |
| `src/style.css`, `src/index.html` | appearance and page skeleton |
| `src/pieces.svg` | piece artwork |
| `build.js` | joins the sources into `chess.html` |
| `test/test.js` | engine tests |

    node chess/test/test.js   # run the tests
    node chess/build.js       # rebuild chess.html

The tests check move generation by perft against the published node counts for
six standard positions (over 16 million positions in total), plus hashing,
notation, every draw rule, tactical test positions and self-play at each level.

## Credits

The chess pieces are the standard Wikimedia Commons set by Colin M.L. Burnett
(User:Cburnett) and User:Rfc1394, licensed
[CC BY-SA 3.0](https://creativecommons.org/licenses/by-sa/3.0/), in the sprite
form prepared by Stefan Haack for
[cm-chessboard](https://github.com/shaack/cm-chessboard). The piece-square
tables are from Tomasz Michniewski's "Simplified Evaluation Function".
