// A pretend DeskHog: it answers the same web API the firmware serves, from
// memory. The page's tests (portal.test.js) and the development server
// (serve.js) both use it.


function field(key, label, extra) {
    return Object.assign({ key, label, type: 'string', default: '' }, extra || {});
}

/** The fingerprint of a plugin's source, as the firmware computes it (FNV-1a, 32 bits). */
function sourceHash(data) {
    let hash = 2166136261;
    for (const byte of Buffer.from(data)) hash = Math.imul(hash ^ byte, 16777619) >>> 0;
    return ('0000000' + hash.toString(16)).slice(-8);
}

const sourceOf = (title, version, extra) => '-- ' + title + '\nplugin = { name = "' + title + '", version = "' + version + '" }\n' + (extra || '');

/**
 * A pretend plugin directory, as it would sit on GitHub: an index and the files
 * it lists. Against makeDevice() it has one plugin that is the same as on the
 * device (hello), a newer one (weather 1.1), one that was changed on the device
 * (webhook) and one that is not installed (clock).
 */
function makeDirectory() {
    const files = {
        'hello.lua': 'plugin = { name = "Hello" }\n',
        'weather.lua': sourceOf('Weather', '1.1'),
        'webhook.lua': sourceOf('Webhook', '1.0', '-- as published\n'),
        'clock.lua': sourceOf('Clock', '1.0'),
    };
    const entry = (name, title, version, description) => ({
        name, title, version, description, author: 'DeskHog', file: name + '.lua',
        size: files[name + '.lua'].length, hash: sourceHash(files[name + '.lua']),
    });
    const index = {
        format: 1,
        plugins: [
            entry('clock', 'Clock', '1.0', 'The time in large digits'),
            entry('hello', 'Hello', '1.0', 'The smallest useful plugin'),
            entry('weather', 'Weather', '1.1', 'The temperature now, for any number of cities'),
            entry('webhook', 'Webhook', '1.0', 'A button that calls a web address'),
        ],
    };
    return { files, index };
}

function makeDevice() {
    const device = {
        requests: [],
        directory: { source: 'TimBroddin/DeskHog', default: 'TimBroddin/DeskHog' },
        internet: makeDirectory(),   // what the browser finds at the directory's address; null for "no internet"
        wifiConnected: true,
        plugins: [
            { name: 'friend', title: 'Max', multiCard: false, config: [field('messages', 'Your own messages, separated by |')], kinds: [{ key: 'main', name: 'Max', config: [] }] },
            { name: 'hello', title: 'Hello', multiCard: false, config: [], kinds: [{ key: 'main', name: 'Hello', config: [] }] },
            {
                name: 'webhook', title: 'Webhook', multiCard: false,
                config: [field('url', 'Webhook URL'), field('token', 'Token', { secret: true }), field('label', 'Button label', { default: 'Send' })],
                kinds: [{ key: 'main', name: 'Webhook', config: [] }],
            },
            {
                name: 'weather', title: 'Weather', multiCard: true,
                config: [field('unit', 'Unit', { default: 'C', options: ['C', 'F'] })],
                kinds: [{ key: 'city', name: 'City', config: [field('city', 'City', { default: 'Ghent' })] }],
            },
            {
                name: 'posthog', title: 'PostHog', multiCard: true,
                config: [field('host', 'Host', { default: 'https://eu.posthog.com' }), field('key', 'API key', { secret: true })],
                kinds: [
                    { key: 'funnel', name: 'Funnel', config: [field('id', 'Funnel ID')] },
                    { key: 'insight', name: 'Insight', config: [field('id', 'Insight ID'), field('compact', 'Compact', { type: 'boolean', default: 'false' })] },
                ],
            },
            { name: 'broken', title: 'broken', multiCard: false, error: 'broken:3: unexpected symbol', config: [], kinds: [] },
        ],
        connections: [
            { id: 'hello', plugin: 'hello', name: 'Hello', settings: {}, secretsSet: [], state: 'running', memory: 12000 },
            { id: 'webhook', plugin: 'webhook', name: 'Webhook', settings: { url: 'http://example.com/hook', label: 'Deploy' }, secretsSet: ['token'], state: 'running', memory: 14000 },
            { id: 'weather', plugin: 'weather', name: 'Weather', settings: { unit: 'C' }, secretsSet: [], state: 'running', memory: 30000 },
            { id: 'posthog', plugin: 'posthog', name: 'Work', settings: { host: 'https://eu.posthog.com' }, secretsSet: ['key'], state: 'failed', error: 'posthog:12: 401 unauthorized' },
        ],
        cards: [
            { type: 'PLUGIN', config: 'weather:1', name: 'City', plugin: 'weather', connection: 'weather', card: '1', kind: 'city', settings: { city: 'Lisbon' }, title: 'Lisbon', frames: 3, onScreen: true },
            { type: 'PLUGIN', config: 'weather:2', name: 'Home', plugin: 'weather', connection: 'weather', card: '2', kind: 'city', settings: { city: 'Ghent' }, title: 'Ghent', frames: 1, onScreen: false },
            { type: 'PLUGIN', config: 'hello:1', name: 'Hello', plugin: 'hello', connection: 'hello', card: '1', kind: 'main', settings: {}, frames: 2, onScreen: false },
            { type: 'PLUGIN', config: 'webhook:1', name: 'Webhook', plugin: 'webhook', connection: 'webhook', card: '1', kind: 'main', settings: {}, frames: 2, onScreen: false },
            { type: 'PLUGIN', config: 'posthog:1', name: 'Insight', plugin: 'posthog', connection: 'posthog', card: '1', kind: 'insight', settings: { id: '42' }, frames: 1, onScreen: false },
        ],
    };

    const json = (data, status) => ({ status: status || 200, body: JSON.stringify(data), type: 'application/json' });

    // What each plugin's file says, and what the firmware reads from it.
    const sources = { hello: 'plugin = { name = "Hello" }\n', weather: sourceOf('Weather', '1.0'), webhook: sourceOf('Webhook', '1.0', '-- edited on the device\n') };
    for (const plugin of device.plugins) {
        plugin.source = sources[plugin.name] || sourceOf(plugin.title, '1.0');
        plugin.version = '1.0';
        plugin.hash = sourceHash(plugin.source);
    }

    device.state = () => ({
        directory: device.directory,
        plugins: device.plugins.map(plugin => { const { source, ...rest } = plugin; return rest; }),
        connections: device.connections.map(c => Object.assign({ cards: device.cards.filter(card => card.connection === c.id).length }, c)),
        cards: device.cards.map(c => Object.assign({ key: c.type + '|' + c.config }, c)),
        storage: { used: 45056, total: 1048576 },
        system: { plugin_cards: 5, heap_free: 80000, psram_free: 1400000 },
    });

    // A card's pixels (RGB565). The real thing is a picture; this is a stand-in
    // with one colour per card, so the page has something to show.
    device.picture = key => {
        const picture = Buffer.alloc(233 * 135 * 2);
        const card = device.cards.find(c => c.config === key);
        let hash = 7;
        for (const character of String(key)) hash = (hash * 31 + character.charCodeAt(0)) >>> 0;
        const color = (((hash >> 3) & 0x1f) << 11) | (((hash >> 9) & 0x3f) << 5) | ((hash >> 16) & 0x1f) | 0x0841;
        const bar = 30 + ((card && card.frames) || 0) * 17 % 150;
        for (let y = 0; y < 135; y++) {
            for (let x = 0; x < 233; x++) {
                const lit = (y > 18 && y < 30 && x > 16 && x < 110) || (y > 48 && y < 96 && x > 16 && x < 16 + bar);
                if (lit) picture.writeUInt16LE(y < 30 ? 0x8410 : color, (y * 233 + x) * 2);
            }
        }
        return picture;
    };

    device.handle = (method, url, body) => {
        const [pathname, query] = url.split('?');
        const params = new URLSearchParams(query || '');
        if (method === 'GET' && pathname === '/api/state') return json(device.state());
        if (method === 'GET' && pathname === '/api/status') {
            return json({
                firmware_version: '0.2.0',
                wifi: {
                    is_connected: device.wifiConnected, connected_ssid: 'Home WiFi', ip_address: '192.168.1.50',
                    networks: [{ ssid: 'Home WiFi', rssi: -50, encrypted: true }, { ssid: 'Home WiFi', rssi: -70, encrypted: true },
                        { ssid: '', rssi: -40, encrypted: true }, { ssid: 'Cafe', rssi: -60, encrypted: false }],
                },
            });
        }
        if (method === 'GET' && pathname === '/api/cards/screen') {
            const card = device.cards.find(c => c.config === params.get('key'));
            const frames = String((card && card.frames) || 0);
            // The page says which picture it has; one it already has is not sent again.
            if (params.get('since') === frames) return { status: 204, body: '', headers: { 'X-Frames': frames } };
            const headers = { 'X-Width': '233', 'X-Height': '135', 'X-Frames': frames };
            let picture = device.picture(params.get('key'));
            if (params.get('packed')) {
                // As the firmware does: runs of equal pixels, (how many, pixel).
                const runs = [];
                for (let at = 0; at < picture.length; ) {
                    let run = 1;
                    while (run < 255 && at + run * 2 < picture.length && picture.readUInt16LE(at + run * 2) === picture.readUInt16LE(at)) run++;
                    runs.push(run, picture[at], picture[at + 1]);
                    at += run * 2;
                }
                picture = Buffer.from(runs);
                headers['X-Packed'] = 'runs';
            }
            return { status: 200, body: picture, type: 'application/octet-stream', headers };
        }
        if (method === 'GET' && pathname === '/api/plugins/hello/status') return json({ name: 'hello', state: 'running', memory: 12288, error: '', log: ['hi', 'there'] });

        if (method === 'POST' && pathname === '/api/cards/add') {
            const plugin = device.plugins.find(p => p.name === body.plugin);
            if (!plugin) return json({ ok: false, error: 'no plugin named ' + body.plugin }, 400);
            let connection = body.connection && device.connections.find(c => c.id === body.connection);
            if (!connection) {
                connection = { id: plugin.name + '-' + (device.connections.length + 1), plugin: plugin.name, name: body.connectionName || plugin.title, settings: body.connectionSettings || {}, secretsSet: [], state: 'running' };
                device.connections.push(connection);
            }
            const id = String(device.cards.filter(c => c.connection === connection.id).length + 1);
            device.cards.push({ type: 'PLUGIN', config: connection.id + ':' + id, name: body.name || (plugin.multiCard ? plugin.kinds.find(k => k.key === body.kind).name : plugin.title), plugin: plugin.name, connection: connection.id, card: id, kind: body.kind, settings: body.settings || {}, frames: 0, onScreen: false });
            return json({ ok: true, key: connection.id + ':' + id });
        }
        if (method === 'POST' && pathname === '/api/cards/update') {
            const card = device.cards.find(c => c.config === params.get('key'));
            if (!card) return json({ ok: false, error: 'no such card' }, 404);
            if (body.name) card.name = body.name;
            Object.assign(card.settings, body.settings || {});
            return json({ ok: true });
        }
        if (method === 'POST' && pathname === '/api/cards/layout') {
            device.cards = body.map(key => device.cards.find(c => c.type + '|' + c.config === key)).filter(Boolean);
            return json({ ok: true });
        }
        if (method === 'POST' && pathname === '/api/connections/update') {
            const connection = device.connections.find(c => c.id === params.get('id'));
            if (!connection) return json({ ok: false, error: 'no such connection' }, 404);
            if (body.name) connection.name = body.name;
            Object.assign(connection.settings, body.settings || {});
            return json({ ok: true });
        }
        if (method === 'POST' && pathname === '/api/connections/remove') {
            device.connections = device.connections.filter(c => c.id !== params.get('id'));
            device.cards = device.cards.filter(c => c.connection !== params.get('id'));
            return json({ ok: true });
        }
        if (method === 'POST' && pathname === '/api/cards/show') {
            for (const card of device.cards) card.onScreen = card.config === params.get('key');
            return json({ ok: true });
        }
        if (method === 'POST' && pathname === '/api/cards/press') {
            if (!['up', 'center', 'down'].includes(params.get('button'))) return json({ ok: false, error: 'button is up, center or down' }, 400);
            const card = device.cards.find(c => c.config === params.get('key'));
            if (card) card.frames = (card.frames || 0) + 1;
            return json({ ok: true });
        }
        if (method === 'DELETE' && pathname.startsWith('/api/plugins/')) {
            const name = pathname.split('/').pop();
            device.plugins = device.plugins.filter(p => p.name !== name);
            device.connections = device.connections.filter(c => c.plugin !== name);
            device.cards = device.cards.filter(c => c.plugin !== name);
            return json({ ok: true });
        }
        if (method === 'GET' && /^\/api\/plugins\/[a-z0-9_-]+$/.test(pathname)) {
            const plugin = device.plugins.find(p => p.name === pathname.split('/').pop());
            return plugin ? { status: 200, body: plugin.source, type: 'text/plain' } : json({ ok: false, error: 'no such plugin' }, 404);
        }
        if (method === 'GET' && /^\/api\/plugins\/[a-z0-9_-]+\/status$/.test(pathname)) return json({ name: pathname.split('/')[3], state: 'stopped', memory: 0, error: '', log: [] });
        if (method === 'POST' && pathname === '/api/actions/start-wifi-scan') return json({ status: 'queued' }, 202);
        if (method === 'PUT' && pathname.startsWith('/api/plugins/')) {
            const name = pathname.split('/').pop();
            const source = Buffer.from(body).toString('utf8');
            let plugin = device.plugins.find(p => p.name === name);
            if (!plugin) {
                plugin = { name, multiCard: false, config: [], kinds: [{ key: 'main', name, config: [] }] };
                device.plugins.push(plugin);
                // As the firmware does: a plugin nothing shows yet gets a first card, unless asked not to.
                if (params.get('card') !== 'no') {
                    device.connections.push({ id: name, plugin: name, name, settings: {}, secretsSet: [], state: 'running' });
                    device.cards.push({ type: 'PLUGIN', config: name + ':1', name, plugin: name, connection: name, card: '1', kind: 'main', settings: {}, frames: 0, onScreen: false });
                }
            }
            // As the firmware does: read the plugin's own description of itself.
            const title = /name\s*=\s*"([^"]*)"/.exec(source);
            const version = /version\s*=\s*"([^"]*)"/.exec(source);
            plugin.title = title ? title[1] : name;
            if (!plugin.multiCard) plugin.kinds[0].name = plugin.title;
            plugin.version = version ? version[1] : undefined;
            plugin.source = source;
            plugin.hash = sourceHash(source);
            return json({ ok: true, name });
        }
        if (method === 'POST' && pathname === '/api/directory') {
            device.directory.source = body.source || device.directory.default;
            return json({ ok: true, source: device.directory.source });
        }
        if (method === 'POST' && pathname === '/api/actions/save-wifi') return json({ status: 'queued' }, 202);
        return json({ ok: false, error: 'not found: ' + method + ' ' + pathname }, 404);
    };
    // The browser's own requests to the plugin directory (not to the device).
    device.fetchOutside = url => {
        device.outside.push(url);
        if (!device.internet) return null; // no connection
        const base = 'https://raw.githubusercontent.com/TimBroddin/DeskHog/HEAD/plugins/';
        const path = url.startsWith(base) ? url.slice(base.length) : url.startsWith('https://plugins.example/') ? url.slice('https://plugins.example/'.length) : null;
        if (path === 'index.json') return { status: 200, body: JSON.stringify(device.internet.index) };
        if (path !== null && device.internet.files[path] !== undefined) return { status: 200, body: device.internet.files[path] };
        return { status: 404, body: 'Not found' };
    };
    device.outside = [];

    return device;
}

module.exports = { makeDevice, makeDirectory, sourceHash };
