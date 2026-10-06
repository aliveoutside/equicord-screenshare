#!/usr/bin/env bash
# Vencord, a Discord client mod
# Copyright (c) 2026 Vendicated and contributors
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

plugin=$(dirname "$(realpath "${BASH_SOURCE[0]}")")
repo=$(realpath "$plugin/../../..")
discord="${XDG_CONFIG_HOME:-$HOME/.config}/discord"
build_only=false
action=install
while (($#)); do
    case "$1" in
        --uninstall | --install-openasar | --uninstall-openasar)
            action=${1#--}
            shift
            ;;
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
                '  --uninstall     Remove Equicord from Discord.' \
                '  --install-openasar / --uninstall-openasar  Manage OpenAsar.' \
                'Fully restart Discord after changes.'
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
if "$build_only" && [[ $action != install ]]; then
    printf '%s\n' '--build-only cannot be combined with a management action.' >&2
    exit 2
fi
tools=(node)
if [[ $action == install ]]; then
    tools+=(pnpm g++ pkg-config)
fi
for tool in "${tools[@]}"; do
    command -v "$tool" >/dev/null || {
        printf 'Required tool is missing: %s\n' "$tool" >&2
        exit 1
    }
done
if [[ $action == install ]]; then
    pkg-config --exists gio-2.0 gio-unix-2.0 libpipewire-0.3 libpulse || {
        printf 'Install the GIO, PipeWire and PulseAudio development libraries.\n' >&2
        exit 1
    }
    headers=$(node -p 'process.env.NODE_INCLUDE_DIR ?? require("node:path").resolve(require("node:path").dirname(process.execPath), "../include/node")')
    [[ -f "$headers/node_api.h" ]] || {
        printf 'Node development headers are missing. Run setup.sh first.\n' >&2
        exit 1
    }
fi
discord=$(realpath -m "$discord")
if ! "$build_only" && [[ ! -e "$discord/Discord" ]]; then
    printf 'Discord was not found at %s. Use --discord to select its configuration directory.\n' "$discord" >&2
    exit 1
fi
trap 'printf "Installation stopped at line %s. See the error above.\n" "$LINENO" >&2' ERR
cd "$repo"
if [[ $action == install ]]; then
    printf 'Installing dependencies and building WaylandScreenshare…\n'
    pnpm install --frozen-lockfile
    node "$plugin/build.mjs"
    pnpm build --disable-updater
    if "$build_only"; then
        printf 'Build complete. Discord was not modified.\n'
        exit 0
    fi
fi
installer="$repo/dist/Installer/EquilotlCli-Linux"
if [[ ! -x "$installer" ]]; then
    node scripts/runInstaller.mjs -- --help
fi
[[ -x "$installer" ]] || {
    printf 'Could not download the official installer.\n' >&2
    exit 1
}
printf 'Running official installer: %s…\n' "$action"
EQUICORD_USER_DATA_DIR="$repo" EQUICORD_DIRECTORY="$repo/dist/desktop" EQUICORD_DEV_INSTALL=1 \
    "$installer" "--$action" --location "$discord"
if [[ $action == install || $action == uninstall ]]; then
    node --input-type=module - "$repo" "$discord" "$action" <<'JS'
import assert from 'node:assert/strict';
import { realpathSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { extractFile } from '@electron/asar';

const [repo, discord, action] = process.argv.slice(2);
const host = dirname(realpathSync(join(discord, 'Discord')));
const archive = join(host, 'resources/app.asar');
const expected = `require(${JSON.stringify(join(repo, 'dist/desktop'))})`;
if (action === 'install') {
    const bootstrap = extractFile(archive, 'index.js').toString().trim();
    assert.equal(bootstrap, expected, 'Discord is not connected to this Equicord build.');
} else {
    let bootstrap = '';
    try {
        bootstrap = extractFile(archive, 'index.js').toString().trim();
    } catch (error) {
        if (!String(error).includes('was not found')) throw error;
    }
    assert.notEqual(bootstrap, expected, 'Discord still points to this Equicord build.');
}
console.log(`Discord ${action} verified.`);
JS
fi
printf 'Done. Fully close Discord, including the tray, then reopen it.\n'
