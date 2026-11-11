/*
 * test.js - engine tests. Run with:  node chess/test/test.js
 *
 * Move generation is checked with perft: counting every legal line to a
 * fixed depth and comparing with the published totals for positions chosen
 * to exercise castling, en passant, promotion and pins.
 */
'use strict';
const C = require('../src/engine.js');

let failures = 0, checks = 0;
function check(cond, what) {
  checks++;
  if (!cond) { failures++; console.log('  FAIL ' + what); }
}

function playUci(pos, line) {
  for (const uci of line.split(/\s+/).filter(Boolean)) {
    const m = pos.findMove(uci);
    if (!m) throw new Error('illegal ' + uci + ' in ' + pos.fen());
    pos.make(m);
  }
}

console.log('perft');
const PERFT = [
  ['start position', C.START_FEN, [20, 400, 8902, 197281, 4865609]],
  ['kiwipete', 'r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1', [48, 2039, 97862, 4085603]],
  ['endgame', '8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1', [14, 191, 2812, 43238, 674624]],
  ['promotions', 'r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1', [6, 264, 9467, 422333]],
  ['position 5', 'rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8', [44, 1486, 62379, 2103487]],
  ['position 6', 'r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10', [46, 2079, 89890, 3894594]],
];
for (const [name, fen, expected] of PERFT) {
  const pos = new C.Position(fen);
  const before = pos.fen(), hash = [pos.hashLo, pos.hashHi];
  expected.forEach((want, i) => {
    const got = pos.perft(i + 1);
    check(got === want, `${name} depth ${i + 1}: got ${got}, want ${want}`);
  });
  check(pos.fen() === before, name + ': position restored after perft');
  check(pos.hashLo === hash[0] && pos.hashHi === hash[1], name + ': hash restored');
  console.log(`  ${name}: ${expected[expected.length - 1]} nodes at depth ${expected.length}`);
}

console.log('hashing');
{
  /* The incremental hash must equal a hash computed from scratch. */
  const pos = new C.Position();
  let seed = 12345;
  const rnd = () => (seed = (seed * 1103515245 + 12345) & 0x7fffffff) / 0x80000000;
  for (let i = 0; i < 400; i++) {
    const legal = pos.legalMoves();
    if (!legal.length || pos.status().over) pos.setFen(C.START_FEN);
    else {
      pos.make(legal[Math.floor(rnd() * legal.length)]);
      const lo = pos.hashLo, hi = pos.hashHi;
      pos.computeHash();
      check(pos.hashLo === lo && pos.hashHi === hi, 'incremental hash at ply ' + i);
      const fen = pos.fen();
      check(new C.Position(fen).fen() === fen, 'FEN round trip ' + fen);
    }
  }
}

console.log('notation');
{
  const pos = new C.Position();
  const game = 'e2e4 e7e5 g1f3 b8c6 f1b5 a7a6 b5c6 d7c6 e1g1 f7f6 d2d4 e5d4 f3d4 c6c5 d4b3 d8d1 f1d1';
  const want = 'e4 e5 Nf3 Nc6 Bb5 a6 Bxc6 dxc6 O-O f6 d4 exd4 Nxd4 c5 Nb3 Qxd1 Rxd1'.split(' ');
  game.split(' ').forEach((uci, i) => {
    const m = pos.findMove(uci);
    check(pos.san(m) === want[i], `SAN ${uci}: got ${pos.san(m)}, want ${want[i]}`);
    pos.make(m);
  });

  const cases = [
    ['4k3/8/8/8/8/8/8/R3K2R w KQ - 0 1', 'a1a8', 'Ra8+'],
    ['4k3/8/8/8/8/8/8/R3K2R w KQ - 0 1', 'e1c1', 'O-O-O'],
    ['6k1/8/8/8/8/8/6K1/R6R w - - 0 1', 'a1d1', 'Rad1'],
    ['7k/8/8/R7/8/8/8/R3K3 w - - 0 1', 'a1a3', 'R1a3'],
    ['7k/P7/8/8/8/8/8/K7 w - - 0 1', 'a7a8q', 'a8=Q+'],
    ['6k1/5ppp/8/8/8/8/8/R3K3 w - - 0 1', 'a1a8', 'Ra8#'],
    ['k7/8/8/3pP3/8/8/8/K7 w - d6 0 1', 'e5d6', 'exd6'],
    ['8/k7/8/8/4Q2Q/8/K7/7Q w - - 0 1', 'h4e1', 'Qh4e1'],
  ];
  for (const [fen, uci, san] of cases) {
    const p = new C.Position(fen);
    const m = p.findMove(uci);
    check(m && p.san(m) === san, `SAN ${uci} in ${fen}: got ${m ? p.san(m) : 'illegal'}, want ${san}`);
  }
}

console.log('game end');
{
  const st = (fen) => new C.Position(fen).status();
  check(st('7k/5Q2/6K1/8/8/8/8/8 b - - 0 1').reason === 'stalemate', 'stalemate');
  check(st('6rk/5Npp/8/8/8/8/8/K7 b - - 0 1').reason === 'checkmate', 'smothered mate');
  check(st('6rk/5Npp/8/8/8/8/8/K7 b - - 0 1').winner === C.WHITE, 'mate winner');
  check(st('8/8/4k3/8/8/3K4/8/8 w - - 0 1').reason === 'insufficient material', 'K v K');
  check(st('8/8/4k3/8/8/3KN3/8/8 w - - 0 1').reason === 'insufficient material', 'K+N v K');
  check(st('8/8/4k3/8/8/3KB3/8/8 w - - 0 1').reason === 'insufficient material', 'K+B v K');
  check(st('8/8/4k3/5b2/8/3KB3/8/8 w - - 0 1').over === false, 'opposite bishops can still mate');
  check(st('8/8/4k3/8/8/3KR3/8/8 w - - 0 1').over === false, 'K+R v K is playable');
  check(st('8/8/4k3/8/8/3KP3/8/8 w - - 99 1').over === false, 'half-move clock 99');
  check(st('8/8/4k3/8/8/3KR3/8/8 w - - 100 80').reason === 'fifty-move rule', 'fifty-move rule');

  const pos = new C.Position();
  playUci(pos, 'g1f3 g8f6 f3g1 f6g8 g1f3 g8f6 f3g1');
  check(pos.status().over === false, 'twice is not yet a draw');
  playUci(pos, 'f6g8');
  check(pos.status().reason === 'threefold repetition', 'threefold repetition');

  /* A double push with no pawn able to capture leaves no en passant square. */
  const ep = new C.Position();
  playUci(ep, 'e2e4');
  check(ep.fen().split(' ')[3] === '-', 'no en passant square without a capturer');
  playUci(ep, 'a7a6 e4e5 d7d5');
  check(ep.fen().split(' ')[3] === 'd6', 'en passant square when capturable');
}

console.log('search');
{
  const s = new C.Searcher();
  const best = (fen, opts) => {
    const pos = new C.Position(fen);
    const r = s.search(pos, opts || { depth: 6, timeMs: 5000 });
    return { uci: C.moveToUci(r.move), score: r.score, depth: r.depth, nodes: r.nodes, same: pos.fen() === fen };
  };
  let r = best('6k1/5ppp/8/8/8/8/8/R3K3 w - - 0 1');
  check(r.uci === 'a1a8' && r.score > C.MATE - 10, 'mate in 1: ' + r.uci);
  check(r.same, 'search leaves the position unchanged');
  r = best('r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 4 4');
  check(r.uci === 'h5f7', "scholar's mate: " + r.uci);
  r = best('2rr3k/pp3pp1/1nnqbN1p/3pN3/2pP4/2P3Q1/PPB4P/R4RK1 w - - 0 1');
  check(r.uci === 'g3g6', 'mate in 3 (Qg6): ' + r.uci);
  r = best('4k3/8/8/8/3q4/8/4N3/4K3 w - - 0 1');
  check(r.uci === 'e2d4', 'wins a hanging queen: ' + r.uci);
  r = best('8/8/8/8/8/5k2/8/4K2r b - - 0 1', { depth: 8, timeMs: 5000 });
  check(r.score > 400, 'K+R v K is evaluated as winning');

  const t0 = Date.now();
  const mid = new C.Position('r1bq1rk1/pp2bppp/2n1pn2/2pp4/3P1B2/2PBPN2/PP1N1PPP/R2QK2R w KQ - 0 8');
  const res = s.search(mid, { timeMs: 1000 });
  const ms = Date.now() - t0;
  console.log(`  1 s middlegame search: depth ${res.depth}, ${res.nodes} nodes, ${Math.round(res.nodes / ms)} knps, ${ms} ms`);
  check(ms < 1300, 'respects the time limit (' + ms + ' ms)');
  check(res.depth >= 5, 'reaches a useful depth (' + res.depth + ')');
}

console.log('self-play');
{
  /* Every level must produce only legal moves and reach a proper result. */
  let seed = 99;
  const rnd = () => (seed = (seed * 1103515245 + 12345) & 0x7fffffff) / 0x80000000;
  const tally = {};
  for (const [white, black] of [[1, 2], [3, 4], [5, 2], [4, 5], [6, 3], [2, 6]]) {
    const moves = [], pos = new C.Position();
    let st = pos.status();
    while (!st.over && moves.length < 400) {
      const level = pos.side === C.WHITE ? white : black;
      const r = C.chooseMove(moves, level, rnd);
      const m = pos.findMove(r.uci);
      check(m !== 0, `level ${level} played a legal move (${r.uci})`);
      if (!m) break;
      pos.make(m);
      moves.push(r.uci);
      st = pos.status();
    }
    const result = st.over ? (st.winner === null ? 'draw' : st.winner === C.WHITE ? 'white' : 'black') : 'unfinished';
    tally[`L${white} v L${black}`] = `${result} (${st.reason || 'move cap'}, ${moves.length} plies)`;
  }
  for (const k in tally) console.log(`  ${k}: ${tally[k]}`);
}

console.log(`\n${checks} checks, ${failures} failures`);
process.exit(failures ? 1 : 0);
