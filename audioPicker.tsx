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
        openAudioPicker(hook, "Couldn't identify the window. Choose an audio source.");
        return;
    }
    const current = hook.status();
    if (
        current.created !== capture.created ||
        current.switched !== capture.switched ||
        current.destroyed !== capture.destroyed
    ) {
        openAudioPicker(hook, "Share changed. Identify the window again.");
        return;
    }
    const status = hook.selectAudio("application", result.application);
    if (status.error) {
        openAudioPicker(hook, "Couldn't select audio. Choose another source.");
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
                label: node.name,
                value: `application:${node.application}`
            })),
            ...nodes.map(node => ({
                label: `${node.name}: ${node.description || "Playback stream"}`,
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
                        ? "Stream unavailable"
                        : "Waiting for playback",
                value: current
            });
        let message = "";
        if (status.error) {
            message = "Audio unavailable. Try another source.";
        } else if (openingReason) {
            message = openingReason;
        } else if (status.kind !== "none" && status.linked === 0) {
            if (status.kind === "stream") {
                message = "Stream ended. Choose another source.";
            } else if (status.kind === "auto" && !status.waiting && !status.matched) {
                message = "Choose an audio source or identify the window.";
            } else {
                message = "Waiting for audio.";
            }
        }
        return (
            <Flex flexDirection="column" gap={16}>
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
                    {message ? (
                        <BaseText
                            size="sm"
                            color={status.error ? "text-danger" : "text-muted"}
                            role={status.error ? "alert" : "status"}
                        >
                            {message}
                        </BaseText>
                    ) : null}
                </Flex>
                <BaseText size="sm" color="text-muted">
                    Enable <strong>Sound</strong> when starting your share.
                </BaseText>
                <Flex flexDirection="column" gap={8}>
                    <Flex gap={8} flexWrap="wrap">
                        <Button onClick={onIdentify}>Identify window audio</Button>
                        <Button color={Button.Colors.PRIMARY} onClick={refresh}>
                            Refresh
                        </Button>
                    </Flex>
                    <BaseText size="sm" color="text-muted">
                        Click Identify, then the shared window.
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
