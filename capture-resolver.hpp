/*
 * Vencord, a Discord client mod
 * Copyright (c) 2026 Vendicated and contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include <node_api.h>
#include <string>

std::string audio_process_identity(const std::string &pid);
void register_capture_resolver(napi_env env, napi_value exports);
