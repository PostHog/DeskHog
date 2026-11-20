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
        screenshot: name === 'hello' ? undefined : 'screenshots/' + name + '.png',   // not every plugin has a picture
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

/**
 * Give a pretend device a plugin that uses the newer parts of the API: settings
 * whose choices the plugin lists (`choices`), and messages pushed to it. It has
 * one connection ("homey") with one card ("homey:1").
 */
function addHomey(device) {
    device.plugins.push({
        name: 'homey', title: 'Homey', multiCard: true, message: true,
        config: [field('url', 'Address'), field('home', 'Home', { choices: true })],
        kinds: [
            { key: 'device', name: 'Device', message: true, config: [field('device', 'Device', { choices: true }), field('label', 'Label')] },
            { key: 'flow', name: 'Flow', config: [field('flow', 'Flow', { choices: true })] },
        ],
        source: 'plugin = { name = "Homey" }\n', version: '1.0', hash: sourceHash('plugin = { name = "Homey" }\n'),
    });
    device.connections.push({ id: 'homey', plugin: 'homey', name: 'Homey', settings: { url: 'http://homey.local' }, secretsSet: [], state: 'running', memory: 20000 });
    device.cards.push({ type: 'PLUGIN', config: 'homey:1', name: 'Device', plugin: 'homey', connection: 'homey', card: '1', kind: 'device', settings: { device: 'lamp' }, frames: 1, onScreen: false });
    // What the plugin answers, by "<connection>/<kind>/<field>". A string is an error it reports.
    device.choices['homey/device/device'] = [{ value: 'lamp', label: 'Kitchen lamp' }, { value: 'plug', label: 'Coffee machine' }];
    device.choices['homey/flow/flow'] = 'log in to Homey first';
    device.choices['homey//home'] = [{ value: 'h1', label: 'Home' }];
    return device;
}

function makeDevice() {
    const device = {
        requests: [],
        notifications: [],   // what plugins said with notify() that nobody has seen yet: {card, text, count, age}
        messages: [],        // what was pushed to plugins: {card | connection, body}
        choices: {},         // lists plugins offer for settings, see addHomey()
        choiceForms: [],     // what each list was asked with: {key, form}, the form being the page's &settings=
        choicePolls: {},     // how often each was asked for: the first answer is "pending", as on the device
        directory: { source: 'TimBroddin/DeskHog', default: 'TimBroddin/DeskHog' },
        timezone: { posix: 'UTC0', name: '' },   // as on a new device
        restarts: 0,         // how often the page asked for a restart
        // The firmware and its updater. `inUpdater` is the device after POST /api/update/start;
        // `failUploads` makes that many uploads fail, as a broken connection would.
        firmware: { version: '0.4.0', layout: 2, repo: 'TimBroddin/DeskHog', inUpdater: false, failUploads: 0, installed: [] },
        release: { version: '0.5.0', layout: 2, size: 12, sha256: 'abc123', file: 'deskhog-update.bin', notes: 'https://example.com/notes' },
        device: {            // the device's own settings, as a new one has them; see POST /api/device
            name: 'deskhog',
            screen: { day: 80, night: 10 },
            led: { day: 100, night: 0 },
            night: { enabled: false, from: '22:00', until: '07:00' },
            rotate: { seconds: 0 },
            battery: false,
            status: { night: false, manual: false, battery: null, gauge: { percent: 100, volts: 4.2, charging: false } },
        },
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
                kinds: [{ key: 'city', name: 'City', config: [field('city', 'City', { default: 'Antwerp' })] }],
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
            { type: 'PLUGIN', config: 'weather:2', name: 'Home', plugin: 'weather', connection: 'weather', card: '2', kind: 'city', settings: { city: 'Antwerp' }, title: 'Antwerp', frames: 1, onScreen: false },
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
        timezone: device.timezone,
        device: deviceNow(),
        plugins: device.plugins.map(plugin => { const { source, ...rest } = plugin; return rest; }),
        connections: device.connections.map(c => Object.assign({ cards: device.cards.filter(card => card.connection === c.id).length }, c)),
        cards: device.cards.map(c => Object.assign({ key: c.type + '|' + c.config, front: true, rotate: true }, c)),
        notifications: device.notifications,
        storage: { used: 45056, total: 1048576 },
        system: { plugin_cards: 5, heap_free: 80000, psram_free: 1400000 },
    });

    // As the firmware does: the object as it is now, with the battery as the gauge reports it when there is one.
    const deviceNow = () => {
        const now = JSON.parse(JSON.stringify(device.device));
        now.status.battery = now.battery && now.status.gauge ? now.status.gauge : null;
        if (device.firmware) {
            Object.assign(now.status, { firmware: device.firmware.version, layout: device.firmware.layout, update_repo: device.firmware.repo, firmware_room: 2228224 });
        }
        return now;
    };
    // As the firmware does: kinds are checked strictly, nothing is changed unless everything is right.
    const wholeNumber = (value, low, high) => typeof value === 'number' && Number.isInteger(value) && value >= low && value <= high;
    const clock = value => typeof value === 'string' && /^([01]\d|2[0-3]):[0-5]\d$/.test(value);
    const checks = {
        name: value => typeof value === 'string' && /^[a-z0-9]([a-z0-9-]{0,22}[a-z0-9])?$/.test(value),
        screen: { day: value => wholeNumber(value, 5, 100), night: value => wholeNumber(value, 0, 100) },
        led: { day: value => wholeNumber(value, 0, 100), night: value => wholeNumber(value, 0, 100) },
        night: { enabled: value => typeof value === 'boolean', from: clock, until: clock },
        rotate: { seconds: value => value === 0 || wholeNumber(value, 10, 600) },
        battery: value => typeof value === 'boolean',
        preview: value => value === 'night' || value === 'day',
    };
    const postDevice = body => {
        const isObject = value => value && typeof value === 'object' && !Array.isArray(value);
        if (!isObject(body)) return json({ ok: false, error: 'send a JSON object' }, 400);
        const changes = [];
        for (const [key, value] of Object.entries(body)) {
            const rule = checks[key];
            if (!rule) return json({ ok: false, error: 'unknown setting: ' + key }, 400);
            if (typeof rule === 'function') {
                if (!rule(value)) return json({ ok: false, error: key + ' is not valid' }, 400);
                if (key !== 'preview') changes.push([device.device, key, value]);
                continue;
            }
            if (!isObject(value)) return json({ ok: false, error: key + ' must be an object' }, 400);
            for (const [inner, innerValue] of Object.entries(value)) {
                if (!rule[inner]) return json({ ok: false, error: 'unknown setting: ' + key + '.' + inner }, 400);
                if (!rule[inner](innerValue)) return json({ ok: false, error: key + '.' + inner + ' is not valid' }, 400);
                changes.push([device.device[key], inner, innerValue]);
            }
        }
        for (const [where, key, value] of changes) where[key] = value;
        return json(deviceNow());
    };

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
        if (device.firmware && device.firmware.inUpdater) {
            // The updater answers only this much.
            const updater = { updater: true, version: '1', main_intact: true, slot_size: 2228224, error: '' };
            if (method === 'GET' && pathname === '/api/status') return json(updater);
            if (method === 'POST' && pathname === '/api/firmware') {
                if (device.firmware.failUploads > 0) {
                    device.firmware.failUploads--;
                    return json(Object.assign(updater, { main_intact: false, error: 'The connection was lost.' }), 400);
                }
                device.firmware.installed.push({ size: params.get('size'), sha256: params.get('sha256') });
                device.firmware.version = params.get('sha256') === device.release.sha256 ? device.release.version : 'custom';
                device.firmware.inUpdater = false;
                return json(updater);
            }
            return { status: 404, body: 'The updater has no such page.' };
        }
        if (method === 'POST' && pathname === '/api/update/start') {
            if (device.firmware.layout !== 2) return json({ ok: false, error: 'This device has the old flash layout' }, 409);
            device.firmware.inUpdater = true;
            return json({ ok: true });
        }
        if (method === 'GET' && pathname === '/api/state') return json(device.state());
        if (method === 'GET' && pathname === '/api/device') return json(deviceNow());
        if (method === 'POST' && pathname === '/api/device') return postDevice(body);
        if (method === 'POST' && pathname === '/api/restart') { device.restarts++; return json({ ok: true }); }
        if (method === 'GET' && pathname === '/api/status') {
            return json({
                firmware_version: '0.3.0',
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
            device.cards.push({ type: 'PLUGIN', config: connection.id + ':' + id, name: body.name || (plugin.multiCard ? plugin.kinds.find(k => k.key === body.kind).name : plugin.title), plugin: plugin.name, connection: connection.id, card: id, kind: body.kind, settings: body.settings || {}, front: body.front !== false, rotate: body.rotate !== false, frames: 0, onScreen: false });
            return json({ ok: true, key: connection.id + ':' + id });
        }
        if (method === 'POST' && pathname === '/api/cards/update') {
            const card = device.cards.find(c => c.config === params.get('key'));
            if (!card) return json({ ok: false, error: 'no such card' }, 404);
            if (body.name) card.name = body.name;
            if (typeof body.front === 'boolean') card.front = body.front;
            if (typeof body.rotate === 'boolean') card.rotate = body.rotate;
            Object.assign(card.settings, body.settings || {});
            return json({ ok: true });
        }
        if (pathname === '/api/cards/message' || pathname === '/api/connections/message') {
            // As the firmware does: the plugin's message() gets the body, if it has one.
            if (method !== 'POST') return json({ ok: false, error: 'POST the message as the request body' }, 405);
            const toCard = pathname === '/api/cards/message';
            const card = toCard ? device.cards.find(c => c.config === params.get('key')) : null;
            const connection = device.connections.find(c => c.id === (toCard ? card && card.connection : params.get('id')));
            if (!connection) return json({ ok: false, error: toCard ? 'no such card' : 'no such connection' }, 404);
            const plugin = device.plugins.find(p => p.name === connection.plugin);
            const kind = card && plugin ? plugin.kinds.find(k => k.key === card.kind) : null;
            const takes = plugin && (toCard ? (plugin.multiCard ? kind && kind.message : plugin.message) : plugin.message);
            if (!takes) return json({ ok: false, error: 'this plugin has no message() function' }, 404);
            const text = typeof body === 'string' ? body : Buffer.isBuffer(body) ? body.toString('utf8') : JSON.stringify(body === undefined ? '' : body);
            if (Buffer.byteLength(text) > 4096) return json({ ok: false, error: 'the message is larger than 4 KB' }, 413);
            if (device.messages.length >= 8) return json({ ok: false, error: 'too many messages are waiting for this plugin. Try again in a moment' }, 429);
            device.messages.push(toCard ? { card: card.config, body: text } : { connection: connection.id, body: text });
            return json({ ok: true });
        }
        if (method === 'GET' && pathname === '/api/connections/choices') {
            const connection = device.connections.find(c => c.id === params.get('id'));
            if (!connection) return json({ ok: false, error: 'no such connection' }, 404);
            const plugin = device.plugins.find(p => p.name === connection.plugin);
            const kind = params.get('kind') || '';
            const fields = !plugin ? [] : kind ? ((plugin.kinds.find(k => k.key === kind) || {}).config || []) : plugin.config;
            if (!fields.some(f => f.key === params.get('field') && f.choices)) return json({ ok: false, error: 'that setting has no list of choices' }, 404);
            const key = connection.id + '/' + kind + '/' + params.get('field');
            // As the firmware does: the first request starts the plugin on it, a later one gets the list.
            // &settings= is what the form holds, as a JSON object: the plugin's function gets it.
            let form = {};
            if (params.get('settings')) {
                try { form = JSON.parse(params.get('settings')); } catch (error) { form = null; }
                if (!form || typeof form !== 'object' || Array.isArray(form) || params.get('settings').length > 1024) {
                    return json({ ok: false, error: 'settings is a JSON object of at most 1 KB' }, 400);
                }
            }
            device.choicePolls[key] = (device.choicePolls[key] || 0) + 1;
            if (device.choicePolls[key] % 2 === 1) return json({ ok: true, pending: true }, 202);
            device.choiceForms.push({ key, form });
            // A function stands for a plugin whose list depends on the form.
            const answer = typeof device.choices[key] === 'function' ? device.choices[key](form) : device.choices[key];
            if (typeof answer === 'string') return json({ ok: false, error: answer }, 502);
            return json({ ok: true, choices: answer || [] });
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
        if (method === 'POST' && pathname === '/api/timezone') {
            // As the firmware does: a POSIX string, or nothing for UTC.
            if (body.posix && !/^(<[^>]+>|[A-Za-z]{3,})[-+]?\d/.test(body.posix)) return json({ ok: false, error: 'that is not a POSIX time zone' }, 400);
            device.timezone = body.posix ? { posix: body.posix, name: body.name || '' } : { posix: 'UTC0', name: '' };
            return json({ ok: true, posix: device.timezone.posix });
        }
        if (method === 'POST' && pathname === '/api/actions/save-wifi') return json({ status: 'queued' }, 202);
        return json({ ok: false, error: 'not found: ' + method + ' ' + pathname }, 404);
    };
    // The browser's own requests to the plugin directory (not to the device).
    device.fetchOutside = url => {
        device.outside.push(url);
        if (!device.internet) return null; // no connection
        const firmwareBase = 'https://raw.githubusercontent.com/TimBroddin/DeskHog/firmware/';
        if (url === firmwareBase + 'latest.json') return device.release ? { status: 200, body: JSON.stringify(device.release) } : { status: 404, body: 'Not found' };
        if (url === firmwareBase + 'deskhog-update.bin') return { status: 200, body: 'the firmware' };
        const base = 'https://raw.githubusercontent.com/TimBroddin/DeskHog/HEAD/plugins/';
        const path = url.startsWith(base) ? url.slice(base.length) : url.startsWith('https://plugins.example/') ? url.slice('https://plugins.example/'.length) : null;
        if (path === 'index.json') return { status: 200, body: JSON.stringify(device.internet.index) };
        if (path !== null && device.internet.files[path] !== undefined) return { status: 200, body: device.internet.files[path] };
        return { status: 404, body: 'Not found' };
    };
    device.outside = [];

    return device;
}

module.exports = { makeDevice, makeDirectory, addHomey, sourceHash };
