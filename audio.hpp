/*
 * Vencord, a Discord client mod
 * Copyright (c) 2026 Vendicated and contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include <node_api.h>
#include <link.h>
#include <string>

void audio_locate(dl_phdr_info *info, const ElfW(Rela) &entry, const char *name);
std::string audio_install(uintptr_t base, bool (*writable)(const void *));
void audio_register(napi_env env, napi_value exports);
void audio_clear_selection();
void audio_capture_end();
void audio_set_capture(uint32_t node);
