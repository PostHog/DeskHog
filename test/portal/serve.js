#!/usr/bin/env node
// Work on the web page without flashing anything.
//
//   node test/portal/serve.js                       a pretend device, from memory
//   node test/portal/serve.js --device 192.168.1.50 your real DeskHog behind the page
//   node test/portal/serve.js --offline             a pretend device that has no WiFi yet
//
// It also serves this repository's plugins/ folder as a plugin directory, at
// /directory/. The pretend device uses that one. To try it with a real device,
// set the directory on the page to http://<this computer's address>:8080/directory/
//
// Then open http://localhost:8080. The page is read from html/ on every
// request, so save a file and reload.

const fs = require('fs');
const http = require('http');
const path = require('path');
const { makeDevice } = require('./pretend-device.js');

const args = process.argv.slice(2);
const option = name => (args.includes(name) ? args[args.indexOf(name) + 1] : null);
const port = parseInt(option('--port') || '8080', 10);
const realDevice = option('--device');
const htmlDir = path.join(__dirname, '../../html');
const pluginDir = path.join(__dirname, '../../plugins');
const TYPES = { '.html': 'text/html; charset=utf-8', '.css': 'text/css', '.js': 'text/javascript' };

const device = makeDevice();
if (args.includes('--offline')) device.wifiConnected = false;
device.directory.source = device.directory.default = 'http://localhost:' + port + '/directory/';

function readBody(request) {
    return new Promise(resolve => {
        const parts = [];
        request.on('data', part => parts.push(part));
        request.on('end', () => resolve(Buffer.concat(parts)));
    });
}

/** Pass a request on to a real device. */
function forward(request, body, response) {
    const headers = Object.assign({}, request.headers, { host: realDevice });
    const upstream = http.request({ host: realDevice, port: 80, method: request.method, path: request.url, headers }, answer => {
        response.writeHead(answer.statusCode, answer.headers);
        answer.pipe(response);
    });
    upstream.on('error', error => {
        response.writeHead(502, { 'Content-Type': 'application/json' });
        response.end(JSON.stringify({ ok: false, error: 'Could not reach ' + realDevice + ': ' + error.message }));
    });
    upstream.end(body);
}

http.createServer(async (request, response) => {
    const pathname = request.url.split('?')[0];
    if (pathname.startsWith('/directory/')) {
        // Like raw.githubusercontent.com: plain files that any page may fetch.
        const picture = /^\/directory\/screenshots\/[a-z0-9_-]+\.png$/.test(pathname);
        const file = path.join(pluginDir, picture ? 'screenshots' : '', path.basename(pathname));
        const found = (picture || /\.(lua|json)$/.test(file)) && fs.existsSync(file);
        const type = found && picture ? 'image/png' : 'text/plain; charset=utf-8';
        response.writeHead(found ? 200 : 404, { 'Content-Type': type, 'Access-Control-Allow-Origin': '*', 'Cache-Control': 'no-store' });
        response.end(found ? fs.readFileSync(file) : 'Not found');
        return;
    }
    if (!pathname.startsWith('/api/')) {
        const file = path.join(htmlDir, pathname === '/' ? 'portal.html' : path.basename(pathname));
        if (!fs.existsSync(file)) { response.writeHead(404); response.end('not found'); return; }
        response.writeHead(200, { 'Content-Type': TYPES[path.extname(file)] || 'application/octet-stream', 'Cache-Control': 'no-store' });
        response.end(fs.readFileSync(file));
        return;
    }
    const raw = await readBody(request);
    if (realDevice) return forward(request, raw, response);

    let body = raw;
    const type = request.headers['content-type'] || '';
    if (type.includes('application/json')) {
        try { body = JSON.parse(raw.toString('utf8')); } catch (error) { body = {}; }
    }
    const answer = device.handle(request.method, request.url, body);
    response.writeHead(answer.status, Object.assign({ 'Content-Type': answer.type || 'application/json' }, answer.headers || {}));
    response.end(answer.body);
}).listen(port, () => {
    console.log('DeskHog page on http://localhost:' + port + (realDevice ? ', talking to the device at ' + realDevice : ', with a pretend device'));
});
