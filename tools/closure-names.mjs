// tools/closure-names.mjs: whether a release build for the web kept the names a JS library hands the browser. node 22 or
// later; no npm packages.
//
//   node tools/closure-names.mjs <library.js> <release build's .js>
//
// A wasm release runs Closure over the page's JS (kama 0.9.966, KR-289), and Closure renames every property name it
// cannot prove belongs to something outside the program. A name the browser reads that it renamed reaches the browser as
// another, which the browser ignores without a word: an option key, a gamepad's `effects`, a table looked up by the
// DOM's own strings. Most such paths no headless case reaches, so this reads the build instead.
//
// Every name the library writes or reads (`x.name`, `name:`), outside its strings and comments, must still be in the
// release build, unless it is the library's own: its top-level entries, which emscripten names, and the names its
// `// closure-own:` lines list, which nothing outside the program reads and Closure may rename. So a name a later change
// adds is held by default, and one that is the library's own fails until it is listed. A listed name the library no
// longer uses fails too, so the list follows the code. A name the browser reads is kept by writing it quoted (`x['name']`,
// `{ 'name': v }`), as kama's docs/targets.md § jsLibraries says.
//
// It reads the build for each name as a word, so a name renamed in one place but written elsewhere passes: the tests'
// fake pads' `effects`, once the package read it quoted, did. The release legs' cases are the first check, and this one
// holds what no case reaches.
//
// Exit 0 with an `ok` line when every name is kept; else a FAIL line a name, and exit 1.

import fs from 'node:fs';

const [libraryPath, builtPath] = process.argv.slice(2);
if (!libraryPath || !builtPath) {
    console.error('usage: node tools/closure-names.mjs <library.js> <release build\'s .js>');
    process.exit(2);
}
const library = fs.readFileSync(libraryPath, 'utf8');
const built = fs.readFileSync(builtPath, 'utf8');

// The library's code with its comments removed and, unless `keepStrings`, each string literal emptied, so neither yields
// a name.
function codeOnly(src, keepStrings) {
    let out = '';
    for (let i = 0; i < src.length;) {
        if (src.startsWith('//', i)) { const j = src.indexOf('\n', i); i = j < 0 ? src.length : j; }
        else if (src.startsWith('/*', i)) { const j = src.indexOf('*/', i + 2); i = j < 0 ? src.length : j + 2; }
        else if (src[i] === '\'' || src[i] === '"' || src[i] === '`') {
            const q = src[i];
            let j = i + 1;
            while (j < src.length && src[j] !== q) j += src[j] === '\\' ? 2 : 1;
            out += keepStrings ? src.slice(i, j + 1) : '""';
            i = j + 1;
        } else if (src[i] === '/' && /[=(,:!&|?{};]\s*$/.test(out)) {
            // a regular expression literal
            let j = i + 1;
            while (j < src.length && src[j] !== '/') j += src[j] === '\\' ? 2 : 1;
            out += '/x/';
            i = j + 1;
        } else { out += src[i]; i++; }
    }
    return out;
}

const code = codeOnly(library, false);
const IDENT = '[A-Za-z_$][A-Za-z0-9_$]*';
const names = new Set();
for (const m of code.matchAll(new RegExp(`\\.\\s*(${IDENT})`, 'g'))) names.add(m[1]);
for (const m of code.matchAll(new RegExp(`(?<![A-Za-z0-9_$.?])(${IDENT})\\s*:(?!:)`, 'g'))) names.add(m[1]);

// The library object's top-level entries (`mergeInto(LibraryManager.library, { … })`): emscripten's to name.
const own = new Set(['library']);
const start = code.search(/(mergeInto|addToLibrary)\s*\(/);
if (start >= 0) {
    let depth = 0, open = code.indexOf('{', start);
    for (let i = open; i < code.length; i++) {
        const c = code[i];
        if (c === '{' || c === '(' || c === '[') depth++;
        else if (c === '}' || c === ')' || c === ']') { depth--; if (depth === 0) break; }
        else if (depth === 1) {
            const m = new RegExp(`^(${IDENT})\\s*:`).exec(code.slice(i));
            if (m && !/[A-Za-z0-9_$]/.test(code[i - 1] || '')) { own.add(m[1]); i += m[1].length - 1; }
        }
    }
}

// The names the library lists as its own.
const listed = [];
for (const line of library.split('\n')) {
    const m = /^\s*\/\/\s*closure-own:(.*)$/.exec(line);
    if (m) listed.push(...m[1].trim().split(/\s+/).filter(Boolean));
}
const word = n => new RegExp(`(?<![A-Za-z0-9_$])${n.replace(/\$/g, '\\$')}(?![A-Za-z0-9_$])`);

let failed = 0;
for (const n of listed) {
    if (!names.has(n)) { console.log(`FAIL ${libraryPath} lists ${n} as its own, and no longer uses it: take it off its closure-own lines`); failed++; }
    own.add(n);
}
const held = [...names].filter(n => !own.has(n)).sort();
for (const n of held) {
    if (!word(n).test(built)) {
        console.log(`FAIL a release build renamed ${n}, which ${libraryPath} uses: if the browser or the page reads it, quote it; if nothing outside the program does, list it on a closure-own line`);
        failed++;
    }
}

// A quoted name (`'name':`, `x['name']`) is one the library means something outside the program to read, so it must
// reach the release build too. emscripten writes a library object's own literal back with its keys unquoted, so a key
// quoted there reaches Closure unquoted and is renamed all the same: only a function's body is kept as written.
const quoted = new Set();
const withStrings = codeOnly(library, true);
for (const m of withStrings.matchAll(new RegExp(`(?<![A-Za-z0-9_$])['"](${IDENT})['"]\\s*:(?!:)`, 'g'))) quoted.add(m[1]);
// a property read by its quoted name: `x['name']`, never an array holding one (`__deps: ['$name']`)
for (const m of withStrings.matchAll(new RegExp(`(?<=[A-Za-z0-9_$)\\]])\\s*\\[\\s*['"](${IDENT})['"]\\s*\\]`, 'g'))) quoted.add(m[1]);
for (const n of [...quoted].sort()) {
    if (!word(n).test(built)) {
        console.log(`FAIL a release build lost ${n}, which ${libraryPath} quotes: a key quoted in a library object's own literal reaches Closure unquoted, so make the object in a function`);
        failed++;
    }
}
if (failed) process.exit(1);
console.log(`ok   the ${held.length} names and ${quoted.size} quoted names ${libraryPath} hands the browser are kept in ${builtPath}, and its ${listed.length} own are listed`);
