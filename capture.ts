/*
 * Vencord, a Discord client mod
 * Copyright (c) 2026 Vendicated and contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

export interface HookStatus {
    installed: boolean;
    error: string;
    created: number;
    updated: number;
    renditions: number;
    destroyed: number;
    failed: number;
    active: number;
    ready: number;
    switched: number;
}

export interface AudioStatus {
    ready: boolean;
    matched: boolean;
    error: string;
    selection: string;
    kind: string;
    hint: string;
    reason: string;
    linked: number;
    recordings: number;
    enabled: boolean;
}

export interface AudioNode {
    serial: string;
    application: string;
    name: string;
    description: string;
    pid: string;
}

export interface CaptureHook {
    install(): HookStatus;
    status(): HookStatus;
    changeWindow(): Promise<HookStatus & { success: boolean; cancelled: boolean }>;
    startAudio(): AudioStatus;
    stopAudio(): AudioStatus;
    audioStatus(): AudioStatus;
    listAudio(): AudioNode[];
    identifyWindowAudio(): Promise<{ application: string; error: string }>;
    selectAudio(kind: string, target: string): AudioStatus;
}

export function isCaptureHook(value: unknown): value is CaptureHook {
    return (
        typeof value === "object" &&
        value !== null &&
        [
            "install",
            "status",
            "changeWindow",
            "startAudio",
            "stopAudio",
            "audioStatus",
            "listAudio",
            "selectAudio",
            "identifyWindowAudio"
        ].every(key => key in value && typeof Reflect.get(value, key) === "function")
    );
}
