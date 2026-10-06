/*
 * Vencord, a Discord client mod
 * Copyright (c) 2026 Vendicated and contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct AudioNode {
    std::string serial, application, name, description, pid, binary, app_id;
};
struct AudioState {
    bool ready;
    std::string error, selection, kind, hint, reason;
    uint32_t linked;
};

bool pipewire_start();
void pipewire_stop();
bool pipewire_select(const std::string &kind, const std::string &target);
void pipewire_clear();
void pipewire_capture(uint32_t node);
AudioState pipewire_state();
std::vector<AudioNode> pipewire_nodes();
const std::string &pipewire_monitor();
void discord_audio_enable(bool enabled);
bool discord_audio_enabled();
uint64_t discord_audio_recordings();
const std::string &discord_audio_error();
