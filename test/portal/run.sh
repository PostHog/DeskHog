#!/usr/bin/env bash
# Test the web page (html/portal.*) without a browser or a device: it runs in
# jsdom against a pretend device. Needs Node.js; jsdom is installed on first
# use into .pio/portal-test.

set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
work="$root/.pio/portal-test"
mkdir -p "$work"
if [ ! -d "$work/node_modules/jsdom" ]; then
    (cd "$work" && npm init -y >/dev/null && npm install --silent jsdom >/dev/null)
fi
NODE_PATH="$work/node_modules" node "$root/test/portal/portal.test.js" "$root"
