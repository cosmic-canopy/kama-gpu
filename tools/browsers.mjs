// tools/browsers.mjs: the browsers the web leg runs in under `kama run --target WASM`, and what each is started with
// (AGENTS.gpu.md item 11, with @kama/window's WIN-18). node 22 or later; no npm packages.
//
//   node tools/browsers.mjs run              # one path a line: Edge and Chrome as installed, then the newest Firefox and
//                                            # the newest Chrome for Testing pinned under $KAMA_BROWSERS (~/browsers)
//   node tools/browsers.mjs flags <browser>  # the `kama run` options that browser takes, one a line (none for most)
//   node tools/browsers.mjs kind <browser>   # firefox, chrome-for-testing, edge or chrome: which leg it runs
//
// $KAMA_BROWSER alone when it is set. Exit 3 when the list is empty. The pinned browsers are never auto-updated, so a red
// run can be reproduced; what each is, and how it was verified, is in that folder's README.md.

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

// Chrome for Testing, by the folder its archive unpacks to: an installed Chrome lives elsewhere.
export function isChromeForTesting(browser) {
    return /chrome-(win64|mac-arm64|mac-x64|linux64)[\\/]/.test(browser) || /Google Chrome for Testing$/.test(browser);
}

// What a browser is, which decides the leg it runs: firefox, chrome-for-testing (the pinned one), edge or chrome (each as
// installed).
export function kind(browser) {
    if (isFirefox(browser)) return 'firefox';
    if (isChromeForTesting(browser)) return 'chrome-for-testing';
    return /edge/i.test(browser) ? 'edge' : 'chrome';
}

export function runBrowsers() {
    if (process.env.KAMA_BROWSER) return [process.env.KAMA_BROWSER];
    return [...installed(), ...firefox(), ...chromeForTesting()];
}

// What `kama run` hands a browser (kama 0.9.956's --chromium-arg, kyoten's KY-57), decided with the owner on 2026-11-14
// (AGENTS.gpu.md item 11): the pinned Chrome for Testing alone takes flags, on its own command line rather than in the
// tests' kama.json, whose `chromiumArgs` would reach Edge too. Edge and an installed Chrome run as a player's do, on
// their real GPU.
//   --enable-unsafe-webgpu, --enable-unsafe-swiftshader: Chrome offers WebGPU its fallback adapter, SwiftShader, only
//     with both (measured 2026-11-14, one flag at a time), and the cases on the fallback adapter ask for it;
//   --disable-features=NetworkServiceSandbox: an unzipped Chrome for Testing never loads a page without it, since its
//     sandboxed network service crashes (kyoten's KY-53, ~/browsers/README.md);
//   --scale 1.5: a device scale that is not 1, so pixel sizes are not CSS sizes, as the retired driver ran it.
export function kamaRunFlags(browser) {
    if (kind(browser) !== 'chrome-for-testing') return [];
    return ['--scale', '1.5',
            '--chromium-arg', '--enable-unsafe-webgpu',
            '--chromium-arg', '--enable-unsafe-swiftshader',
            '--chromium-arg', '--disable-features=NetworkServiceSandbox'];
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
    const which = process.argv[2];
    if (which === 'flags' && process.argv[3]) {
        for (const f of kamaRunFlags(process.argv[3])) console.log(f);
        process.exit(0);
    }
    if (which === 'kind' && process.argv[3]) {
        console.log(kind(process.argv[3]));
        process.exit(0);
    }
    if (which !== 'run') {
        console.error('usage: node tools/browsers.mjs run | flags <browser> | kind <browser>');
        process.exit(2);
    }
    const list = runBrowsers();
    if (!list.length) {
        console.error('browsers: none for run (pin one under ~/browsers, or set KAMA_BROWSER)');
        process.exit(3);
    }
    for (const b of list) console.log(b);
}
