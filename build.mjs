/*
 * Vencord, a Discord client mod
 * Copyright (c) 2026 Vendicated and contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

import { spawnSync } from "node:child_process";
import { existsSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

if (process.platform === "linux" && process.arch === "x64") {
    const source = fileURLToPath(new URL(".", import.meta.url));
    const output = source;
    const headers =
        process.env.NODE_INCLUDE_DIR ?? resolve(dirname(process.execPath), "../include/node");
    if (!existsSync(resolve(headers, "node_api.h")))
        throw new Error(
            "Node development headers are missing. Run setup.sh to install the build tools."
        );
    const portal = spawnSync(
        "pkg-config",
        ["--cflags", "--libs", "gio-2.0", "gio-unix-2.0", "libpipewire-0.3", "libpulse"],
        { encoding: "utf8" }
    );
    if (portal.error) {
        throw portal.error;
    }
    if (portal.status !== 0)
        throw new Error(
            "GIO, PipeWire and PulseAudio development libraries are required for WaylandScreenshare."
        );
    const build = spawnSync(
        "g++",
        [
            "-std=c++17",
            "-shared",
            "-fPIC",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-O2",
            `-I${headers}`,
            resolve(source, "capture-hook.cpp"),
            resolve(source, "portal.cpp"),
            resolve(source, "audio.cpp"),
            resolve(source, "pipewire-audio.cpp"),
            resolve(source, "discord-audio.cpp"),
            resolve(source, "audio-symbols.cpp"),
            resolve(source, "audio-selection.cpp"),
            resolve(source, "capture-resolver.cpp"),
            ...portal.stdout.trim().split(/\s+/),
            "-ldl",
            "-pthread",
            "-o",
            resolve(output, "capture-hook.node")
        ],
        { stdio: "inherit" }
    );
    if (build.error) {
        throw build.error;
    }
    if (build.status !== 0)
        throw new Error("Could not build WaylandScreenshare native capture module.");
    console.log("Built WaylandScreenshare native capture module.");
}
