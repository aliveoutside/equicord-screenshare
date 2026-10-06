#!/usr/bin/env bash
# Vencord, a Discord client mod
# Copyright (c) 2026 Vendicated and contributors
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

plugin=$(dirname "$(realpath "${BASH_SOURCE[0]}")")
repo=$(realpath "$plugin/../../..")
discord="${XDG_CONFIG_HOME:-$HOME/.config}/discord"
build_only=false
while (($#)); do
    case "$1" in
        --discord)
            [[ $# -ge 2 && -n "$2" ]] || {
                printf 'Missing path after --discord.\n' >&2
                exit 2
            }
            discord=$2
            shift 2
            ;;
        --build-only)
            build_only=true
            shift
            ;;
        -h | --help)
            printf '%s\n' \
                'Usage: bash install.sh [--discord PATH] [--build-only]' \
                'Build WaylandScreenshare and Equicord, then install into Discord.' \
                'Run from a plugin cloned into src/userplugins/waylandScreenshare.' \
                '  --discord PATH  Discord configuration directory (default: XDG config/discord).' \
                '  --build-only    Build without installing into Discord.' \
                'Fully restart Discord after installation.'
            exit 0
            ;;
        *)
            printf 'Unknown argument: %s\n' "$1" >&2
            exit 2
            ;;
    esac
done

[[ $(uname -s) == Linux && $(uname -m) == x86_64 ]] || {
    printf 'Linux x64 is required.\n' >&2
    exit 1
}
[[ -f "$repo/package.json" && -f "$repo/scripts/runInstaller.mjs" ]] || {
    printf 'Clone this plugin into an Equicord checkout at src/userplugins/waylandScreenshare.\n' >&2
    exit 1
}
for tool in node pnpm g++ pkg-config; do
    command -v "$tool" >/dev/null || {
        printf 'Required tool is missing: %s\n' "$tool" >&2
        exit 1
    }
done
pkg-config --exists gio-2.0 gio-unix-2.0 libpipewire-0.3 libpulse || {
    printf 'Install the GIO, PipeWire and PulseAudio development libraries.\n' >&2
    exit 1
}
headers=$(node -p 'process.env.NODE_INCLUDE_DIR ?? require("node:path").resolve(require("node:path").dirname(process.execPath), "../include/node")')
[[ -f "$headers/node_api.h" ]] || {
    printf 'Node development headers are missing. Run setup.sh first.\n' >&2
    exit 1
}
discord=$(realpath -m "$discord")
if ! "$build_only" && [[ ! -e "$discord/Discord" ]]; then
    printf 'Discord was not found at %s. Use --discord to select its configuration directory.\n' "$discord" >&2
    exit 1
fi
trap 'printf "Installation stopped at line %s. See the error above.\n" "$LINENO" >&2' ERR
cd "$repo"
printf 'Installing dependencies and building WaylandScreenshare…\n'
pnpm install --frozen-lockfile
node "$plugin/build.mjs"
pnpm build --disable-updater
if "$build_only"; then
    printf 'Build complete. Discord was not modified.\n'
    exit 0
fi
printf 'Installing Equicord into Discord…\n'
installer="$repo/dist/Installer/EquilotlCli-Linux"
if [[ -x "$installer" ]]; then
    EQUICORD_USER_DATA_DIR="$repo" EQUICORD_DIRECTORY="$repo/dist/desktop" EQUICORD_DEV_INSTALL=1 \
        "$installer" --install --location "$discord"
else
    node scripts/runInstaller.mjs -- --install --location "$discord"
fi
node --input-type=module - "$repo" "$discord" <<'JS'
import assert from 'node:assert/strict';
import { realpathSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { extractFile } from '@electron/asar';

const [repo, discord] = process.argv.slice(2);
const host = dirname(realpathSync(join(discord, 'Discord')));
const bootstrap = extractFile(join(host, 'resources/app.asar'), 'index.js').toString().trim();
assert.equal(bootstrap, `require(${JSON.stringify(join(repo, 'dist/desktop'))})`, 'Discord is not connected to this Equicord build.');
console.log('Discord installation verified.');
JS
printf 'Done. Fully close Discord, including the tray, then reopen it.\n'
