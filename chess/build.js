/*
 * build.js - assemble the sources into one self-contained page.
 *
 *     node chess/build.js        writes chess/chess.html
 *
 * The result has no external references at all, so it runs from a local
 * file with no network.
 */
'use strict';
const fs = require('fs');
const path = require('path');

const src = (name) => fs.readFileSync(path.join(__dirname, 'src', name), 'utf8');

/* The piece sprite becomes an invisible inline <svg>; pieces are drawn with
   <use href="#wk"> and so on. Its licence comment is kept in the output. */
const pieces = src('pieces.svg')
  .replace(/<\?xml[^>]*\?>/, '')
  .replace(/<svg [^>]*>/, '<svg xmlns="http://www.w3.org/2000/svg" aria-hidden="true" ' +
    'style="position:absolute;width:0;height:0;overflow:hidden">')
  .replace(/<title>[^<]*<\/title>\s*<desc>[^<]*<\/desc>/, '');

const parts = {
  '/*{{CSS}}*/': src('style.css'),
  '<!--{{PIECES}}-->': pieces,
  '/*{{ENGINE}}*/': src('engine.js'),
  '/*{{UI}}*/': src('ui.js'),
};

let html = src('index.html');
for (const [marker, text] of Object.entries(parts)) {
  if (!html.includes(marker)) throw new Error('missing marker ' + marker);
  if (/<\/script/i.test(text) && marker !== '<!--{{PIECES}}-->') throw new Error('script terminator in ' + marker);
  html = html.replace(marker, () => text);
}
if (/(src|href)\s*=\s*["']?(https?:)?\/\//i.test(html.replace(/<!--[\s\S]*?-->/g, '').replace(/xmlns="[^"]*"/g, '')))
  throw new Error('the page must not reference external resources');

const out = path.join(__dirname, 'chess.html');
fs.writeFileSync(out, html);
console.log('wrote ' + path.relative(process.cwd(), out) + ' (' + Math.round(html.length / 1024) + ' KB)');
