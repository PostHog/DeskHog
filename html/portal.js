'use strict';

// The DeskHog web page. One file, no dependencies: it is served by the device
// itself and has to work without internet.
//
// Three levels, as on the device:
//   plugin      the code                                   "Weather"
//   connection  a plugin plus settings its cards share     "PostHog, Work"
//   card        one screen                                 "Lisbon"
//
// Four pages (Cards, Connections, Plugins, Device). Each page with things to
// change has a list and a pane for the one thing you picked. The pane sits
// beside the list on a wide screen and covers it on a phone; the CSS decides.

// ---------------------------------------------------------------- helpers

const $ = id => document.getElementById(id);

/** Create an element: el('button', {class: 'primary', onclick: fn}, 'Save') */
function el(tag, props, ...children) {
    const node = document.createElement(tag);
    for (const [key, value] of Object.entries(props || {})) {
        if (value === null || value === undefined || value === false) continue;
        if (key === 'class') node.className = value;
        else if (key.startsWith('on')) node.addEventListener(key.slice(2), value);
        else if (key === 'hidden' || key === 'disabled' || key === 'checked' || key === 'selected') node[key] = true;
        else node.setAttribute(key, value);
    }
    for (const child of children.flat()) {
        if (child === null || child === undefined || child === false) continue;
        node.appendChild(typeof child === 'string' ? document.createTextNode(child) : child);
    }
    return node;
}

async function api(method, url, body, contentType) {
    const options = { method };
    if (body !== undefined) {
        const asIs = typeof body === 'string' || body instanceof FormData || body instanceof Uint8Array;
        options.body = asIs ? body : JSON.stringify(body);
        if (!(body instanceof FormData)) options.headers = { 'Content-Type': contentType || 'application/json' };
    }
    let response;
    try {
        response = await fetch(url, options);
    } catch (error) {
        throw new Error('Could not reach the device.');
    }
    const text = await response.text();
    let data = null;
    try { data = JSON.parse(text); } catch (error) { data = null; }
    if (!response.ok || (data && (data.ok === false || data.success === false))) {
        throw new Error((data && (data.error || data.message)) || 'The device answered with an error (' + response.status + ').');
    }
    return data === null ? text : data;
}

let noticeTimer = null;
function notify(text, isError) {
    const box = $('notice');
    box.textContent = text;
    box.className = 'notice' + (isError ? ' error' : '');
    box.hidden = false;
    clearTimeout(noticeTimer);
    noticeTimer = setTimeout(() => { box.hidden = true; }, isError ? 8000 : 4000);
}

/** Run an action, show what went wrong if it fails. */
async function attempt(action, doneText) {
    try {
        await action();
        if (doneText) notify(doneText);
        return true;
    } catch (error) {
        notify(error.message, true);
        return false;
    }
}

const encode = encodeURIComponent;

/** Wide enough for the pane to sit beside the list. */
const isWide = () => typeof window.matchMedia === 'function' && window.matchMedia('(min-width: 900px)').matches;

// ------------------------------------------------------------------ state

let state = null;        // GET /api/state
let deviceStatus = null; // GET /api/status
const ui = {
    page: 'cards',
    openCard: null,       // key of the card in the side pane
    adding: null,         // null, or {step, plugin, kind}: the side pane shows the "add card" steps
    openConnection: null, // id of the connection in the side pane
    openPlugin: null,     // name of the plugin in the editor; '' while it is new and unsaved
    directorySignature: '',
    showingDirectory: false, // someone asked for the directory: scroll to it when it is there
    dragging: false,
    justDragged: false,
    choseFirstCard: false,
    choseFirstConnection: false,
    cardsSignature: '',
    cardPaneSignature: '',
    connectionsSignature: '',
    connectionPaneSignature: '',
    pluginsSignature: '',
    zoneSignature: null,
};

const pluginByName = name => (state ? state.plugins.find(p => p.name === name) : null);
const connectionById = id => (state ? state.connections.find(c => c.id === id) : null);
const cardByKey = key => (state ? state.cards.find(c => c.key === key) : null);

/** A connection nobody named is called after its plugin; no need to say that twice. */
const sameName = (name, plugin) => name === plugin.name || name.toLowerCase() === plugin.title.toLowerCase();

function kindOf(card, plugin) {
    if (!plugin || !plugin.kinds.length) return null;
    return plugin.kinds.find(k => k.key === card.kind) || plugin.kinds[0];
}

/** What a card is called when nobody named it. */
function defaultCardName(card) {
    const plugin = pluginByName(card.plugin);
    if (!plugin) return card.name || 'Card';
    const kind = kindOf(card, plugin);
    return plugin.multiCard && kind ? kind.name : plugin.title;
}

function cardTitle(card) {
    if (card.type !== 'PLUGIN') return card.name || card.type;
    const fallback = defaultCardName(card);
    const named = card.name && card.name !== fallback && card.name !== card.plugin;
    // A name someone typed wins. Otherwise use what the plugin calls the card.
    return named ? card.name : (card.title || fallback);
}

function cardSubtitle(card) {
    if (card.type !== 'PLUGIN') return { text: 'No longer supported', bad: true };
    const plugin = pluginByName(card.plugin);
    const connection = connectionById(card.connection);
    if (!plugin || !connection) return { text: 'Its plugin is missing', bad: true };
    if (connection.state === 'failed') return { text: 'Crashed: ' + (connection.error || 'unknown error'), bad: true };
    const parts = [];
    if (plugin.title !== cardTitle(card)) parts.push(plugin.title);
    if (plugin.multiCard && !sameName(connection.name, plugin)) parts.push(connection.name);
    if (card.onScreen) parts.push('on screen now');
    // Something its plugin said with notify() that nobody has seen yet.
    const waiting = (state.notifications || []).find(n => n.card === card.config);
    if (waiting) parts.push(waiting.text ? 'has news: ' + waiting.text : 'has news');
    return { text: parts.join(', ') };
}

async function refresh() {
    if (ui.dragging || document.hidden) return;
    try {
        state = await api('GET', '/api/state');
    } catch (error) {
        return; // the device is busy or away; the next round will tell
    }
    // On a wide screen the pane starts with the card the device is showing.
    if (!ui.choseFirstCard && state.cards.length) {
        ui.choseFirstCard = true;
        if (isWide() && !ui.openCard && !ui.adding) ui.openCard = (state.cards.find(c => c.onScreen) || state.cards[0]).key;
    }
    renderCards();
    renderConnections();
    renderPlugins();
    renderDirectory();
    renderAbout();
    renderTimeZone();
    updateScreens();
    if (ui.page === 'plugins' && ui.openPlugin) showPluginStatus(ui.openPlugin, true);
}

/** After a change the device rebuilds its cards, which takes a moment. */
function refreshSoon() {
    setTimeout(refresh, 700);
    setTimeout(refresh, 2500);
}

// ------------------------------------------------------------------ pages

const PAGES = ['cards', 'connections', 'plugins', 'device'];

function showPage(name, remember) {
    // "#plugins/weather" is the plugins page with that plugin's code open.
    const [first, detail] = String(name || '').split('/');
    name = PAGES.includes(first) ? first : 'cards';
    // ("#plugins/~directory" is an address from when the directory had to be opened first.)
    if (name === 'plugins' && detail && detail !== '~directory' && detail !== ui.openPlugin) editPlugin(detail);
    // "#cards/PLUGIN|weather:1" is the cards page with that card in the pane.
    if (name === 'cards' && detail) {
        try { ui.openCard = decodeURIComponent(detail); } catch (error) { ui.openCard = null; }
        ui.choseFirstCard = true;
        renderCards();
    }
    ui.page = name;
    for (const page of PAGES) $('page-' + page).hidden = page !== name;
    document.querySelectorAll('.tabs a').forEach(link => {
        if (link.dataset.page === name) link.setAttribute('aria-current', 'page');
        else link.removeAttribute('aria-current');
    });
    if (remember) rememberPage();
    if (state) renderDirectory(); // the plugins page shows the directory: fetch it on the first look
}

/** Keep the address in step, so reloading comes back to the same place. */
function rememberPage() {
    const detail = ui.page === 'plugins' && ui.openPlugin ? '/' + ui.openPlugin
        : ui.page === 'cards' && ui.openCard && !ui.adding ? '/' + encode(ui.openCard) : '';
    try { history.replaceState(null, '', '#' + ui.page + detail); } catch (error) { /* a page without history still works */ }
}

/** A pane's heading, with the button that closes it. */
function paneHead(title, close) {
    return el('div', { class: 'pane-head' }, el('h2', {}, title),
        el('button', { type: 'button', class: 'close', 'aria-label': 'Close', onclick: close }, '✕'));
}

// ----------------------------------------------------------------- fields

/** How long to wait before asking again for a list the plugin is still working on. */
function choicePollDelay() { return 400; }

/**
 * What a plugin offers for a setting that has `choices`: [{value, label}].
 * The plugin may have to ask the network first, so the device answers
 * "pending" until it knows; ask again until it does.
 * @param form What the form holds right now, saved or not: the plugin's function gets it, so a list can
 *             depend on another setting. It goes in the address, so that asking again is the same request.
 */
async function fetchChoices(source, key, form) {
    const settings = form && Object.keys(form).length ? JSON.stringify(form) : '';
    const url = '/api/connections/choices?id=' + encode(source.connection) + '&field=' + encode(key)
        + (source.kind ? '&kind=' + encode(source.kind) : '')
        + (settings && settings.length <= 1024 ? '&settings=' + encode(settings) : ''); // the device takes 1 KB
    for (let tries = 0; tries < 40; tries++) {
        const answer = await api('GET', url);
        if (answer && !answer.pending) return answer.choices || [];
        await new Promise(resolve => setTimeout(resolve, choicePollDelay()));
    }
    throw new Error('The plugin did not answer in time.');
}

/**
 * A setting whose choices the plugin lists: a dropdown filled in by the
 * device, with a Refresh button. When there is no list (the plugin could not
 * say, or the connection does not exist yet) it is a text box instead.
 * @param source {connection, kind}: whose list to ask for. Null while there is no connection to ask.
 */
function choiceField(field, current, source) {
    const text = el('input', { type: 'text', value: current, placeholder: field.default, autocomplete: 'off', autocapitalize: 'off', spellcheck: 'false' });
    const select = el('select', { hidden: true });
    const refresh = el('button', { type: 'button' }, 'Refresh');
    const note = el('small', {});
    // Whichever of the two shows is the one readFields() reads.
    const use = input => {
        for (const other of [text, select]) {
            other.hidden = other !== input;
            if (other === input) other.dataset.key = field.key; else delete other.dataset.key;
        }
    };
    use(text);
    const label = el('label', { class: 'field' }, el('span', {}, field.label), el('div', { class: 'pick' }, text, select, refresh), note);
    const load = async () => {
        const value = select.hidden ? text.value : select.value;
        refresh.disabled = true;
        note.textContent = 'Asking the plugin what there is to pick from…';
        try {
            // The fields next to this one, as they are filled in right now. Secrets stay here.
            const list = await fetchChoices(source, field.key, readFields(label.parentElement, true));
            const listed = wanted => list.some(choice => choice.value === wanted);
            const options = list.map(choice => el('option', { value: choice.value, selected: choice.value === value }, choice.label || choice.value));
            // A setting that may be left empty can always be emptied again.
            const optional = field.default === '';
            // What is saved stays, also when the plugin no longer lists it.
            if (!listed(value) && !(optional && value === '')) {
                options.unshift(el('option', { value, selected: true }, value ? value + ' (not in the list)' : 'Choose…'));
            }
            if (optional && !listed('')) options.unshift(el('option', { value: '', selected: value === '' }, '—'));
            select.replaceChildren(...options);
            use(select);
            note.textContent = list.length ? '' : 'The plugin has nothing to pick from right now.';
        } catch (error) {
            text.value = value;
            use(text);
            note.textContent = 'No list to pick from: ' + error.message + ' You can type the value instead.';
        }
        refresh.disabled = false;
    };
    if (source && source.connection) {
        refresh.addEventListener('click', load);
        // Once the form is put together, so that the first list knows the other fields too.
        Promise.resolve().then(load);
    } else {
        // The list comes from the plugin running with this connection's settings, and that does not exist yet.
        refresh.hidden = true;
        note.textContent = 'Type it for now. Once the card is added, this becomes a list to pick from.';
    }
    return label;
}

/**
 * One form field for a plugin setting.
 * @param source For a setting the plugin lists the choices for: {connection, kind}, see choiceField().
 */
function fieldInput(field, value, secretIsSet, source) {
    const current = value === undefined || value === null ? field.default : value;
    let input;
    if (field.choices) {
        return choiceField(field, current, source);
    } else if (field.options && field.options.length) {
        input = el('select', {}, field.options.map(option => el('option', { value: option, selected: option === current }, option)));
    } else if (field.type === 'boolean') {
        input = el('input', { type: 'checkbox', checked: current === 'true' });
        input.dataset.key = field.key;
        input.dataset.kind = 'boolean';
        return el('label', { class: 'field check' }, input, el('span', {}, field.label));
    } else if (field.type === 'timezone') {
        // The plugin gets a POSIX string; people pick a place. Empty means the device's own zone.
        input = zoneSelect(current, '', 'Same as the DeskHog');
        input.dataset.kind = 'timezone';
    } else if (field.secret) {
        input = el('input', { type: 'password', autocomplete: 'off', placeholder: secretIsSet ? 'Saved. Leave empty to keep it.' : '' });
        input.dataset.kind = 'secret';
    } else {
        input = el('input', {
            type: 'text', value: current, placeholder: field.default, autocomplete: 'off', autocapitalize: 'off',
            spellcheck: 'false', inputmode: field.type === 'number' ? 'decimal' : null,
        });
    }
    input.dataset.key = field.key;
    return el('label', { class: 'field' }, el('span', {}, field.label), input);
}

function fieldInputs(fields, values, secretsSet, source) {
    return (fields || []).map(field => fieldInput(field, (values || {})[field.key], (secretsSet || []).includes(field.key), source));
}

/**
 * Read the values back out of fields made by fieldInputs().
 * @param withoutSecrets Leave out what was typed into a secret field.
 */
function readFields(container, withoutSecrets) {
    const values = {};
    if (!container) return values;
    container.querySelectorAll('[data-key]').forEach(input => {
        if (withoutSecrets && input.dataset.kind === 'secret') return;
        if (input.dataset.kind === 'boolean') values[input.dataset.key] = input.checked ? 'true' : 'false';
        else if (input.dataset.kind === 'secret') { if (input.value) values[input.dataset.key] = input.value; }
        else if (input.dataset.kind === 'timezone') values[input.dataset.key] = zoneValue(input).posix;
        else values[input.dataset.key] = input.value;
    });
    return values;
}

// ------------------------------------------------------------- time zones

// The device keeps a time zone as a POSIX string ("CET-1CEST,M3.5.0,M10.5.0/3")
// and has no list of places. This page has the list (TIME_ZONES, at the end
// of this file): people pick "Europe/Brussels", the device gets the string.

/** The time zone of the computer or phone showing this page, if the list has it. */
function browserZone() {
    try {
        const name = Intl.DateTimeFormat().resolvedOptions().timeZone;
        return TIME_ZONES[name] ? name : '';
    } catch (error) {
        return '';
    }
}

/** Many places share a POSIX string. Which one to show: the one that was picked, else where this browser is, else the first. */
function zoneNameFor(posix, picked) {
    if (!posix) return '';
    if (picked && TIME_ZONES[picked] === posix) return picked;
    const here = browserZone();
    if (here && TIME_ZONES[here] === posix) return here;
    return Object.keys(TIME_ZONES).find(name => TIME_ZONES[name] === posix) || '';
}

const zoneLabel = name => name.slice(name.indexOf('/') + 1).replace(/_/g, ' ').replace(/\//g, ', ');

/**
 * A dropdown of places, by continent. Read the choice back with zoneValue().
 * @param posix  The saved POSIX string.
 * @param picked The place it was picked by, if that is known.
 * @param first  An extra first choice that stands for "no zone", e.g. "Same as the DeskHog".
 */
function zoneSelect(posix, picked, first) {
    const chosen = zoneNameFor(posix, picked);
    const select = el('select', {});
    if (first) select.appendChild(el('option', { value: '', selected: !posix }, first));
    // A zone the list has no place for (set through the API) stays what it is.
    if (posix && !chosen) select.appendChild(el('option', { value: '=' + posix, selected: true }, posix));
    const regions = new Map();
    for (const name of Object.keys(TIME_ZONES)) {
        const region = name.includes('/') ? name.slice(0, name.indexOf('/')) : 'Universal';
        if (!regions.has(region)) regions.set(region, []);
        regions.get(region).push(name);
    }
    for (const [region, names] of regions) {
        select.appendChild(el('optgroup', { label: region },
            names.map(name => el('option', { value: name, selected: name === chosen }, zoneLabel(name)))));
    }
    return select;
}

/** What a zoneSelect() says: {posix, name}. Both are '' for its "no zone" choice. */
function zoneValue(select) {
    const value = select.value;
    if (!value) return { posix: '', name: '' };
    if (value[0] === '=') return { posix: value.slice(1), name: '' };
    return { posix: TIME_ZONES[value] || '', name: value };
}

/** The device's own time zone, on the Device page. */
function renderTimeZone() {
    if (!state || !state.timezone) return;
    const zone = state.timezone;
    const signature = zone.posix + '|' + zone.name;
    if (signature === ui.zoneSignature) return; // leave an open dropdown alone
    ui.zoneSignature = signature;

    const save = async value => {
        if (await attempt(() => api('POST', '/api/timezone', value), 'Time zone saved.')) await refresh();
    };
    const select = zoneSelect(zone.posix, zone.name);
    select.id = 'timezone-select';
    select.addEventListener('change', () => save(zoneValue(select)));
    // Most of the time the DeskHog is where the person looking at this page is.
    const here = browserZone();
    const offer = here && TIME_ZONES[here] !== zone.posix
        ? el('div', { class: 'row-buttons' }, el('button', { type: 'button', onclick: () => save({ posix: TIME_ZONES[here], name: here }) }, 'Use ' + zoneLabel(here)))
        : null;
    $('timezone-form').replaceChildren(...[
        el('label', { class: 'field' }, el('span', {}, 'Where this DeskHog is'), select,
            el('small', {}, 'Clocks and other cards tell local time by it, summer time included.')),
        offer].filter(Boolean));
}

// ------------------------------------------------------------------ cards

function renderCards() {
    if (!state) return;
    const list = $('cards-list');
    const cards = state.cards;
    // Rebuild the tiles only when the list changes shape; names and status are filled in below.
    const signature = cards.map(c => c.key).join('\n') + (cards.length ? '' : '|' + state.plugins.length);
    if (signature !== ui.cardsSignature) {
        ui.cardsSignature = signature;
        list.replaceChildren(...cards.map(cardTile));
        if (!cards.length) {
            list.appendChild(state.plugins.length
                ? el('p', { class: 'empty' }, 'No cards yet. Add one to get started.')
                : el('p', { class: 'empty' }, 'No cards yet, and no plugins to make them with. ',
                    el('button', { type: 'button', class: 'primary', onclick: showDirectory }, 'Get plugins')));
        }
    }
    $('cards-hint').hidden = cards.length < 2;
    list.querySelectorAll('.item').forEach(item => {
        const card = cardByKey(item.dataset.key);
        if (!card) return;
        item.querySelector('.item-title').textContent = cardTitle(card);
        const subtitle = cardSubtitle(card);
        const sub = item.querySelector('.item-sub');
        sub.textContent = subtitle.text;
        sub.className = 'item-sub' + (subtitle.bad ? ' bad' : '');
        item.classList.toggle('selected', !ui.adding && ui.openCard === card.key);
        item.classList.toggle('live', !!card.onScreen);
    });
    renderCardPane();
}

function cardTile(card) {
    const handle = el('span', { class: 'handle', title: 'Drag to reorder', 'aria-hidden': 'true' }, '⠿');
    const screen = el('span', { class: 'screen' }, card.type === 'PLUGIN' ? screenFor(card.config).canvas : null);
    const open = el('button', { type: 'button', class: 'item-open' }, screen,
        el('span', { class: 'item-text' }, el('span', { class: 'item-title' }), el('span', { class: 'item-sub' })));
    open.addEventListener('click', () => {
        if (ui.justDragged) return; // that was the end of a drag, not a click
        ui.adding = null;
        ui.openCard = card.key;
        renderCards();
    });
    const item = el('div', { class: 'item tile' }, open, handle);
    item.dataset.key = card.key;
    item.addEventListener('pointerdown', event => armDrag(event, item, handle));
    return item;
}

function closeCardPane() {
    ui.openCard = null;
    ui.adding = null;
    renderCards();
}

// The pane beside the cards: the card you picked, or the steps to add one.
function renderCardPane() {
    const pane = $('card-pane');
    if (!ui.adding && ui.openCard && !cardByKey(ui.openCard)) ui.openCard = null; // it was removed
    const card = ui.adding ? null : cardByKey(ui.openCard);
    const adding = ui.adding;
    const signature = adding ? 'add|' + adding.step + '|' + (adding.plugin ? adding.plugin.name + '/' + adding.kind.key : offersSignature())
        : card ? 'card|' + card.key + '|' + (pluginByName(card.plugin) ? '' : 'no plugin') : '';
    pane.hidden = !signature;
    // Rebuilt only when it shows something else, so what you typed survives the page refreshing.
    if (signature !== ui.cardPaneSignature) {
        ui.cardPaneSignature = signature;
        for (const screen of Object.values(screens)) screen.mirror = null;
        pane.replaceChildren(...(adding ? addPanel() : card ? cardPanel(card) : []).filter(Boolean));
        pane.scrollTop = 0;
        if (ui.page === 'cards') rememberPage();
    }
    if (!card) return;
    // The live parts.
    pane.querySelector('.pane-head h2').textContent = cardTitle(card);
    const status = pane.querySelector('.pane-status');
    if (status) {
        const connection = connectionById(card.connection);
        const failed = connection && connection.state === 'failed';
        status.textContent = failed ? 'Crashed: ' + (connection.error || 'unknown error')
            : card.onScreen ? 'On the device\'s screen now. The three buttons work from here.'
                : 'Not on screen. Press a button to bring it up.';
        status.className = 'pane-status' + (failed ? ' bad' : '');
    }
    const index = state.cards.indexOf(card);
    const earlier = pane.querySelector('[data-move="-1"]');
    const later = pane.querySelector('[data-move="1"]');
    if (earlier) earlier.disabled = index <= 0;
    if (later) later.disabled = index >= state.cards.length - 1;
}

function cardPanel(card) {
    const head = paneHead(cardTitle(card), closeCardPane);
    const remove = el('button', { type: 'button', class: 'danger', onclick: () => removeCard(card.key) }, 'Remove card');
    const plugin = pluginByName(card.plugin);
    const connection = connectionById(card.connection);
    if (card.type !== 'PLUGIN' || !plugin || !connection) {
        const why = card.type !== 'PLUGIN' ? 'This kind of card is no longer supported. Remove it and add a card in its place.'
            : 'This card needs the plugin "' + (card.plugin || '?') + '", which is not on the device.';
        // A card can be there before its plugin: after an update from older firmware, or when the plugin was deleted.
        const install = card.type === 'PLUGIN' && card.plugin
            ? el('button', { type: 'button', class: 'primary', onclick: () => installByName(card.plugin) }, 'Install ' + card.plugin) : null;
        return [head, el('p', { class: 'hint' }, why), el('div', { class: 'row-buttons' }, install, remove)];
    }

    // The device, with this card on its screen and buttons that work.
    const mirror = el('canvas', { width: '233', height: '135' });
    const screen = screenFor(card.config);
    screen.mirror = mirror;
    copyPicture(screen.canvas, mirror);
    const press = (button, label, glyph) => el('button', {
        type: 'button', class: 'hw', 'aria-label': label + ' button', title: label,
        onclick: () => pressButton(card.key, button),
    }, glyph);
    const device = el('div', { class: 'device' },
        el('div', { class: 'hw-buttons' }, press('up', 'Up', '▲'), press('center', 'Centre', '●'), press('down', 'Down', '▼')),
        el('div', { class: 'screen' }, mirror));

    const kind = kindOf(card, plugin);
    const fallback = defaultCardName(card);
    const named = card.name && card.name !== fallback && card.name !== card.plugin;
    const nameInput = el('input', { type: 'text', value: named ? card.name : '', placeholder: card.title || fallback });
    const cardFields = el('div', {}, fieldInputs(plugin.multiCard && kind ? kind.config : [], card.settings, [],
        { connection: connection.id, kind: kind ? kind.key : '' }));
    // A single-card plugin has no separate connection to speak of: its settings are this card's settings.
    const ownFields = el('div', {}, plugin.multiCard ? [] : fieldInputs(plugin.config, connection.settings, connection.secretsSet,
        { connection: connection.id, kind: '' }));
    // The device's own setting, which every plugin card has: may the plugin put this card on screen?
    const frontInput = el('input', { type: 'checkbox', checked: card.front !== false });
    const front = el('label', { class: 'field check' }, frontInput, el('span', {}, 'May come to the front',
        el('small', {}, 'When its plugin has something to tell you. Off: the light flashes, and the card says it when you get there.')));

    const save = el('button', { type: 'button', class: 'primary' }, 'Save');
    save.addEventListener('click', async () => {
        save.disabled = true;
        const done = await attempt(async () => {
            await api('POST', '/api/cards/update?key=' + encode(card.config),
                { name: nameInput.value.trim() || fallback, settings: readFields(cardFields), front: frontInput.checked });
            const own = readFields(ownFields);
            if (Object.keys(own).length) {
                await api('POST', '/api/connections/update?id=' + encode(connection.id), { settings: own });
            }
        }, 'Saved.');
        save.disabled = false;
        if (!done) return;
        if (!isWide()) ui.openCard = null;  // on a phone, back to the list
        ui.cardPaneSignature = '';          // show what the device saved
        await refresh();
        refreshSoon();
    });
    const move = (delta, label) => {
        const button = el('button', { type: 'button', onclick: () => moveCard(card.key, delta) }, label);
        button.dataset.move = String(delta);
        return button;
    };

    const form = el('div', { class: 'pane-form' },
        el('label', { class: 'field' }, el('span', {}, 'Name'), nameInput), cardFields, ownFields, front);
    if (plugin.multiCard && plugin.config.length) {
        const link = el('a', { href: '#connections', onclick: event => {
            event.preventDefault();
            ui.openConnection = connection.id;
            showPage('connections', true);
            renderConnections();
        } }, 'Change them under Connections');
        form.append(el('p', { class: 'hint' }, 'Shares the settings of "' + connection.name + '". ', link, '.'));
    }
    return [head, device, el('p', { class: 'pane-status' }), form,
        el('div', { class: 'row-buttons' }, save, el('span', { class: 'spacer' }), move(-1, 'Move earlier'), move(1, 'Move later')),
        el('div', { class: 'row-buttons' }, remove)];
}

/** Press a button on the device from here. A card that is not on screen is brought up first. */
async function pressButton(key, button) {
    const card = cardByKey(key);
    if (!card) return;
    await attempt(async () => {
        if (!card.onScreen) await api('POST', '/api/cards/show?key=' + encode(card.config));
        await api('POST', '/api/cards/press?key=' + encode(card.config) + '&button=' + button);
    });
    setTimeout(refresh, 250);
    setTimeout(refresh, 1000);
}

async function saveLayout(keys) {
    await api('POST', '/api/cards/layout', keys);
}

async function applyOrder(keys) {
    state.cards.sort((a, b) => keys.indexOf(a.key) - keys.indexOf(b.key));
    if (await attempt(() => saveLayout(keys))) refreshSoon();
    else { ui.cardsSignature = ''; refresh(); }
}

async function moveCard(key, delta) {
    const keys = state.cards.map(c => c.key);
    const from = keys.indexOf(key);
    const to = from + delta;
    if (from < 0 || to < 0 || to >= keys.length) return;
    keys.splice(to, 0, keys.splice(from, 1)[0]);
    const order = applyOrder(keys);
    renderCards();
    await order;
}

async function removeCard(key) {
    const card = cardByKey(key);
    if (!card || !confirm('Remove "' + cardTitle(card) + '" from your DeskHog?')) return;
    const keys = state.cards.filter(c => c !== card).map(c => c.key);
    if (await attempt(() => saveLayout(keys), 'Card removed.')) {
        ui.openCard = null;
        await refresh();
        refreshSoon();
    }
}

// Drag a card to reorder. A mouse drags the whole tile once it has moved a
// little, so a plain click still opens the card. A finger drags by the handle,
// because dragging a tile is how you scroll the page.
function armDrag(event, item, handle) {
    const onHandle = handle.contains(event.target);
    if (event.pointerType === 'mouse' ? event.button !== 0 : !onHandle) return;
    const list = $('cards-list');
    const order = () => [...list.querySelectorAll('.item')].map(i => i.dataset.key);
    const before = order().join('\n');
    const startX = event.clientX, startY = event.clientY;
    let active = false;
    if (onHandle) event.preventDefault();

    const move = moveEvent => {
        if (!active) {
            if (!onHandle && Math.hypot(moveEvent.clientX - startX, moveEvent.clientY - startY) < 6) return;
            active = true;
            ui.dragging = true;
            item.classList.add('dragging');
        }
        moveEvent.preventDefault();
        const x = moveEvent.clientX, y = moveEvent.clientY;
        const target = [...list.querySelectorAll('.item')].find(other => {
            if (other === item) return false;
            const box = other.getBoundingClientRect();
            return x >= box.left && x <= box.right && y >= box.top && y <= box.bottom;
        });
        if (!target) return;
        const box = target.getBoundingClientRect();
        const inGrid = list.clientWidth > box.width * 1.5; // tiles side by side, or one per row
        const after = inGrid ? x > box.left + box.width / 2 : y > box.top + box.height / 2;
        list.insertBefore(item, after ? target.nextSibling : target);
    };
    const drop = () => {
        window.removeEventListener('pointermove', move);
        window.removeEventListener('pointerup', drop);
        window.removeEventListener('pointercancel', drop);
        if (!active) return;
        item.classList.remove('dragging');
        ui.dragging = false;
        ui.justDragged = true;
        setTimeout(() => { ui.justDragged = false; }, 0);
        const keys = order();
        if (keys.join('\n') === before) return;
        ui.cardsSignature = keys.join('\n'); // the tiles already are in this order
        applyOrder(keys).then(renderCards);
    };
    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', drop);
    window.addEventListener('pointercancel', drop);
}

// ------------------------------------------------- pictures of the cards

// The device sends a card's pixels as it has them: 16 bits per pixel (RGB565).
// Each card has a small picture in the list, and a second one (`mirror`) while
// it is in the side pane.
const screens = {}; // card config ("connection:card") -> {canvas, mirror, frames, busy}

function screenFor(config) {
    if (!screens[config]) {
        screens[config] = { canvas: el('canvas', { width: '233', height: '135' }), mirror: null, frames: -1, version: null, busy: false };
    }
    return screens[config];
}

/** Undo the device's packing: runs of (how many, pixel), three bytes each. */
function unpackRuns(buffer, pixelCount) {
    const bytes = new Uint8Array(buffer);
    const pixels = new Uint16Array(pixelCount);
    let at = 0;
    for (let i = 0; i + 2 < bytes.length && at < pixelCount; i += 3) {
        const end = Math.min(pixelCount, at + bytes[i]);
        pixels.fill(bytes[i + 1] | (bytes[i + 2] << 8), at, end);
        at = end;
    }
    return pixels.buffer;
}

function paintScreen(canvas, buffer, width, height) {
    const pixels = new Uint16Array(buffer, 0, Math.floor(buffer.byteLength / 2));
    if (pixels.length < width * height) return;
    canvas.width = width;
    canvas.height = height;
    const context = canvas.getContext('2d');
    if (!context) return;
    const image = context.createImageData(width, height);
    for (let i = 0; i < width * height; i++) {
        const pixel = pixels[i];
        image.data[i * 4] = ((pixel >> 11) & 0x1f) * 255 / 31;
        image.data[i * 4 + 1] = ((pixel >> 5) & 0x3f) * 255 / 63;
        image.data[i * 4 + 2] = (pixel & 0x1f) * 255 / 31;
        image.data[i * 4 + 3] = 255;
    }
    context.putImageData(image, 0, 0);
}

function copyPicture(from, to) {
    const context = to.getContext('2d');
    if (!context) return;
    to.width = from.width;
    to.height = from.height;
    context.drawImage(from, 0, 0);
}

let screenQueue = Promise.resolve();

/**
 * Fetch a card's picture. One at a time: the device is small.
 * With `ifChanged`, the device only sends it when it has painted the card since.
 */
function fetchScreen(config, frames, ifChanged) {
    const screen = screenFor(config);
    if (screen.busy) return;
    screen.busy = true;
    screenQueue = screenQueue.then(async () => {
        try {
            const since = ifChanged && screen.version !== null ? '&since=' + screen.version : '';
            const response = await fetch('/api/cards/screen?packed=1&key=' + encode(config) + since);
            if (response.ok && response.status !== 204) {
                screen.version = response.headers.get('X-Frames');
                const width = parseInt(response.headers.get('X-Width'), 10) || 233;
                const height = parseInt(response.headers.get('X-Height'), 10) || 135;
                let buffer = await response.arrayBuffer();
                if (response.headers.get('X-Packed') === 'runs') buffer = unpackRuns(buffer, width * height);
                paintScreen(screen.canvas, buffer, width, height);
                if (screen.mirror) paintScreen(screen.mirror, buffer, width, height);
                if (frames !== undefined) screen.frames = frames;
            }
        } catch (error) { /* next round */ }
        screen.busy = false;
    });
}

function updateScreens() {
    for (const card of state.cards) {
        if (card.type !== 'PLUGIN' || card.frames === undefined) continue;
        if (screenFor(card.config).frames !== card.frames) fetchScreen(card.config, card.frames);
    }
}

/** The card in the side pane is fetched every second, so the buttons feel live. */
function watchOpenCard() {
    if (document.hidden || ui.page !== 'cards' || ui.adding || ui.dragging) return;
    const card = cardByKey(ui.openCard);
    if (card && card.type === 'PLUGIN' && screenFor(card.config).mirror) fetchScreen(card.config, undefined, true);
}

// --------------------------------------------------------------- add card

function startAdding() {
    if (!state) return;
    ui.adding = { step: 'pick' };
    renderCards();
    renderDirectory(); // the choices include what the directory has: fetch it if that has not happened yet
}

/** Install a plugin from the directory and carry on adding a card of it. */
async function installAndAdd(entry) {
    if (!(await installEntry(entry))) return;
    const plugin = pluginByName(entry.name);
    if (!plugin || plugin.error || !plugin.kinds.length) return; // the plugins page says what is wrong with it
    // A plugin with several kinds of card: back to the choices, which now include them.
    ui.adding = plugin.kinds.length === 1 ? { step: 'plugin', plugin, kind: plugin.kinds[0] } : { step: 'pick' };
    renderCards();
}

/** A picture for a choice, when the directory has one. */
function choicePicture(entry) {
    const shot = entry ? shotAddress(entry) : null;
    return shot ? el('img', { class: 'shot', src: shot, alt: '', loading: 'lazy', width: '233', height: '135', onerror: event => event.target.remove() }) : null;
}

/** What the side pane shows while a card is being added. */
function addPanel() {
    const adding = ui.adding;
    const cancel = el('button', { type: 'button', onclick: closeCardPane }, 'Cancel');

    if (adding.step === 'pick') {
        const entries = directory.entries || [];
        const choices = [];
        for (const plugin of state.plugins) {
            if (plugin.error) continue;
            // One picture per plugin: it only says what the card looks like when the plugin has one kind of card.
            const picture = plugin.kinds.length === 1 ? choicePicture(entries.find(entry => entry.name === plugin.name)) : null;
            for (const kind of plugin.kinds) {
                choices.push(el('button', {
                    type: 'button', class: 'choice',
                    onclick: () => { ui.adding = { step: 'plugin', plugin, kind }; renderCards(); },
                }, picture, el('b', {}, plugin.multiCard ? kind.name : plugin.title), plugin.multiCard ? el('span', {}, plugin.title) : null));
            }
        }
        // What the directory has and this device does not: choosing one installs it, then carries on.
        const more = entries.filter(entry => !pluginByName(entry.name)).map(entry => {
            const choice = el('button', { type: 'button', class: 'choice', disabled: !!directory.busy, onclick: () => installAndAdd(entry) },
                choicePicture(entry), el('b', {}, entry.title || entry.name),
                el('span', {}, directory.busy === entry.name ? 'Installing…' : entry.description || ''));
            choice.dataset.name = entry.name;
            return choice;
        });
        return [paneHead('Add a card', closeCardPane),
            el('p', { class: 'hint' }, 'What should the new card show?'),
            choices.length ? el('div', { class: 'choices' }, choices) : null,
            choices.length || more.length || directory.loading ? null : el('p', { class: 'empty' }, 'No plugins are installed yet.'),
            more.length ? el('h3', { class: 'sub' }, choices.length ? 'More, from the plugin directory' : 'From the plugin directory') : null,
            more.length ? el('p', { class: 'hint' }, 'Choosing one installs its plugin first.') : null,
            more.length ? el('div', { class: 'choices more' }, more) : null,
            directory.loading ? el('p', { class: 'hint' }, 'Looking in the plugin directory…') : null,
            directory.error ? el('p', { class: 'hint' }, directory.error) : null,
            el('div', { class: 'row-buttons' },
                el('button', { type: 'button', onclick: showDirectory }, 'Open the plugin directory'))];
    }

    const { plugin, kind } = adding;
    const existing = state.connections.filter(c => c.plugin === plugin.name);
    const shared = plugin.multiCard && plugin.config.length > 0;
    const nameInput = el('input', { type: 'text', placeholder: plugin.multiCard ? kind.name : plugin.title });
    // A setting the plugin lists the choices for needs a connection to ask. A
    // card for an existing connection has one; a new connection does not yet.
    const newFields = plugin.multiCard ? kind.config : plugin.config;
    const sourceFor = id => (plugin.multiCard && id ? { connection: id, kind: kind.key } : null);
    const cardFields = el('div', {}, fieldInputs(newFields, {}, [], sourceFor(shared || !existing.length ? '' : existing[0].id)));

    // Cards of this plugin can share a connection. Ask which one only when there is something to share.
    let select = null;
    const connectionName = el('input', { type: 'text', placeholder: plugin.title });
    const connectionFields = el('div', {}, shared ? fieldInputs(plugin.config, {}) : []);
    const newConnection = el('div', {},
        el('label', { class: 'field' }, el('span', {}, 'Name for this connection'), connectionName,
            el('small', {}, 'For example "Work", if you connect more than one account.')),
        connectionFields);
    if (shared) {
        select = el('select', {},
            existing.map(c => el('option', { value: c.id }, c.name)),
            el('option', { value: '' }, existing.length ? 'A new connection…' : 'New connection'));
        const sync = () => {
            newConnection.hidden = select.value !== '';
            // The lists to pick from belong to the connection: ask the one that is chosen now.
            if (newFields.some(field => field.choices)) {
                cardFields.replaceChildren(...fieldInputs(newFields, readFields(cardFields), [], sourceFor(select.value)));
            }
        };
        select.addEventListener('change', sync);
        sync();
    }

    const add = el('button', { type: 'button', class: 'primary' }, 'Add card');
    add.addEventListener('click', async () => {
        const request = { plugin: plugin.name, kind: kind.key, name: nameInput.value.trim() };
        if (!plugin.multiCard) {
            request.connectionSettings = readFields(cardFields);
        } else {
            request.settings = readFields(cardFields);
            if (shared) {
                request.connection = select.value;
                if (!select.value) {
                    request.connectionName = connectionName.value.trim();
                    request.connectionSettings = readFields(connectionFields);
                }
            } else {
                request.connection = existing.length ? existing[0].id : '';
            }
        }
        add.disabled = true;
        let answer = null;
        const done = await attempt(async () => { answer = await api('POST', '/api/cards/add', request); }, 'Card added. It is on your DeskHog now.');
        add.disabled = false;
        if (!done) return;
        ui.adding = null;
        // On a wide screen the new card goes straight into the pane.
        ui.openCard = isWide() && answer && answer.key ? 'PLUGIN|' + answer.key : null;
        await refresh();
        renderCards();
        refreshSoon();
    });

    const back = el('button', { type: 'button', onclick: () => { ui.adding = { step: 'pick' }; renderCards(); } }, 'Back');
    return [paneHead('New card: ' + (plugin.multiCard ? plugin.title + ', ' + kind.name : plugin.title), closeCardPane),
        shared ? el('label', { class: 'field' }, el('span', {}, 'Connection'), select) : null,
        shared ? newConnection : null,
        cardFields,
        el('label', { class: 'field' }, el('span', {}, 'Name (optional)'), nameInput),
        el('div', { class: 'row-buttons' }, add, back, cancel)];
}

// ------------------------------------------------------------ connections

// Only connections with settings to share are worth showing. The rest exist
// quietly, one per card.
function sharedConnections() {
    return state.connections.filter(connection => {
        const plugin = pluginByName(connection.plugin);
        return plugin && plugin.multiCard && plugin.config.length > 0;
    });
}

function renderConnections() {
    if (!state) return;
    const connections = sharedConnections();
    // On a wide screen the pane starts with the first connection rather than empty.
    if (!ui.choseFirstConnection && connections.length) {
        ui.choseFirstConnection = true;
        if (isWide() && !ui.openConnection) ui.openConnection = connections[0].id;
    }
    const list = $('connections-list');
    const signature = connections.map(c => c.id).join('\n');
    if (signature !== ui.connectionsSignature) {
        ui.connectionsSignature = signature;
        list.replaceChildren(...connections.map(connection => {
            const open = el('button', { type: 'button', class: 'item-open', onclick: () => { ui.openConnection = connection.id; renderConnections(); } },
                el('span', { class: 'item-text' }, el('span', { class: 'item-title' }), el('span', { class: 'item-sub' })));
            const item = el('div', { class: 'item' }, open);
            item.dataset.id = connection.id;
            return item;
        }));
    }
    $('connections-empty').hidden = connections.length > 0;
    list.querySelectorAll('.item').forEach(item => {
        const connection = connections.find(c => c.id === item.dataset.id);
        if (!connection) return;
        const plugin = pluginByName(connection.plugin);
        item.querySelector('.item-title').textContent = connection.name;
        const sub = item.querySelector('.item-sub');
        const failed = connection.state === 'failed';
        sub.textContent = failed ? 'Crashed: ' + (connection.error || 'unknown error')
            : plugin.title + ', ' + connection.cards + (connection.cards === 1 ? ' card' : ' cards');
        sub.className = 'item-sub' + (failed ? ' bad' : '');
        item.classList.toggle('selected', ui.openConnection === connection.id);
    });

    const pane = $('connection-pane');
    const connection = connections.find(c => c.id === ui.openConnection);
    if (!connection) ui.openConnection = null;
    pane.hidden = !connection;
    const paneSignature = connection ? connection.id + '|' + state.cards.filter(c => c.connection === connection.id).map(c => c.key).join(',') : '';
    if (paneSignature !== ui.connectionPaneSignature) {
        ui.connectionPaneSignature = paneSignature;
        pane.replaceChildren(...(connection ? connectionPanel(connection) : []).filter(Boolean));
    }
}

function connectionPanel(connection) {
    const plugin = pluginByName(connection.plugin);
    const close = () => { ui.openConnection = null; renderConnections(); };
    const nameInput = el('input', { type: 'text', value: connection.name, placeholder: plugin.title });
    const fields = el('div', {}, fieldInputs(plugin.config, connection.settings, connection.secretsSet,
        { connection: connection.id, kind: '' }));
    const save = el('button', { type: 'button', class: 'primary' }, 'Save');
    save.addEventListener('click', async () => {
        save.disabled = true;
        const done = await attempt(() => api('POST', '/api/connections/update?id=' + encode(connection.id),
            { name: nameInput.value.trim() || plugin.title, settings: readFields(fields) }), 'Saved. Its cards are restarting.');
        save.disabled = false;
        if (!done) return;
        if (!isWide()) ui.openConnection = null;
        ui.connectionPaneSignature = '';
        await refresh();
        refreshSoon();
    });
    const remove = el('button', { type: 'button', class: 'danger' }, 'Delete connection');
    remove.addEventListener('click', async () => {
        const cards = connection.cards === 1 ? 'its 1 card' : 'its ' + connection.cards + ' cards';
        if (!confirm('Delete "' + connection.name + '" and ' + cards + '?')) return;
        if (await attempt(() => api('POST', '/api/connections/remove?id=' + encode(connection.id)), 'Connection deleted.')) {
            ui.openConnection = null;
            await refresh();
            refreshSoon();
        }
    });
    // The cards that use it. Each one is a way to that card.
    const cards = state.cards.filter(c => c.connection === connection.id);
    const usedBy = cards.map(card => el('li', {}, el('button', { type: 'button', onclick: () => {
        ui.adding = null;
        ui.openCard = card.key;
        showPage('cards', true);
        renderCards();
    } }, cardTitle(card))));
    return [paneHead(plugin.title + ' connection', close),
        el('label', { class: 'field' }, el('span', {}, 'Name'), nameInput), fields,
        el('div', { class: 'row-buttons' }, save, el('span', { class: 'spacer' }), remove),
        el('p', { class: 'group-label' }, cards.length ? 'Cards that use it' : 'No cards use it yet.'),
        el('ul', { class: 'used-by' }, usedBy)];
}

// ---------------------------------------------------------------- plugins

const NEW_PLUGIN_TEMPLATE = `-- A new plugin. Save it and it shows up as a card.

plugin = { name = "My plugin" }

local presses = 0

function draw()
  ui.title("My plugin")
  ui.big(tostring(presses))
  ui.label("Press the centre button")
end

function button(name)
  if name == "center" then
    presses = presses + 1
    return true
  end
end
`;

let pluginStatusTimer = null;

function renderPlugins() {
    const signature = state.plugins.map(p => p.name + (p.error || '') + p.title + (p.version || '')).join('\n');
    if (signature !== ui.pluginsSignature) {
        ui.pluginsSignature = signature;
        const rows = state.plugins.map(plugin => {
            const sub = plugin.error ? 'Does not load: ' + plugin.error
                : [plugin.multiCard ? 'Cards: ' + plugin.kinds.map(k => k.name).join(', ') : plugin.name + '.lua',
                    plugin.version ? 'version ' + plugin.version : null].filter(Boolean).join(', ');
            const open = el('button', { type: 'button', class: 'item-open', onclick: () => editPlugin(plugin.name) },
                el('span', { class: 'item-text' },
                    el('span', { class: 'item-title' }, plugin.title),
                    el('span', { class: 'item-sub' + (plugin.error ? ' bad' : '') }, sub)));
            const item = el('div', { class: 'item' }, open);
            item.dataset.name = plugin.name;
            return item;
        });
        $('plugins-list').replaceChildren(...rows);
        if (!rows.length) $('plugins-list').appendChild(el('p', { class: 'empty' }, 'No plugins on this device yet.'));
    }
    $('plugins-list').querySelectorAll('.item').forEach(item => item.classList.toggle('selected', item.dataset.name === ui.openPlugin));
}

function setPluginResult(text, kind) {
    const result = $('plugin-result');
    result.textContent = text || '';
    result.className = 'result' + (kind ? ' ' + kind : '');
}

/** Line numbers beside the code: a crash names the line it happened on. */
function updateGutter() {
    const lines = $('plugin-source').value.split('\n').length;
    const gutter = $('plugin-gutter');
    if (gutter.dataset.lines === String(lines)) return;
    gutter.dataset.lines = String(lines);
    let text = '';
    for (let line = 1; line <= lines; line++) text += line + '\n';
    gutter.textContent = text;
}

function openPluginEditor(name, source, isNew) {
    $('directory-pane').hidden = true; // the code takes its place
    ui.openPlugin = isNew ? '' : name;
    $('plugin-heading').textContent = isNew ? 'New plugin' : name + '.lua';
    $('plugin-name').value = name;
    $('plugin-name-field').hidden = !isNew;
    $('delete-plugin-btn').hidden = isNew;
    $('plugin-source').value = source;
    $('plugin-source').scrollTop = 0;
    updateGutter();
    setPluginResult('');
    $('plugin-log').hidden = true;
    $('plugin-editor').hidden = false;
    rememberPage();
    if (state) renderPlugins();
    if (isNew) $('plugin-name').focus();
}

function closePluginEditor() {
    ui.openPlugin = null;
    $('plugin-editor').hidden = true;
    clearTimeout(pluginStatusTimer);
    rememberPage();
    if (state) renderPlugins();
    if (state) renderDirectory();
}

async function editPlugin(name) {
    await attempt(async () => {
        openPluginEditor(name, await api('GET', '/api/plugins/' + encode(name)), false);
        showPluginStatus(name);
    });
}

async function savePlugin() {
    const name = $('plugin-name').value.trim();
    const source = $('plugin-source').value;
    if (!/^[a-z0-9_-]{1,32}$/.test(name)) { setPluginResult('Names are 1-32 characters: lowercase letters, digits, - and _.', 'error'); return; }
    if (!source.trim()) { setPluginResult('The plugin is empty.', 'error'); return; }
    setPluginResult('Saving…');
    try {
        // Not text/plain: the device's web server would read Lua source that way as a form and drop it.
        await api('PUT', '/api/plugins/' + encode(name), source, 'application/octet-stream');
    } catch (error) {
        setPluginResult(error.message, 'error');
        return;
    }
    ui.openPlugin = name;
    rememberPage();
    $('plugin-heading').textContent = name + '.lua';
    $('plugin-name-field').hidden = true;
    $('delete-plugin-btn').hidden = false;
    setPluginResult('Saved. Starting on the device…');
    clearTimeout(pluginStatusTimer);
    pluginStatusTimer = setTimeout(() => { showPluginStatus(name); refresh(); }, 1500);
}

/** How the plugin is doing, and what it printed. `quiet` leaves a message about saving alone. */
async function showPluginStatus(name, quiet) {
    let status;
    try { status = await api('GET', '/api/plugins/' + encode(name) + '/status'); } catch (error) { return; }
    if (ui.openPlugin !== name) return;
    const busy = /^Sav/.test($('plugin-result').textContent);
    if (!(quiet && busy)) {
        if (status.state === 'failed') setPluginResult('Crashed: ' + status.error, 'error');
        else if (status.state === 'running') setPluginResult('Running (' + Math.round((status.memory || 0) / 1024) + ' KB of memory).', 'success');
        else setPluginResult('Installed. Add a card for it to run it.');
    }
    const log = $('plugin-log');
    log.hidden = !(status.log && status.log.length);
    log.textContent = (status.log || []).join('\n');
}

async function deletePlugin() {
    const plugin = pluginByName(ui.openPlugin);
    if (!plugin) return;
    const used = state.cards.filter(c => c.plugin === plugin.name).length;
    const what = used ? ' and the ' + used + (used === 1 ? ' card' : ' cards') + ' that use it' : '';
    if (!confirm('Delete the plugin "' + plugin.title + '"' + what + '?')) return;
    if (await attempt(() => api('DELETE', '/api/plugins/' + encode(plugin.name)), 'Plugin deleted.')) {
        closePluginEditor();
        await refresh();
        refreshSoon();
    }
}

// ------------------------------------------------------- plugin directory

// A plugin directory is a folder of plugins with an index.json that lists them,
// usually in a git repository. This page fetches it: the browser has the
// internet connection and checks certificates; the device only remembers where
// the directory is.
const directory = { source: null, entries: null, error: '', loading: false, pages: null, base: '', changing: false, busy: '' };

/** Where a directory's index.json is, and where to read a file of it. Mirrors tools/deskhog. */
function directoryAddress(source) {
    source = String(source || '').trim();
    if (/^https?:\/\//.test(source)) {
        return { index: source.endsWith('.json') ? source : source.replace(/\/+$/, '') + '/index.json', pages: null };
    }
    const match = /^([A-Za-z0-9_.-]+)\/([A-Za-z0-9_.-]+)(?:@([A-Za-z0-9_.\/-]+))?$/.exec(source);
    if (!match) return null;
    const ref = match[3] || 'HEAD';
    return {
        index: 'https://raw.githubusercontent.com/' + match[1] + '/' + match[2] + '/' + ref + '/plugins/index.json',
        pages: 'https://github.com/' + match[1] + '/' + match[2] + '/blob/' + ref + '/plugins/',
    };
}

/** The same fingerprint of a plugin's source that the device reports (FNV-1a, 32 bits). */
function sourceHash(bytes) {
    let hash = 2166136261;
    for (let i = 0; i < bytes.length; i++) hash = Math.imul(hash ^ bytes[i], 16777619) >>> 0;
    return ('0000000' + hash.toString(16)).slice(-8);
}

async function loadDirectory() {
    const source = state && state.directory ? state.directory.source : '';
    const address = directoryAddress(source);
    Object.assign(directory, { source, entries: null, error: '', loading: true, pages: address && address.pages, base: address ? address.index : '' });
    renderDirectory();
    try {
        if (!address) throw new Error('"' + source + '" is not a plugin directory. Give "owner/repo" or the address of an index.json.');
        let response;
        try {
            response = await fetch(address.index, { cache: 'no-store' });
        } catch (error) {
            throw new Error('Could not reach the plugin directory. That needs internet on the computer or phone you are using now, not on the DeskHog.');
        }
        if (!response.ok) throw new Error('There is no plugin directory at ' + source + ' (the answer was ' + response.status + ').');
        let index = null;
        try { index = JSON.parse(await response.text()); } catch (error) { index = null; }
        if (!index || !Array.isArray(index.plugins)) throw new Error('What is at ' + source + ' is not a plugin directory.');
        if (index.format > 1) throw new Error('That directory is written for newer firmware than this device has.');
        directory.entries = index.plugins.filter(entry => entry && /^[a-z0-9_-]{1,32}$/.test(entry.name) && typeof entry.file === 'string');
    } catch (error) {
        directory.error = error.message;
    }
    directory.loading = false;
    renderDirectory();
}

/** Where the picture of a directory entry's card is, if it has one. Only a plain path inside the directory counts. */
function shotAddress(entry) {
    const shot = entry.screenshot;
    if (typeof shot !== 'string' || !/^[A-Za-z0-9_./-]+\.png$/.test(shot) || shot.startsWith('/') || shot.includes('..')) return null;
    return new URL(shot, directory.base).href;
}

/** How a directory entry relates to what is on the device. */
function offerState(entry) {
    const installed = pluginByName(entry.name);
    if (!installed) return { action: 'Install' };
    if (installed.hash === entry.hash) return { note: 'Installed' };
    if ((installed.version || '') !== (entry.version || '')) return { action: 'Update to ' + (entry.version || 'this one'), note: installed.version ? 'You have ' + installed.version : null };
    return { action: 'Replace with this one', note: 'Changed on this device' };
}

async function installEntry(entry) {
    directory.busy = entry.name;
    renderDirectory();
    const done = await attempt(async () => {
        let response;
        try {
            response = await fetch(new URL(entry.file, directory.base).href, { cache: 'no-store' });
        } catch (error) {
            throw new Error('Could not download ' + entry.file + ' from the directory.');
        }
        if (!response.ok) throw new Error('The directory lists ' + entry.file + ', but it is not there (' + response.status + ').');
        const bytes = new Uint8Array(await response.arrayBuffer());
        if (entry.hash && sourceHash(bytes) !== entry.hash) {
            throw new Error(entry.file + ' is not the file the directory describes. Try again in a few minutes; it may be in the middle of an update.');
        }
        // "card=no": installing is not adding a card. That is the next step, with the settings you want.
        await api('PUT', '/api/plugins/' + encode(entry.name) + '?card=no', bytes, 'application/octet-stream');
    }, (entry.title || entry.name) + ' is on your DeskHog.');
    directory.busy = '';
    if (done) {
        // The device looks at the new file on its next round.
        await new Promise(resolve => setTimeout(resolve, 600));
        await refresh();
        refreshSoon();
    }
    renderDirectory();
    return done;
}

/** Install a plugin a card is waiting for. */
async function installByName(name) {
    if (!directory.entries || directory.source !== state.directory.source) await loadDirectory();
    const entry = (directory.entries || []).find(e => e.name === name);
    if (!entry) {
        notify(directory.error || 'The plugin directory has no plugin called "' + name + '".', true);
        return;
    }
    await installEntry(entry);
}

/** From another page: go to where the plugin directory is shown. */
function showDirectory() {
    if (ui.openPlugin !== null) closePluginEditor();
    ui.adding = null;
    showPage('plugins', true);
    renderCards();
    // On a phone it is below the list of installed plugins: go there, also once the list has arrived.
    ui.showingDirectory = !isWide();
    scrollToDirectory();
}

function scrollToDirectory() {
    const pane = $('directory-pane');
    if (ui.showingDirectory && pane.scrollIntoView) pane.scrollIntoView();
    if (!directory.loading) ui.showingDirectory = false;
}

/** Everything the directory's list depends on: it is rebuilt only when this changes. */
function offersSignature() {
    return [directory.source, directory.loading, directory.error, directory.changing, directory.busy,
        (directory.entries || []).map(entry => { const o = offerState(entry); return entry.name + ':' + (o.action || '') + ':' + (o.note || ''); }).join(',')].join('|');
}

function renderDirectory() {
    const pane = $('directory-pane');
    pane.hidden = ui.openPlugin !== null;
    if (!state) return;
    // It is fetched when someone looks: on the plugins page, or when choosing what a new card shows.
    const choosing = !!ui.adding && ui.adding.step === 'pick';
    if (ui.page !== 'plugins' && !choosing && directory.source === null) return;
    // The first look, or the device was pointed at another directory since.
    if (!directory.loading && directory.source !== state.directory.source) { loadDirectory(); return; }
    if (choosing) renderCardPane();

    const offers = (directory.entries || []).map(entry => Object.assign({ entry }, offerState(entry)));
    const signature = offersSignature();
    if (signature === ui.directorySignature) return;
    ui.directorySignature = signature;

    const sourceLine = el('p', { class: 'source-line' }, 'From ', el('b', {}, directory.source || ''),
        el('button', { type: 'button', onclick: () => { directory.changing = !directory.changing; renderDirectory(); } }, directory.changing ? 'Cancel' : 'Change'));
    let change = null;
    if (directory.changing) {
        const input = el('input', { type: 'text', value: directory.source || '', autocomplete: 'off', autocapitalize: 'off', spellcheck: 'false' });
        const use = async source => {
            if (source && !directoryAddress(source)) { notify('Give "owner/repo" as on GitHub, or the address of an index.json.', true); return; }
            if (await attempt(() => api('POST', '/api/directory', { source }))) {
                directory.changing = false;
                await refresh();
                loadDirectory();
            }
        };
        change = el('div', { class: 'panel' },
            el('label', { class: 'field' }, el('span', {}, 'Plugin directory'), input,
                el('small', {}, 'A GitHub repository with a plugins folder, as "owner/repo" or "owner/repo@branch". Or the full address of an index.json. Only use one you trust: plugins can reach the internet and see the settings you give them.')),
            el('div', { class: 'row-buttons' },
                el('button', { type: 'button', class: 'primary', onclick: () => use(input.value.trim()) }, 'Use this directory'),
                state.directory.source !== state.directory.default
                    ? el('button', { type: 'button', onclick: () => use('') }, 'Back to ' + state.directory.default) : null));
    }

    const rows = offers.map(offer => {
        const entry = offer.entry;
        const facts = [entry.author ? 'By ' + entry.author : null, entry.version ? 'version ' + entry.version : null].filter(Boolean).join(', ');
        const address = directory.pages ? directory.pages + entry.file : new URL(entry.file, directory.base).href;
        const installed = pluginByName(entry.name);
        const busy = directory.busy === entry.name;
        const shot = shotAddress(entry);
        const item = el('div', { class: 'item offer' },
            // The card as the plugin's author showed it. A picture that will not load just goes away.
            shot ? el('img', { class: 'shot', src: shot, alt: '', loading: 'lazy', width: '233', height: '135', onerror: event => event.target.remove() }) : null,
            el('span', { class: 'item-text' },
                el('span', { class: 'item-title' }, entry.title || entry.name),
                el('span', { class: 'item-sub' }, entry.description || ''),
                el('span', { class: 'about-plugin' }, facts, facts ? '. ' : '', el('a', { href: address, target: '_blank', rel: 'noopener' }, 'Read the code'))),
            el('span', { class: 'actions' },
                offer.note ? el('small', {}, offer.note) : null,
                offer.action ? el('button', { type: 'button', class: installed ? null : 'primary', disabled: !!directory.busy, onclick: () => installEntry(entry) }, busy ? 'Installing…' : offer.action) : null,
                installed && !installed.error ? el('button', { type: 'button', onclick: () => addCardOf(entry.name) }, 'Add a card') : null));
        item.dataset.name = entry.name;
        return item;
    });

    pane.replaceChildren(...[
        el('div', { class: 'pane-head' }, el('h2', {}, 'Get plugins')),
        sourceLine, change,
        directory.loading ? el('p', { class: 'empty' }, 'Looking in the directory…') : null,
        directory.error ? el('p', { class: 'result error' }, directory.error) : null,
        directory.error ? el('div', { class: 'row-buttons' }, el('button', { type: 'button', onclick: loadDirectory }, 'Try again')) : null,
        el('div', { class: 'list' }, rows),
        !directory.loading && !directory.error && !rows.length ? el('p', { class: 'empty' }, 'This directory is empty.') : null,
    ].filter(Boolean));
    scrollToDirectory();
}

/** Go and add a card of a plugin that was just installed. */
function addCardOf(name) {
    const plugin = pluginByName(name);
    if (!plugin || !plugin.kinds.length) return;
    ui.adding = plugin.kinds.length === 1 ? { step: 'plugin', plugin, kind: plugin.kinds[0] } : { step: 'pick' };
    showPage('cards', true);
    renderCards();
}

// ------------------------------------------------------------------- wifi

const wifi = { selected: '', signature: '', password: null, form: null };

function buildWifiForm() {
    wifi.password = el('input', { type: 'password', autocomplete: 'off' });
    wifi.list = el('div', { class: 'networks' });
    wifi.connect = el('button', { type: 'button', class: 'primary', disabled: true }, 'Connect');
    wifi.connect.addEventListener('click', connectWifi);
    const scan = el('button', { type: 'button', onclick: () => attempt(() => api('POST', '/api/actions/start-wifi-scan'), 'Looking for networks…') }, 'Scan again');
    wifi.form = el('div', {},
        wifi.list,
        el('label', { class: 'field' }, el('span', {}, 'Password'), wifi.password,
            el('small', {}, 'Leave empty if the network has none.')),
        el('div', { class: 'row-buttons' }, wifi.connect, scan));
}

function renderWifi() {
    const info = deviceStatus.wifi || {};
    const connected = !!info.is_connected;
    const pill = $('wifi-pill');
    pill.textContent = connected ? info.connected_ssid + ', ' + info.ip_address : 'No WiFi';
    pill.className = 'pill' + (connected ? ' ok' : '');

    // The same form is the first thing on every page while offline, and sits on the Device page once online.
    $('wifi-setup').hidden = connected;
    $('wifi-panel').hidden = !connected;
    const home = connected ? $('wifi-change') : $('wifi-form');
    if (wifi.form.parentNode !== home) home.appendChild(wifi.form);

    // Strongest signal first, one row per network name.
    const seen = new Set();
    const networks = (info.networks || []).filter(n => n.ssid).sort((a, b) => b.rssi - a.rssi)
        .filter(n => !seen.has(n.ssid) && seen.add(n.ssid));
    const signature = networks.map(n => n.ssid).join('\n') + '|' + wifi.selected + '|' + info.connected_ssid;
    if (signature === wifi.signature) return;
    wifi.signature = signature;
    wifi.list.replaceChildren(...networks.map(network => {
        const current = connected && network.ssid === info.connected_ssid;
        return el('button', {
            type: 'button', class: 'network' + (network.ssid === wifi.selected ? ' selected' : ''),
            onclick: () => { wifi.selected = network.ssid; wifi.connect.disabled = false; renderWifi(); wifi.password.focus(); },
        }, el('span', {}, network.ssid), el('small', {}, current ? 'Connected' : (network.encrypted ? 'Secured' : 'Open')));
    }));
    if (!networks.length) wifi.list.appendChild(el('p', { class: 'empty' }, 'Looking for networks…'));
}

async function connectWifi() {
    const form = new FormData();
    form.append('ssid', wifi.selected);
    form.append('password', wifi.password.value);
    wifi.connect.disabled = true;
    const done = await attempt(() => api('POST', '/api/actions/save-wifi', form));
    wifi.connect.disabled = false;
    if (done) {
        notify('DeskHog is joining "' + wifi.selected + '". If this page stops loading, put this device back on your own WiFi and open http://deskhog.local.');
    }
}

// ------------------------------------------------------ status and extras

async function pollStatus() {
    if (document.hidden) return;
    try {
        deviceStatus = await api('GET', '/api/status');
    } catch (error) {
        $('wifi-pill').textContent = 'Not reachable';
        $('wifi-pill').className = 'pill';
        return;
    }
    renderWifi();
    renderAbout();
}

function renderAbout() {
    const rows = [];
    const add = (label, value) => { if (value !== undefined && value !== null && value !== '') rows.push(el('dt', {}, label), el('dd', {}, String(value))); };
    const kb = bytes => Math.round(bytes / 1024) + ' KB';
    if (deviceStatus) {
        add('Firmware', deviceStatus.firmware_version);
        if (deviceStatus.wifi && deviceStatus.wifi.is_connected) add('Address', deviceStatus.wifi.ip_address);
    }
    if (state) {
        add('Cards', state.cards.length + ' of 12');
        add('Plugins', state.plugins.length);
        if (state.storage && state.storage.total) add('Plugin storage', kb(state.storage.used) + ' of ' + kb(state.storage.total) + ' used');
        if (state.system) add('Memory free', kb(state.system.psram_free) + ' for plugins, ' + kb(state.system.heap_free) + ' internal');
    }
    $('about-list').replaceChildren(...rows);
}

// ------------------------------------------------------------------ start

function start() {
    buildWifiForm();

    // Pages: the address remembers which one, so reloading stays put.
    document.querySelectorAll('a[data-page]').forEach(link => link.addEventListener('click', event => {
        event.preventDefault();
        showPage(link.dataset.page, true);
    }));
    window.addEventListener('hashchange', () => showPage(location.hash.slice(1)));
    showPage(location.hash.slice(1));

    $('add-card-btn').addEventListener('click', startAdding);
    $('cards-get-plugins-btn').addEventListener('click', showDirectory);
    $('new-plugin-btn').addEventListener('click', () => openPluginEditor('', NEW_PLUGIN_TEMPLATE, true));
    $('save-plugin-btn').addEventListener('click', savePlugin);
    $('delete-plugin-btn').addEventListener('click', deletePlugin);
    $('close-plugin-btn').addEventListener('click', closePluginEditor);
    $('upload-plugin-btn').addEventListener('click', () => $('upload-plugin-input').click());
    $('upload-plugin-input').addEventListener('change', event => {
        const file = event.target.files[0];
        if (!file) return;
        const name = file.name.replace(/\.lua$/i, '').toLowerCase().replace(/[^a-z0-9_-]/g, '-').slice(0, 32);
        const reader = new FileReader();
        reader.onload = () => openPluginEditor(name, reader.result, true);
        reader.readAsText(file);
        event.target.value = '';
    });

    const source = $('plugin-source');
    const mac = /Mac|iPhone|iPad/.test(navigator.platform || '');
    $('save-keys').textContent = mac ? '⌘S' : 'Ctrl+S';
    source.addEventListener('input', updateGutter);
    source.addEventListener('scroll', () => { $('plugin-gutter').scrollTop = source.scrollTop; });
    source.addEventListener('keydown', event => {
        if (event.key !== 'Tab') return;
        // Tab indents instead of leaving the editor.
        event.preventDefault();
        const at = source.selectionStart;
        source.value = source.value.slice(0, at) + '  ' + source.value.slice(source.selectionEnd);
        source.selectionStart = source.selectionEnd = at + 2;
    });
    document.addEventListener('keydown', event => {
        if ((event.metaKey || event.ctrlKey) && event.key.toLowerCase() === 's' && ui.page === 'plugins' && ui.openPlugin !== null) {
            event.preventDefault();
            savePlugin();
        } else if (event.key === 'Escape') {
            if (ui.page === 'cards' && (ui.adding || ui.openCard)) closeCardPane();
            else if (ui.page === 'connections' && ui.openConnection) { ui.openConnection = null; renderConnections(); }
        }
    });

    pollStatus();
    refresh();
    setInterval(pollStatus, 3000);
    setInterval(refresh, 3000);
    setInterval(watchOpenCard, 1000);
    document.addEventListener('visibilitychange', () => { if (!document.hidden) { pollStatus(); refresh(); } });
}

// The places someone can pick as a time zone, and what the device is told for
// each: a POSIX TZ string. See "time zones" above.
// <timezones> Written by tools/timezones from the tz database. Do not edit by hand.
const TIME_ZONES = {
"UTC":"UTC0",
"Africa/Abidjan":"GMT0",
"Africa/Accra":"GMT0",
"Africa/Addis_Ababa":"EAT-3",
"Africa/Algiers":"CET-1",
"Africa/Asmara":"EAT-3",
"Africa/Bamako":"GMT0",
"Africa/Bangui":"WAT-1",
"Africa/Banjul":"GMT0",
"Africa/Bissau":"GMT0",
"Africa/Blantyre":"CAT-2",
"Africa/Brazzaville":"WAT-1",
"Africa/Bujumbura":"CAT-2",
"Africa/Cairo":"EET-2EEST,M4.5.5/0,M10.5.4/24",
"Africa/Casablanca":"<+00>0",
"Africa/Ceuta":"CET-1CEST,M3.5.0,M10.5.0/3",
"Africa/Conakry":"GMT0",
"Africa/Dakar":"GMT0",
"Africa/Dar_es_Salaam":"EAT-3",
"Africa/Djibouti":"EAT-3",
"Africa/Douala":"WAT-1",
"Africa/El_Aaiun":"<+00>0",
"Africa/Freetown":"GMT0",
"Africa/Gaborone":"CAT-2",
"Africa/Harare":"CAT-2",
"Africa/Johannesburg":"SAST-2",
"Africa/Juba":"CAT-2",
"Africa/Kampala":"EAT-3",
"Africa/Khartoum":"CAT-2",
"Africa/Kigali":"CAT-2",
"Africa/Kinshasa":"WAT-1",
"Africa/Lagos":"WAT-1",
"Africa/Libreville":"WAT-1",
"Africa/Lome":"GMT0",
"Africa/Luanda":"WAT-1",
"Africa/Lubumbashi":"CAT-2",
"Africa/Lusaka":"CAT-2",
"Africa/Malabo":"WAT-1",
"Africa/Maputo":"CAT-2",
"Africa/Maseru":"SAST-2",
"Africa/Mbabane":"SAST-2",
"Africa/Mogadishu":"EAT-3",
"Africa/Monrovia":"GMT0",
"Africa/Nairobi":"EAT-3",
"Africa/Ndjamena":"WAT-1",
"Africa/Niamey":"WAT-1",
"Africa/Nouakchott":"GMT0",
"Africa/Ouagadougou":"GMT0",
"Africa/Porto-Novo":"WAT-1",
"Africa/Sao_Tome":"GMT0",
"Africa/Tripoli":"EET-2",
"Africa/Tunis":"CET-1",
"Africa/Windhoek":"CAT-2",
"America/Adak":"HST10HDT,M3.2.0,M11.1.0",
"America/Anchorage":"AKST9AKDT,M3.2.0,M11.1.0",
"America/Anguilla":"AST4",
"America/Antigua":"AST4",
"America/Araguaina":"<-03>3",
"America/Argentina/Buenos_Aires":"<-03>3",
"America/Argentina/Catamarca":"<-03>3",
"America/Argentina/Cordoba":"<-03>3",
"America/Argentina/Jujuy":"<-03>3",
"America/Argentina/La_Rioja":"<-03>3",
"America/Argentina/Mendoza":"<-03>3",
"America/Argentina/Rio_Gallegos":"<-03>3",
"America/Argentina/Salta":"<-03>3",
"America/Argentina/San_Juan":"<-03>3",
"America/Argentina/San_Luis":"<-03>3",
"America/Argentina/Tucuman":"<-03>3",
"America/Argentina/Ushuaia":"<-03>3",
"America/Aruba":"AST4",
"America/Asuncion":"<-03>3",
"America/Atikokan":"EST5",
"America/Bahia":"<-03>3",
"America/Bahia_Banderas":"CST6",
"America/Barbados":"AST4",
"America/Belem":"<-03>3",
"America/Belize":"CST6",
"America/Blanc-Sablon":"AST4",
"America/Boa_Vista":"<-04>4",
"America/Bogota":"<-05>5",
"America/Boise":"MST7MDT,M3.2.0,M11.1.0",
"America/Cambridge_Bay":"MST7MDT,M3.2.0,M11.1.0",
"America/Campo_Grande":"<-04>4",
"America/Cancun":"EST5",
"America/Caracas":"<-04>4",
"America/Cayenne":"<-03>3",
"America/Cayman":"EST5",
"America/Chicago":"CST6CDT,M3.2.0,M11.1.0",
"America/Chihuahua":"CST6",
"America/Ciudad_Juarez":"MST7MDT,M3.2.0,M11.1.0",
"America/Costa_Rica":"CST6",
"America/Coyhaique":"<-03>3",
"America/Creston":"MST7",
"America/Cuiaba":"<-04>4",
"America/Curacao":"AST4",
"America/Danmarkshavn":"GMT0",
"America/Dawson":"MST7",
"America/Dawson_Creek":"MST7",
"America/Denver":"MST7MDT,M3.2.0,M11.1.0",
"America/Detroit":"EST5EDT,M3.2.0,M11.1.0",
"America/Dominica":"AST4",
"America/Edmonton":"CST6",
"America/Eirunepe":"<-05>5",
"America/El_Salvador":"CST6",
"America/Fort_Nelson":"MST7",
"America/Fortaleza":"<-03>3",
"America/Glace_Bay":"AST4ADT,M3.2.0,M11.1.0",
"America/Goose_Bay":"AST4ADT,M3.2.0,M11.1.0",
"America/Grand_Turk":"EST5EDT,M3.2.0,M11.1.0",
"America/Grenada":"AST4",
"America/Guadeloupe":"AST4",
"America/Guatemala":"CST6",
"America/Guayaquil":"<-05>5",
"America/Guyana":"<-04>4",
"America/Halifax":"AST4ADT,M3.2.0,M11.1.0",
"America/Havana":"CST5CDT,M3.2.0/0,M11.1.0/1",
"America/Hermosillo":"MST7",
"America/Indiana/Indianapolis":"EST5EDT,M3.2.0,M11.1.0",
"America/Indiana/Knox":"CST6CDT,M3.2.0,M11.1.0",
"America/Indiana/Marengo":"EST5EDT,M3.2.0,M11.1.0",
"America/Indiana/Petersburg":"EST5EDT,M3.2.0,M11.1.0",
"America/Indiana/Tell_City":"CST6CDT,M3.2.0,M11.1.0",
"America/Indiana/Vevay":"EST5EDT,M3.2.0,M11.1.0",
"America/Indiana/Vincennes":"EST5EDT,M3.2.0,M11.1.0",
"America/Indiana/Winamac":"EST5EDT,M3.2.0,M11.1.0",
"America/Inuvik":"CST6",
"America/Iqaluit":"EST5EDT,M3.2.0,M11.1.0",
"America/Jamaica":"EST5",
"America/Juneau":"AKST9AKDT,M3.2.0,M11.1.0",
"America/Kentucky/Louisville":"EST5EDT,M3.2.0,M11.1.0",
"America/Kentucky/Monticello":"EST5EDT,M3.2.0,M11.1.0",
"America/Kralendijk":"AST4",
"America/La_Paz":"<-04>4",
"America/Lima":"<-05>5",
"America/Los_Angeles":"PST8PDT,M3.2.0,M11.1.0",
"America/Lower_Princes":"AST4",
"America/Maceio":"<-03>3",
"America/Managua":"CST6",
"America/Manaus":"<-04>4",
"America/Marigot":"AST4",
"America/Martinique":"AST4",
"America/Matamoros":"CST6CDT,M3.2.0,M11.1.0",
"America/Mazatlan":"MST7",
"America/Menominee":"CST6CDT,M3.2.0,M11.1.0",
"America/Merida":"CST6",
"America/Metlakatla":"AKST9AKDT,M3.2.0,M11.1.0",
"America/Mexico_City":"CST6",
"America/Miquelon":"<-03>3<-02>,M3.2.0,M11.1.0",
"America/Moncton":"AST4ADT,M3.2.0,M11.1.0",
"America/Monterrey":"CST6",
"America/Montevideo":"<-03>3",
"America/Montserrat":"AST4",
"America/Nassau":"EST5EDT,M3.2.0,M11.1.0",
"America/New_York":"EST5EDT,M3.2.0,M11.1.0",
"America/Nome":"AKST9AKDT,M3.2.0,M11.1.0",
"America/Noronha":"<-02>2",
"America/North_Dakota/Beulah":"CST6CDT,M3.2.0,M11.1.0",
"America/North_Dakota/Center":"CST6CDT,M3.2.0,M11.1.0",
"America/North_Dakota/New_Salem":"CST6CDT,M3.2.0,M11.1.0",
"America/Nuuk":"<-02>2<-01>,M3.5.0/-1,M10.5.0/0",
"America/Ojinaga":"CST6CDT,M3.2.0,M11.1.0",
"America/Panama":"EST5",
"America/Paramaribo":"<-03>3",
"America/Phoenix":"MST7",
"America/Port-au-Prince":"EST5EDT,M3.2.0,M11.1.0",
"America/Port_of_Spain":"AST4",
"America/Porto_Velho":"<-04>4",
"America/Puerto_Rico":"AST4",
"America/Punta_Arenas":"<-03>3",
"America/Rankin_Inlet":"CST6CDT,M3.2.0,M11.1.0",
"America/Recife":"<-03>3",
"America/Regina":"CST6",
"America/Resolute":"CST6CDT,M3.2.0,M11.1.0",
"America/Rio_Branco":"<-05>5",
"America/Santarem":"<-03>3",
"America/Santiago":"<-04>4<-03>,M9.1.6/24,M4.1.6/24",
"America/Santo_Domingo":"AST4",
"America/Sao_Paulo":"<-03>3",
"America/Scoresbysund":"<-02>2<-01>,M3.5.0/-1,M10.5.0/0",
"America/Sitka":"AKST9AKDT,M3.2.0,M11.1.0",
"America/St_Barthelemy":"AST4",
"America/St_Johns":"NST3:30NDT,M3.2.0,M11.1.0",
"America/St_Kitts":"AST4",
"America/St_Lucia":"AST4",
"America/St_Thomas":"AST4",
"America/St_Vincent":"AST4",
"America/Swift_Current":"CST6",
"America/Tegucigalpa":"CST6",
"America/Thule":"AST4ADT,M3.2.0,M11.1.0",
"America/Tijuana":"PST8PDT,M3.2.0,M11.1.0",
"America/Toronto":"EST5EDT,M3.2.0,M11.1.0",
"America/Tortola":"AST4",
"America/Vancouver":"MST7",
"America/Whitehorse":"MST7",
"America/Winnipeg":"EST5",
"America/Yakutat":"AKST9AKDT,M3.2.0,M11.1.0",
"Antarctica/Casey":"<+08>-8",
"Antarctica/Davis":"<+07>-7",
"Antarctica/DumontDUrville":"<+10>-10",
"Antarctica/Macquarie":"AEST-10AEDT,M10.1.0,M4.1.0/3",
"Antarctica/Mawson":"<+05>-5",
"Antarctica/McMurdo":"NZST-12NZDT,M9.5.0,M4.1.0/3",
"Antarctica/Palmer":"<-03>3",
"Antarctica/Rothera":"<-03>3",
"Antarctica/Syowa":"<+03>-3",
"Antarctica/Troll":"<+00>0<+02>-2,M3.5.0/1,M10.5.0/3",
"Antarctica/Vostok":"<+05>-5",
"Arctic/Longyearbyen":"CET-1CEST,M3.5.0,M10.5.0/3",
"Asia/Aden":"<+03>-3",
"Asia/Almaty":"<+05>-5",
"Asia/Amman":"<+03>-3",
"Asia/Anadyr":"<+12>-12",
"Asia/Aqtau":"<+05>-5",
"Asia/Aqtobe":"<+05>-5",
"Asia/Ashgabat":"<+05>-5",
"Asia/Atyrau":"<+05>-5",
"Asia/Baghdad":"<+03>-3",
"Asia/Bahrain":"<+03>-3",
"Asia/Baku":"<+04>-4",
"Asia/Bangkok":"<+07>-7",
"Asia/Barnaul":"<+07>-7",
"Asia/Beirut":"EET-2EEST,M3.5.0/0,M10.5.0/0",
"Asia/Bishkek":"<+06>-6",
"Asia/Brunei":"<+08>-8",
"Asia/Chita":"<+09>-9",
"Asia/Colombo":"<+0530>-5:30",
"Asia/Damascus":"<+03>-3",
"Asia/Dhaka":"<+06>-6",
"Asia/Dili":"<+09>-9",
"Asia/Dubai":"<+04>-4",
"Asia/Dushanbe":"<+05>-5",
"Asia/Famagusta":"EET-2EEST,M3.5.0/3,M10.5.0/4",
"Asia/Gaza":"EET-2EEST,M3.4.4/50,M10.4.4/50",
"Asia/Hebron":"EET-2EEST,M3.4.4/50,M10.4.4/50",
"Asia/Ho_Chi_Minh":"<+07>-7",
"Asia/Hong_Kong":"HKT-8",
"Asia/Hovd":"<+07>-7",
"Asia/Irkutsk":"<+08>-8",
"Asia/Jakarta":"WIB-7",
"Asia/Jayapura":"WIT-9",
"Asia/Jerusalem":"IST-2IDT,M3.4.4/26,M10.5.0",
"Asia/Kabul":"<+0430>-4:30",
"Asia/Kamchatka":"<+12>-12",
"Asia/Karachi":"PKT-5",
"Asia/Kathmandu":"<+0545>-5:45",
"Asia/Khandyga":"<+09>-9",
"Asia/Kolkata":"IST-5:30",
"Asia/Krasnoyarsk":"<+07>-7",
"Asia/Kuala_Lumpur":"<+08>-8",
"Asia/Kuching":"<+08>-8",
"Asia/Kuwait":"<+03>-3",
"Asia/Macau":"CST-8",
"Asia/Magadan":"<+11>-11",
"Asia/Makassar":"WITA-8",
"Asia/Manila":"PST-8",
"Asia/Muscat":"<+04>-4",
"Asia/Nicosia":"EET-2EEST,M3.5.0/3,M10.5.0/4",
"Asia/Novokuznetsk":"<+07>-7",
"Asia/Novosibirsk":"<+07>-7",
"Asia/Omsk":"<+06>-6",
"Asia/Oral":"<+05>-5",
"Asia/Phnom_Penh":"<+07>-7",
"Asia/Pontianak":"WIB-7",
"Asia/Pyongyang":"KST-9",
"Asia/Qatar":"<+03>-3",
"Asia/Qostanay":"<+05>-5",
"Asia/Qyzylorda":"<+05>-5",
"Asia/Riyadh":"<+03>-3",
"Asia/Sakhalin":"<+11>-11",
"Asia/Samarkand":"<+05>-5",
"Asia/Seoul":"KST-9",
"Asia/Shanghai":"CST-8",
"Asia/Singapore":"<+08>-8",
"Asia/Srednekolymsk":"<+11>-11",
"Asia/Taipei":"CST-8",
"Asia/Tashkent":"<+05>-5",
"Asia/Tbilisi":"<+04>-4",
"Asia/Tehran":"<+0330>-3:30",
"Asia/Thimphu":"<+06>-6",
"Asia/Tokyo":"JST-9",
"Asia/Tomsk":"<+07>-7",
"Asia/Ulaanbaatar":"<+08>-8",
"Asia/Urumqi":"<+06>-6",
"Asia/Ust-Nera":"<+10>-10",
"Asia/Vientiane":"<+07>-7",
"Asia/Vladivostok":"<+10>-10",
"Asia/Yakutsk":"<+09>-9",
"Asia/Yangon":"<+0630>-6:30",
"Asia/Yekaterinburg":"<+05>-5",
"Asia/Yerevan":"<+04>-4",
"Atlantic/Azores":"<-01>1<+00>,M3.5.0/0,M10.5.0/1",
"Atlantic/Bermuda":"AST4ADT,M3.2.0,M11.1.0",
"Atlantic/Canary":"WET0WEST,M3.5.0/1,M10.5.0",
"Atlantic/Cape_Verde":"<-01>1",
"Atlantic/Faroe":"WET0WEST,M3.5.0/1,M10.5.0",
"Atlantic/Madeira":"WET0WEST,M3.5.0/1,M10.5.0",
"Atlantic/Reykjavik":"GMT0",
"Atlantic/South_Georgia":"<-02>2",
"Atlantic/St_Helena":"GMT0",
"Atlantic/Stanley":"<-03>3",
"Australia/Adelaide":"ACST-9:30ACDT,M10.1.0,M4.1.0/3",
"Australia/Brisbane":"AEST-10",
"Australia/Broken_Hill":"ACST-9:30ACDT,M10.1.0,M4.1.0/3",
"Australia/Darwin":"ACST-9:30",
"Australia/Eucla":"<+0845>-8:45",
"Australia/Hobart":"AEST-10AEDT,M10.1.0,M4.1.0/3",
"Australia/Lindeman":"AEST-10",
"Australia/Lord_Howe":"<+1030>-10:30<+11>-11,M10.1.0,M4.1.0",
"Australia/Melbourne":"AEST-10AEDT,M10.1.0,M4.1.0/3",
"Australia/Perth":"AWST-8",
"Australia/Sydney":"AEST-10AEDT,M10.1.0,M4.1.0/3",
"Europe/Amsterdam":"CET-1CEST,M3.5.0,M10.5.0/3",
"Europe/Andorra":"CET-1CEST,M3.5.0,M10.5.0/3",
"Europe/Astrakhan":"<+04>-4",
"Europe/Athens":"EET-2EEST,M3.5.0/3,M10.5.0/4",
"Europe/Belgrade":"CET-1CEST,M3.5.0,M10.5.0/3",
"Europe/Berlin":"CET-1CEST,M3.5.0,M10.5.0/3",
"Europe/Bratislava":"CET-1CEST,M3.5.0,M10.5.0/3",
"Europe/Brussels":"CET-1CEST,M3.5.0,M10.5.0/3",
"Europe/Bucharest":"EET-2EEST,M3.5.0/3,M10.5.0/4",
"Europe/Budapest":"CET-1CEST,M3.5.0,M10.5.0/3",
"Europe/Busingen":"CET-1CEST,M3.5.0,M10.5.0/3",
"Europe/Chisinau":"EET-2EEST,M3.5.0/3,M10.5.0/4",
"Europe/Copenhagen":"CET-1CEST,M3.5.0,M10.5.0/3",
"Europe/Dublin":"GMT0IST,M3.5.0/1,M10.5.0",
"Europe/Gibraltar":"CET-1CEST,M3.5.0,M10.5.0/3",
"Europe/Guernsey":"GMT0BST,M3.5.0/1,M10.5.0",
"Europe/Helsinki":"EET-2EEST,M3.5.0/3,M10.5.0/4",
"Europe/Isle_of_Man":"GMT0BST,M3.5.0/1,M10.5.0",
"Europe/Istanbul":"<+03>-3",
"Europe/Jersey":"GMT0BST,M3.5.0/1,M10.5.0",
"Europe/Kaliningrad":"EET-2",
"Europe/Kirov":"MSK-3",
"Europe/Kyiv":"EET-2EEST,M3.5.0/3,M10.5.0/4",
"Europe/Lisbon":"WET0WEST,M3.5.0/1,M10.5.0",
"Europe/Ljubljana":"CET-1CEST,M3.5.0,M10.5.0/3",
"Europe/London":"GMT0BST,M3.5.0/1,M10.5.0",
"Europe/Luxembourg":"CET-1CEST,M3.5.0,M10.5.0/3",
"Europe/Madrid":"CET-1CEST,M3.5.0,M10.5.0/3",
"Europe/Malta":"CET-1CEST,M3.5.0,M10.5.0/3",
"Europe/Mariehamn":"EET-2EEST,M3.5.0/3,M10.5.0/4",
"Europe/Minsk":"<+03>-3",
"Europe/Monaco":"CET-1CEST,M3.5.0,M10.5.0/3",
"Europe/Moscow":"MSK-3",
"Europe/Oslo":"CET-1CEST,M3.5.0,M10.5.0/3",
"Europe/Paris":"CET-1CEST,M3.5.0,M10.5.0/3",
"Europe/Podgorica":"CET-1CEST,M3.5.0,M10.5.0/3",
"Europe/Prague":"CET-1CEST,M3.5.0,M10.5.0/3",
"Europe/Riga":"EET-2EEST,M3.5.0/3,M10.5.0/4",
"Europe/Rome":"CET-1CEST,M3.5.0,M10.5.0/3",
"Europe/Samara":"<+04>-4",
"Europe/San_Marino":"CET-1CEST,M3.5.0,M10.5.0/3",
"Europe/Sarajevo":"CET-1CEST,M3.5.0,M10.5.0/3",
"Europe/Saratov":"<+04>-4",
"Europe/Simferopol":"MSK-3",
"Europe/Skopje":"CET-1CEST,M3.5.0,M10.5.0/3",
"Europe/Sofia":"EET-2EEST,M3.5.0/3,M10.5.0/4",
"Europe/Stockholm":"CET-1CEST,M3.5.0,M10.5.0/3",
"Europe/Tallinn":"EET-2EEST,M3.5.0/3,M10.5.0/4",
"Europe/Tirane":"CET-1CEST,M3.5.0,M10.5.0/3",
"Europe/Ulyanovsk":"<+04>-4",
"Europe/Vaduz":"CET-1CEST,M3.5.0,M10.5.0/3",
"Europe/Vatican":"CET-1CEST,M3.5.0,M10.5.0/3",
"Europe/Vienna":"CET-1CEST,M3.5.0,M10.5.0/3",
"Europe/Vilnius":"EET-2EEST,M3.5.0/3,M10.5.0/4",
"Europe/Volgograd":"MSK-3",
"Europe/Warsaw":"CET-1CEST,M3.5.0,M10.5.0/3",
"Europe/Zagreb":"CET-1CEST,M3.5.0,M10.5.0/3",
"Europe/Zurich":"CET-1CEST,M3.5.0,M10.5.0/3",
"Indian/Antananarivo":"EAT-3",
"Indian/Chagos":"<+06>-6",
"Indian/Christmas":"<+07>-7",
"Indian/Cocos":"<+0630>-6:30",
"Indian/Comoro":"EAT-3",
"Indian/Kerguelen":"<+05>-5",
"Indian/Mahe":"<+04>-4",
"Indian/Maldives":"<+05>-5",
"Indian/Mauritius":"<+04>-4",
"Indian/Mayotte":"EAT-3",
"Indian/Reunion":"<+04>-4",
"Pacific/Apia":"<+13>-13",
"Pacific/Auckland":"NZST-12NZDT,M9.5.0,M4.1.0/3",
"Pacific/Bougainville":"<+11>-11",
"Pacific/Chatham":"<+1245>-12:45<+1345>,M9.5.0/2:45,M4.1.0/3:45",
"Pacific/Chuuk":"<+10>-10",
"Pacific/Easter":"<-06>6<-05>,M9.1.6/22,M4.1.6/22",
"Pacific/Efate":"<+11>-11",
"Pacific/Fakaofo":"<+13>-13",
"Pacific/Fiji":"<+12>-12",
"Pacific/Funafuti":"<+12>-12",
"Pacific/Galapagos":"<-06>6",
"Pacific/Gambier":"<-09>9",
"Pacific/Guadalcanal":"<+11>-11",
"Pacific/Guam":"ChST-10",
"Pacific/Honolulu":"HST10",
"Pacific/Kanton":"<+13>-13",
"Pacific/Kiritimati":"<+14>-14",
"Pacific/Kosrae":"<+11>-11",
"Pacific/Kwajalein":"<+12>-12",
"Pacific/Majuro":"<+12>-12",
"Pacific/Marquesas":"<-0930>9:30",
"Pacific/Midway":"SST11",
"Pacific/Nauru":"<+12>-12",
"Pacific/Niue":"<-11>11",
"Pacific/Norfolk":"<+11>-11<+12>,M10.1.0,M4.1.0/3",
"Pacific/Noumea":"<+11>-11",
"Pacific/Pago_Pago":"SST11",
"Pacific/Palau":"<+09>-9",
"Pacific/Pitcairn":"<-08>8",
"Pacific/Pohnpei":"<+11>-11",
"Pacific/Port_Moresby":"<+10>-10",
"Pacific/Rarotonga":"<-10>10",
"Pacific/Saipan":"ChST-10",
"Pacific/Tahiti":"<-10>10",
"Pacific/Tarawa":"<+12>-12",
"Pacific/Tongatapu":"<+13>-13",
"Pacific/Wake":"<+12>-12",
"Pacific/Wallis":"<+12>-12",
};
// </timezones>

if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start);
else start();
