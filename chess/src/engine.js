/*
 * engine.js - chess rules and a computer opponent, in plain JavaScript.
 *
 * No dependencies. The same file runs in the page (rules, notation), in a
 * Web Worker (search) and under Node (tests).
 *
 * Board: 0x88 mailbox. Square = rank * 16 + file, a1 = 0x00, h8 = 0x77;
 * a square is on the board iff (sq & 0x88) === 0.
 * Piece: type (1 pawn .. 6 king) plus 8 for black; 0 is empty.
 * Move: one integer,
 *     from | to << 7 | promotion << 14 | flags << 17 | captured << 20
 */
(function (root) {
  'use strict';

  const WHITE = 0, BLACK = 8;
  const P = 1, N = 2, B = 3, R = 4, Q = 5, K = 6;
  const FLAG_EP = 1, FLAG_CASTLE = 2, FLAG_DOUBLE = 4;
  const START_FEN = 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1';

  const OFF_N = [-33, -31, -18, -14, 14, 18, 31, 33];
  const OFF_B = [-17, -15, 15, 17];
  const OFF_R = [-16, -1, 1, 16];
  const OFF_K = [-17, -16, -15, -1, 1, 15, 16, 17];
  const PIECE_CHARS = '.PNBRQK..pnbrqk';

  /* Castling rights that survive a move touching each square. */
  const CASTLE_MASK = new Int8Array(128).fill(15);
  CASTLE_MASK[0x00] = 13; CASTLE_MASK[0x07] = 14; CASTLE_MASK[0x04] = 12;
  CASTLE_MASK[0x70] = 7;  CASTLE_MASK[0x77] = 11; CASTLE_MASK[0x74] = 3;

  /* ------------------------------------------------------------ zobrist -- */

  let zseed = 0x2545F491;
  function rand32() {
    zseed ^= zseed << 13; zseed ^= zseed >>> 17; zseed ^= zseed << 5;
    return zseed | 0;
  }
  function randTable(n) {
    const t = new Int32Array(n);
    for (let i = 0; i < n; i++) t[i] = rand32();
    return t;
  }
  const Z_PIECE_LO = randTable(16 * 128), Z_PIECE_HI = randTable(16 * 128);
  const Z_CASTLE_LO = randTable(16), Z_CASTLE_HI = randTable(16);
  const Z_EP_LO = randTable(8), Z_EP_HI = randTable(8);
  const Z_SIDE_LO = rand32(), Z_SIDE_HI = rand32();

  /* ------------------------------------------------------------ helpers -- */

  const moveFrom = (m) => m & 127;
  const moveTo = (m) => (m >> 7) & 127;
  const movePromo = (m) => (m >> 14) & 7;
  const moveFlags = (m) => (m >> 17) & 7;
  const moveCaptured = (m) => (m >> 20) & 15;

  function sqName(sq) {
    return 'abcdefgh'[sq & 7] + ((sq >> 4) + 1);
  }
  function parseSq(s) {
    return (s.charCodeAt(1) - 49) * 16 + (s.charCodeAt(0) - 97);
  }
  function moveToUci(m) {
    const promo = movePromo(m);
    return sqName(moveFrom(m)) + sqName(moveTo(m)) + (promo ? 'pnbrqk'[promo - 1] : '');
  }

  const MAX_HISTORY = 4096;

  /* ----------------------------------------------------------- position -- */

  class Position {
    constructor(fen) {
      this.board = new Int8Array(128);
      this.kingSq = [0, 0];
      this.undoCastle = new Int8Array(MAX_HISTORY);
      this.undoEp = new Int16Array(MAX_HISTORY);
      this.undoHalf = new Int16Array(MAX_HISTORY);
      this.histLo = new Int32Array(MAX_HISTORY);   /* hash after each ply */
      this.histHi = new Int32Array(MAX_HISTORY);
      this.setFen(fen || START_FEN);
    }

    setFen(fen) {
      const parts = fen.trim().split(/\s+/);
      this.board.fill(0);
      let rank = 7, file = 0;
      for (const ch of parts[0]) {
        if (ch === '/') { rank--; file = 0; }
        else if (ch >= '1' && ch <= '8') file += +ch;
        else {
          const idx = PIECE_CHARS.indexOf(ch);
          if (idx < 1 || rank < 0 || file > 7) throw new Error('bad FEN: ' + fen);
          this.board[rank * 16 + file] = idx;
          if ((idx & 7) === K) this.kingSq[idx >> 3] = rank * 16 + file;
          file++;
        }
      }
      this.side = parts[1] === 'b' ? BLACK : WHITE;
      this.castle = 0;
      const c = parts[2] || '-';
      if (c.includes('K')) this.castle |= 1;
      if (c.includes('Q')) this.castle |= 2;
      if (c.includes('k')) this.castle |= 4;
      if (c.includes('q')) this.castle |= 8;
      this.ep = parts[3] && parts[3] !== '-' ? parseSq(parts[3]) : -1;
      this.half = parts[4] ? +parts[4] : 0;
      this.full = parts[5] ? +parts[5] : 1;
      this.ply = 0;
      this.computeHash();
      this.histLo[0] = this.hashLo;
      this.histHi[0] = this.hashHi;
    }

    computeHash() {
      let lo = 0, hi = 0;
      for (let sq = 0; sq < 120; sq++) {
        if (sq & 0x88) { sq += 7; continue; }
        const p = this.board[sq];
        if (p) { lo ^= Z_PIECE_LO[p * 128 + sq]; hi ^= Z_PIECE_HI[p * 128 + sq]; }
      }
      lo ^= Z_CASTLE_LO[this.castle]; hi ^= Z_CASTLE_HI[this.castle];
      if (this.ep >= 0) { lo ^= Z_EP_LO[this.ep & 7]; hi ^= Z_EP_HI[this.ep & 7]; }
      if (this.side === BLACK) { lo ^= Z_SIDE_LO; hi ^= Z_SIDE_HI; }
      this.hashLo = lo; this.hashHi = hi;
    }

    fen() {
      let s = '';
      for (let rank = 7; rank >= 0; rank--) {
        let empty = 0;
        for (let file = 0; file < 8; file++) {
          const p = this.board[rank * 16 + file];
          if (!p) { empty++; continue; }
          if (empty) { s += empty; empty = 0; }
          s += PIECE_CHARS[p];
        }
        if (empty) s += empty;
        if (rank) s += '/';
      }
      let c = '';
      if (this.castle & 1) c += 'K';
      if (this.castle & 2) c += 'Q';
      if (this.castle & 4) c += 'k';
      if (this.castle & 8) c += 'q';
      return s + (this.side === WHITE ? ' w ' : ' b ') + (c || '-') + ' ' +
        (this.ep >= 0 ? sqName(this.ep) : '-') + ' ' + this.half + ' ' + this.full;
    }

    isAttacked(sq, by) {
      const b = this.board;
      if (by === WHITE) {
        if (!((sq - 15) & 0x88) && b[sq - 15] === P) return true;
        if (!((sq - 17) & 0x88) && b[sq - 17] === P) return true;
      } else {
        if (!((sq + 15) & 0x88) && b[sq + 15] === (BLACK | P)) return true;
        if (!((sq + 17) & 0x88) && b[sq + 17] === (BLACK | P)) return true;
      }
      const knight = by | N, king = by | K, bishop = by | B, rook = by | R, queen = by | Q;
      for (let i = 0; i < 8; i++) {
        let t = sq + OFF_N[i];
        if (!(t & 0x88) && b[t] === knight) return true;
        t = sq + OFF_K[i];
        if (!(t & 0x88) && b[t] === king) return true;
      }
      for (let i = 0; i < 4; i++) {
        let d = OFF_B[i], t = sq + d;
        while (!(t & 0x88)) {
          const p = b[t];
          if (p) { if (p === bishop || p === queen) return true; break; }
          t += d;
        }
        d = OFF_R[i]; t = sq + d;
        while (!(t & 0x88)) {
          const p = b[t];
          if (p) { if (p === rook || p === queen) return true; break; }
          t += d;
        }
      }
      return false;
    }

    inCheck() {
      return this.isAttacked(this.kingSq[this.side >> 3], this.side ^ 8);
    }

    /* Pseudo-legal moves into `list`; returns how many. Legality (own king
       left in check) is decided by make(). */
    generate(list, capturesOnly) {
      const b = this.board, us = this.side, them = us ^ 8;
      let n = 0;
      for (let sq = 0; sq < 120; sq++) {
        if (sq & 0x88) { sq += 7; continue; }
        const p = b[sq];
        if (!p || (p & 8) !== us) continue;
        const type = p & 7;

        if (type === P) {
          const dir = us === WHITE ? 16 : -16;
          const rank = sq >> 4;
          const promoting = rank === (us === WHITE ? 6 : 1);
          let to = sq + dir;
          if (!b[to] && (!capturesOnly || promoting)) {
            if (promoting) {
              list[n++] = sq | to << 7 | Q << 14;
              if (!capturesOnly) {
                list[n++] = sq | to << 7 | N << 14;
                list[n++] = sq | to << 7 | R << 14;
                list[n++] = sq | to << 7 | B << 14;
              }
            } else {
              list[n++] = sq | to << 7;
              if (rank === (us === WHITE ? 1 : 6) && !b[to + dir])
                list[n++] = sq | (to + dir) << 7 | FLAG_DOUBLE << 17;
            }
          }
          for (let d = dir - 1; d <= dir + 1; d += 2) {
            to = sq + d;
            if (to & 0x88) continue;
            const target = b[to];
            if (target && (target & 8) === them) {
              const base = sq | to << 7 | target << 20;
              if (promoting) {
                list[n++] = base | Q << 14;
                if (!capturesOnly) {
                  list[n++] = base | N << 14;
                  list[n++] = base | R << 14;
                  list[n++] = base | B << 14;
                }
              } else list[n++] = base;
            } else if (to === this.ep) {
              list[n++] = sq | to << 7 | FLAG_EP << 17 | (them | P) << 20;
            }
          }
        } else if (type === N || type === K) {
          const offs = type === N ? OFF_N : OFF_K;
          for (let i = 0; i < 8; i++) {
            const to = sq + offs[i];
            if (to & 0x88) continue;
            const target = b[to];
            if (!target) { if (!capturesOnly) list[n++] = sq | to << 7; }
            else if ((target & 8) === them) list[n++] = sq | to << 7 | target << 20;
          }
        } else {
          for (let i = 0; i < 4; i++) {
            for (let pass = 0; pass < 2; pass++) {
              if (pass === 0 && type === R) continue;
              if (pass === 1 && type === B) continue;
              const d = pass === 0 ? OFF_B[i] : OFF_R[i];
              let to = sq + d;
              while (!(to & 0x88)) {
                const target = b[to];
                if (!target) { if (!capturesOnly) list[n++] = sq | to << 7; }
                else {
                  if ((target & 8) === them) list[n++] = sq | to << 7 | target << 20;
                  break;
                }
                to += d;
              }
            }
          }
        }
      }

      if (!capturesOnly) {
        const home = us === WHITE ? 0x04 : 0x74;
        const ks = us === WHITE ? 1 : 4, qs = us === WHITE ? 2 : 8;
        if ((this.castle & (ks | qs)) && this.kingSq[us >> 3] === home && !this.isAttacked(home, them)) {
          if ((this.castle & ks) && !b[home + 1] && !b[home + 2] &&
              !this.isAttacked(home + 1, them))
            list[n++] = home | (home + 2) << 7 | FLAG_CASTLE << 17;
          if ((this.castle & qs) && !b[home - 1] && !b[home - 2] && !b[home - 3] &&
              !this.isAttacked(home - 1, them))
            list[n++] = home | (home - 2) << 7 | FLAG_CASTLE << 17;
        }
      }
      return n;
    }

    /* Play a move. Returns false, leaving the position unchanged, if it
       would leave the mover's king in check. */
    make(m) {
      const from = m & 127, to = (m >> 7) & 127, promo = (m >> 14) & 7;
      const flags = (m >> 17) & 7, cap = (m >> 20) & 15;
      const b = this.board, us = this.side, them = us ^ 8;
      const piece = b[from];
      const u = this.ply;

      this.undoCastle[u] = this.castle;
      this.undoEp[u] = this.ep;
      this.undoHalf[u] = this.half;

      let lo = this.hashLo, hi = this.hashHi;
      if (this.ep >= 0) { lo ^= Z_EP_LO[this.ep & 7]; hi ^= Z_EP_HI[this.ep & 7]; }

      b[from] = 0;
      lo ^= Z_PIECE_LO[piece * 128 + from]; hi ^= Z_PIECE_HI[piece * 128 + from];
      if (flags & FLAG_EP) {
        const capSq = to + (us === WHITE ? -16 : 16);
        b[capSq] = 0;
        lo ^= Z_PIECE_LO[cap * 128 + capSq]; hi ^= Z_PIECE_HI[cap * 128 + capSq];
      } else if (cap) {
        lo ^= Z_PIECE_LO[cap * 128 + to]; hi ^= Z_PIECE_HI[cap * 128 + to];
      }
      const placed = promo ? (us | promo) : piece;
      b[to] = placed;
      lo ^= Z_PIECE_LO[placed * 128 + to]; hi ^= Z_PIECE_HI[placed * 128 + to];

      if (flags & FLAG_CASTLE) {
        const rookFrom = to > from ? to + 1 : to - 2;
        const rookTo = to > from ? to - 1 : to + 1;
        const rook = b[rookFrom];
        b[rookFrom] = 0; b[rookTo] = rook;
        lo ^= Z_PIECE_LO[rook * 128 + rookFrom] ^ Z_PIECE_LO[rook * 128 + rookTo];
        hi ^= Z_PIECE_HI[rook * 128 + rookFrom] ^ Z_PIECE_HI[rook * 128 + rookTo];
      }
      if ((piece & 7) === K) this.kingSq[us >> 3] = to;

      const castle = this.castle & CASTLE_MASK[from] & CASTLE_MASK[to];
      lo ^= Z_CASTLE_LO[this.castle] ^ Z_CASTLE_LO[castle];
      hi ^= Z_CASTLE_HI[this.castle] ^ Z_CASTLE_HI[castle];
      this.castle = castle;

      /* The en passant square only counts if a pawn can actually take there,
         so that repetition detection compares like with like. */
      this.ep = -1;
      if (flags & FLAG_DOUBLE) {
        const enemyPawn = them | P;
        if ((!((to - 1) & 0x88) && b[to - 1] === enemyPawn) ||
            (!((to + 1) & 0x88) && b[to + 1] === enemyPawn)) {
          this.ep = (from + to) >> 1;
          lo ^= Z_EP_LO[this.ep & 7]; hi ^= Z_EP_HI[this.ep & 7];
        }
      }

      this.half = ((piece & 7) === P || cap) ? 0 : this.half + 1;
      if (us === BLACK) this.full++;
      this.side = them;
      lo ^= Z_SIDE_LO; hi ^= Z_SIDE_HI;
      this.hashLo = lo; this.hashHi = hi;
      this.ply = u + 1;
      this.histLo[u + 1] = lo; this.histHi[u + 1] = hi;

      if (this.isAttacked(this.kingSq[us >> 3], them)) {
        this.unmake(m);
        return false;
      }
      return true;
    }

    unmake(m) {
      const from = m & 127, to = (m >> 7) & 127, promo = (m >> 14) & 7;
      const flags = (m >> 17) & 7, cap = (m >> 20) & 15;
      const b = this.board, us = this.side ^ 8;
      const u = --this.ply;

      this.side = us;
      this.castle = this.undoCastle[u];
      this.ep = this.undoEp[u];
      this.half = this.undoHalf[u];
      this.hashLo = this.histLo[u];
      this.hashHi = this.histHi[u];
      if (us === BLACK) this.full--;

      const piece = promo ? (us | P) : b[to];
      b[from] = piece;
      b[to] = 0;
      if (flags & FLAG_EP) b[to + (us === WHITE ? -16 : 16)] = cap;
      else if (cap) b[to] = cap;
      if (flags & FLAG_CASTLE) {
        const rookFrom = to > from ? to + 1 : to - 2;
        const rookTo = to > from ? to - 1 : to + 1;
        b[rookFrom] = b[rookTo];
        b[rookTo] = 0;
      }
      if ((piece & 7) === K) this.kingSq[us >> 3] = from;
    }

    makeNull() {
      const u = this.ply;
      this.undoCastle[u] = this.castle;
      this.undoEp[u] = this.ep;
      this.undoHalf[u] = this.half;
      let lo = this.hashLo ^ Z_SIDE_LO, hi = this.hashHi ^ Z_SIDE_HI;
      if (this.ep >= 0) { lo ^= Z_EP_LO[this.ep & 7]; hi ^= Z_EP_HI[this.ep & 7]; }
      this.ep = -1;
      this.half++;
      this.side ^= 8;
      this.hashLo = lo; this.hashHi = hi;
      this.ply = u + 1;
      this.histLo[u + 1] = lo; this.histHi[u + 1] = hi;
    }

    unmakeNull() {
      const u = --this.ply;
      this.side ^= 8;
      this.ep = this.undoEp[u];
      this.half = this.undoHalf[u];
      this.hashLo = this.histLo[u];
      this.hashHi = this.histHi[u];
    }

    legalMoves() {
      const list = new Int32Array(256), out = [];
      const n = this.generate(list, false);
      for (let i = 0; i < n; i++) {
        if (this.make(list[i])) { this.unmake(list[i]); out.push(list[i]); }
      }
      return out;
    }

    findMove(uci) {
      for (const m of this.legalMoves()) if (moveToUci(m) === uci) return m;
      return 0;
    }

    /* Standard algebraic notation for a legal move, e.g. "Nbd7", "exd8=Q#". */
    san(m) {
      const from = moveFrom(m), to = moveTo(m), promo = movePromo(m);
      const type = this.board[from] & 7;
      let s;
      if (moveFlags(m) & FLAG_CASTLE) {
        s = to > from ? 'O-O' : 'O-O-O';
      } else {
        s = '';
        if (type === P) {
          if (moveCaptured(m)) s = 'abcdefgh'[from & 7];
        } else {
          s = 'PNBRQK'[type - 1];
          let clash = false, sameFile = false, sameRank = false;
          for (const o of this.legalMoves()) {
            const of = moveFrom(o);
            if (of === from || moveTo(o) !== to || (this.board[of] & 7) !== type) continue;
            clash = true;
            if ((of & 7) === (from & 7)) sameFile = true;
            if ((of >> 4) === (from >> 4)) sameRank = true;
          }
          if (clash) {
            if (!sameFile) s += 'abcdefgh'[from & 7];
            else if (!sameRank) s += (from >> 4) + 1;
            else s += sqName(from);
          }
        }
        if (moveCaptured(m)) s += 'x';
        s += sqName(to);
        if (promo) s += '=' + 'PNBRQK'[promo - 1];
      }
      this.make(m);
      if (this.inCheck()) s += this.legalMoves().length ? '+' : '#';
      this.unmake(m);
      return s;
    }

    /* How many times the current position has occurred in the game. */
    repetitions() {
      let count = 1;
      for (let i = this.ply - 2; i >= 0 && i >= this.ply - this.half; i -= 2)
        if (this.histLo[i] === this.hashLo && this.histHi[i] === this.hashHi) count++;
      return count;
    }

    insufficientMaterial() {
      let minors = 0, bishopColours = 0, knights = 0;
      for (let sq = 0; sq < 120; sq++) {
        if (sq & 0x88) { sq += 7; continue; }
        const type = this.board[sq] & 7;
        if (!type || type === K) continue;
        if (type === P || type === R || type === Q) return false;
        minors++;
        if (type === N) knights++;
        else bishopColours |= ((sq >> 4) + (sq & 7)) & 1 ? 1 : 2;
      }
      if (minors <= 1) return true;
      return knights === 0 && bishopColours !== 3;   /* bishops all on one colour */
    }

    /* { over, reason, winner } - winner is WHITE, BLACK or null for a draw. */
    status() {
      if (!this.legalMoves().length) {
        return this.inCheck()
          ? { over: true, reason: 'checkmate', winner: this.side ^ 8 }
          : { over: true, reason: 'stalemate', winner: null };
      }
      if (this.insufficientMaterial()) return { over: true, reason: 'insufficient material', winner: null };
      if (this.half >= 100) return { over: true, reason: 'fifty-move rule', winner: null };
      if (this.repetitions() >= 3) return { over: true, reason: 'threefold repetition', winner: null };
      return { over: false, reason: '', winner: null };
    }

    /* Count leaf nodes of the legal move tree; the standard movegen test. */
    perft(depth) {
      if (depth === 0) return 1;
      const list = new Int32Array(256);
      const n = this.generate(list, false);
      let nodes = 0;
      for (let i = 0; i < n; i++) {
        if (!this.make(list[i])) continue;
        nodes += depth === 1 ? 1 : this.perft(depth - 1);
        this.unmake(list[i]);
      }
      return nodes;
    }
  }

  /* --------------------------------------------------------- evaluation -- */

  /* Piece-square tables from Tomasz Michniewski's "Simplified Evaluation
     Function", written from White's side with rank 8 on the first row. */
  const PST = {
    [P]: [
        0,   0,   0,   0,   0,   0,   0,   0,
       50,  50,  50,  50,  50,  50,  50,  50,
       10,  10,  20,  30,  30,  20,  10,  10,
        5,   5,  10,  25,  25,  10,   5,   5,
        0,   0,   0,  20,  20,   0,   0,   0,
        5,  -5, -10,   0,   0, -10,  -5,   5,
        5,  10,  10, -20, -20,  10,  10,   5,
        0,   0,   0,   0,   0,   0,   0,   0],
    [N]: [
      -50, -40, -30, -30, -30, -30, -40, -50,
      -40, -20,   0,   0,   0,   0, -20, -40,
      -30,   0,  10,  15,  15,  10,   0, -30,
      -30,   5,  15,  20,  20,  15,   5, -30,
      -30,   0,  15,  20,  20,  15,   0, -30,
      -30,   5,  10,  15,  15,  10,   5, -30,
      -40, -20,   0,   5,   5,   0, -20, -40,
      -50, -40, -30, -30, -30, -30, -40, -50],
    [B]: [
      -20, -10, -10, -10, -10, -10, -10, -20,
      -10,   0,   0,   0,   0,   0,   0, -10,
      -10,   0,   5,  10,  10,   5,   0, -10,
      -10,   5,   5,  10,  10,   5,   5, -10,
      -10,   0,  10,  10,  10,  10,   0, -10,
      -10,  10,  10,  10,  10,  10,  10, -10,
      -10,   5,   0,   0,   0,   0,   5, -10,
      -20, -10, -10, -10, -10, -10, -10, -20],
    [R]: [
        0,   0,   0,   0,   0,   0,   0,   0,
        5,  10,  10,  10,  10,  10,  10,   5,
       -5,   0,   0,   0,   0,   0,   0,  -5,
       -5,   0,   0,   0,   0,   0,   0,  -5,
       -5,   0,   0,   0,   0,   0,   0,  -5,
       -5,   0,   0,   0,   0,   0,   0,  -5,
       -5,   0,   0,   0,   0,   0,   0,  -5,
        0,   0,   0,   5,   5,   0,   0,   0],
    [Q]: [
      -20, -10, -10,  -5,  -5, -10, -10, -20,
      -10,   0,   0,   0,   0,   0,   0, -10,
      -10,   0,   5,   5,   5,   5,   0, -10,
       -5,   0,   5,   5,   5,   5,   0,  -5,
        0,   0,   5,   5,   5,   5,   0,  -5,
      -10,   5,   5,   5,   5,   5,   0, -10,
      -10,   0,   5,   0,   0,   0,   0, -10,
      -20, -10, -10,  -5,  -5, -10, -10, -20],
    [K]: [
      -30, -40, -40, -50, -50, -40, -40, -30,
      -30, -40, -40, -50, -50, -40, -40, -30,
      -30, -40, -40, -50, -50, -40, -40, -30,
      -30, -40, -40, -50, -50, -40, -40, -30,
      -20, -30, -30, -40, -40, -30, -30, -20,
      -10, -20, -20, -20, -20, -20, -20, -10,
       20,  20,   0,   0,   0,   0,  20,  20,
       20,  30,  10,   0,   0,  10,  30,  20],
  };
  const KING_ENDGAME = [
      -50, -40, -30, -20, -20, -30, -40, -50,
      -30, -20, -10,   0,   0, -10, -20, -30,
      -30, -10,  20,  30,  30,  20, -10, -30,
      -30, -10,  30,  40,  40,  30, -10, -30,
      -30, -10,  30,  40,  40,  30, -10, -30,
      -30, -10,  20,  30,  30,  20, -10, -30,
      -30, -30,   0,   0,   0,   0, -30, -30,
      -50, -30, -30, -30, -30, -30, -30, -50];
  const VALUE = [0, 100, 320, 330, 500, 900, 0];
  const PHASE = [0, 0, 1, 1, 2, 4, 0];
  const PASSED_BONUS = [0, 5, 10, 20, 35, 60, 100, 0];

  /* Middlegame and endgame value of each piece on each square, material
     included, signed from White's point of view. */
  const MG = new Int16Array(16 * 128), EG = new Int16Array(16 * 128);
  for (let type = P; type <= K; type++) {
    for (let rank = 0; rank < 8; rank++) {
      for (let file = 0; file < 8; file++) {
        const sq = rank * 16 + file;
        const w = (7 - rank) * 8 + file, bl = rank * 8 + file;
        const eg = type === K ? KING_ENDGAME : PST[type];
        MG[type * 128 + sq] = VALUE[type] + PST[type][w];
        EG[type * 128 + sq] = VALUE[type] + eg[w];
        MG[(type | 8) * 128 + sq] = -(VALUE[type] + PST[type][bl]);
        EG[(type | 8) * 128 + sq] = -(VALUE[type] + eg[bl]);
      }
    }
  }

  /* Static evaluation in centipawns, from the side to move's point of view. */
  function evaluate(pos) {
    const b = pos.board;
    let mg = 0, eg = 0, phase = 0, wBishops = 0, bBishops = 0, extra = 0;

    for (let sq = 0; sq < 120; sq++) {
      if (sq & 0x88) { sq += 7; continue; }
      const p = b[sq];
      if (!p) continue;
      const type = p & 7;
      mg += MG[p * 128 + sq];
      eg += EG[p * 128 + sq];
      phase += PHASE[type];
      if (p === B) wBishops++;
      else if (p === (BLACK | B)) bBishops++;
      else if (type === P) {
        /* Passed pawn: no enemy pawn ahead on this or a neighbouring file. */
        const white = p === P, dir = white ? 16 : -16, enemy = white ? (BLACK | P) : P;
        let passed = true;
        for (let t = sq + dir; !(t & 0x88) && passed; t += dir) {
          if (b[t] === enemy || (!((t - 1) & 0x88) && b[t - 1] === enemy) ||
              (!((t + 1) & 0x88) && b[t + 1] === enemy)) passed = false;
        }
        if (passed) extra += white ? PASSED_BONUS[sq >> 4] : -PASSED_BONUS[7 - (sq >> 4)];
      }
    }
    if (wBishops >= 2) extra += 30;
    if (bBishops >= 2) extra -= 30;

    if (phase > 24) phase = 24;
    let score = ((mg * phase + eg * (24 - phase)) / 24 | 0) + extra;

    /* Mop-up: when far ahead in an endgame, drive the losing king to the
       edge and bring the winning king close, so basic mates get delivered. */
    if (phase <= 8 && (score >= 300 || score <= -300)) {
      const loser = pos.kingSq[score > 0 ? 1 : 0], winner = pos.kingSq[score > 0 ? 0 : 1];
      const lf = loser & 7, lr = loser >> 4;
      const edge = Math.max(3 - lf, lf - 4) + Math.max(3 - lr, lr - 4);
      const dist = Math.abs(lf - (winner & 7)) + Math.abs(lr - (winner >> 4));
      const bonus = edge * 10 + (14 - dist) * 4;
      score += score > 0 ? bonus : -bonus;
    }
    return pos.side === WHITE ? score : -score;
  }

  /* -------------------------------------------------------------- search -- */

  const INF = 32000, MATE = 30000, MAX_PLY = 64;
  const TT_BITS = 20, TT_SIZE = 1 << TT_BITS, TT_MASK = TT_SIZE - 1;
  const TT_EXACT = 1, TT_LOWER = 2, TT_UPPER = 3;

  /*
   * Iterative-deepening principal variation search with a transposition
   * table, null-move pruning, late move reductions, killer and history move
   * ordering, check extensions and a capture-only quiescence search.
   */
  class Searcher {
    constructor() {
      this.ttKey = new Int32Array(TT_SIZE);
      this.ttMove = new Int32Array(TT_SIZE);
      this.ttScore = new Int16Array(TT_SIZE);
      this.ttDepth = new Int8Array(TT_SIZE);
      this.ttFlag = new Int8Array(TT_SIZE);
      this.history = new Int32Array(16 * 128);
      this.killers = new Int32Array(MAX_PLY * 2);
      this.lists = [];
      this.scores = [];
      for (let i = 0; i <= MAX_PLY; i++) {
        this.lists.push(new Int32Array(256));
        this.scores.push(new Int32Array(256));
      }
      this.nodes = 0;
      this.stop = false;
      this.deadline = 0;
    }

    now() {
      return typeof performance !== 'undefined' ? performance.now() : Date.now();
    }

    scoreMoves(pos, list, scores, n, ttMove, ply) {
      const k1 = this.killers[ply * 2], k2 = this.killers[ply * 2 + 1];
      for (let i = 0; i < n; i++) {
        const m = list[i];
        const cap = (m >> 20) & 15;
        if (m === ttMove) scores[i] = 10000000;
        else if (cap) scores[i] = 1000000 + VALUE[cap & 7] * 16 - (pos.board[m & 127] & 7);
        else if ((m >> 14) & 7) scores[i] = 900000 + ((m >> 14) & 7);
        else if (m === k1) scores[i] = 800000;
        else if (m === k2) scores[i] = 799999;
        else scores[i] = this.history[pos.board[m & 127] * 128 + ((m >> 7) & 127)];
      }
    }

    /* Selection sort step: bring the best remaining move to position i. */
    pick(list, scores, i, n) {
      let best = i;
      for (let j = i + 1; j < n; j++) if (scores[j] > scores[best]) best = j;
      if (best !== i) {
        const m = list[i]; list[i] = list[best]; list[best] = m;
        const s = scores[i]; scores[i] = scores[best]; scores[best] = s;
      }
      return list[i];
    }

    quiesce(pos, alpha, beta, ply) {
      this.nodes++;
      const stand = evaluate(pos);
      if (ply >= MAX_PLY) return stand;
      if (stand >= beta) return stand;
      if (stand > alpha) alpha = stand;

      const list = this.lists[ply], scores = this.scores[ply];
      const n = pos.generate(list, true);
      this.scoreMoves(pos, list, scores, n, 0, ply);
      for (let i = 0; i < n; i++) {
        const m = this.pick(list, scores, i, n);
        /* Delta pruning: skip captures that cannot lift the score to alpha. */
        if (stand + VALUE[(m >> 20) & 7] + 200 < alpha && !((m >> 14) & 7)) continue;
        if (!pos.make(m)) continue;
        const score = -this.quiesce(pos, -beta, -alpha, ply + 1);
        pos.unmake(m);
        if (score > alpha) {
          alpha = score;
          if (score >= beta) break;
        }
      }
      return alpha;
    }

    negamax(pos, depth, alpha, beta, ply, allowNull) {
      if ((++this.nodes & 2047) === 0 && this.now() > this.deadline) this.stop = true;
      if (this.stop) return 0;

      if (ply > 0 && (pos.half >= 100 || pos.repetitions() > 1 || pos.insufficientMaterial())) return 0;
      if (ply >= MAX_PLY) return evaluate(pos);

      const inCheck = pos.inCheck();
      if (inCheck) depth++;
      if (depth <= 0) return this.quiesce(pos, alpha, beta, ply);

      /* Transposition table. Mate scores are stored relative to this node. */
      const slot = pos.hashLo & TT_MASK;
      let ttMove = 0;
      if (this.ttKey[slot] === pos.hashHi && this.ttFlag[slot]) {
        ttMove = this.ttMove[slot];
        if (ply > 0 && this.ttDepth[slot] >= depth) {
          let s = this.ttScore[slot];
          if (s > MATE - 1000) s -= ply; else if (s < -MATE + 1000) s += ply;
          const f = this.ttFlag[slot];
          if (f === TT_EXACT || (f === TT_LOWER && s >= beta) || (f === TT_UPPER && s <= alpha)) return s;
        }
      }

      const pvNode = beta - alpha > 1;

      /* Null move: if passing still beats beta, a real move surely does. */
      if (allowNull && !inCheck && !pvNode && depth >= 3 && this.hasPieces(pos) && evaluate(pos) >= beta) {
        pos.makeNull();
        const s = -this.negamax(pos, depth - 3, -beta, -beta + 1, ply + 1, false);
        pos.unmakeNull();
        if (this.stop) return 0;
        if (s >= beta) return beta;
      }

      const list = this.lists[ply], scores = this.scores[ply];
      const n = pos.generate(list, false);
      this.scoreMoves(pos, list, scores, n, ttMove, ply);

      const alpha0 = alpha;
      let best = -INF, bestMove = 0, legal = 0;
      for (let i = 0; i < n; i++) {
        const m = this.pick(list, scores, i, n);
        const mover = pos.board[m & 127];
        if (!pos.make(m)) continue;
        legal++;

        const quiet = !((m >> 20) & 15) && !((m >> 14) & 7);
        let score;
        if (legal === 1) {
          score = -this.negamax(pos, depth - 1, -beta, -alpha, ply + 1, true);
        } else {
          /* Late move reduction: moves ordered late are searched shallower
             first, then again at full depth only if they look good. */
          let reduce = 0;
          if (quiet && depth >= 3 && legal > 4 && !inCheck) reduce = legal > 12 ? 2 : 1;
          score = -this.negamax(pos, depth - 1 - reduce, -alpha - 1, -alpha, ply + 1, true);
          if (score > alpha && reduce)
            score = -this.negamax(pos, depth - 1, -alpha - 1, -alpha, ply + 1, true);
          if (score > alpha && score < beta)
            score = -this.negamax(pos, depth - 1, -beta, -alpha, ply + 1, true);
        }
        pos.unmake(m);
        if (this.stop) return 0;

        if (score > best) {
          best = score;
          bestMove = m;
          if (score > alpha) {
            alpha = score;
            if (score >= beta) {
              if (quiet) {
                if (this.killers[ply * 2] !== m) {
                  this.killers[ply * 2 + 1] = this.killers[ply * 2];
                  this.killers[ply * 2] = m;
                }
                const h = mover * 128 + ((m >> 7) & 127);
                this.history[h] += depth * depth;
                if (this.history[h] > 700000) for (let j = 0; j < this.history.length; j++) this.history[j] >>= 1;
              }
              break;
            }
          }
        }
      }

      if (!legal) return inCheck ? -MATE + ply : 0;

      let stored = best;
      if (stored > MATE - 1000) stored += ply; else if (stored < -MATE + 1000) stored -= ply;
      this.ttKey[slot] = pos.hashHi;
      this.ttMove[slot] = bestMove;
      this.ttScore[slot] = stored;
      this.ttDepth[slot] = depth;
      this.ttFlag[slot] = best >= beta ? TT_LOWER : best > alpha0 ? TT_EXACT : TT_UPPER;
      return best;
    }

    /* Null move is unsound in pawn endings (zugzwang), so require a piece. */
    hasPieces(pos) {
      const b = pos.board, us = pos.side;
      for (let sq = 0; sq < 120; sq++) {
        if (sq & 0x88) { sq += 7; continue; }
        const p = b[sq];
        if (p && (p & 8) === us && (p & 7) !== P && (p & 7) !== K) return true;
      }
      return false;
    }

    /*
     * Choose a move. options:
     *   depth   maximum search depth (default 64)
     *   timeMs  thinking time (default 1000)
     *   noise   centipawns of random error added to each root move's score;
     *           above zero every root move gets an exact score, so weaker
     *           levels make plausible mistakes rather than random moves
     *   random  function returning [0, 1) (default Math.random)
     * Returns { move, score, depth, nodes } or { move: 0 } if there is none.
     */
    search(pos, options) {
      const opt = options || {};
      const maxDepth = Math.min(opt.depth || MAX_PLY - 1, MAX_PLY - 1);
      const noise = opt.noise || 0;
      const random = opt.random || Math.random;
      const rootMoves = pos.legalMoves();
      if (!rootMoves.length) return { move: 0, score: 0, depth: 0, nodes: 0 };

      this.nodes = 0;
      this.stop = false;
      this.deadline = this.now() + (opt.timeMs || 1000);
      this.killers.fill(0);
      this.history.fill(0);

      let bestMove = rootMoves[0], bestScore = 0, reached = 0;
      for (let depth = 1; depth <= maxDepth; depth++) {
        let alpha = -INF, iterBest = 0, iterScore = -INF, iterPick = -INF;
        for (let i = 0; i < rootMoves.length; i++) {
          const m = rootMoves[i];
          pos.make(m);
          let score;
          if (noise > 0 || i === 0) {
            score = -this.negamax(pos, depth - 1, -INF, noise > 0 ? INF : -alpha, 1, true);
          } else {
            score = -this.negamax(pos, depth - 1, -alpha - 1, -alpha, 1, true);
            if (score > alpha) score = -this.negamax(pos, depth - 1, -INF, -alpha, 1, true);
          }
          pos.unmake(m);
          if (this.stop) break;
          const pickScore = noise > 0 ? score + (random() * 2 - 1) * noise : score;
          if (pickScore > iterPick) { iterPick = pickScore; iterBest = m; iterScore = score; }
          if (noise === 0 && score > alpha) alpha = score;
        }
        if (this.stop && depth > 1) break;
        if (iterBest) {
          bestMove = iterBest; bestScore = iterScore; reached = depth;
          /* Search the best move first next time round. */
          const at = rootMoves.indexOf(iterBest);
          rootMoves.splice(at, 1);
          rootMoves.unshift(iterBest);
        }
        if (this.stop) break;
        if (noise === 0 && Math.abs(bestScore) > MATE - 1000) break;   /* forced mate found */
        if (rootMoves.length === 1 && depth >= 2) break;               /* only one legal move */
      }
      return { move: bestMove, score: bestScore, depth: reached, nodes: this.nodes };
    }
  }

  /* -------------------------------------------------------------- levels -- */

  /* Eight strength levels. `blunder` is the chance of a random legal move. */
  const LEVELS = [
    null,
    { depth: 1,  timeMs: 500,  noise: 400, blunder: 0.35, book: false },
    { depth: 1,  timeMs: 500,  noise: 150, blunder: 0.20, book: false },
    { depth: 2,  timeMs: 500,  noise: 100, blunder: 0.10, book: true },
    { depth: 3,  timeMs: 800,  noise: 60,  blunder: 0.03, book: true },
    { depth: 4,  timeMs: 1000, noise: 30,  blunder: 0,    book: true },
    { depth: 6,  timeMs: 1000, noise: 0,   blunder: 0,    book: true },
    { depth: 63, timeMs: 1500, noise: 0,   blunder: 0,    book: true },
    { depth: 63, timeMs: 4000, noise: 0,   blunder: 0,    book: true },
  ];

  /* A few mainline openings so games do not all start the same way. Keys
     are the moves played so far; values are candidate replies. */
  const BOOK = {
    '': ['e2e4', 'd2d4', 'g1f3', 'c2c4'],
    'e2e4': ['e7e5', 'c7c5', 'e7e6', 'c7c6'],
    'd2d4': ['d7d5', 'g8f6'],
    'g1f3': ['d7d5', 'g8f6'],
    'c2c4': ['e7e5', 'g8f6', 'c7c5'],
    'e2e4 e7e5': ['g1f3'],
    'e2e4 e7e5 g1f3': ['b8c6'],
    'e2e4 e7e5 g1f3 b8c6': ['f1b5', 'f1c4', 'd2d4'],
    'e2e4 c7c5': ['g1f3', 'b1c3'],
    'e2e4 e7e6': ['d2d4'],
    'e2e4 e7e6 d2d4': ['d7d5'],
    'e2e4 c7c6': ['d2d4'],
    'e2e4 c7c6 d2d4': ['d7d5'],
    'd2d4 d7d5': ['c2c4', 'g1f3'],
    'd2d4 g8f6': ['c2c4', 'g1f3'],
    'd2d4 d7d5 c2c4': ['e7e6', 'c7c6'],
    'd2d4 g8f6 c2c4': ['e7e6', 'g7g6'],
  };

  /* A book reply for the game so far, or null once the game leaves the book. */
  function bookMove(moves, random) {
    const replies = BOOK[moves.join(' ')];
    if (!replies) return null;
    return replies[Math.floor((random || Math.random)() * replies.length)];
  }

  let sharedSearcher = null;

  /*
   * Pick the computer's move for a game given as a list of UCI moves from
   * the starting position. Returns { uci, score, depth, nodes }.
   */
  function chooseMove(moves, level, random) {
    const rnd = random || Math.random;
    const cfg = LEVELS[Math.max(1, Math.min(8, level | 0))];
    const pos = new Position();
    for (const uci of moves) {
      const m = pos.findMove(uci);
      if (!m) throw new Error('illegal move in game: ' + uci);
      pos.make(m);
    }
    const legal = pos.legalMoves();
    if (!legal.length) return { uci: null, score: 0, depth: 0, nodes: 0 };

    if (cfg.book) {
      const uci = bookMove(moves, rnd);
      if (uci && pos.findMove(uci)) return { uci, score: 0, depth: 0, nodes: 0, book: true };
    }
    if (cfg.blunder && rnd() < cfg.blunder) {
      const m = legal[Math.floor(rnd() * legal.length)];
      return { uci: moveToUci(m), score: 0, depth: 0, nodes: 0 };
    }
    if (!sharedSearcher) sharedSearcher = new Searcher();
    const r = sharedSearcher.search(pos, { depth: cfg.depth, timeMs: cfg.timeMs, noise: cfg.noise, random: rnd });
    return { uci: moveToUci(r.move), score: r.score, depth: r.depth, nodes: r.nodes };
  }

  const api = {
    WHITE, BLACK, P, N, B, R, Q, K, START_FEN, MATE, LEVELS,
    Position, Searcher, evaluate, chooseMove, bookMove,
    moveFrom, moveTo, movePromo, moveFlags, moveCaptured, moveToUci, sqName, parseSq,
  };
  root.Chess = api;
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
})(typeof self !== 'undefined' ? self : globalThis);
