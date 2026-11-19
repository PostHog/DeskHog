// Tests for the web page, run by test/portal/run.sh.
//
// The page is loaded into jsdom and talks to a pretend device (below) that
// answers the same API the firmware serves.

const fs = require('fs');
const path = require('path');
const { JSDOM, VirtualConsole } = require('jsdom');

const root = process.argv[2];
const html = fs.readFileSync(path.join(root, 'html/portal.html'), 'utf8').replace('<script src="portal.js"></script>', '');
const script = fs.readFileSync(path.join(root, 'html/portal.js'), 'utf8');
const { makeDevice, addHomey, sourceHash } = require('./pretend-device.js');

// --------------------------------------------------------------- harness

let failures = 0;
let checks = 0;
function check(condition, what) {
    checks++;
    if (!condition) {
        failures++;
        console.log('  FAIL ' + what);
    }
}
function equal(actual, expected, what) {
    checks++;
    if (JSON.stringify(actual) !== JSON.stringify(expected)) {
        failures++;
        console.log('  FAIL ' + what + '\n    got:      ' + JSON.stringify(actual) + '\n    expected: ' + JSON.stringify(expected));
    }
}

async function openPage(device, options) {
    // jsdom has no canvas; the page copes, so its "not implemented" notes are just noise.
    const quiet = new VirtualConsole();
    options = options || {};
    const dom = new JSDOM(html, { runScripts: 'dangerously', pretendToBeVisual: true, url: 'http://deskhog.local/' + (options.hash || ''), virtualConsole: quiet });
    const window = dom.window;
    // jsdom has no idea how wide it is. Tests say whether the page is on a desktop.
    window.matchMedia = () => ({ matches: !!options.wide });
    window.fetch = async (url, options) => {
        options = options || {};
        const method = options.method || 'GET';
        let body = options.body;
        const type = options.headers && options.headers['Content-Type'];
        if (typeof body === 'string' && type === 'application/json') body = JSON.parse(body);
        if (body instanceof window.FormData) body = Object.fromEntries(body.entries());
        if (body instanceof window.Uint8Array) body = Buffer.from(body).toString('utf8');
        let answer;
        if (/^https?:/.test(url)) {
            // Not the device: the plugin directory, somewhere on the internet.
            answer = device.fetchOutside(url);
            if (!answer) throw new TypeError('Failed to fetch');
        } else {
            device.requests.push({ method, url, body, type });
            answer = device.handle(method, url, body);
        }
        return {
            ok: answer.status >= 200 && answer.status < 300,
            status: answer.status,
            headers: { get: name => (answer.headers || {})[name] || null },
            text: async () => String(answer.body),
            arrayBuffer: async () => new window.Uint8Array(Buffer.from(answer.body)).buffer,
        };
    };
    window.confirm = () => true;
    window.Element.prototype.scrollIntoView = () => {};
    const errors = [];
    window.addEventListener('error', event => errors.push(event.message));
    // As a real <script>, so the page's functions are global like in a browser.
    const tag = window.document.createElement('script');
    tag.textContent = script;
    window.document.body.appendChild(tag);
    await settle(window);
    return { window, document: window.document, errors };
}

/** Let pending fetches and their follow-ups finish. */
async function settle(window) {
    for (let i = 0; i < 12; i++) await new Promise(resolve => window.setTimeout(resolve, 0));
}

const text = node => (node ? node.textContent : null);
const titles = document => [...document.querySelectorAll('#cards-list .item-title')].map(text);
const tile = (document, key) => document.querySelector('#cards-list .item[data-key="' + key + '"] .item-open');
const visiblePage = document => [...document.querySelectorAll('.page')].filter(page => !page.hidden).map(page => page.id);
const click = async (window, node) => { node.click(); await settle(window); };
const buttonNamed = (scope, label) => [...scope.querySelectorAll('button')].find(b => b.textContent.trim().startsWith(label));
const lastRequest = (device, url) => [...device.requests].reverse().find(r => r.url.startsWith(url));
const fieldNamed = (scope, label) => {
    const wrap = [...scope.querySelectorAll('label.field')].find(l => l.querySelector('span').textContent === label);
    return wrap ? wrap.querySelector('input, select, textarea') : null;
};

const tests = [];
const test = (name, body) => tests.push({ name, body });

// ------------------------------------------------------------------ tests

test('shows the cards with sensible names', async () => {
    const device = makeDevice();
    const { document, errors } = await openPage(device);
    // "City" is the default name, so the plugin's own title wins; "Home" was typed by someone.
    equal(titles(document), ['Lisbon', 'Home', 'Hello', 'Webhook', 'Insight'], 'card titles');
    const subs = [...document.querySelectorAll('#cards-list .item-sub')].map(text);
    equal(subs[0], 'Weather, on screen now', 'subtitle of the visible weather card');
    equal(subs[1], 'Weather', 'subtitle of a named card');
    equal(subs[2], '', 'a single-card plugin needs no subtitle');
    check(subs[4].startsWith('Crashed: posthog:12: 401'), 'a crashed connection says why on its card');
    check(document.querySelector('#cards-list .item').classList.contains('live'), 'the card on screen is marked');
    equal(text(document.getElementById('wifi-pill')), 'Home WiFi, 192.168.1.50', 'wifi pill');
    check(document.getElementById('wifi-setup').hidden, 'no wifi setup panel while connected');
    check(document.querySelectorAll('#cards-list canvas').length === 5, 'one picture per card');
    check(device.requests.some(r => r.url.startsWith('/api/cards/screen?packed=1&key=weather%3A1')), 'fetches card pictures, packed');
    check(document.getElementById('card-pane').hidden, 'on a phone nothing is open until you pick a card');
    equal(errors, [], 'no script errors');
});

test('unpacks the pictures the device sends', async () => {
    const { window } = await openPage(makeDevice());
    const packed = new window.Uint8Array([2, 0x34, 0x12, 1, 0xff, 0xff, 200, 0x01, 0x00]);
    const pixels = new window.Uint16Array(window.unpackRuns(packed.buffer, 5));
    equal([...pixels], [0x1234, 0x1234, 0xffff, 1, 1], 'runs become pixels, and never more than the picture holds');
});

test('has four pages and remembers which one in the address', async () => {
    const { window, document } = await openPage(makeDevice());
    equal(visiblePage(document), ['page-cards'], 'starts on the cards');
    await click(window, document.querySelector('.tabs a[data-page="plugins"]'));
    equal(visiblePage(document), ['page-plugins'], 'a tab switches page');
    equal(window.location.hash, '#plugins', 'the address follows');
    equal(document.querySelector('.tabs a[aria-current="page"]').textContent, 'Plugins', 'the tab is marked');
    await click(window, document.getElementById('wifi-pill'));
    equal(visiblePage(document), ['page-device'], 'the wifi pill leads to the device page');

    const reopened = await openPage(makeDevice(), { hash: '#connections' });
    equal(visiblePage(reopened.document), ['page-connections'], 'opening with an address goes straight there');
    const odd = await openPage(makeDevice(), { hash: '#nonsense' });
    equal(visiblePage(odd.document), ['page-cards'], 'an unknown address falls back to the cards');
});

test('on a wide screen the pane starts with the card that is on the device', async () => {
    const device = makeDevice();
    device.cards[0].onScreen = false;
    device.cards[3].onScreen = true;
    const { window, document } = await openPage(device, { wide: true });
    const pane = document.getElementById('card-pane');
    check(!pane.hidden, 'the pane is open');
    equal(text(pane.querySelector('.pane-head h2')), 'Webhook', 'with the card on screen');
    check(tile(document, 'PLUGIN|webhook:1').parentNode.classList.contains('selected'), 'its tile is marked');
    check(pane.querySelector('.device canvas') !== null, 'it shows the device');

    await click(window, pane.querySelector('.pane-head .close'));
    check(pane.hidden, 'closing it works');
    await window.refresh();
    check(pane.hidden, 'and it stays closed');
});

test('the buttons of the device work from the page', async () => {
    const device = makeDevice();
    const { window, document } = await openPage(device);
    await click(window, tile(document, 'PLUGIN|hello:1'));
    const pane = document.getElementById('card-pane');
    check(text(pane.querySelector('.pane-status')).startsWith('Not on screen'), 'says the card is not showing');
    equal([...pane.querySelectorAll('.hw')].map(b => b.getAttribute('aria-label')), ['Up button', 'Centre button', 'Down button'], 'three buttons');

    device.requests.length = 0;
    await click(window, pane.querySelector('.hw[aria-label="Centre button"]'));
    const calls = device.requests.filter(r => r.method === 'POST').map(r => r.url);
    equal(calls, ['/api/cards/show?key=hello%3A1', '/api/cards/press?key=hello%3A1&button=center'], 'brings the card up, then presses');
    check(document.getElementById('notice').hidden, 'and the device accepts it');
    await window.refresh();
    check(text(pane.querySelector('.pane-status')).startsWith('On the device'), 'now it is on screen');

    // The open card is watched, but its picture is only sent again once it has changed.
    await settle(window); // the new picture has arrived
    await window.watchOpenCard();
    await settle(window);
    equal(lastRequest(device, '/api/cards/screen').url, '/api/cards/screen?packed=1&key=hello%3A1&since=' + device.cards[2].frames, 'asks only for a newer picture');

    device.requests.length = 0;
    await click(window, pane.querySelector('.hw[aria-label="Up button"]'));
    equal(device.requests.filter(r => r.method === 'POST').map(r => r.url), ['/api/cards/press?key=hello%3A1&button=up'], 'a card on screen is just pressed');
});

test('lists only connections that have shared settings', async () => {
    const { document } = await openPage(makeDevice());
    equal([...document.querySelectorAll('#connections-list .item-title')].map(text), ['Weather', 'Work'], 'connection names');
    const subs = [...document.querySelectorAll('#connections-list .item-sub')].map(text);
    equal(subs[0], 'Weather, 2 cards', 'card count');
    check(subs[1].startsWith('Crashed:'), 'crashed connection');
    check(document.getElementById('connections-empty').hidden, 'no empty note');

    const device = makeDevice();
    device.plugins = device.plugins.filter(p => !p.multiCard);
    device.connections = device.connections.filter(c => c.plugin === 'hello' || c.plugin === 'webhook');
    device.cards = device.cards.filter(c => c.plugin === 'hello' || c.plugin === 'webhook');
    const simple = await openPage(device);
    equal(simple.document.querySelectorAll('#connections-list .item').length, 0, 'none listed when nothing is shared');
    check(!simple.document.getElementById('connections-empty').hidden, 'and the page says why');
});

test('adds a card to an existing connection', async () => {
    const device = makeDevice();
    const { window, document } = await openPage(device);
    await click(window, document.getElementById('add-card-btn'));
    const pane = document.getElementById('card-pane');
    check(!pane.hidden, 'the steps open in the pane');
    const choices = [...pane.querySelectorAll('.choices:not(.more) .choice')].map(c => c.querySelector('b').textContent);
    equal(choices, ['Max', 'Hello', 'Webhook', 'City', 'Funnel', 'Insight'], 'choices offered: a card of anything that is installed');
    check(!choices.includes('broken'), 'a plugin that does not load is not offered');
    check(!pane.textContent.includes('null') && !pane.textContent.includes('undefined'), 'nothing stray in the pane');

    await click(window, buttonNamed(pane, 'City'));
    equal(fieldNamed(pane, 'Connection').value, 'weather', 'the existing connection is preselected');
    check(fieldNamed(pane, 'Unit').closest('div[hidden]') !== null, 'shared fields stay hidden for an existing connection');
    fieldNamed(pane, 'City').value = 'Oslo';
    await click(window, buttonNamed(pane, 'Add card'));
    equal(lastRequest(device, '/api/cards/add').body, { plugin: 'weather', kind: 'city', name: '', settings: { city: 'Oslo' }, connection: 'weather' }, 'request');
    check(pane.hidden, 'on a phone the pane closes');
    equal(titles(document).length, 6, 'the new card is listed');
});

test('on a wide screen a new card goes straight into the pane', async () => {
    const device = makeDevice();
    const { window, document } = await openPage(device, { wide: true });
    await click(window, document.getElementById('add-card-btn'));
    const pane = document.getElementById('card-pane');
    await click(window, buttonNamed(pane, 'Max'));
    await click(window, buttonNamed(pane, 'Back'));
    check(buttonNamed(pane, 'Webhook') !== undefined, 'back returns to the choices');
    await click(window, buttonNamed(pane, 'Max'));
    await click(window, buttonNamed(pane, 'Add card'));
    check(!pane.hidden, 'the pane stays');
    equal(text(pane.querySelector('.pane-head h2')), 'Max', 'and shows the new card');
    check(fieldNamed(pane, 'Your own messages, separated by |') !== null, 'with its settings');
});

test('adds a card with a new connection', async () => {
    const device = makeDevice();
    const { window, document } = await openPage(device);
    await click(window, document.getElementById('add-card-btn'));
    const pane = document.getElementById('card-pane');
    await click(window, buttonNamed(pane, 'Insight'));
    const select = fieldNamed(pane, 'Connection');
    equal([...select.options].map(o => o.textContent), ['Work', 'A new connection…'], 'connection choices');
    select.value = '';
    select.dispatchEvent(new window.Event('change'));
    fieldNamed(pane, 'Name for this connection').value = 'Client X';
    fieldNamed(pane, 'API key').value = 'phx_123';
    fieldNamed(pane, 'Insight ID').value = '777';
    pane.querySelector('input[data-key="compact"]').checked = true;
    fieldNamed(pane, 'Name (optional)').value = 'Signups';
    await click(window, buttonNamed(pane, 'Add card'));
    equal(lastRequest(device, '/api/cards/add').body, {
        plugin: 'posthog', kind: 'insight', name: 'Signups', settings: { id: '777', compact: 'true' },
        connection: '', connectionName: 'Client X', connectionSettings: { host: 'https://eu.posthog.com', key: 'phx_123' },
    }, 'request');
});

test('adds a single-card plugin: its settings are the card\'s settings', async () => {
    const device = makeDevice();
    const { window, document } = await openPage(device);
    await click(window, document.getElementById('add-card-btn'));
    const pane = document.getElementById('card-pane');
    await click(window, buttonNamed(pane, 'Webhook'));
    check(fieldNamed(pane, 'Connection') === null, 'no talk of connections');
    fieldNamed(pane, 'Webhook URL').value = 'http://example.com/2';
    await click(window, buttonNamed(pane, 'Add card'));
    equal(lastRequest(device, '/api/cards/add').body, { plugin: 'webhook', kind: 'main', name: '', connectionSettings: { url: 'http://example.com/2', label: 'Send' } }, 'request');
});

test('edits a card of a multi-card plugin', async () => {
    const device = makeDevice();
    const { window, document } = await openPage(device);
    await click(window, tile(document, 'PLUGIN|weather:1'));
    const pane = document.getElementById('card-pane');
    equal(fieldNamed(pane, 'Name').value, '', 'a default name shows as empty');
    equal(fieldNamed(pane, 'City').value, 'Lisbon', 'saved setting shown');
    check(fieldNamed(pane, 'Unit') === null, 'shared settings are not on the card');
    check(pane.textContent.includes('Shares the settings of "Weather"'), 'says which connection it uses');
    fieldNamed(pane, 'City').value = 'Porto';
    fieldNamed(pane, 'Name').value = 'Holiday';
    await click(window, buttonNamed(pane, 'Save'));
    equal(lastRequest(device, '/api/cards/update').url, '/api/cards/update?key=weather%3A1', 'url');
    equal(lastRequest(device, '/api/cards/update').body, { name: 'Holiday', settings: { city: 'Porto' }, front: true, rotate: true }, 'body');
    check(!device.requests.some(r => r.url.startsWith('/api/connections/update')), 'the connection is left alone');
    equal(titles(document)[0], 'Holiday', 'new name shown');
    check(pane.hidden, 'on a phone saving goes back to the list');
});

test('a card leads to its connection, and a connection to its cards', async () => {
    const { window, document } = await openPage(makeDevice());
    await click(window, tile(document, 'PLUGIN|weather:2'));
    await click(window, document.querySelector('#card-pane .hint a'));
    equal(visiblePage(document), ['page-connections'], 'the link opens the connections page');
    const pane = document.getElementById('connection-pane');
    check(!pane.hidden, 'with that connection open');
    equal([...pane.querySelectorAll('.used-by button')].map(text), ['Lisbon', 'Home'], 'which lists its cards');
    await click(window, buttonNamed(pane, 'Lisbon'));
    equal(visiblePage(document), ['page-cards'], 'a card name goes back to the cards');
    equal(text(document.querySelector('#card-pane .pane-head h2')), 'Lisbon', 'with that card open');
});

test('edits a single-card plugin: secrets stay unless retyped', async () => {
    const device = makeDevice();
    const { window, document } = await openPage(device);
    await click(window, tile(document, 'PLUGIN|webhook:1'));
    const pane = document.getElementById('card-pane');
    equal(fieldNamed(pane, 'Webhook URL').value, 'http://example.com/hook', 'saved value');
    equal(fieldNamed(pane, 'Token').value, '', 'a secret is never shown');
    equal(fieldNamed(pane, 'Token').placeholder, 'Saved. Leave empty to keep it.', 'but it says one is saved');
    fieldNamed(pane, 'Button label').value = 'Ship';
    await click(window, buttonNamed(pane, 'Save'));
    equal(lastRequest(device, '/api/connections/update').body, { settings: { url: 'http://example.com/hook', label: 'Ship' } }, 'the untouched secret is not sent');
    equal(lastRequest(device, '/api/cards/update').body, { name: 'Webhook', settings: {}, front: true, rotate: true }, 'card request');
});

test('keeps what you typed while the page refreshes', async () => {
    const device = makeDevice();
    const { window, document } = await openPage(device);
    await click(window, tile(document, 'PLUGIN|weather:1'));
    const pane = document.getElementById('card-pane');
    fieldNamed(pane, 'City').value = 'Half typed';
    device.cards[0].title = 'Lisboa';
    await window.refresh();
    equal(fieldNamed(pane, 'City').value, 'Half typed', 'input survives');
    equal(titles(document)[0], 'Lisboa', 'while live parts still update');
    equal(text(pane.querySelector('.pane-head h2')), 'Lisboa', 'in the pane too');
});

test('moves a card earlier and later', async () => {
    const device = makeDevice();
    const { window, document } = await openPage(device);
    await click(window, tile(document, 'PLUGIN|weather:1'));
    const pane = document.getElementById('card-pane');
    check(buttonNamed(pane, 'Move earlier').disabled, 'the first card cannot move earlier');
    fieldNamed(pane, 'City').value = 'Typed';
    await click(window, buttonNamed(pane, 'Move later'));
    equal(lastRequest(device, '/api/cards/layout').body, ['PLUGIN|weather:2', 'PLUGIN|weather:1', 'PLUGIN|hello:1', 'PLUGIN|webhook:1', 'PLUGIN|posthog:1'], 'new order sent');
    equal(titles(document).slice(0, 2), ['Home', 'Lisbon'], 'and shown');
    equal(fieldNamed(pane, 'City').value, 'Typed', 'without losing what was typed');
    check(!buttonNamed(pane, 'Move earlier').disabled, 'now it can move back');
});

test('removes a card', async () => {
    const device = makeDevice();
    const { window, document } = await openPage(device);
    await click(window, tile(document, 'PLUGIN|hello:1'));
    await click(window, buttonNamed(document.getElementById('card-pane'), 'Remove card'));
    equal(lastRequest(device, '/api/cards/layout').body, ['PLUGIN|weather:1', 'PLUGIN|weather:2', 'PLUGIN|webhook:1', 'PLUGIN|posthog:1'], 'layout without the card');
    check(!titles(document).includes('Hello'), 'gone from the list');
    check(document.getElementById('card-pane').hidden, 'and the pane closes');
});

test('a card of a kind that no longer exists can only be removed', async () => {
    const device = makeDevice();
    device.cards.push({ type: 'INSIGHT', config: '99', name: 'Old insight' });
    const { window, document } = await openPage(device);
    equal(text(document.querySelector('#cards-list .item:last-child .item-sub')), 'No longer supported', 'it says so');
    await click(window, tile(document, 'INSIGHT|99'));
    const pane = document.getElementById('card-pane');
    check(pane.querySelector('.device') === null && buttonNamed(pane, 'Save') === undefined, 'nothing to set');
    await click(window, buttonNamed(pane, 'Remove card'));
    equal(lastRequest(device, '/api/cards/layout').body.length, 5, 'removed');
});

test('edits and deletes a connection', async () => {
    const device = makeDevice();
    const { window, document } = await openPage(device, { hash: '#connections' });
    await click(window, document.querySelector('#connections-list .item[data-id="posthog"] .item-open'));
    const pane = document.getElementById('connection-pane');
    equal(fieldNamed(pane, 'Name').value, 'Work', 'name');
    fieldNamed(pane, 'API key').value = 'phx_new';
    await click(window, buttonNamed(pane, 'Save'));
    equal(lastRequest(device, '/api/connections/update').body, { name: 'Work', settings: { host: 'https://eu.posthog.com', key: 'phx_new' } }, 'update');

    await click(window, document.querySelector('#connections-list .item[data-id="weather"] .item-open'));
    equal(fieldNamed(pane, 'Unit').tagName, 'SELECT', 'options become a dropdown');
    await click(window, buttonNamed(pane, 'Delete connection'));
    equal(lastRequest(device, '/api/connections/remove').url, '/api/connections/remove?id=weather', 'remove');
    equal(titles(document), ['Hello', 'Webhook', 'Insight'], 'its cards are gone');
    check(pane.hidden, 'and the pane closes');
});

test('plugins page: lists plugins, edits and saves source', async () => {
    const device = makeDevice();
    const { window, document } = await openPage(device, { hash: '#plugins' });
    const rows = [...document.querySelectorAll('#plugins-list .item')];
    equal(rows.length, 6, 'all plugins listed');
    check(rows[5].textContent.includes('Does not load: broken:3'), 'load errors are shown');
    check(rows[3].textContent.includes('Cards: City'), 'card kinds are shown');
    check(document.getElementById('plugin-editor').hidden, 'no editor until a plugin is picked');

    await click(window, rows[1].querySelector('.item-open'));
    check(!document.getElementById('plugin-editor').hidden, 'the editor opens');
    equal(text(document.getElementById('plugin-heading')), 'hello.lua', 'named after the file');
    check(document.getElementById('plugin-name-field').hidden, 'an existing plugin keeps its name');
    equal(document.getElementById('plugin-source').value, 'plugin = { name = "Hello" }\n', 'source loaded');
    equal(document.getElementById('plugin-gutter').textContent, '1\n2\n', 'line numbers');
    equal(document.getElementById('plugin-result').textContent, 'Running (12 KB of memory).', 'status');
    equal(document.getElementById('plugin-log').textContent, 'hi\nthere', 'log');
    check(rows[1].classList.contains('selected'), 'the open plugin is marked in the list');

    const source = document.getElementById('plugin-source');
    source.value = 'plugin = {}\n\nfunction draw() end\n';
    source.dispatchEvent(new window.Event('input'));
    equal(document.getElementById('plugin-gutter').textContent, '1\n2\n3\n4\n', 'line numbers follow the code');
    // Ctrl+S saves, like the button.
    document.dispatchEvent(new window.KeyboardEvent('keydown', { key: 's', ctrlKey: true, cancelable: true }));
    await settle(window);
    const put = lastRequest(device, '/api/plugins/hello');
    equal([put.method, put.type, put.body], ['PUT', 'application/octet-stream', 'plugin = {}\n\nfunction draw() end\n'], 'upload is not sent as text/plain');
});

test('plugins page: starts a new plugin and deletes one', async () => {
    const device = makeDevice();
    const { window, document } = await openPage(device, { hash: '#plugins' });
    await click(window, document.getElementById('new-plugin-btn'));
    check(!document.getElementById('plugin-name-field').hidden, 'a new plugin asks for a name');
    check(document.getElementById('delete-plugin-btn').hidden, 'and has nothing to delete yet');
    await click(window, document.getElementById('save-plugin-btn'));
    check(document.getElementById('plugin-result').textContent.startsWith('Names are'), 'a name is required');
    document.getElementById('plugin-name').value = 'counter';
    await click(window, document.getElementById('save-plugin-btn'));
    equal(lastRequest(device, '/api/plugins/counter').method, 'PUT', 'saved under its name');
    check(device.cards.some(card => card.plugin === 'counter'), 'a plugin you write gets a first card, to see it run');
    equal(text(document.getElementById('plugin-heading')), 'counter.lua', 'the editor now shows the file');

    await click(window, document.querySelector('#plugins-list .item[data-name="weather"] .item-open'));
    await click(window, document.getElementById('delete-plugin-btn'));
    equal(lastRequest(device, '/api/plugins/weather').method, 'DELETE', 'deleted');
    check(document.getElementById('plugin-editor').hidden, 'the editor closes');
    equal(titles(document), ['Hello', 'Webhook', 'Insight', 'My plugin'], 'its cards are gone');
});

// ------------------------------------------------------ plugin directory

const offers = document => [...document.querySelectorAll('#directory-pane .offer')].map(row => ({
    name: row.dataset.name,
    title: text(row.querySelector('.item-title')),
    note: text(row.querySelector('.actions small')),
    buttons: [...row.querySelectorAll('.actions button')].map(text),
}));
const offer = (document, name) => document.querySelector('#directory-pane .offer[data-name="' + name + '"]');
/** Installing waits a moment for the device to look at the new file. */
const afterInstall = async window => { await new Promise(resolve => window.setTimeout(resolve, 750)); await settle(window); };

test('directory: shows what there is and how it relates to the device', async () => {
    const device = makeDevice();
    const { window, document } = await openPage(device);
    equal(device.outside, [], 'nothing is fetched from the internet while nobody looks');
    await click(window, document.getElementById('cards-get-plugins-btn'));
    equal(visiblePage(document), ['page-plugins'], 'the button beside "Add card" goes to the plugins page');
    check(!document.getElementById('directory-pane').hidden, 'where the directory is in plain sight');
    equal(device.outside, ['https://raw.githubusercontent.com/TimBroddin/DeskHog/HEAD/plugins/index.json'], 'the browser fetches the index from GitHub');
    check(!device.requests.some(r => r.url.includes('github')), 'the device is not asked to');
    equal(offers(document), [
        { name: 'clock', title: 'Clock', note: null, buttons: ['Install'] },
        { name: 'hello', title: 'Hello', note: 'Installed', buttons: ['Add a card'] },
        { name: 'weather', title: 'Weather', note: 'You have 1.0', buttons: ['Update to 1.1', 'Add a card'] },
        { name: 'webhook', title: 'Webhook', note: 'Changed on this device', buttons: ['Replace with this one', 'Add a card'] },
    ], 'each plugin says what can be done with it');
    equal(offer(document, 'clock').querySelector('a').href, 'https://github.com/TimBroddin/DeskHog/blob/HEAD/plugins/clock.lua', 'and links to its code');
    check(text(offer(document, 'clock')).includes('By DeskHog, version 1.0'), 'with who wrote it');
    equal(offer(document, 'clock').querySelector('img.shot').src, 'https://raw.githubusercontent.com/TimBroddin/DeskHog/HEAD/plugins/screenshots/clock.png', 'and a picture of its card, from the directory');
    check(!offer(document, 'hello').querySelector('img'), 'a plugin without a picture has none');
    offer(document, 'weather').querySelector('img.shot').dispatchEvent(new window.Event('error'));
    check(!offer(document, 'weather').querySelector('img'), 'a picture that does not load goes away');
    equal(window.shotAddress({ screenshot: 'https://elsewhere.example/x.png' }), null, 'a picture has to be a file in the directory');
    equal(window.shotAddress({ screenshot: '../x.png' }), null, 'and not outside it');
    equal(window.location.hash, '#plugins', 'the address remembers the page');

    await click(window, document.querySelector('#plugins-list .item .item-open'));
    check(document.getElementById('directory-pane').hidden && !document.getElementById('plugin-editor').hidden, 'opening a plugin\'s code replaces it');
    await click(window, document.getElementById('close-plugin-btn'));
    check(!document.getElementById('directory-pane').hidden && offers(document).length === 4, 'and closing the code brings it back');

    const straight = await openPage(makeDevice(), { hash: '#plugins' });
    equal(offers(straight.document).length, 4, 'opening the plugins page shows it without asking');
});

test('directory: installs a plugin, then offers a card of it', async () => {
    const device = makeDevice();
    const { window, document } = await openPage(device, { hash: '#plugins/~directory' });
    await click(window, buttonNamed(offer(document, 'clock'), 'Install'));
    await afterInstall(window);
    const put = lastRequest(device, '/api/plugins/clock');
    equal([put.method, put.type, put.body], ['PUT', 'application/octet-stream', device.internet.files['clock.lua']], 'the file goes to the device as it is');
    equal(put.url, '/api/plugins/clock?card=no', 'without the first card a developer\'s upload gets');
    check(!device.cards.some(card => card.plugin === 'clock'), 'so installing adds no card');
    equal(text(document.getElementById('notice')), 'Clock is on your DeskHog.', 'says so');
    equal(offers(document)[0], { name: 'clock', title: 'Clock', note: 'Installed', buttons: ['Add a card'] }, 'now it is installed');
    check([...document.querySelectorAll('#plugins-list .item-title')].map(text).includes('Clock'), 'and in the list of plugins');

    await click(window, buttonNamed(offer(document, 'clock'), 'Add a card'));
    equal(visiblePage(document), ['page-cards'], 'goes to the cards');
    equal(text(document.querySelector('#card-pane .pane-head h2')), 'New card: Clock', 'with the new card ready to add');
});

test('adding a card offers what the directory has, and installs it on the way', async () => {
    const device = makeDevice();
    const { window, document } = await openPage(device);
    await click(window, document.getElementById('add-card-btn'));
    const pane = document.getElementById('card-pane');
    const more = () => [...pane.querySelectorAll('.choices.more .choice')];
    equal(more().map(choice => choice.dataset.name), ['clock'], 'the one plugin this device does not have yet');
    check(text(more()[0]).includes('The time in large digits'), 'with what it is');
    equal(more()[0].querySelector('img.shot').src, 'https://raw.githubusercontent.com/TimBroddin/DeskHog/HEAD/plugins/screenshots/clock.png', 'and its picture');
    const webhook = [...pane.querySelectorAll('.choices:not(.more) .choice')].find(choice => text(choice).startsWith('Webhook'));
    check(webhook.querySelector('img.shot') !== null, 'installed ones show their picture too');

    await click(window, more()[0]);
    await afterInstall(window);
    equal(lastRequest(device, '/api/plugins/clock').url, '/api/plugins/clock?card=no', 'choosing it installs the plugin');
    equal(text(pane.querySelector('.pane-head h2')), 'New card: Clock', 'and carries on with the new card');
    await click(window, buttonNamed(pane, 'Add card'));
    check(titles(document).includes('Clock'), 'which is added');

    const offline = makeDevice();
    offline.internet = null;
    const second = await openPage(offline);
    await click(second.window, second.document.getElementById('add-card-btn'));
    const choices = [...second.document.querySelectorAll('#card-pane .choice b')].map(text);
    equal(choices, ['Max', 'Hello', 'Webhook', 'City', 'Funnel', 'Insight'], 'without internet the installed ones are still offered');
    check(text(second.document.getElementById('card-pane')).includes('needs internet'), 'and it says why there are no more');
});

test('directory: updates a plugin and replaces a changed one', async () => {
    const device = makeDevice();
    const { window, document } = await openPage(device, { hash: '#plugins/~directory' });
    await click(window, buttonNamed(offer(document, 'weather'), 'Update to 1.1'));
    await afterInstall(window);
    equal(lastRequest(device, '/api/plugins/weather').body, device.internet.files['weather.lua'], 'new source sent');
    equal(offers(document)[2].note, 'Installed', 'up to date now');
    await click(window, buttonNamed(offer(document, 'webhook'), 'Replace with this one'));
    await afterInstall(window);
    equal(offers(document)[3].note, 'Installed', 'the changed one is back to the published text');
});

test('directory: can come from another repository or address', async () => {
    const device = makeDevice();
    const { window, document } = await openPage(device, { hash: '#plugins/~directory' });
    const pane = document.getElementById('directory-pane');
    check(text(pane.querySelector('.source-line')).includes('TimBroddin/DeskHog'), 'says where it comes from');
    await click(window, buttonNamed(pane, 'Change'));
    check(buttonNamed(pane, 'Back to') === undefined, 'nothing to go back to while it is the default');
    fieldNamed(pane, 'Plugin directory').value = 'not a directory';
    await click(window, buttonNamed(pane, 'Use this directory'));
    check(!device.requests.some(r => r.url === '/api/directory'), 'nonsense is not saved');
    check(document.getElementById('notice').className.includes('error'), 'and is explained');

    fieldNamed(pane, 'Plugin directory').value = 'https://plugins.example/';
    await click(window, buttonNamed(pane, 'Use this directory'));
    equal(lastRequest(device, '/api/directory').body, { source: 'https://plugins.example/' }, 'the device remembers it');
    equal(device.outside[device.outside.length - 1], 'https://plugins.example/index.json', 'and the list comes from there');
    equal(offers(document).length, 4, 'listed');
    equal(offer(document, 'clock').querySelector('a').href, 'https://plugins.example/clock.lua', 'code links go to the file itself');
    await click(window, buttonNamed(pane, 'Change'));
    await click(window, buttonNamed(pane, 'Back to TimBroddin/DeskHog'));
    equal(lastRequest(device, '/api/directory').body, { source: '' }, 'and can go back to the default');

    equal(window.directoryAddress('acme/hogs@dev').index, 'https://raw.githubusercontent.com/acme/hogs/dev/plugins/index.json', 'a branch can be named');
    equal(window.directoryAddress('just-a-word'), null, 'one word is not a repository');
    equal(window.sourceHash(new window.Uint8Array(Buffer.from('plugin = {}\n'))), sourceHash('plugin = {}\n'), 'fingerprints match the device\'s');
});

test('directory: says what is wrong when it cannot be used', async () => {
    const device = makeDevice();
    device.internet = null;
    const { window, document } = await openPage(device, { hash: '#plugins/~directory' });
    const pane = document.getElementById('directory-pane');
    check(text(pane.querySelector('.result.error')).includes('needs internet on the computer or phone'), 'no connection');
    device.internet = { index: { plugins: 'oops' }, files: {} };
    await click(window, buttonNamed(pane, 'Try again'));
    check(text(pane.querySelector('.result.error')).includes('is not a plugin directory'), 'not a directory');

    const good = makeDevice();
    good.internet.files['clock.lua'] += '-- changed after the index was written\n';
    const second = await openPage(good, { hash: '#plugins/~directory' });
    await click(second.window, buttonNamed(offer(second.document, 'clock'), 'Install'));
    check(text(second.document.getElementById('notice')).includes('is not the file the directory describes'), 'a file that does not match its index is refused');
    check(!good.requests.some(r => r.method === 'PUT'), 'and not sent to the device');
});

test('a card whose plugin is not on the device offers to install it', async () => {
    const device = makeDevice();
    device.connections.push({ id: 'clock', plugin: 'clock', name: 'clock', settings: {}, secretsSet: [], state: 'failed', error: 'the plugin "clock" is not installed' });
    device.cards.push({ type: 'PLUGIN', config: 'clock:1', name: 'clock', plugin: 'clock', connection: 'clock', card: '1', kind: '', settings: {}, frames: 1, onScreen: false });
    const { window, document } = await openPage(device);
    equal(text(document.querySelector('#cards-list .item:last-child .item-sub')), 'Its plugin is missing', 'the card says so');
    await click(window, tile(document, 'PLUGIN|clock:1'));
    const pane = document.getElementById('card-pane');
    check(text(pane).includes('needs the plugin "clock"'), 'and which one');
    await click(window, buttonNamed(pane, 'Install clock'));
    await afterInstall(window);
    equal(lastRequest(device, '/api/plugins/clock').method, 'PUT', 'installed from the directory');
    check(fieldNamed(pane, 'Name') !== null, 'and the card can be set up now');
});

test('a new device with nothing on it leads to the directory', async () => {
    const device = makeDevice();
    device.plugins = [];
    device.connections = [];
    device.cards = [];
    const { window, document } = await openPage(device);
    await click(window, buttonNamed(document.getElementById('cards-list'), 'Get plugins'));
    equal(visiblePage(document), ['page-plugins'], 'the plugins page');
    equal(offers(document).map(o => o.buttons[0]), ['Install', 'Install', 'Install', 'Install'], 'with everything on offer');
    await click(window, buttonNamed(offer(document, 'hello'), 'Install'));
    await afterInstall(window);
    await click(window, buttonNamed(offer(document, 'hello'), 'Add a card'));
    await click(window, buttonNamed(document.getElementById('card-pane'), 'Add card'));
    equal(titles(document), ['Hello'], 'first card added');
});

test('offline: wifi setup comes first and connects', async () => {
    const device = makeDevice();
    device.wifiConnected = false;
    const { window, document } = await openPage(device);
    check(!document.getElementById('wifi-setup').hidden, 'setup panel shown');
    check(document.getElementById('wifi-panel').hidden, 'and not a second time on the device page');
    equal(text(document.getElementById('wifi-pill')), 'No WiFi', 'pill');
    const networks = [...document.querySelectorAll('#wifi-form .network span')].map(text);
    equal(networks, ['Home WiFi', 'Cafe'], 'one row per name, strongest first, unnamed ones dropped');
    await click(window, document.querySelector('#wifi-form .network'));
    document.querySelector('#wifi-form input[type="password"]').value = 'hunter2';
    await click(window, buttonNamed(document.getElementById('wifi-form'), 'Connect'));
    equal(lastRequest(device, '/api/actions/save-wifi').body, { ssid: 'Home WiFi', password: 'hunter2' }, 'credentials sent');
    check(document.getElementById('notice').textContent.includes('is joining "Home WiFi"'), 'says what happens next');
});

test('the device page sets the time zone', async () => {
    const device = makeDevice();
    const { window, document } = await openPage(device, { hash: '#device' });
    const select = document.getElementById('timezone-select');
    check(select !== null, 'there is a dropdown');
    equal(select.value, 'UTC', 'a new device is on UTC');
    check(select.querySelectorAll('option').length > 300, 'with the places of the world in it');
    equal([...select.querySelectorAll('optgroup')].map(group => group.label).slice(0, 3), ['Universal', 'Africa', 'America'], 'by continent');
    equal(text(select.querySelector('option[value="America/Argentina/Buenos_Aires"]')), 'Argentina, Buenos Aires', 'names read like places');

    select.value = 'Europe/Brussels';
    select.dispatchEvent(new window.Event('change'));
    await settle(window);
    equal(lastRequest(device, '/api/timezone').body, { posix: 'CET-1CEST,M3.5.0,M10.5.0/3', name: 'Europe/Brussels' }, 'the device gets the POSIX string and the name');
    equal(document.getElementById('timezone-select').value, 'Europe/Brussels', 'and the page shows what was picked');
    check(document.getElementById('notice').textContent.includes('Time zone saved'), 'and says so');

    // Amsterdam has the same rules as Brussels: the name that was picked is the one shown.
    device.timezone = { posix: 'CET-1CEST,M3.5.0,M10.5.0/3', name: 'Europe/Amsterdam' };
    await window.refresh();
    equal(document.getElementById('timezone-select').value, 'Europe/Amsterdam', 'the picked name wins over other places with the same rules');

    // A zone set through the API, which the list has no place for.
    device.timezone = { posix: '<+0123>-1:23', name: '' };
    await window.refresh();
    const custom = document.getElementById('timezone-select');
    equal(custom.value, '=<+0123>-1:23', 'an unknown zone is shown as it is');
    equal(window.zoneValue(custom), { posix: '<+0123>-1:23', name: '' }, 'and kept as it is');

    // The page offers the zone of the computer it is shown on.
    const here = window.browserZone();
    if (here) {
        await click(window, buttonNamed(document.getElementById('timezone-form'), 'Use '));
        equal(lastRequest(device, '/api/timezone').body.name, here, 'one click sets it');
    }
});

test('a time zone setting of a card is a dropdown that saves a POSIX string', async () => {
    const device = makeDevice();
    // What the firmware reports for a plugin setting written as { label = "Time zone", type = "timezone" }.
    device.plugins.find(p => p.name === 'weather').kinds[0].config.push({ key: 'zone', label: 'Time zone', type: 'timezone', default: '' });
    const { window, document } = await openPage(device);
    await click(window, tile(document, 'PLUGIN|weather:1'));
    const pane = document.getElementById('card-pane');
    const zone = fieldNamed(pane, 'Time zone');
    equal(zone.tagName, 'SELECT', 'a dropdown, not a text field');
    equal(zone.value, '', 'nothing saved: the device\'s own zone');
    equal(text(zone.querySelector('option')), 'Same as the DeskHog', 'which is the first choice');
    zone.value = 'Asia/Tokyo';
    await click(window, buttonNamed(pane, 'Save'));
    equal(lastRequest(device, '/api/cards/update').body.settings.zone, 'JST-9', 'the plugin gets the POSIX string');

    await click(window, tile(document, 'PLUGIN|weather:1'));
    const again = fieldNamed(document.getElementById('card-pane'), 'Time zone');
    equal(window.zoneValue(again).posix, 'JST-9', 'reopened, it shows a place with those rules');
    again.value = '';
    await click(window, buttonNamed(document.getElementById('card-pane'), 'Save'));
    equal(lastRequest(device, '/api/cards/update').body.settings.zone, '', 'back to the device\'s zone');
});

test('every plugin card can be told not to come to the front', async () => {
    const device = makeDevice();
    const { window, document } = await openPage(device);
    await click(window, tile(document, 'PLUGIN|hello:1'));
    const pane = document.getElementById('card-pane');
    const front = [...pane.querySelectorAll('label.field.check')].find(l => l.textContent.startsWith('May come to the front'));
    check(!!front, 'a card of a plugin without any settings still has the setting');
    const box = front.querySelector('input[type="checkbox"]');
    check(box.checked, 'on unless someone switched it off');
    box.checked = false;
    await click(window, buttonNamed(pane, 'Save'));
    equal(lastRequest(device, '/api/cards/update?key=hello%3A1').body.front, false, 'saved with the card');
    equal(device.cards.find(c => c.config === 'hello:1').front, false, 'the device has it');

    // It is the device's own setting, so it shows what the device says.
    const again = await openPage(device);
    await click(again.window, tile(again.document, 'PLUGIN|hello:1'));
    const shown = [...again.document.querySelectorAll('#card-pane label.field.check')].find(l => l.textContent.startsWith('May come to the front'));
    check(!shown.querySelector('input').checked, 'and it is shown as saved');
    await click(again.window, tile(again.document, 'PLUGIN|weather:2'));
    const other = [...again.document.querySelectorAll('#card-pane label.field.check')].find(l => l.textContent.startsWith('May come to the front'));
    check(other.querySelector('input').checked, 'other cards keep theirs');
    await click(again.window, buttonNamed(again.document.getElementById('card-pane'), 'Save'));
    equal(lastRequest(device, '/api/cards/update?key=weather%3A2').body.front, true, 'a card that may is saved as such');
});

test('a card with something to tell says so in the list', async () => {
    const device = makeDevice();
    device.notifications = [{ card: 'weather:2', text: 'Rain in 10 minutes', count: 2, age: 40 }, { card: 'hello:1', text: '', count: 1, age: 3 }];
    const { document } = await openPage(device);
    const subs = [...document.querySelectorAll('#cards-list .item-sub')].map(text);
    equal(subs[1], 'Weather, has news: Rain in 10 minutes', 'the waiting text is in the subtitle');
    equal(subs[2], 'has news', 'a notify() without text still counts');
    equal(subs[0], 'Weather, on screen now', 'cards with nothing waiting are as before');
});

/** The parts of a field whose choices the plugin lists. */
const pickOf = (scope, label) => {
    const wrap = [...scope.querySelectorAll('label.field')].find(l => l.querySelector('span').textContent === label);
    return wrap && {
        wrap, text: wrap.querySelector('input[type="text"]'), select: wrap.querySelector('select'),
        refresh: buttonNamed(wrap, 'Refresh'), note: wrap.querySelector('small').textContent,
    };
};

test('a setting the plugin lists the choices for is a dropdown with a refresh button', async () => {
    const device = addHomey(makeDevice());
    const { window, document, errors } = await openPage(device);
    window.choicePollDelay = () => 0;
    await click(window, tile(document, 'PLUGIN|homey:1'));
    await settle(window);
    const pane = document.getElementById('card-pane');
    let pick = pickOf(pane, 'Device');
    check(!pick.select.hidden && pick.text.hidden, 'the list arrived: a dropdown, not a text box');
    equal([...pick.select.options].map(o => o.value + '=' + o.textContent), ['=—', 'lamp=Kitchen lamp', 'plug=Coffee machine'],
        'what the plugin offers, by label, and a way to empty a setting that may be empty');
    equal(pick.select.value, 'lamp', 'the saved value is selected');
    equal(pick.note, '', 'nothing to explain');
    const asked = device.requests.filter(r => r.url.startsWith('/api/connections/choices'));
    const address = '/api/connections/choices?id=homey&field=device&kind=device&settings=' + encodeURIComponent('{"device":"lamp","label":""}');
    equal(asked.map(r => r.url), [address, address], 'asks the device, with what the form holds, and again while the plugin is still working on it');

    // Picking one saves its value, not its label.
    pick.select.value = 'plug';
    await click(window, buttonNamed(pane, 'Save'));
    equal(lastRequest(device, '/api/cards/update?key=homey%3A1').body.settings, { device: 'plug', label: '' }, 'the value is what is saved');

    // Refresh asks the plugin again.
    await click(window, tile(document, 'PLUGIN|homey:1'));
    await settle(window);
    pick = pickOf(document.getElementById('card-pane'), 'Device');
    device.choices['homey/device/device'] = [{ value: 'lamp', label: 'Kitchen lamp' }, { value: 'tv', label: 'Television' }];
    await click(window, pick.refresh);
    await settle(window);
    equal([...pick.select.options].map(o => o.textContent), ['—', 'plug (not in the list)', 'Kitchen lamp', 'Television'], 'a fresh list; what is saved stays even if the plugin dropped it');
    equal(pick.select.value, 'plug', 'and stays selected');
    equal(errors, [], 'no script errors');
});

test('without a list a setting with choices is plain text', async () => {
    const device = addHomey(makeDevice());
    device.cards.push({ type: 'PLUGIN', config: 'homey:2', name: 'Flow', plugin: 'homey', connection: 'homey', card: '2', kind: 'flow', settings: { flow: 'f-17' }, frames: 1, onScreen: false });
    const { window, document } = await openPage(device);
    window.choicePollDelay = () => 0;
    await click(window, tile(document, 'PLUGIN|homey:2'));
    await settle(window);
    const pane = document.getElementById('card-pane');
    const pick = pickOf(pane, 'Flow');
    check(pick.select.hidden && !pick.text.hidden, 'the plugin could not say: a text box');
    equal(pick.text.value, 'f-17', 'with the saved value in it');
    check(pick.note.includes('log in to Homey first') && pick.note.includes('type the value'), 'and why, in the plugin\'s words');
    pick.text.value = 'f-18';
    await click(window, buttonNamed(pane, 'Save'));
    equal(lastRequest(device, '/api/cards/update?key=homey%3A2').body.settings, { flow: 'f-18' }, 'what was typed is saved');

    // Once the plugin can say, Refresh turns it into a dropdown.
    await click(window, tile(document, 'PLUGIN|homey:2'));
    await settle(window);
    const later = pickOf(document.getElementById('card-pane'), 'Flow');
    device.choices['homey/flow/flow'] = [{ value: 'f-18', label: 'Good morning' }];
    await click(window, later.refresh);
    await settle(window);
    check(!later.select.hidden && later.text.hidden, 'a dropdown after all');
    equal(later.select.value, 'f-18', 'with what was typed selected');
});

test('choices when adding a card: from the connection it joins, typed for a new one', async () => {
    const device = addHomey(makeDevice());
    const { window, document } = await openPage(device);
    window.choicePollDelay = () => 0;
    await click(window, document.getElementById('add-card-btn'));
    const pane = document.getElementById('card-pane');
    await click(window, [...pane.querySelectorAll('.choice')].find(c => c.textContent.startsWith('Device')));
    await settle(window);
    let pick = pickOf(pane, 'Device');
    check(!pick.select.hidden, 'a card for an existing connection gets that connection\'s list');
    equal([...pick.select.options].map(o => o.textContent), ['—', 'Kitchen lamp', 'Coffee machine'], 'with nothing picked yet');
    pick.select.value = 'plug';
    await click(window, buttonNamed(pane, 'Add card'));
    const added = lastRequest(device, '/api/cards/add').body;
    equal([added.connection, added.kind, added.settings.device], ['homey', 'device', 'plug'], 'the card is added with the value that was picked');

    // A new connection does not exist on the device yet, so there is nobody to ask.
    await click(window, document.getElementById('add-card-btn'));
    await click(window, [...pane.querySelectorAll('.choice')].find(c => c.textContent.startsWith('Device')));
    await settle(window);
    const connection = fieldNamed(pane, 'Connection');
    connection.value = '';
    connection.dispatchEvent(new window.Event('change'));
    await settle(window);
    pick = pickOf(pane, 'Device');
    check(pick.select.hidden && !pick.text.hidden && pick.refresh.hidden, 'a text box, and nothing to refresh');
    check(pick.note.startsWith('Type it for now'), 'that says the list comes later');
    const home = pickOf(pane, 'Home');
    check(home.select.hidden && home.note.startsWith('Type it for now'), 'the same for the new connection\'s own settings');

    // On the connections page the connection exists, so its own setting has a list.
    await click(window, document.querySelector('.tabs a[data-page="connections"]'));
    await click(window, [...document.querySelectorAll('#connections-list .item-open')].find(b => b.textContent.startsWith('Homey')));
    await settle(window);
    const own = pickOf(document.getElementById('connection-pane'), 'Home');
    check(!own.select.hidden, 'a connection\'s own setting gets its list');
    check(device.requests.some(r => r.url.startsWith('/api/connections/choices?id=homey&field=home&settings=')), 'asked for without a card kind');
});

test('a list of choices is asked for with what the form holds, saved or not', async () => {
    const device = addHomey(makeDevice());
    const flowKind = device.plugins.find(p => p.name === 'homey').kinds.find(k => k.key === 'flow');
    const field = (key, label, extra) => Object.assign({ key, label, type: 'string', default: '' }, extra);
    flowKind.config.push(field('advanced', 'Advanced flows', { type: 'boolean', default: 'false' }), field('token', 'Token', { secret: true }),
        field('room', 'Room', { choices: true, default: 'hall' }));
    device.cards.push({ type: 'PLUGIN', config: 'homey:2', name: 'Flow', plugin: 'homey', connection: 'homey', card: '2', kind: 'flow', settings: { flow: 'a-1' }, frames: 1, onScreen: false });
    // A plugin that lists one kind of flow or the other, going by the checkbox next to the list.
    device.choices['homey/flow/flow'] = form => form.advanced === 'true' ? [{ value: 'a-1', label: 'Party mode' }] : [{ value: 'f-1', label: 'Good morning' }];
    device.choices['homey/flow/room'] = [{ value: 'hall', label: 'Hall' }];
    const { window, document, errors } = await openPage(device);
    window.choicePollDelay = () => 0;
    await click(window, tile(document, 'PLUGIN|homey:2'));
    await settle(window);
    const pane = document.getElementById('card-pane');
    const pick = pickOf(pane, 'Flow');
    equal(device.choiceForms.find(asked => asked.key === 'homey/flow/flow').form, { flow: 'a-1', advanced: 'false', room: 'hall' },
        'the first list is asked for with the saved values and the defaults');
    equal([...pick.select.options].map(o => o.textContent), ['—', 'a-1 (not in the list)', 'Good morning'], 'and is the one that goes with them');
    equal([...pickOf(pane, 'Room').select.options].map(o => o.textContent), ['Hall'], 'a setting with a default has no empty entry');

    // Tick the box, type a secret, Refresh: the plugin hears about the box, not about the secret.
    fieldNamed(pane, 'Advanced flows').checked = true;
    fieldNamed(pane, 'Token').value = 'hunter2';
    await click(window, pick.refresh);
    await settle(window);
    equal(device.choiceForms[device.choiceForms.length - 1].form, { flow: 'a-1', advanced: 'true', room: 'hall' }, 'what is in the form now, unsaved, without secrets');
    equal([...pick.select.options].map(o => o.textContent), ['—', 'Party mode'], 'so the list follows the other setting');
    equal(pick.select.value, 'a-1', 'and the saved flow is found in it');
    check(!device.requests.some(r => r.url.includes('hunter2')), 'a secret is never put in an address');

    // Picking the empty entry saves an empty setting.
    pick.select.value = '';
    await click(window, buttonNamed(pane, 'Save'));
    equal(lastRequest(device, '/api/cards/update?key=homey%3A2').body.settings.flow, '', 'the setting can be emptied');
    equal(device.handle('GET', '/api/connections/choices?id=homey&field=home&settings=nope').status, 400, 'settings has to be JSON');
    equal(errors, [], 'no script errors');
});

test('the pretend device takes pushed messages like the firmware', async () => {
    const device = addHomey(makeDevice());
    equal(device.handle('POST', '/api/cards/message?key=homey:1', 'Pizza is here').status, 200, 'a card whose kind has message() takes one');
    equal(device.handle('POST', '/api/connections/message?id=homey', { door: 'open' }).status, 200, 'and so does the plugin itself');
    equal(device.messages, [{ card: 'homey:1', body: 'Pizza is here' }, { connection: 'homey', body: '{"door":"open"}' }], 'text and JSON arrive as sent');
    equal(device.handle('POST', '/api/cards/message?key=hello:1', 'x').status, 404, 'a plugin without message() answers 404');
    equal(device.handle('POST', '/api/cards/message?key=nope:1', 'x').status, 404, 'so does a card that is not there');
    equal(device.handle('POST', '/api/cards/message?key=homey:1', 'x'.repeat(5000)).status, 413, 'more than 4 KB is refused');
    equal(device.handle('GET', '/api/cards/message?key=homey:1').status, 405, 'only POST');
    for (let i = 0; i < 6; i++) device.handle('POST', '/api/connections/message?id=homey', 'again');
    equal(device.handle('POST', '/api/connections/message?id=homey', 'one too many').status, 429, 'at most 8 wait at a time');
    equal(device.handle('GET', '/api/connections/choices?id=homey&field=url').status, 404, 'a setting without choices has no list');
    equal(device.handle('GET', '/api/connections/choices?id=nope&field=home').status, 404, 'nor does a connection that is not there');
});

// ---- the device's own settings

const slidersOf = document => [...document.querySelectorAll('#display-form input[type="range"]')];
const move = async (window, input, value, type) => { input.value = String(value); input.dispatchEvent(new window.Event(type, { bubbles: true })); await settle(window); };
const rowOf = (document, label) => [...document.querySelectorAll('#about-list dt')].find(dt => dt.textContent === label);

test('shows the device\'s settings', async () => {
    const device = makeDevice();
    const { document } = await openPage(device, { hash: '#device' });
    equal(slidersOf(document).map(s => s.value), ['80', '10', '100', '0'], 'screen and LED, by day and at night');
    equal([...document.querySelectorAll('#display-form output')].map(text), ['80%', '10%', '100%', 'off'], 'with their values beside them');
    const [enabled, from, until] = document.querySelectorAll('#display-form .times input');
    check(!enabled.checked, 'night hours are off');
    equal([from.value, until.value], ['22:00', '07:00'], 'from 22:00 until 07:00');
    equal(document.querySelector('#display-form select').selectedOptions[0].textContent, 'Off', 'the rotation is off');
    equal(document.title, 'DeskHog', 'the title is as before at the default name');
    equal(document.querySelector('[data-brand]').textContent, 'DeskHog', 'and so is the header');
});

test('an older device without settings has no such panels', async () => {
    const device = makeDevice();
    const original = device.state;
    device.state = () => { const { device: gone, ...rest } = original(); return rest; };
    const { document, errors } = await openPage(device, { hash: '#device' });
    equal(errors, [], 'nothing breaks');
    for (const id of ['display-panel', 'name-panel', 'battery-panel', 'restart-btn']) check(document.getElementById(id).hidden, id + ' is left out');
    const { window } = await openPage(device);
    await click(window, tile(window.document, 'PLUGIN|hello:1'));
    check(!window.document.getElementById('card-pane').textContent.includes('Skip in rotation'), 'and cards have no rotation setting');
    await click(window, buttonNamed(window.document.getElementById('card-pane'), 'Save'));
    check(!('rotate' in lastRequest(device, '/api/cards/update').body), 'which is not sent');
});

test('a slider saves its level', async () => {
    const device = makeDevice();
    const { window, document } = await openPage(device, { hash: '#device' });
    await move(window, slidersOf(document)[0], 60, 'change');
    equal(lastRequest(device, '/api/device').body, { screen: { day: 60 } }, 'only that level is sent');
    equal(device.device.screen.day, 60, 'the device has it');
    equal(document.querySelector('#display-form output').textContent, '60%', 'and the page shows it');
});

test('dragging a night slider previews it', async () => {
    const device = makeDevice();
    const { window, document } = await openPage(device, { hash: '#device' });
    await move(window, slidersOf(document)[1], 20, 'input');
    equal(lastRequest(device, '/api/device').body, { screen: { night: 20 }, preview: 'night' }, 'the night level is shown');
    await move(window, slidersOf(document)[3], 30, 'input');
    equal(lastRequest(device, '/api/device').body, { led: { night: 30 }, preview: 'night' }, 'the LED too');
    const count = device.requests.length;
    await move(window, slidersOf(document)[2], 50, 'input');
    equal(device.requests.length, count + 1, 'a day slider is applied while it is dragged');
    equal(lastRequest(device, '/api/device').body, { led: { day: 50 } }, 'without a preview');
    // Faster than every 150 ms, only the last value follows.
    const before = device.requests.length;
    for (const value of [51, 52, 53]) await move(window, slidersOf(document)[2], value, 'input');
    equal(device.requests.length, before, 'a burst of moves is held back');
    await new Promise(resolve => window.setTimeout(resolve, 200));
    equal(lastRequest(device, '/api/device').body, { led: { day: 53 } }, 'and the last one is sent');
});

test('letting go of a night slider cancels its pending preview', async () => {
    const device = makeDevice();
    const { window, document } = await openPage(device, { hash: '#device' });
    const night = slidersOf(document)[1];
    await move(window, night, 20, 'input');
    await move(window, night, 25, 'input');
    await move(window, night, 25, 'change');
    await new Promise(resolve => window.setTimeout(resolve, 250));
    const posts = device.requests.filter(r => r.url.startsWith('/api/device'));
    equal(posts[posts.length - 1].body, { screen: { night: 25 } }, 'the last word is the save, not a preview');
    equal(posts.filter(r => r.body.preview).length, 1, 'and no preview follows it');
});

test('a poll does not undo a slider or a name that someone is working on', async () => {
    const device = makeDevice();
    const { window, document } = await openPage(device, { hash: '#device' });
    const slide = slidersOf(document)[0];
    slide.focus();
    await move(window, slide, 33, 'input');
    await move(window, slide, 34, 'input'); // held back for the next 150 ms
    await window.refresh();
    equal(slide.value, '34', 'a slider being dragged stays where it is, though the device has 33');
    slide.blur();
    await window.refresh();
    equal(slide.value, '34', 'and so does one let go before the device has caught up');
    await new Promise(resolve => window.setTimeout(resolve, 200));
    device.device.screen.day = 90;
    await window.refresh();
    equal(slide.value, '90', 'once the device has it, the page follows the device again');
    const name = document.querySelector('#name-form input');
    name.value = 'kitch';
    name.dispatchEvent(new window.Event('input', { bubbles: true }));
    await window.refresh();
    equal(name.value, 'kitch', 'a half-typed name stays');
    equal(document.querySelector('#name-form small').textContent, 'http://deskhog.local', 'and the address stays that of the saved name');
    const other = slidersOf(document)[3];
    device.device.led.night = 40;
    await window.refresh();
    equal(other.value, '40', 'what nobody touches follows the device');
});

test('night hours are saved', async () => {
    const device = makeDevice();
    const { window, document } = await openPage(device, { hash: '#device' });
    const [enabled, from] = document.querySelectorAll('#display-form .times input');
    enabled.checked = true;
    enabled.dispatchEvent(new window.Event('change', { bubbles: true }));
    await settle(window);
    equal(lastRequest(device, '/api/device').body, { night: { enabled: true } }, 'switched on');
    await move(window, from, '23:15', 'change');
    equal(lastRequest(device, '/api/device').body, { night: { from: '23:15' } }, 'a new start');
    equal(device.device.night, { enabled: true, from: '23:15', until: '07:00' }, 'the device has both');
});

test('rotate offers fixed steps', async () => {
    const device = makeDevice();
    const { window, document } = await openPage(device, { hash: '#device' });
    const select = document.querySelector('#display-form select');
    equal([...select.options].map(text), ['Off', '15 s', '30 s', '1 min', '2 min', '5 min'], 'the steps');
    await move(window, select, 60, 'change');
    equal(lastRequest(device, '/api/device').body, { rotate: { seconds: 60 } }, 'one minute');
    device.device.rotate.seconds = 45;
    await window.refresh();
    equal([...select.options].map(text), ['Off', '15 s', '30 s', '45 s', '1 min', '2 min', '5 min'], 'a value set through the API gets its own step');
    equal(select.selectedOptions[0].textContent, '45 s', 'and shows');
});

test('renaming sends you to the new address', async () => {
    const device = makeDevice();
    const { window, document } = await openPage(device, { hash: '#device' });
    const name = document.querySelector('#name-form input');
    equal(document.querySelector('#name-form small').textContent, 'http://deskhog.local', 'the address is shown');
    name.value = 'kitchen';
    name.dispatchEvent(new window.Event('input', { bubbles: true }));
    await click(window, buttonNamed(document.getElementById('name-form'), 'Save'));
    equal(lastRequest(device, '/api/device').body, { name: 'kitchen' }, 'the name is sent');
    check(document.getElementById('notice').textContent.includes('http://kitchen.local'), 'and the notice gives the address');
    equal(document.title, 'DeskHog · kitchen', 'the title shows the new name');
    equal(document.querySelector('[data-brand]').textContent, 'DeskHog · kitchen', 'and so does the header');
});

test('a bad name shows the device\'s reason', async () => {
    const device = makeDevice();
    const { window, document } = await openPage(device, { hash: '#device' });
    const name = document.querySelector('#name-form input');
    name.value = '-Kitchen';
    name.dispatchEvent(new window.Event('input', { bubbles: true }));
    await click(window, buttonNamed(document.getElementById('name-form'), 'Save'));
    equal(document.getElementById('notice').textContent, 'name is not valid', 'the device\'s words');
    check(document.getElementById('notice').className.includes('error'), 'as an error');
    await window.refresh();
    equal(name.value, '-Kitchen', 'the field keeps what was typed, also through a refresh');
    equal(device.device.name, 'deskhog', 'the device is unchanged');
});

test('battery line only with a battery', async () => {
    const device = makeDevice();
    const { window, document } = await openPage(device, { hash: '#device' });
    equal(rowOf(document, 'Battery'), undefined, 'no row without a battery');
    check(document.querySelector('#battery-form small').textContent.includes('A battery gauge was found'), 'the panel says a gauge was found');
    device.device.status.gauge = { percent: 82, volts: 3.98, charging: true };
    const box = document.querySelector('#battery-form input');
    box.checked = true;
    box.dispatchEvent(new window.Event('change', { bubbles: true }));
    await settle(window);
    equal(lastRequest(device, '/api/device').body, { battery: true }, 'ticked');
    equal(text(rowOf(document, 'Battery').nextSibling), '82%, charging', 'About shows the reading');
});

test('no battery panel hint without a gauge', async () => {
    const device = makeDevice();
    device.device.status.gauge = null;
    const { document } = await openPage(device, { hash: '#device' });
    check(document.querySelector('#battery-form input[type="checkbox"]') !== null, 'the tick box is there');
    check(document.querySelector('#battery-form small').textContent.includes('No battery gauge was found'), 'the hint says no gauge was found');
});

test('restart asks first', async () => {
    const device = makeDevice();
    const { window, document } = await openPage(device, { hash: '#device' });
    window.confirm = () => false;
    await click(window, document.getElementById('restart-btn'));
    check(!lastRequest(device, '/api/restart'), 'cancelled: nothing is sent');
    window.confirm = () => true;
    await click(window, document.getElementById('restart-btn'));
    equal(lastRequest(device, '/api/restart').method, 'POST', 'confirmed: the device is asked');
    equal(device.restarts, 1, 'once');
});

test('a card can be kept out of the rotation', async () => {
    const device = makeDevice();
    const { window, document } = await openPage(device);
    await click(window, tile(document, 'PLUGIN|hello:1'));
    const pane = document.getElementById('card-pane');
    const skip = [...pane.querySelectorAll('label.field.check')].find(l => l.textContent.startsWith('Skip in rotation'));
    check(!!skip && !skip.querySelector('input').checked, 'unticked to begin with');
    check(skip.textContent.includes('stays on it when you go there yourself'), 'with its hint');
    skip.querySelector('input').checked = true;
    await click(window, buttonNamed(pane, 'Save'));
    equal(lastRequest(device, '/api/cards/update?key=hello%3A1').body.rotate, false, 'saved with the card');
    equal(device.cards.find(c => c.config === 'hello:1').rotate, false, 'the device has it');
    await click(window, tile(document, 'PLUGIN|hello:1'));
    const again = [...document.querySelectorAll('#card-pane label.field.check')].find(l => l.textContent.startsWith('Skip in rotation'));
    check(again.querySelector('input').checked, 'and shows it as saved');
});

test('the pretend device is as strict as the firmware', async () => {
    const device = makeDevice();
    const post = body => device.handle('POST', '/api/device', body);
    for (const bad of [{ screen: { day: 4 } }, { screen: { day: 60.5 } }, { screen: { day: '60' } }, { led: { night: 101 } }, { name: '-a' }, { name: 'a-' },
        { name: 'A' }, { name: 5 }, { rotate: { seconds: 5 } }, { rotate: { seconds: 601 } }, { battery: 1 }, { night: { enabled: 'yes' } },
        { night: { from: '25:00' } }, { night: { until: 2200 } }, { colour: 1 }, { screen: { dusk: 1 } }, { preview: 'dusk' }, { screen: 3 }]) {
        const answer = post(bad);
        equal([answer.status, JSON.parse(answer.body).ok], [400, false], 'refuses ' + JSON.stringify(bad));
    }
    equal(post({ screen: { day: 60 }, name: 'A' }).status, 400, 'one bad value refuses the lot');
    equal(device.device.screen.day, 80, 'and nothing changed');
    const good = post({ screen: { day: 5 }, rotate: { seconds: 0 }, preview: 'night', night: { from: '22:30' } });
    equal(good.status, 200, 'a good body');
    equal([JSON.parse(good.body).screen.day, JSON.parse(good.body).night.from], [5, '22:30'], 'answers with the whole object');
    equal(JSON.parse(device.handle('GET', '/api/device').body), JSON.parse(good.body), 'which GET gives too');
});

test('errors from the device are shown', async () => {
    const device = makeDevice();
    const { window, document } = await openPage(device);
    await click(window, document.getElementById('add-card-btn'));
    const pane = document.getElementById('card-pane');
    const original = device.handle;
    device.handle = (method, url, body) => (url === '/api/cards/add' ? { status: 400, body: JSON.stringify({ ok: false, error: 'the device shows at most 12 plugin cards' }) } : original(method, url, body));
    await click(window, buttonNamed(pane, 'Hello'));
    await click(window, buttonNamed(pane, 'Add card'));
    equal(document.getElementById('notice').textContent, 'the device shows at most 12 plugin cards', 'message');
    check(document.getElementById('notice').className.includes('error'), 'styled as an error');
    check(buttonNamed(pane, 'Add card') !== undefined && !pane.hidden, 'the form stays open so nothing typed is lost');
});

(async () => {
    for (const { name, body } of tests) {
        const before = failures;
        try {
            await body();
        } catch (error) {
            failures++;
            console.log('  FAIL threw: ' + (error.stack || error));
        }
        console.log((failures === before ? 'ok   ' : 'FAIL ') + name);
    }
    console.log('\n' + tests.length + ' tests, ' + checks + ' checks, ' + failures + ' failed');
    process.exit(failures ? 1 : 0);
})();
