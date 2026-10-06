/*
 * Vencord, a Discord client mod
 * Copyright (c) 2026 Vendicated and contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once
#include <gio/gio.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

struct PortalSession {
    GDBusConnection *bus = nullptr;
    std::string path;
    int fd = -1;
    uint32_t node = 0;
    ~PortalSession();
};

std::unique_ptr<PortalSession>
choose_window(std::atomic<bool> &stopped, std::string &error, bool &cancelled);
