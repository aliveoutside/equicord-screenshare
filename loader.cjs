/*
 * Vencord, a Discord client mod
 * Copyright (c) 2026 Vendicated and contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
const unavailable = error => ({ installed: false, error, created: 0, updated: 0, renditions: 0, destroyed: 0, failed: 0, active: 0 });
let addon;
let failure;
try {
    addon = require("./capture-hook.node");
} catch {
    failure = "Could not load the native capture module.";
}
module.exports = {
    install: () => addon ? addon.install() : unavailable(failure),
    status: () => addon ? addon.status() : unavailable(failure),
    changeWindow: () => addon ? addon.changeWindow() : Promise.resolve({ ...unavailable(failure), success: false, cancelled: false }),
    startAudio: () => addon ? addon.startAudio() : { ready: false, error: failure },
    stopAudio: () => addon?.stopAudio(),
    audioStatus: () => addon ? addon.audioStatus() : { ready: false, error: failure },
    listAudio: () => addon ? addon.listAudio() : [],
    identifyWindowAudio: () => addon ? addon.identifyWindowAudio() : Promise.resolve({ application: "", error: failure }),
    selectAudio: (kind, target) => addon ? addon.selectAudio(kind, target) : { ready: false, error: failure }
};
