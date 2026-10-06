/*
 * Vencord, a Discord client mod
 * Copyright (c) 2026 Vendicated and contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

import { BaseText } from "@components/BaseText";
import ErrorBoundary from "@components/ErrorBoundary";
import { Flex } from "@components/Flex";
import { useForceUpdater, useTimer } from "@utils/react";
import { Button, Modal, openModal, React, Select } from "@webpack/common";

import { AudioNode, CaptureHook } from "./capture";

interface AudioPickerProps {
    hook: CaptureHook;
    openingReason?: string;
    onIdentify(): void;
}

async function identifyWindowAudio(hook: CaptureHook) {
    const capture = hook.status();
    const result = await hook.identifyWindowAudio();
    if (result.error) {
        openAudioPicker(hook, result.error);
        return;
    }
    const current = hook.status();
    if (
        current.created !== capture.created ||
        current.switched !== capture.switched ||
        current.destroyed !== capture.destroyed
    ) {
        openAudioPicker(hook, "The share changed while identifying the window. Identify it again.");
        return;
    }
    const status = hook.selectAudio("application", result.application);
    if (status.error) {
        openAudioPicker(hook, status.error);
    }
}

const AudioPicker = ErrorBoundary.wrap(
    function AudioPicker({ hook, openingReason, onIdentify }: AudioPickerProps) {
        useTimer({ interval: 1000 });
        const refresh = useForceUpdater();
        const nodes = hook.listAudio();
        const status = hook.audioStatus();
        const applications = new Map<string, AudioNode>();
        for (const node of nodes) {
            applications.set(node.application, node);
        }
        const options = [
            { label: "Automatic", value: "auto:" },
            { label: "Desktop audio", value: "desktop:" },
            { label: "No audio", value: "none:" },
            ...Array.from(applications.values(), node => ({
                label: `${node.name} (${node.pid || "Application"})`,
                value: `application:${node.application}`
            })),
            ...nodes.map(node => ({
                label: `${node.name}: ${node.description || "Playback stream"} (${node.serial})`,
                value: `stream:${node.serial}`
            }))
        ];
        let current = "none:";
        if (status.kind === "auto" || status.kind === "desktop") {
            current = `${status.kind}:`;
        } else if (status.selection) {
            current = `${status.kind}:${status.selection}`;
        }
        if (!options.some(option => option.value === current))
            options.push({
                label:
                    status.kind === "stream"
                        ? "Selected stream is no longer available"
                        : "Selected application is waiting for playback",
                value: current
            });
        const selected = nodes.find(
            node => node.application === status.selection || node.serial === status.selection
        );
        let message = status.error;
        if (!message) {
            if (status.kind === "desktop") {
                message = `Desktop audio: ${status.linked} playback streams connected.`;
            } else if (status.kind === "auto" && (!status.selection || status.linked === 0)) {
                message = status.reason;
            } else if (!status.selection) {
                message = "Screenshare audio is muted.";
            } else if (status.linked === 0 && status.kind === "stream") {
                message =
                    "This stream disappeared. Select an application to follow recreated playback streams.";
            } else if (status.linked === 0) {
                message =
                    "Waiting for this application's audio to return. Other applications remain excluded.";
            } else {
                message = `${selected?.name || "Selected application"}: ${status.linked} playback streams connected.`;
            }
        }
        return (
            <Flex flexDirection="column" gap={16}>
                {openingReason ? <BaseText size="sm">{openingReason}</BaseText> : null}
                <Flex flexDirection="column" gap={8}>
                    <BaseText size="sm" weight="semibold">
                        Audio source
                    </BaseText>
                    <Select
                        options={options}
                        isSelected={value => value === current}
                        select={value => {
                            const split = value.indexOf(":");
                            hook.selectAudio(value.slice(0, split), value.slice(split + 1));
                            refresh();
                        }}
                        serialize={value => value}
                        placeholder="Select audio"
                    />
                    <BaseText
                        size="sm"
                        color={status.error ? "text-danger" : "text-muted"}
                        role={status.error ? "alert" : "status"}
                    >
                        {message}
                    </BaseText>
                </Flex>
                <BaseText size="sm" color="text-muted">
                    Enable <strong>Sound</strong> when starting your share. You'll still hear audio
                    locally.
                </BaseText>
                <Flex flexDirection="column" gap={8}>
                    <Flex gap={8} flexWrap="wrap">
                        <Button onClick={onIdentify}>Identify window audio</Button>
                        <Button color={Button.Colors.PRIMARY} onClick={refresh}>
                            Refresh
                        </Button>
                    </Flex>
                    <BaseText size="sm" color="text-muted">
                        On KDE, click Identify window audio, then click the shared window. Press
                        Escape to cancel.
                    </BaseText>
                </Flex>
            </Flex>
        );
    },
    { noop: true }
);

export function openAudioPicker(hook: CaptureHook, openingReason?: string) {
    openModal(props => (
        <Modal {...props} size="md" title="Screenshare audio">
            <AudioPicker
                hook={hook}
                openingReason={openingReason}
                onIdentify={() => {
                    props.onClose();
                    void identifyWindowAudio(hook);
                }}
            />
        </Modal>
    ));
}
