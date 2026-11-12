// tools/webtest.mjs — @kama/gpu's browser leg in the pinned Chrome for Testing (AGENTS.gpu.md items 10 and 11): a
// program built for the web with its own page, run in a headless Chromium browser driven over the DevTools protocol.
// node 22 or later, for its built-in WebSocket; no npm packages.
//
// STOPGAP: every other browser runs the leg under `kama run --target WASM` (tools/test.sh). Chrome for Testing,
// unzipped into ~/browsers, never loads a page under `kama run` (kyoten's KY-53), so it keeps this driver, which
// navigates after the browser has started. Delete it, and the page's exit hooks, once that browser runs under
// `kama run`.
//
//   node tools/webtest.mjs <dir> <page[?query]> [--browser <path>] [--timeout <seconds>]
//   node tools/webtest.mjs --list     # the browsers it runs in (tools/browsers.mjs driver), or exit 3 when none
//
// It serves <dir> on 127.0.0.1, opens <page> in a throwaway profile at a device scale of 1.5 (so pixel sizes are not CSS
// sizes) with WebGPU on, and prints the browser's name and the page's console as it comes. The page reports the
// program's end on the console: "webtest: exit <status>" from emscripten's `onExit`, or "webtest: abort <what>" from
// `onAbort` (tests/web/shell.html). The run's status is the program's; an abort, an uncaught exception or the time
// limit is 1. A page's query is the program's arguments: `?arg=--frames&arg=60` (the page reads them).
import { spawn } from 'node:child_process';
import http from 'node:http';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { driverBrowsers } from './browsers.mjs';

const SCALE = 1.5;

const TYPES = {
    '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.data': 'application/octet-stream',
    '.json': 'application/json', '.css': 'text/css',
};

function serve(root) {
    const base = path.resolve(root);
    const server = http.createServer((req, res) => {
        const u = new URL(req.url, 'http://x');
        const p = path.resolve(path.join(base, decodeURIComponent(u.pathname)));
        if (!p.startsWith(base)) { res.writeHead(403); res.end(); return; }
        fs.readFile(p, (err, data) => {
            if (err) { res.writeHead(404); res.end('not found'); return; }
            res.writeHead(200, { 'Content-Type': TYPES[path.extname(p)] || 'application/octet-stream', 'Cache-Control': 'no-store' });
            res.end(data);
        });
    });
    return new Promise(r => server.listen(0, '127.0.0.1', () => r({ server, port: server.address().port })));
}

// One DevTools-protocol connection: requests by id, events to the listeners.
class Cdp {
    constructor(ws) {
        this.ws = ws; this.next = 0; this.pending = new Map(); this.listeners = [];
        ws.onmessage = (m) => {
            const msg = JSON.parse(m.data);
            if (msg.id !== undefined) {
                const p = this.pending.get(msg.id);
                if (!p) return;
                this.pending.delete(msg.id);
                if (msg.error) p.reject(new Error(`${p.method}: ${JSON.stringify(msg.error)}`)); else p.resolve(msg.result);
            } else {
                for (const l of this.listeners) l(msg);
            }
        };
    }
    send(method, params = {}, sessionId) {
        const id = ++this.next;
        const msg = { id, method, params };
        if (sessionId) msg.sessionId = sessionId;
        this.ws.send(JSON.stringify(msg));
        return new Promise((resolve, reject) => this.pending.set(id, { resolve, reject, method }));
    }
}

async function launch(browser, profile) {
    fs.rmSync(profile, { recursive: true, force: true });
    const args = ['--headless=new', '--remote-debugging-port=0', `--user-data-dir=${profile}`, '--no-first-run',
                  '--no-default-browser-check', '--disable-extensions', `--force-device-scale-factor=${SCALE}`,
                  '--window-size=1280,1024', '--enable-unsafe-webgpu', 'about:blank'];
    const proc = spawn(browser, args, { stdio: ['ignore', 'ignore', 'pipe'] });
    const url = await new Promise((resolve, reject) => {
        let buf = '';
        const t = setTimeout(() => reject(new Error('the browser printed no DevTools address: ' + buf)), 30000);
        proc.stderr.on('data', d => {
            buf += d;
            const m = buf.match(/DevTools listening on (ws:\/\/\S+)/);
            if (m) { clearTimeout(t); resolve(m[1]); }
        });
        proc.on('exit', c => { clearTimeout(t); reject(new Error(`the browser exited (${c}): ${buf}`)); });
        proc.on('error', e => { clearTimeout(t); reject(new Error(`the browser did not start: ${e.message}`)); });
    });
    const ws = new WebSocket(url);
    await new Promise((r, j) => { ws.onopen = r; ws.onerror = j; });
    return { cdp: new Cdp(ws), proc };
}

async function main(argv) {
    if (argv[0] === '--list') {
        const found = driverBrowsers();
        if (!found.length) { console.error('webtest: no Chrome for Testing pinned under ~/browsers (or set KAMA_BROWSER)'); return 3; }
        for (const b of found) console.log(b);
        return 0;
    }
    const [dir, page] = argv;
    const opt = (name) => { const i = argv.indexOf(name); return i >= 0 ? argv[i + 1] : undefined; };
    const limit = Number(opt('--timeout') || 120);
    const browserPath = opt('--browser') || driverBrowsers()[0];
    if (!dir || !page) { console.error('usage: node tools/webtest.mjs <dir> <page[?query]> [--browser <path>] [--timeout <seconds>]'); return 2; }
    if (!browserPath) { console.error('webtest: no Chrome for Testing pinned under ~/browsers (or set KAMA_BROWSER)'); return 3; }

    const { server, port } = await serve(dir);
    const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'kama-gpu-webtest-'));
    let launched;
    try {
        launched = await launch(browserPath, profile);
    } catch (e) {
        console.log(`webtest: ${e.message}`);
        server.close();
        try { fs.rmSync(profile, { recursive: true, force: true }); } catch (e2) { }
        return 1;
    }
    const { cdp, proc } = launched;
    let verdict = null;   // the run's status, once it has ended
    let finish;
    const ended = new Promise(r => { finish = r; });
    const end = (status, why) => { if (verdict === null) { verdict = status; if (why) console.log(why); finish(); } };

    try {
        const version = await cdp.send('Browser.getVersion');
        console.log(`webtest: ${version.product} (${browserPath})`);
        const { targetId } = await cdp.send('Target.createTarget', { url: 'about:blank' });
        const { sessionId } = await cdp.send('Target.attachToTarget', { targetId, flatten: true });
        const send = (method, params = {}) => cdp.send(method, params, sessionId);
        cdp.listeners.push(msg => {
            if (msg.sessionId !== sessionId || verdict !== null) return;
            if (msg.method === 'Runtime.consoleAPICalled') {
                const text = msg.params.args.map(a => a.value !== undefined ? String(a.value) : (a.description || '')).join(' ');
                for (const line of text.split('\n')) {
                    const exit = line.match(/^webtest: exit (-?\d+)$/);
                    if (exit) { end(Number(exit[1]), `webtest: the program exited with ${exit[1]}`); continue; }
                    if (line.startsWith('webtest: abort')) { console.log(line); end(1, 'webtest: the program aborted'); continue; }
                    console.log(line);
                }
            } else if (msg.method === 'Runtime.exceptionThrown') {
                const d = msg.params.exceptionDetails;
                end(1, `webtest: an exception: ${d.exception?.description || d.text}`);
            }
        });
        await send('Runtime.enable');
        await send('Page.enable');
        await send('Page.navigate', { url: `http://127.0.0.1:${port}/${page}` });
        const timer = setTimeout(() => end(1, `webtest: the program did not end within ${limit} s`), limit * 1000);
        await ended;
        clearTimeout(timer);
    } catch (e) {
        end(1, `webtest: ${e.message}`);
    } finally {
        try { await cdp.send('Browser.close'); } catch (e) { }
        await new Promise(r => { if (proc.exitCode !== null) r(); else { proc.on('exit', r); setTimeout(r, 5000); } });
        server.close();
        // The browser's helper processes may hold the profile a moment after it exits; a profile left behind in the
        // system's temporary folder is said, and is not the run's verdict.
        try { fs.rmSync(profile, { recursive: true, force: true, maxRetries: 20, retryDelay: 250 }); }
        catch (e) { console.log(`webtest: the throwaway profile ${profile} is left behind: ${e.message}`); }
    }
    return verdict;
}

process.exitCode = await main(process.argv.slice(2));
