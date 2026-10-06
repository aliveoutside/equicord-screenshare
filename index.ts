/*
 * Vencord, a Discord client mod
 * Copyright (c) 2026 Vendicated and contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

import ErrorBoundary from "@components/ErrorBoundary";
import { Devs, IS_LINUX } from "@utils/constants";
import { Logger } from "@utils/Logger";
import definePlugin, { PluginNative } from "@utils/types";
import { Button, MediaEngineStore, Menu, React, showToast, UserStore } from "@webpack/common";

import { openAudioPicker } from "./audioPicker";
import { CaptureHook, isCaptureHook } from "./capture";

const logger = new Logger("WaylandScreenshare");
let hook: CaptureHook | undefined;

function audioPicker() {
    if (hook) openAudioPicker(hook);
}

export default definePlugin({
    name: "WaylandScreenshare",
    description: "Changes the captured Wayland window and shares selected application audio through PipeWire.",
    authors: [Devs.prism],
    tags: ["Voice", "Utility"],
    enabledByDefault: true,
    settingsAboutComponent: ErrorBoundary.wrap(() => React.createElement(Button, { onClick: audioPicker }, "Select screenshare audio"), { noop: true }),
    toolboxActions: {
        "Select screenshare audio": audioPicker,
        "Log screenshare audio status": () => logger.info(hook?.audioStatus(), hook?.listAudio())
    },
    contextMenus: {
        "stream-context"(children, { stream }: { stream: { ownerId: string; }; }) {
            if (!IS_DISCORD_DESKTOP || !IS_LINUX || stream.ownerId !== UserStore.getCurrentUser().id) return;
            children.push(React.createElement(Menu.MenuItem, {
                id: "equicord-screenshare-audio",
                label: "Select screenshare audio",
                action: audioPicker
            }));
        }
    },
    flux: {
        STREAM_CREATE({ streamKey }: { streamKey: string; }) {
            if (!hook || !streamKey.endsWith(`:${UserStore.getCurrentUser().id}`)) return;
            hook.selectAudio("auto", "");
            audioPicker();
        },
        STREAM_DELETE({ streamKey }: { streamKey: string; }) {
            if (!hook || !streamKey.endsWith(`:${UserStore.getCurrentUser().id}`)) return;
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
        if (!IS_DISCORD_DESKTOP || !IS_LINUX) return;
        try {
            const Native = VencordNative.pluginHelpers.WaylandScreenshare as PluginNative<typeof import("./native")>;
            const prepared = await Native.prepare();
            if (!prepared.success) {
                logger.error(prepared.error);
                return;
            }
            const candidate: unknown = DiscordNative.nativeModules.requireModule("discord_equicord_capture");
            if (!isCaptureHook(candidate)) {
                logger.error("Native capture module has an incompatible interface.");
                return;
            }
            hook = candidate;
            const status = hook.install();
            if (status.installed) logger.info("Native capture hooks installed.");
            else { logger.warn(status.error); return; }
            const audio = hook.startAudio();
            if (!audio.ready) logger.warn(audio.error);
        } catch (error) {
            logger.error("Could not load native capture hooks.", error);
        }
    },
    stop() {
        hook?.stopAudio();
        hook = undefined;
    },
    async changeWindow() {
        const source = MediaEngineStore.getGoLiveSource();
        if (!source?.desktopSource?.id.startsWith("prepicked:")) {
            showToast("This stream does not use the system window picker.");
            return;
        }
        if (!hook) {
            showToast("The native capture module is not loaded. Fully restart Discord.");
            return;
        }
        const status = hook.status();
        if (!status.installed) {
            showToast(status.error);
            return;
        }
        try {
            const result = await hook.changeWindow();
            logger.info("Window switch result:", result);
            if (result.success) {
                showToast("The shared window has changed.");
                audioPicker();
            } else if (!result.cancelled) showToast(result.error);
        } catch (error) {
            logger.error("Native window switching failed.", error);
            showToast("Could not change the shared window.");
        }
    }
});
