// tools/browsers.mjs: the browsers the web leg runs in, one path a line (AGENTS.gpu.md item 11, with @kama/window's
// WIN-18). node 22 or later; no npm packages.
//
//   node tools/browsers.mjs run      # `kama run --target WASM` runs the leg in each: Edge and Chrome as installed, then
//                                    # the newest Firefox pinned under $KAMA_BROWSERS (default ~/browsers)
//   node tools/browsers.mjs driver   # tools/webtest.mjs runs the leg in each: the newest Chrome for Testing pinned
//                                    # there, which never loads a page under `kama run` (kyoten's KY-53)
//
// $KAMA_BROWSER alone when it is set (in the driver's list only when it is not Firefox, which the driver cannot drive).
// Exit 3 when the list is empty. The pinned browsers are never auto-updated, so a red run can be reproduced; what each
// is, and how it was verified, is in that folder's README.md.

import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

// "157.0.8103.50" sorts after "157.0.812.1": compare each number.
function newer(a, b) {
    const pa = a.split('.').map(Number), pb = b.split('.').map(Number);
    for (let i = 0; i < Math.max(pa.length, pb.length); i++) {
        if ((pa[i] || 0) !== (pb[i] || 0)) return (pa[i] || 0) - (pb[i] || 0);
    }
    return 0;
}

// The newest `<prefix>-<version>` folder under the pinned directory that holds `inner`.
function newestPinned(prefix, inner) {
    const dir = process.env.KAMA_BROWSERS || path.join(os.homedir(), 'browsers');
    if (!fs.existsSync(dir)) return [];
    const re = new RegExp('^' + prefix + '-(\\d+(?:\\.\\d+)+)$');
    const found = fs.readdirSync(dir)
        .map(n => re.exec(n))
        .filter(m => m && fs.existsSync(path.join(dir, m[0], inner)))
        .sort((x, y) => newer(x[1], y[1]));
    return found.length ? [path.join(dir, found[found.length - 1][0], inner)] : [];
}

// Chrome for Testing's full browser: its headless shell gets no WebGPU adapter (measured 2026-11-11).
function chromeForTesting() {
    return newestPinned('chrome', process.platform === 'win32' ? 'chrome-win64/chrome.exe'
        : process.platform === 'darwin' ? 'chrome-mac-arm64/Google Chrome for Testing.app/Contents/MacOS/Google Chrome for Testing'
        : 'chrome-linux64/chrome');
}

function firefox() {
    return newestPinned('firefox', process.platform === 'win32' ? 'core/firefox.exe'
        : process.platform === 'darwin' ? 'Firefox.app/Contents/MacOS/firefox'
        : 'firefox/firefox');
}

// Edge and Chrome as installed, one of each.
function installed() {
    const local = process.env.LOCALAPPDATA || '';
    const candidates = process.platform === 'win32' ? [
        'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',
        'C:/Program Files/Microsoft/Edge/Application/msedge.exe',
        'C:/Program Files/Google/Chrome/Application/chrome.exe',
        path.join(local, 'Google/Chrome/Application/chrome.exe'),
    ] : process.platform === 'darwin' ? [
        '/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge',
        '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    ] : ['/usr/bin/microsoft-edge', '/usr/bin/google-chrome', '/usr/bin/chromium', '/usr/bin/chromium-browser'];
    const out = [];
    const seen = new Set();
    for (const c of candidates) {
        if (!fs.existsSync(c)) continue;
        const kind = /edge/i.test(c) ? 'edge' : 'chrome';
        if (seen.has(kind)) continue;
        seen.add(kind);
        out.push(c);
    }
    return out;
}

export function isFirefox(browser) { return /firefox/i.test(path.basename(browser)); }

export function runBrowsers() {
    if (process.env.KAMA_BROWSER) return [process.env.KAMA_BROWSER];
    return [...installed(), ...firefox()];
}

export function driverBrowsers() {
    const named = process.env.KAMA_BROWSER;
    if (named) return isFirefox(named) ? [] : [named];
    return chromeForTesting();
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
    const which = process.argv[2];
    const list = which === 'run' ? runBrowsers() : which === 'driver' ? driverBrowsers() : null;
    if (!list) {
        console.error('usage: node tools/browsers.mjs run|driver');
        process.exit(2);
    }
    if (!list.length) {
        console.error(`browsers: none for ${which} (pin one under ~/browsers, or set KAMA_BROWSER)`);
        process.exit(3);
    }
    for (const b of list) console.log(b);
}
