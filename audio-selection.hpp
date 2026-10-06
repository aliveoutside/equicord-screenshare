/*
 * Vencord, a Discord client mod
 * Copyright (c) 2026 Vendicated and contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include "audio-backend.hpp"

struct AudioMatch {
    std::string application;
    std::string reason;
    bool waiting = false;
};

AudioMatch match_audio_application(const std::string &hint, const std::vector<AudioNode> &nodes);
