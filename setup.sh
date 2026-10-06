#!/usr/bin/env bash
# Vencord, a Discord client mod
# Copyright (c) 2026 Vendicated and contributors
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

root="${XDG_DATA_HOME:-$HOME/.local/share}/equicord-screenshare"
discord="${XDG_CONFIG_HOME:-$HOME/.config}/discord"
command=install
while (($#)); do
    case "$1" in
        install | update)
            command=$1
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
        -h | --help)
            printf '%s\n' \
                'Usage: wayland-screenshare [install|update] [--discord PATH]' \
                'Install build tools, download Equicord and build the screenshare plugin.' \
                'Run wayland-screenshare update to install updates.' \
                'Official Discord on Linux x64 is required. Fully restart it after installation.'
            exit 0
            ;;
        *)
            printf 'Unknown argument: %s\n' "$1" >&2
            exit 2
            ;;
    esac
done
[[ $(uname -s) == Linux && $(uname -m) == x86_64 ]] || {
    printf 'This plugin requires Linux x64.\n' >&2
    exit 1
}
[[ $EUID -ne 0 ]] || {
    printf 'Run this as your normal user, without sudo. It asks for sudo only when installing system packages.\n' >&2
    exit 1
}
mkdir -p "$root"
root=$(realpath "$root")
exec 9>"$root/setup.lock"
flock -n 9 || {
    printf 'Another screenshare installation is running.\n' >&2
    exit 1
}
if [[ $command == update && -f "$root/discord-path" && $discord == "${XDG_CONFIG_HOME:-$HOME/.config}/discord" ]]; then
    IFS= read -r discord <"$root/discord-path"
fi
discord=$(realpath -m "$discord")
[[ -e "$discord/Discord" ]] || {
    printf 'Discord was not found at %s. Launch official Discord once, or use --discord /path/to/config.\n' "$discord" >&2
    exit 1
}
trap 'printf "Setup stopped at line %s. Fix the error above, then run the same command again.\n" "$LINENO" >&2' ERR
missing=false
for tool in git curl g++ pkg-config make xz; do
    command -v "$tool" >/dev/null || missing=true
done
if command -v pkg-config >/dev/null; then
    pkg-config --exists gio-2.0 gio-unix-2.0 libpipewire-0.3 libpulse || missing=true
fi
if "$missing"; then
    command -v sudo >/dev/null || {
        printf 'Install sudo or ask your administrator to install the build dependencies.\n' >&2
        exit 1
    }
    . /etc/os-release
    printf 'Installing missing build tools. Your system may ask for your password.\n'
    case " ${ID:-} ${ID_LIKE:-} " in
        *arch* | *cachyos*) sudo pacman -S --needed --noconfirm git curl gcc make pkgconf glib2 pipewire libpulse xz ;;
        *debian* | *ubuntu*)
            sudo apt-get update
            sudo apt-get install -y git curl ca-certificates build-essential pkg-config libglib2.0-dev libpipewire-0.3-dev libpulse-dev xz-utils
            ;;
        *fedora*) sudo dnf install -y git curl gcc-c++ make pkgconf-pkg-config glib2-devel pipewire-devel pulseaudio-libs-devel xz ;;
        *)
            printf 'Automatic dependency installation supports Arch, Debian, Ubuntu and Fedora. Install the dependencies listed in the README, then rerun setup.\n' >&2
            exit 1
            ;;
    esac
fi
mkdir -p "$root/tools"
export PATH="$root/tools/node/bin:$root/tools/bin:$PATH"
if [[ ! -x "$root/tools/node/bin/node" ]]; then
    printf 'Downloading a private Node.js build…\n'
    temporary=$(mktemp -d "$root/tools/download.XXXXXX")
    curl --fail --show-error --silent --location https://nodejs.org/dist/latest-v24.x/SHASUMS256.txt -o "$temporary/SHASUMS256.txt"
    archive=$(awk '$2 ~ /^node-v24\.[0-9]+\.[0-9]+-linux-x64\.tar\.xz$/ { print $2; exit }' "$temporary/SHASUMS256.txt")
    [[ -n "$archive" ]] || {
        printf 'Could not find the Node.js Linux download.\n' >&2
        exit 1
    }
    curl --fail --show-error --location "https://nodejs.org/dist/latest-v24.x/$archive" -o "$temporary/$archive"
    (
        cd "$temporary"
        sha256sum --check --ignore-missing SHASUMS256.txt
    )
    mkdir "$temporary/node"
    tar -xJf "$temporary/$archive" --strip-components=1 -C "$temporary/node"
    [[ $(realpath -m "$root/tools/node") == "$root/tools/node" ]] || {
        printf 'Unexpected tool directory.\n' >&2
        exit 1
    }
    mv "$temporary/node" "$root/tools/node"
fi
node -e 'if (Number(process.versions.node.split(".")[0]) < 22) process.exit(1)'
repo="$root/Equicord"
plugin="$repo/src/userplugins/waylandScreenshare"
if [[ ! -d "$repo/.git" ]]; then
    [[ ! -e "$repo" ]] || {
        printf 'The installation directory exists without a Git checkout: %s\n' "$repo" >&2
        exit 1
    }
    printf 'Downloading Equicord…\n'
    git clone --depth 1 https://github.com/Equicord/Equicord.git "$repo"
fi
if [[ ! -d "$plugin/.git" ]]; then
    [[ ! -e "$plugin" ]] || {
        printf 'The plugin directory already exists without a Git checkout: %s\n' "$plugin" >&2
        exit 1
    }
    printf 'Downloading the screenshare plugin…\n'
    git clone --depth 1 https://github.com/aliveoutside/equicord-screenshare.git "$plugin"
fi
for checkout in "$repo" "$plugin"; do
    [[ -z $(git -C "$checkout" status --porcelain --untracked-files=normal) ]] || {
        printf 'Local source changes found in %s. Save them before installing updates.\n' "$checkout" >&2
        exit 1
    }
    git -C "$checkout" pull --ff-only
done
pnpm_version=$(node -p "require(process.argv[1]).packageManager.replace(/^pnpm@/, '').split('+')[0]" "$repo/package.json")
if [[ ! -x "$root/tools/bin/pnpm" ]] || [[ $("$root/tools/bin/pnpm" --version) != "$pnpm_version" ]]; then
    printf 'Installing the build package manager…\n'
    npm install --global --prefix "$root/tools" "pnpm@$pnpm_version"
fi
bash "$plugin/install.sh" --discord "$discord"
printf '%s\n' "$discord" >"$root/discord-path"
mkdir -p "$HOME/.local/bin"
launcher="$HOME/.local/bin/wayland-screenshare"
if [[ -e "$launcher" ]] && ! cmp -s "$plugin/setup.sh" "$launcher"; then
    cp "$launcher" "$root/wayland-screenshare.previous"
fi
cp "$plugin/setup.sh" "$launcher"
chmod +x "$launcher"
printf '\nInstalled. Fully quit Discord, including its tray icon, then reopen it.\n'
if [[ :$PATH: == *":$HOME/.local/bin:"* ]]; then
    printf 'Next time, update with: wayland-screenshare update\n'
else
    printf 'Next time, update with: ~/.local/bin/wayland-screenshare update\n'
fi
