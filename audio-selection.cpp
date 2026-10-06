/*
 * Vencord, a Discord client mod
 * Copyright (c) 2026 Vendicated and contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "audio-selection.hpp"
#include <algorithm>
#include <cctype>
#include <set>

namespace {
std::string normalize(std::string value) {
    const auto slash = value.rfind('/');
    if (slash != std::string::npos) value.erase(0, slash + 1);
    if (value.size() > 8 && value.compare(value.size() - 8, 8, ".desktop") == 0) value.resize(value.size() - 8);
    for (char &character : value) character = std::tolower(static_cast<unsigned char>(character));
    return value;
}

bool matches(const std::string &hint, const std::string &value) {
    const std::string candidate = normalize(value);
    if (candidate.empty()) return false;
    if (hint == candidate) return true;
    const auto dot = hint.rfind('.');
    return dot != std::string::npos && hint.substr(dot + 1) == candidate;
}
}

AudioMatch match_audio_application(const std::string &hint, const std::vector<AudioNode> &nodes) {
    if (hint.empty()) return {"", "This capture has no application identity. Select audio manually."};
    const std::string normalized = normalize(hint);
    for (const char *prefix : {"dp-", "hdmi-", "edp-", "dvi-", "vga-"})
        if (normalized.find(prefix) == 0) return {"", "Sharing a monitor. Select which application's audio to include."};
    if (normalized.find(',') != std::string::npos)
        return {"", "Sharing a screen region. Select which application's audio to include."};
    int best = 0;
    std::set<std::string> candidates;
    for (const auto &node : nodes) {
        if (node.application.empty()) continue;
        const int score = matches(normalized, node.app_id) ? 3 : matches(normalized, node.binary) ? 2 : matches(normalized, node.name) ? 1 : 0;
        if (score > best) { best = score; candidates.clear(); }
        if (score && score == best) candidates.insert(node.application);
    }
    if (candidates.empty()) return {"", "Waiting for matching application audio. You can also select it manually."};
    if (candidates.size() != 1) return {"", "Several applications match this window. Select audio manually."};
    return {*candidates.begin(), "Automatically matched the captured application's identity."};
}
