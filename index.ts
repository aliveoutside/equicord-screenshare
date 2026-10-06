/*
 * Vencord, a Discord client mod
 * Copyright (c) 2026 Vendicated and contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

import ErrorBoundary from "@components/ErrorBoundary";
import { IS_LINUX } from "@utils/constants";
import { Logger } from "@utils/Logger";
import definePlugin, { PluginNative } from "@utils/types";
import { Button, MediaEngineStore, Menu, React, UserStore } from "@webpack/common";

import { openAudioPicker } from "./audioPicker";
import { CaptureHook, isCaptureHook } from "./capture";

const logger = new Logger("WaylandScreenshare");
let hook: CaptureHook | undefined;
let pickerTimeout: ReturnType<typeof setTimeout> | undefined;

function audioPicker() {
    clearTimeout(pickerTimeout);
    if (hook) {
        openAudioPicker(hook);
    }
}

function checkAudioSelection() {
    clearTimeout(pickerTimeout);
    const deadline = Date.now() + 15_000;
    let readySince = 0;
    function check() {
        pickerTimeout = undefined;
        if (!hook) {
            return;
        }
        const capture = hook.status();
        const audio = hook.audioStatus();
        if ((audio.kind !== "auto" && capture.active > 0)
            || (audio.ready && audio.matched && !audio.error)) {
            return;
        }
        const now = Date.now();
        if (capture.ready > 0 && audio.ready) {
            readySince ||= now;
        } else {
            readySince = 0;
        }
        if ((readySince && now - readySince >= 3000) || now >= deadline) {
            if (capture.active > 0 && (!audio.ready || audio.error || !audio.waiting)) {
                openAudioPicker(hook);
            }
            return;
        }
        pickerTimeout = setTimeout(check, 250);
    }
    pickerTimeout = setTimeout(check, 250);
}

export default definePlugin({
    name: "WaylandScreenshare",
    description:
        "Changes the captured Wayland window and shares selected application audio through PipeWire.",
    authors: [{ name: "aliveoutside", id: 0n }],
    tags: ["Voice", "Utility"],
    enabledByDefault: true,
    settingsAboutComponent: ErrorBoundary.wrap(
        () => React.createElement(Button, { onClick: audioPicker }, "Select screenshare audio"),
        { noop: true }
    ),
    toolboxActions: {
        "Select screenshare audio": audioPicker,
        "Log screenshare audio status": () => logger.info(hook?.audioStatus(), hook?.listAudio())
    },
    contextMenus: {
        "stream-context"(children, { stream }: { stream: { ownerId: string } }) {
            if (
                !IS_DISCORD_DESKTOP ||
                !IS_LINUX ||
                stream.ownerId !== UserStore.getCurrentUser().id
            )
                return;
            children.push(
                React.createElement(Menu.MenuItem, {
                    id: "equicord-screenshare-audio",
                    label: "Select screenshare audio",
                    action: audioPicker
                })
            );
        }
    },
    flux: {
        STREAM_CREATE({ streamKey }: { streamKey: string }) {
            if (!hook || !streamKey.endsWith(`:${UserStore.getCurrentUser().id}`)) {
                return;
            }
            hook.selectAudio("auto", "");
            checkAudioSelection();
        },
        STREAM_DELETE({ streamKey }: { streamKey: string }) {
            if (!hook || !streamKey.endsWith(`:${UserStore.getCurrentUser().id}`)) {
                return;
            }
            clearTimeout(pickerTimeout);
            hook.selectAudio("none", "");
        }
    },
    patches: [
        {
            find: '"change-windows"',
            predicate: () => IS_DISCORD_DESKTOP && IS_LINUX,
            group: true,
            replacement: [
                {
                    match: /\i\.\i\.getUseSystemScreensharePicker\(\)&&\(0,\i\.isLinux\)\(\)/,
                    replace: "!1"
                },
                {
                    match: /(?<=id:"change-windows",.{0,200}?action:)\i/,
                    replace: "$self.changeWindow"
                }
            ]
        }
    ],
    async start() {
        if (!IS_DISCORD_DESKTOP || !IS_LINUX) {
            return;
        }
        try {
            const Native = VencordNative.pluginHelpers.WaylandScreenshare as PluginNative<
                typeof import("./native")
            >;
            const prepared = await Native.prepare();
            if (!prepared.success) {
                logger.error(prepared.error);
                return;
            }
            const candidate: unknown = DiscordNative.nativeModules.requireModule(
                "discord_equicord_capture"
            );
            if (!isCaptureHook(candidate)) {
                logger.error("Native capture module has an incompatible interface.");
                return;
            }
            hook = candidate;
            const status = hook.install();
            if (status.installed) {
                logger.info("Native capture hooks installed.");
            } else {
                logger.warn(status.error);
                return;
            }
            const audio = hook.startAudio();
            if (!audio.ready) {
                logger.warn(audio.error);
            }
        } catch (error) {
            logger.error("Could not load native capture hooks.", error);
        }
    },
    stop() {
        clearTimeout(pickerTimeout);
        hook?.stopAudio();
        hook = undefined;
    },
    async changeWindow() {
        const source = MediaEngineStore.getGoLiveSource();
        if (!source?.desktopSource?.id.startsWith("prepicked:")) {
            logger.warn("This stream does not use the system window picker.");
            return;
        }
        if (!hook) {
            logger.warn("The native capture module is not loaded. Fully restart Discord.");
            return;
        }
        const status = hook.status();
        if (!status.installed) {
            logger.warn(status.error);
            return;
        }
        try {
            const result = await hook.changeWindow();
            logger.info("Window switch result:", result);
            if (result.success) {
                checkAudioSelection();
            } else if (!result.cancelled) {
                logger.warn(result.error);
            }
        } catch (error) {
            logger.error("Native window switching failed.", error);
        }
    }
});
