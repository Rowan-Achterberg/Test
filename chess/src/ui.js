/*
 * ui.js - board, move list and game flow.
 *
 * Rules and notation come from engine.js on this thread. The opponent is
 * Stockfish, embedded in the page and run in a Web Worker; it also supplies
 * the evaluation shown in the bar. If Stockfish cannot start, the small
 * engine in engine.js takes over so the game is still playable.
 */
(function () {
  'use strict';

  const C = window.Chess;
  const $ = (id) => document.getElementById(id);
  const SVG_NS = 'http://www.w3.org/2000/svg';
  const STORE_KEY = 'offline-chess-v1';
  const LEVEL_NOTES = [
    '', 'Beginner: plays fast and blunders often.', 'Novice: still makes frequent mistakes.',
    'Casual: misses deeper tactics.', 'Improving player.', 'Club player.',
    'Strong club player.', 'Expert.', 'Stockfish at full strength.',
  ];

  const state = {
    moves: [],            /* the game, as UCI strings from the start position */
    sans: [],
    fens: [C.START_FEN],  /* fens[i]: position after i moves */
    checks: [false],
    caps: [],             /* caps[i]: piece captured by move i, or 0 */
    evals: [],            /* evals[i]: { cp } or { mate }, from White's side */
    showEval: true,
    showMoveScores: true,
    userColor: C.WHITE,
    level: 3,
    resigned: false,
    flipped: false,
    sound: true,
    view: 0,              /* which ply the board is showing */
    over: { over: false, reason: '', winner: null },
    started: false,
  };

  let pos = new C.Position();
  let legal = [];
  let selected = -1;
  let thinking = false;
  let requestId = 0;
  let worker = null;
  let drag = null;
  let pickedLevel = 3;
  let thinkingSince = 0;

  /* ---------------------------------------------------------- stockfish -- */

  /* Strength levels: Stockfish's own Skill Level plus a search limit. The
     two lowest levels also play an occasional random move. */
  const SF_LEVELS = [
    null,
    { skill: 0,  go: 'depth 1',                blunder: 0.25, book: false },
    { skill: 1,  go: 'depth 2',                blunder: 0.12, book: false },
    { skill: 3,  go: 'depth 3',                blunder: 0,    book: true },
    { skill: 6,  go: 'depth 5',                blunder: 0,    book: true },
    { skill: 9,  go: 'depth 7',                blunder: 0,    book: true },
    { skill: 13, go: 'depth 10 movetime 1000', blunder: 0,    book: true },
    { skill: 17, go: 'depth 14 movetime 1500', blunder: 0,    book: true },
    { skill: 20, go: 'movetime 2000',          blunder: 0,    book: true },
  ];
  const EVAL_MS = 400;   /* thinking time for the evaluation bar */

  /* The engine asks for its .wasm file with fetch(). A page opened from disk
     cannot fetch, so the worker is given the bytes directly instead. */
  const SF_SHIM = `
    self.fetch = function () {
      var bin = atob(__b64), n = bin.length, bytes = new Uint8Array(n);
      for (var i = 0; i < n; i++) bytes[i] = bin.charCodeAt(i);
      return Promise.resolve(new Response(bytes, {
        status: 200, headers: { 'Content-Type': 'application/wasm', 'Content-Length': String(n) }
      }));
    };
  `;

  const sf = { worker: null, status: 'off', active: null, pending: null, timer: 0 };

  function sfStart() {
    if (sf.status !== 'off') return;
    const wasm = $('sf-wasm'), js = $('sf-js');
    const b64 = wasm ? wasm.textContent.trim() : '';
    if (!js || b64.length < 1000) { sf.status = 'failed'; return; }
    try {
      const src = 'var __b64 = ' + JSON.stringify(b64) + ';' + SF_SHIM;
      sf.worker = new Worker(URL.createObjectURL(new Blob([src, js.textContent], { type: 'text/javascript' })));
    } catch (err) {
      sf.status = 'failed';
      return;
    }
    sf.status = 'starting';
    sf.worker.onmessage = (e) => sfLine(String(e.data));
    sf.worker.onerror = sfFail;
    sf.worker.postMessage('uci');
    sf.timer = setTimeout(() => { if (sf.status === 'starting') sfFail(); }, 20000);
  }

  function sfFail() {
    if (sf.status === 'failed') return;
    sf.status = 'failed';
    clearTimeout(sf.timer);
    if (sf.worker) sf.worker.terminate();
    sf.worker = sf.active = sf.pending = null;
    analyse();   /* carry on with the built-in engine */
  }

  function sfLine(line) {
    if (line === 'uciok') {
      sf.worker.postMessage('setoption name Hash value 32');
      sf.worker.postMessage('isready');
    } else if (line === 'readyok') {
      if (sf.status === 'starting') { sf.status = 'ready'; clearTimeout(sf.timer); sfPump(); }
    } else if (line.startsWith('info') && sf.active) {
      const m = / score (cp|mate) (-?\d+)/.exec(line);
      if (!m || /(upper|lower)bound/.test(line)) return;
      const d = / depth (\d+)/.exec(line);
      sf.active.score = { type: m[1], value: +m[2], depth: d ? +d[1] : 0 };
      if (!sf.active.cancelled && sf.active.onInfo) sf.active.onInfo(sf.active.score);
    } else if (line.startsWith('bestmove') && sf.active) {
      const job = sf.active;
      sf.active = null;
      if (!job.cancelled) job.onDone(line.split(' ')[1], job.score);
      sfPump();
    }
  }

  function sfPump() {
    if (sf.status !== 'ready' || sf.active || !sf.pending) return;
    const job = sf.active = sf.pending;
    sf.pending = null;
    sf.worker.postMessage('setoption name Skill Level value ' + job.skill);
    sf.worker.postMessage('position startpos' + (job.moves.length ? ' moves ' + job.moves.join(' ') : ''));
    sf.worker.postMessage('go ' + job.go);
  }

  /* Run one search; a search already running is stopped and discarded. */
  function sfRun(job) {
    sfCancel();
    sf.pending = job;
    sfPump();
  }

  function sfCancel() {
    sf.pending = null;
    if (sf.active && !sf.active.cancelled) {
      sf.active.cancelled = true;
      sf.worker.postMessage('stop');
    }
  }

  /* ------------------------------------------------------ built-in engine -- */

  function makeWorker() {
    try {
      const src = $('engine-src').textContent +
        '\nself.onmessage = function (e) {' +
        '  var d = e.data, r;' +
        '  try { r = self.Chess.chooseMove(d.moves, d.level); } catch (err) { r = { error: String(err) }; }' +
        '  r.id = d.id; self.postMessage(r);' +
        '};';
      const w = new Worker(URL.createObjectURL(new Blob([src], { type: 'text/javascript' })));
      w.onmessage = (e) => { if (e.data.id === requestId && e.data.uci) computerPlays(e.data.uci, e.data.id); };
      w.onerror = () => { worker = null; if (thinking) thinkHere(requestId); };
      return w;
    } catch (err) {
      return null;
    }
  }

  /* Last resort when workers are unavailable: think on this thread. */
  function thinkHere(id) {
    const moves = state.moves.slice(), level = state.level;
    setTimeout(() => {
      if (id !== requestId) return;
      try { computerPlays(C.chooseMove(moves, level).uci, id); } catch (err) { /* no move */ }
    }, 30);
  }

  function builtinGo(id) {
    if (!worker) worker = makeWorker();
    if (worker) worker.postMessage({ id, moves: state.moves, level: state.level });
    else thinkHere(id);
  }

  /* ------------------------------------------------------------ thinking -- */

  /* Score as Stockfish reports it (for the side to move) -> White's side. */
  function whiteEval(score, ply) {
    const sign = ply % 2 === 0 ? 1 : -1;
    return score.type === 'mate' ? { mate: score.value * sign, depth: score.depth }
      : { cp: score.value * sign, depth: score.depth };
  }

  /*
   * Called whenever the live position changes: evaluate it for the bar,
   * then, if it is the computer's turn, choose and play its move.
   */
  function analyse() {
    const id = ++requestId;
    const ply = state.moves.length;
    const moves = state.moves.slice();
    thinking = false;
    sfCancel();

    if (state.over.over) {
      if (state.over.reason === 'checkmate') state.evals[ply] = { mate: 0, white: state.over.winner === C.WHITE };
      else if (state.over.reason !== 'resignation') state.evals[ply] = { cp: 0 };
      renderEval();
      renderMoveScore(ply);
      save();
      return;
    }
    if (!state.started) return;

    const computerToMove = pos.side !== state.userColor;
    thinking = computerToMove;
    thinkingSince = performance.now();
    sfStart();
    if (sf.status === 'failed') {
      if (computerToMove) builtinGo(id);
      return;
    }

    const chooseMove = () => {
      if (id !== requestId || !computerToMove) return;
      const cfg = SF_LEVELS[state.level];
      const book = cfg.book ? C.bookMove(moves) : null;
      if (book && pos.findMove(book)) return computerPlays(book, id);
      if (cfg.blunder && Math.random() < cfg.blunder)
        return computerPlays(C.moveToUci(legal[Math.floor(Math.random() * legal.length)]), id);
      sfRun({ skill: cfg.skill, moves, go: cfg.go, onDone: (best) => computerPlays(best, id) });
    };

    if (!state.showEval && !state.showMoveScores) return chooseMove();
    sfRun({
      skill: 20, moves, go: 'movetime ' + EVAL_MS,
      onInfo: (score) => {
        if (id !== requestId) return;
        state.evals[ply] = whiteEval(score, ply);
        if (state.view === ply) renderEval();
        renderMoveScore(ply);
      },
      onDone: () => { if (id === requestId) save(); chooseMove(); },
    });
  }

  function computerPlays(uci, id) {
    /* An instant reply feels abrupt; give every move a short beat. */
    const wait = Math.max(0, 350 - (performance.now() - thinkingSince));
    setTimeout(() => {
      if (id !== requestId || !thinking) return;
      thinking = false;
      const m = uci ? pos.findMove(uci) : 0;
      if (m) play(m, true);
      else renderAll(false);
    }, wait);
  }

  function cancelThinking() {
    thinking = false;
    requestId++;
    sfCancel();
    if (worker) { worker.terminate(); worker = null; }
  }

  /* --------------------------------------------------------------- game -- */

  function currentStatus() {
    if (state.resigned) return { over: true, reason: 'resignation', winner: state.userColor ^ 8 };
    return pos.status();
  }

  /* Rebuild everything derived from state.moves. */
  function rebuild() {
    pos = new C.Position();
    state.sans = [];
    state.fens = [pos.fen()];
    state.checks = [false];
    state.caps = [];
    state.evals.length = Math.min(state.evals.length, state.moves.length + 1);
    for (const uci of state.moves) {
      const m = pos.findMove(uci);
      if (!m) throw new Error('illegal move ' + uci);
      state.sans.push(pos.san(m));
      state.caps.push(C.moveCaptured(m));
      pos.make(m);
      state.fens.push(pos.fen());
      state.checks.push(pos.inCheck());
    }
    legal = pos.legalMoves();
    state.over = currentStatus();
    state.view = state.moves.length;
    selected = -1;
  }

  function play(m, animate) {
    const capture = C.moveCaptured(m) !== 0;
    state.moves.push(C.moveToUci(m));
    state.sans.push(pos.san(m));
    state.caps.push(C.moveCaptured(m));
    pos.make(m);
    state.fens.push(pos.fen());
    state.checks.push(pos.inCheck());
    legal = pos.legalMoves();
    state.over = currentStatus();
    state.view = state.moves.length;
    selected = -1;
    save();
    beep(capture);
    renderAll(animate);
    analyse();
  }

  function newGame(side, level) {
    cancelThinking();
    if (side === 'random') side = Math.random() < 0.5 ? 'white' : 'black';
    state.userColor = side === 'black' ? C.BLACK : C.WHITE;
    state.level = level;
    state.moves = [];
    state.resigned = false;
    state.flipped = state.userColor === C.BLACK;
    state.started = true;
    state.evals = [];
    rebuild();
    save();
    buildBoard();
    renderAll(false);
    analyse();
  }

  function takeback() {
    if (!state.moves.length) return;
    /* On the player's turn, undo the reply and the player's move before it. */
    const n = pos.side === state.userColor ? 2 : 1;
    if (state.moves.length < n) return;
    cancelThinking();
    state.moves.length -= n;
    state.resigned = false;
    rebuild();
    save();
    renderAll(false);
    analyse();
  }

  function resign() {
    if (state.over.over || !state.moves.length) return;
    cancelThinking();
    state.resigned = true;
    state.over = currentStatus();
    save();
    renderAll(false);
    analyse();
  }

  function canMove() {
    return state.started && !state.over.over && !thinking && state.view === state.moves.length &&
      pos.side === state.userColor && $('promo').hidden;
  }

  /* ------------------------------------------------------------ storage -- */

  function save() {
    try {
      localStorage.setItem(STORE_KEY, JSON.stringify({
        moves: state.moves, userColor: state.userColor, level: state.level,
        resigned: state.resigned, flipped: state.flipped, sound: state.sound, showEval: state.showEval,
        showMoveScores: state.showMoveScores, evals: state.evals,
      }));
    } catch (err) { /* private mode or storage disabled: play on without saving */ }
  }

  function load() {
    try {
      const s = JSON.parse(localStorage.getItem(STORE_KEY) || 'null');
      if (!s || !Array.isArray(s.moves)) return false;
      state.moves = s.moves.map(String);
      state.userColor = s.userColor === C.BLACK ? C.BLACK : C.WHITE;
      state.level = Math.max(1, Math.min(8, s.level | 0)) || 3;
      state.resigned = !!s.resigned;
      state.flipped = !!s.flipped;
      state.sound = s.sound !== false;
      state.showEval = s.showEval !== false;
      state.showMoveScores = s.showMoveScores !== false;
      state.evals = Array.isArray(s.evals)
        ? s.evals.map((e) => (e && (typeof e.cp === 'number' || typeof e.mate === 'number') ? e : undefined)) : [];
      rebuild();
      state.started = true;
      return true;
    } catch (err) {
      state.moves = [];
      state.resigned = false;
      rebuild();
      return false;
    }
  }

  /* -------------------------------------------------------------- sound -- */

  let audio = null;
  function beep(capture) {
    if (!state.sound) return;
    try {
      audio = audio || new (window.AudioContext || window.webkitAudioContext)();
      const t = audio.currentTime;
      const osc = audio.createOscillator(), gain = audio.createGain();
      osc.type = 'triangle';
      osc.frequency.setValueAtTime(capture ? 190 : 330, t);
      osc.frequency.exponentialRampToValueAtTime(capture ? 90 : 210, t + 0.08);
      gain.gain.setValueAtTime(0.22, t);
      gain.gain.exponentialRampToValueAtTime(0.001, t + 0.1);
      osc.connect(gain).connect(audio.destination);
      osc.start(t);
      osc.stop(t + 0.11);
    } catch (err) { /* no audio device: stay silent */ }
  }

  /* -------------------------------------------------------------- board -- */

  function pieceSvg(ch) {
    const svg = document.createElementNS(SVG_NS, 'svg');
    svg.setAttribute('viewBox', '0 0 40 40');
    svg.setAttribute('class', 'piece');
    const use = document.createElementNS(SVG_NS, 'use');
    use.setAttribute('href', '#' + (ch === ch.toUpperCase() ? 'w' : 'b') + ch.toLowerCase());
    svg.appendChild(use);
    return svg;
  }

  /* FEN board field -> { square: piece letter }. */
  function parseBoard(fen) {
    const map = {};
    let rank = 7, file = 0;
    for (const ch of fen.split(' ')[0]) {
      if (ch === '/') { rank--; file = 0; }
      else if (ch >= '1' && ch <= '8') file += +ch;
      else { map[rank * 16 + file] = ch; file++; }
    }
    return map;
  }

  function squareEl(sq) {
    return $('board').querySelector('[data-sq="' + sq + '"]');
  }

  function buildBoard() {
    const board = $('board');
    board.textContent = '';
    for (let row = 0; row < 8; row++) {
      for (let col = 0; col < 8; col++) {
        const rank = state.flipped ? row : 7 - row;
        const file = state.flipped ? 7 - col : col;
        const el = document.createElement('div');
        el.className = 'sq ' + ((rank + file) % 2 ? 'light' : 'dark');
        el.dataset.sq = rank * 16 + file;
        if (col === 7) el.insertAdjacentHTML('beforeend', '<span class="coord rank">' + (rank + 1) + '</span>');
        if (row === 7) el.insertAdjacentHTML('beforeend', '<span class="coord file">' + 'abcdefgh'[file] + '</span>');
        board.appendChild(el);
      }
    }
  }

  function renderBoard(animate) {
    const map = parseBoard(state.fens[state.view]);
    const live = state.view === state.moves.length;
    const last = state.view > 0 ? state.moves[state.view - 1] : null;
    const lastFrom = last ? C.parseSq(last.slice(0, 2)) : -1;
    const lastTo = last ? C.parseSq(last.slice(2, 4)) : -1;
    const whiteToMove = state.fens[state.view].split(' ')[1] === 'w';
    const targets = new Set();
    if (live && selected >= 0)
      for (const m of legal) if (C.moveFrom(m) === selected) targets.add(C.moveTo(m));
    const mayMove = canMove();

    for (const el of $('board').children) {
      const sq = +el.dataset.sq;
      const ch = map[sq];
      const old = el.querySelector('.piece');
      if (old) old.remove();
      if (ch) el.appendChild(pieceSvg(ch));
      const own = ch && mayMove && (ch === ch.toUpperCase()) === (state.userColor === C.WHITE);
      el.classList.toggle('last', sq === lastFrom || sq === lastTo);
      el.classList.toggle('sel', live && sq === selected);
      el.classList.toggle('target', targets.has(sq));
      el.classList.toggle('occupied', targets.has(sq) && !!ch);
      el.classList.toggle('mine', !!own);
      el.classList.toggle('check', state.checks[state.view] && ch === (whiteToMove ? 'K' : 'k'));
    }

    /* Slide the piece that just moved from its old square. */
    if (animate && last) {
      const a = squareEl(lastFrom), b = squareEl(lastTo);
      const piece = b && b.querySelector('.piece');
      if (a && piece) {
        const ra = a.getBoundingClientRect(), rb = b.getBoundingClientRect();
        piece.style.transform = 'translate(' + (ra.left - rb.left) + 'px,' + (ra.top - rb.top) + 'px)';
        piece.getBoundingClientRect();   /* commit the starting position */
        piece.classList.add('moving');
        piece.style.transform = '';
      }
    }
  }

  /* -------------------------------------------------------------- panel -- */

  /* An evaluation as text from the computer's side: "+0.8", "-1.2", "#3". */
  function scoreText(ev, short) {
    if (!ev) return '';
    const computerIsWhite = state.userColor === C.BLACK;
    if (ev.mate !== undefined) {
      const computerWins = (ev.mate === 0 ? ev.white : ev.mate > 0) === computerIsWhite;
      if (short) return (computerWins ? '#' : '\u2212#') + (ev.mate === 0 ? '' : Math.abs(ev.mate));
      if (ev.mate === 0) return computerWins ? 'checkmate, it won' : 'checkmated';
      return (computerWins ? 'mate in ' : 'gets mated in ') + Math.abs(ev.mate);
    }
    const mine = (computerIsWhite ? ev.cp : -ev.cp) / 100;
    const shown = Math.abs(mine).toFixed(1);
    return (shown === '0.0' ? '' : mine > 0 ? '+' : '\u2212') + shown;
  }

  /* Fill in one move's score when its evaluation arrives. */
  function renderMoveScore(ply) {
    const sc = $('moves').querySelector('.mv[data-ply="' + ply + '"] .sc');
    if (sc) sc.textContent = state.showMoveScores ? scoreText(state.evals[ply], true) : '';
  }

  function renderMoves() {
    const box = $('moves');
    box.textContent = '';
    if (!state.sans.length) {
      box.insertAdjacentHTML('beforeend', '<div class="empty">' +
        (state.started ? 'No moves yet.' : 'Start a new game to play.') + '</div>');
    }
    state.sans.forEach((san, i) => {
      if (i % 2 === 0) {
        const num = document.createElement('div');
        num.className = 'num';
        num.textContent = i / 2 + 1;
        box.appendChild(num);
      }
      const mv = document.createElement('div');
      mv.className = 'mv' + (i + 1 === state.view ? ' cur' : '');
      mv.dataset.ply = i + 1;
      const name = document.createElement('span');
      name.textContent = san;
      const sc = document.createElement('span');
      sc.className = 'sc';
      sc.title = 'Score for the computer after this move';
      sc.textContent = state.showMoveScores ? scoreText(state.evals[i + 1], true) : '';
      mv.append(name, sc);
      box.appendChild(mv);
    });
    if (state.over.over) {
      if (state.sans.length % 2) box.appendChild(document.createElement('div'));
      const res = document.createElement('div');
      res.className = 'result';
      res.textContent = resultCode();
      const why = document.createElement('small');
      why.textContent = resultText();
      res.appendChild(why);
      box.appendChild(res);
    }
    const cur = box.querySelector('.cur');
    if (state.view === state.moves.length) box.scrollTop = box.scrollHeight;
    else if (cur) cur.scrollIntoView({ block: 'nearest' });
  }

  function resultCode() {
    if (!state.over.over) return '*';
    return state.over.winner === null ? '½-½' : state.over.winner === C.WHITE ? '1-0' : '0-1';
  }

  function resultText() {
    const o = state.over;
    if (!o.over) return '';
    const winner = o.winner === C.WHITE ? 'White' : 'Black';
    if (o.reason === 'checkmate') return 'Checkmate • ' + winner + ' is victorious';
    if (o.reason === 'resignation')
      return (o.winner === C.WHITE ? 'Black' : 'White') + ' resigned • ' + winner + ' is victorious';
    const reason = o.reason.charAt(0).toUpperCase() + o.reason.slice(1);
    return reason + ' • Draw';
  }

  function renderStatus() {
    let text;
    if (!state.started) text = 'Choose New game to begin.';
    else if (state.over.over) text = resultText();
    else if (thinking || pos.side !== state.userColor) text = 'Computer is thinking…';
    else text = state.checks[state.moves.length] ? 'Check! Your turn.' : 'Your turn.';
    if (state.started && state.view !== state.moves.length) text += '  (viewing move ' + state.view + ')';
    $('status').textContent = text;

    $('btn-takeback').disabled = state.moves.length < (pos.side === state.userColor ? 2 : 1);
    $('btn-resign').disabled = state.over.over || !state.moves.length;
    $('btn-new').classList.toggle('primary', state.over.over || !state.started);
    $('btn-sound').textContent = 'Sound: ' + (state.sound ? 'on' : 'off');
    $('btn-eval').textContent = 'Eval bar: ' + (state.showEval ? 'on' : 'off');
    $('btn-mscore').textContent = 'Move scores: ' + (state.showMoveScores ? 'on' : 'off');
  }

  /* The evaluation bar and the computer's score for the position on view. */
  function renderEval() {
    document.body.classList.toggle('no-eval', !state.showEval);
    const ev = state.started ? state.evals[state.view] : null;
    const bar = $('evalbar'), num = $('evalnum'), line = $('score');
    line.textContent = '';
    if (!state.showEval) return;

    let white = 50, label = '', text = '';
    if (ev && ev.mate !== undefined) {
      const whiteWins = ev.mate === 0 ? ev.white : ev.mate > 0;
      white = whiteWins ? 100 : 0;
      label = ev.mate === 0 ? '#' : 'M' + Math.abs(ev.mate);
      text = scoreText(ev, false);
    } else if (ev) {
      white = 50 + 50 * (2 / (1 + Math.exp(-0.00368208 * ev.cp)) - 1);
      white = Math.max(4, Math.min(96, white));
      label = (Math.abs(ev.cp) / 100).toFixed(1);
      text = scoreText(ev, false);
    }
    bar.classList.toggle('flipped', state.flipped);
    $('evalfill').style.height = white + '%';
    /* The number sits at the end of the side that is ahead. */
    const whiteAhead = white >= 50;
    const atBottom = whiteAhead !== state.flipped;
    num.className = (atBottom ? 'at-bottom' : 'at-top') + (whiteAhead ? ' on-light' : ' on-dark');
    num.textContent = label;

    if (text) {
      line.append("Computer's score: ");
      const b = document.createElement('b');
      b.textContent = text;
      line.append(b);
    } else if (state.started) {
      line.textContent = sf.status === 'failed' ? 'Score unavailable (Stockfish did not start).'
        : "Computer's score: \u2026";
    }
  }

  /* Names, whose turn it is, and the pieces each side has captured. */
  function renderPlayers() {
    const map = parseBoard(state.fens[state.view]);
    const count = {};
    for (const sq in map) count[map[sq]] = (count[map[sq]] || 0) + 1;
    const worth = { p: 1, n: 3, b: 3, r: 5, q: 9 };
    const whiteToMove = state.fens[state.view].split(' ')[1] === 'w';
    const bottom = state.flipped ? C.BLACK : C.WHITE;

    for (const [where, color] of [['bottom', bottom], ['top', bottom ^ 8]]) {
      const el = $('player-' + where), white = color === C.WHITE;
      el.querySelector('.dot').className = 'dot ' + (white ? 'white' : 'black');
      el.querySelector('.name').textContent = !state.started ? (white ? 'White' : 'Black')
        : color === state.userColor ? 'You' : 'Computer level ' + state.level;
      el.classList.toggle('turn', state.started && !state.over.over && white === whiteToMove);

      /* Tray beside the board: every piece this side has taken so far. */
      const tray = $('tray-' + where);
      tray.textContent = '';
      const taken = [];
      for (let i = white ? 0 : 1; i < state.view; i += 2) if (state.caps[i]) taken.push(state.caps[i] & 7);
      taken.sort((a, b) => (b === C.P ? 0 : b) - (a === C.P ? 0 : a));   /* queens first, pawns last */
      let prev = 0;
      for (const type of taken) {
        const ch = 'pnbrqk'[type - 1];
        const svg = pieceSvg(white ? ch : ch.toUpperCase());
        svg.setAttribute('class', prev && prev !== type ? 'gap' : '');
        tray.appendChild(svg);
        prev = type;
      }
      let lead = 0;
      for (const t of 'qrbnp')
        lead += ((count[white ? t.toUpperCase() : t] || 0) - (count[white ? t : t.toUpperCase()] || 0)) * worth[t];
      if (lead > 0) tray.insertAdjacentHTML('beforeend', '<span class="plus">+' + lead + '</span>');
    }
  }

  function renderAll(animate) {
    renderBoard(animate);
    renderMoves();
    renderPlayers();
    renderStatus();
    renderEval();
  }

  function setView(ply) {
    ply = Math.max(0, Math.min(state.moves.length, ply));
    if (ply === state.view) return;
    const forward = ply === state.view + 1;
    state.view = ply;
    selected = -1;
    hidePromo();
    renderAll(forward);
  }

  /* -------------------------------------------------------------- input -- */

  function squareAt(x, y) {
    const el = document.elementFromPoint(x, y);
    const sq = el && el.closest ? el.closest('.sq') : null;
    return sq && $('board').contains(sq) ? +sq.dataset.sq : -1;
  }

  function tryMove(from, to, animate) {
    const options = legal.filter((m) => C.moveFrom(m) === from && C.moveTo(m) === to);
    if (!options.length) return false;
    if (options.length === 1) { play(options[0], animate); return true; }
    showPromo(options, animate);   /* several moves share the squares: a promotion */
    return true;
  }

  function showPromo(options, animate) {
    const box = $('promo');
    const white = state.userColor === C.WHITE;
    const row = document.createElement('div');
    row.className = 'choices';
    for (const type of [C.Q, C.N, C.R, C.B]) {
      const m = options.find((o) => C.movePromo(o) === type);
      if (!m) continue;
      const btn = document.createElement('button');
      const ch = 'pnbrqk'[type - 1];
      btn.title = { q: 'Queen', n: 'Knight', r: 'Rook', b: 'Bishop' }[ch];
      const svg = pieceSvg(white ? ch.toUpperCase() : ch);
      svg.setAttribute('class', '');
      btn.appendChild(svg);
      btn.addEventListener('click', (e) => { e.stopPropagation(); hidePromo(); play(m, animate); });
      row.appendChild(btn);
    }
    box.textContent = '';
    box.appendChild(row);
    box.hidden = false;
  }

  function hidePromo() {
    $('promo').hidden = true;
  }

  function endDrag() {
    if (!drag) return;
    if (drag.ghost) drag.ghost.remove();
    if (drag.piece) drag.piece.classList.remove('lifted');
    drag = null;
  }

  function onPointerDown(e) {
    if (e.button !== 0 && e.pointerType === 'mouse') return;
    const sq = squareAt(e.clientX, e.clientY);
    if (sq < 0 || !canMove()) return;
    e.preventDefault();

    if (selected >= 0 && sq !== selected && tryMove(selected, sq, true)) return;

    const ch = parseBoard(state.fens[state.view])[sq];
    const own = ch && (ch === ch.toUpperCase()) === (state.userColor === C.WHITE);
    if (!own) {
      selected = -1;
      renderBoard(false);
      return;
    }
    const wasSelected = selected === sq;
    selected = sq;
    renderBoard(false);
    drag = { from: sq, x: e.clientX, y: e.clientY, moved: false, wasSelected, ghost: null, piece: null, ch };
    $('board').setPointerCapture(e.pointerId);
  }

  function onPointerMove(e) {
    if (!drag) return;
    if (!drag.moved) {
      if (Math.abs(e.clientX - drag.x) + Math.abs(e.clientY - drag.y) < 5) return;
      drag.moved = true;
      const src = squareEl(drag.from);
      drag.size = src.getBoundingClientRect().width;
      drag.piece = src.querySelector('.piece');
      if (drag.piece) drag.piece.classList.add('lifted');
      drag.ghost = pieceSvg(drag.ch);
      drag.ghost.setAttribute('class', 'ghost');
      drag.ghost.style.width = drag.ghost.style.height = drag.size + 'px';
      document.body.appendChild(drag.ghost);
    }
    drag.ghost.style.left = e.clientX - drag.size / 2 + 'px';
    drag.ghost.style.top = e.clientY - drag.size / 2 + 'px';
  }

  function onPointerUp(e) {
    if (!drag) return;
    const d = drag;
    endDrag();
    if (d.moved) {
      const to = squareAt(e.clientX, e.clientY);
      if (to >= 0 && to !== d.from && tryMove(d.from, to, false)) return;
      renderBoard(false);
    } else if (d.wasSelected) {
      selected = -1;        /* a second click on the same piece puts it down */
      renderBoard(false);
    }
  }

  function buildPgn() {
    const user = state.userColor === C.WHITE;
    const computer = 'Computer level ' + state.level;
    const d = new Date();
    const date = d.getFullYear() + '.' + String(d.getMonth() + 1).padStart(2, '0') + '.' + String(d.getDate()).padStart(2, '0');
    const result = !state.over.over ? '*' : state.over.winner === null ? '1/2-1/2' : state.over.winner === C.WHITE ? '1-0' : '0-1';
    let text = '[Event "Offline game"]\n[Date "' + date + '"]\n[White "' + (user ? 'Player' : computer) + '"]\n' +
      '[Black "' + (user ? computer : 'Player') + '"]\n[Result "' + result + '"]\n\n';
    state.sans.forEach((san, i) => { text += (i % 2 ? '' : i / 2 + 1 + '. ') + san + ' '; });
    return text + result + '\n';
  }

  function copyText(text) {
    const fallback = () => {
      const ta = document.createElement('textarea');
      ta.value = text;
      ta.style.position = 'fixed';
      ta.style.opacity = '0';
      document.body.appendChild(ta);
      ta.select();
      let ok = false;
      try { ok = document.execCommand('copy'); } catch (err) { ok = false; }
      ta.remove();
      return ok;
    };
    if (navigator.clipboard && navigator.clipboard.writeText)
      return navigator.clipboard.writeText(text).then(() => true, fallback);
    return Promise.resolve(fallback());
  }

  /* -------------------------------------------------------------- setup -- */

  function openSetup() {
    pickedLevel = state.level;
    const box = $('levels');
    box.textContent = '';
    for (let i = 1; i <= 8; i++) {
      const b = document.createElement('button');
      b.textContent = i;
      b.className = i === pickedLevel ? 'on' : '';
      b.addEventListener('click', () => {
        pickedLevel = i;
        for (const o of box.children) o.className = o === b ? 'on' : '';
        $('level-note').textContent = LEVEL_NOTES[i];
      });
      box.appendChild(b);
    }
    $('level-note').textContent = LEVEL_NOTES[pickedLevel];
    const dlg = $('setup');
    if (!dlg.open) dlg.showModal();
  }

  function init() {
    const restored = load();
    buildBoard();
    renderAll(false);

    const board = $('board');
    board.addEventListener('pointerdown', onPointerDown);
    board.addEventListener('pointermove', onPointerMove);
    board.addEventListener('pointerup', onPointerUp);
    board.addEventListener('pointercancel', () => { endDrag(); renderBoard(false); });
    board.addEventListener('contextmenu', (e) => e.preventDefault());
    $('promo').addEventListener('click', () => { hidePromo(); selected = -1; renderBoard(false); });

    $('moves').addEventListener('click', (e) => {
      const mv = e.target.closest('.mv');
      if (mv) setView(+mv.dataset.ply);
    });
    $('nav-first').addEventListener('click', () => setView(0));
    $('nav-prev').addEventListener('click', () => setView(state.view - 1));
    $('nav-next').addEventListener('click', () => setView(state.view + 1));
    $('nav-last').addEventListener('click', () => setView(state.moves.length));
    const flip = () => { state.flipped = !state.flipped; save(); buildBoard(); renderAll(false); };
    $('nav-flip').addEventListener('click', flip);

    $('btn-takeback').addEventListener('click', takeback);
    $('btn-resign').addEventListener('click', resign);
    $('btn-new').addEventListener('click', openSetup);
    $('btn-sound').addEventListener('click', () => { state.sound = !state.sound; save(); renderStatus(); });
    $('btn-eval').addEventListener('click', () => {
      state.showEval = !state.showEval;
      save();
      renderAll(false);
      if (state.showEval && !thinking) analyse();
    });
    $('btn-mscore').addEventListener('click', () => {
      state.showMoveScores = !state.showMoveScores;
      save();
      renderAll(false);
      if (state.showMoveScores && !thinking) analyse();
    });
    $('btn-pgn').addEventListener('click', () => {
      copyText(buildPgn()).then((ok) => {
        $('btn-pgn').textContent = ok ? 'Copied' : 'Copy failed';
        setTimeout(() => { $('btn-pgn').textContent = 'Copy PGN'; }, 1500);
      });
    });

    for (const b of $('sides').querySelectorAll('button')) {
      b.addEventListener('click', () => { $('setup').close(); newGame(b.dataset.side, pickedLevel); });
    }
    $('setup-close').addEventListener('click', () => $('setup').close());

    document.addEventListener('keydown', (e) => {
      if ($('setup').open || e.ctrlKey || e.metaKey || e.altKey) return;
      if (e.key === 'ArrowLeft') setView(state.view - 1);
      else if (e.key === 'ArrowRight') setView(state.view + 1);
      else if (e.key === 'ArrowUp') setView(0);
      else if (e.key === 'ArrowDown') setView(state.moves.length);
      else if (e.key === 'f' || e.key === 'F') flip();
      else return;
      e.preventDefault();
    });

    if (restored) analyse();
    else openSetup();
  }

  init();
})();
