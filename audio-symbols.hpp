/*
 * Vencord, a Discord client mod
 * Copyright (c) 2026 Vendicated and contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include <cstdint>
#include <string>

struct AudioSymbols {
    uintptr_t callback = 0;
    uintptr_t attached_pid = 0;
    uintptr_t pulse_table = 0;
};

AudioSymbols resolve_audio_symbols(const std::string &path, uintptr_t base);
