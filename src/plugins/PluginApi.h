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
 *   POST /api/cards/add                     {plugin, kind, name, settings, connection | connectionName + connectionSettings}
 *   POST /api/cards/update?key=<c>:<n>      {name, settings}
 *   POST /api/cards/layout                  ["<TYPE>|<config>", ...] in the new order; cards left out are removed
 *   GET  /api/cards/screen?key=<c>:<n>      the card's pixels, raw RGB565 (width and height in X-Width, X-Height)
 *   POST /api/cards/press?key=..&button=..  press up, center or down for a card
 *   POST /api/cards/show?key=..             bring a card to the front
 *   POST /api/connections/add               {plugin, name, settings}
 *   POST /api/connections/update?id=<c>     {name, settings}
 *   POST /api/connections/remove?id=<c>     also removes its cards
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
