/*
 * Vencord, a Discord client mod
 * Copyright (c) 2026 Vendicated and contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

import { app, IpcMainInvokeEvent } from "electron";
import captureModule from "file://capture-hook.node?base64";
import captureLoader from "file://loader.cjs";
import { existsSync, mkdirSync, readdirSync, readFileSync, renameSync, writeFileSync } from "fs";
import { join } from "path";

export function prepare(_: IpcMainInvokeEvent) {
    if (process.platform !== "linux" || process.arch !== "x64")
        return { success: false, error: "The capture hook supports Linux x64 only." };
    try {
        const modules = join(app.getPath("userData"), `app-${app.getVersion()}`, "modules");
        const voices = readdirSync(modules, { withFileTypes: true })
            .filter(entry => entry.isDirectory() && /^discord_voice-\d+$/.test(entry.name));
        if (voices.length !== 1)
            return { success: false, error: "Could not identify Discord's voice module." };
        const destination = join(modules, voices[0].name, "discord_equicord_capture");
        mkdirSync(destination, { recursive: true });
        for (const [name, data] of [
            ["capture-hook.node", Buffer.from(captureModule, "base64")],
            ["index.js", Buffer.from(captureLoader)]
        ] as const) {
            const target = join(destination, name);
            if (existsSync(target) && readFileSync(target).equals(data)) continue;
            const temporary = `${target}.installing`;
            writeFileSync(temporary, data);
            renameSync(temporary, target);
        }
        return { success: true, error: "" };
    } catch {
        return { success: false, error: "Could not install the native capture module." };
    }
}
