/*
 * Vencord, a Discord client mod
 * Copyright (c) 2026 Vendicated and contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

import ErrorBoundary from "@components/ErrorBoundary";
import { useForceUpdater, useTimer } from "@utils/react";
import { Button, Modal, openModal, React, Select, showToast } from "@webpack/common";

import { AudioNode, CaptureHook } from "./capture";

interface AudioPickerProps {
    hook: CaptureHook;
    onIdentify(): void;
}

async function identifyWindowAudio(hook: CaptureHook) {
    const capture = hook.status();
    showToast("Click the shared window to identify its audio. Press Escape to cancel.");
    const result = await hook.identifyWindowAudio();
    if (result.error) {
        showToast(result.error);
        return;
    }
    const current = hook.status();
    if (current.created !== capture.created || current.switched !== capture.switched || current.destroyed !== capture.destroyed) {
        showToast("The share changed while identifying the window. Identify it again.");
        return;
    }
    const status = hook.selectAudio("application", result.application);
    showToast(status.error || (status.linked ? "Window audio selected." : "Window identified. Waiting for its audio."));
}

const AudioPicker = ErrorBoundary.wrap(function AudioPicker({ hook, onIdentify }: AudioPickerProps) {
    useTimer({ interval: 1000 });
    const refresh = useForceUpdater();
    const nodes = hook.listAudio();
    const status = hook.audioStatus();
    const applications = new Map<string, AudioNode>();
    for (const node of nodes) applications.set(node.application, node);
    const options = [
        { label: "Automatic for the capture", value: "auto:" },
        { label: "Desktop audio", value: "desktop:" },
        { label: "No audio", value: "none:" },
        ...Array.from(applications.values(), node => ({ label: `${node.name} (${node.pid || "Application"})`, value: `application:${node.application}` })),
        ...nodes.map(node => ({ label: `${node.name}: ${node.description || "Playback stream"} (${node.serial})`, value: `stream:${node.serial}` }))
    ];
    const current = status.kind === "auto" || status.kind === "desktop" ? `${status.kind}:` : status.selection ? `${status.kind}:${status.selection}` : "none:";
    if (!options.some(option => option.value === current)) options.push({
        label: status.kind === "stream" ? "Selected stream is no longer available" : "Selected application is waiting for playback",
        value: current
    });
    const selected = nodes.find(node => node.application === status.selection || node.serial === status.selection);
    const message = status.error || (status.kind === "desktop" ? `Desktop audio: ${status.linked} playback streams connected.`
        : status.kind === "auto" && !status.selection ? status.reason
            : status.kind === "auto" && status.linked === 0 ? status.reason
        : !status.selection ? "Screenshare audio is muted."
            : status.linked === 0 ? status.kind === "stream"
                ? "This stream disappeared. Select an application to follow recreated playback streams."
                : "Waiting for this application's audio to return. Other applications remain excluded."
                : `${selected?.name || "Selected application"}: ${status.linked} playback streams connected.`);
    return <>
        <p>Automatic shares desktop audio for a monitor and matches application audio for a window. Discord playback is excluded to avoid call feedback. You can override the source below.</p>
        <Select
            options={options}
            isSelected={value => value === current}
            select={value => {
                const split = value.indexOf(":");
                const next = hook.selectAudio(value.slice(0, split), value.slice(split + 1));
                refresh();
                if (next.error) showToast(next.error);
            }}
            serialize={value => value}
            placeholder="Select audio"
        />
        <p>{message}</p>
        <p>Enable Sound in Discord's share dialog to transmit selected audio. Local playback stays connected.</p>
        <p>On KDE, you can identify a window when its application name is missing.</p>
        <Button onClick={onIdentify}>Identify window audio</Button>
        <Button onClick={refresh}>Refresh</Button>
    </>;
}, { noop: true });

export function openAudioPicker(hook: CaptureHook) {
    openModal(props => <Modal {...props} size="md" title="Screenshare audio">
        <AudioPicker hook={hook} onIdentify={() => {
            props.onClose();
            void identifyWindowAudio(hook);
        }} />
    </Modal>);
}
