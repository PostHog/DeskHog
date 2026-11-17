#pragma once

class AsyncWebServer;
class PluginManager;

/**
 * The HTTP API for plugins, connections and cards, served next to the rest of
 * the portal. Bodies and answers are JSON unless noted.
 *
 * For the web page:
 *
 *   GET  /api/state                         plugins, connections, the card stack and their live state
 *   POST /api/timezone                      {posix, name}: the device's time zone, as a POSIX TZ string; "" is UTC
 *   POST /api/cards/add                     {plugin, kind, name, settings, front, connection | connectionName + connectionSettings}
 *   POST /api/cards/update?key=<c>:<n>      {name, settings, front}; front: may the card come to the front by itself
 *   POST /api/cards/layout                  ["<TYPE>|<config>", ...] in the new order; cards left out are removed
 *   GET  /api/cards/screen?key=<c>:<n>      the card's pixels, raw RGB565 (width and height in X-Width, X-Height)
 *   POST /api/cards/press?key=..&button=..  press up, center or down for a card
 *   POST /api/cards/show?key=..             bring a card to the front
 *   POST /api/connections/add               {plugin, name, settings}
 *   POST /api/connections/update?id=<c>     {name, settings}
 *   POST /api/connections/remove?id=<c>     also removes its cards
 *   GET  /api/connections/choices?id=<c>&field=<key>[&kind=<kind>][&settings=<JSON>]
 *                                           what the plugin offers for a setting with `choices`:
 *                                           {choices: [{value, label}]}. Answers 202 {pending: true}
 *                                           while the plugin is working on it: ask again
 *
 * For anything that wants to tell a plugin something (body: text or JSON, 4 KB at most):
 *
 *   POST /api/cards/message?key=<c>:<n>     calls the card's message(msg); 404 if it has none, 429 if 8 are waiting
 *   POST /api/connections/message?id=<c>    calls the plugin's global message(msg)
 *
 * For developers:
 *
 *   GET    /api/plugins                     plugin files, storage use and headroom
 *   GET    /api/plugins/<name>              the plugin's source (text)
 *   PUT    /api/plugins/<name>              install or replace (body: Lua source); POST works too
 *   DELETE /api/plugins/<name>              remove the plugin, its connections and cards
 *   GET    /api/plugins/<name>/status       how it is running, last error, print() output
 *
 * Like the rest of the portal there is no login: anyone on the same network
 * can use it.
 */
void registerPluginRoutes(AsyncWebServer& server, PluginManager& manager);
