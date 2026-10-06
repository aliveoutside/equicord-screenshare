/*
 * Vencord, a Discord client mod
 * Copyright (c) 2026 Vendicated and contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "audio.hpp"
#include "audio-backend.hpp"
#include "capture-resolver.hpp"
#include <cstring>

extern "C" bool equicord_audio_start();
extern "C" void equicord_audio_stop();
extern "C" bool equicord_audio_select(const char *kind, const char *target);

namespace {
void set_string(napi_env env, napi_value object, const char *key, const std::string &text) {
    napi_value value;
    napi_create_string_utf8(env, text.c_str(), text.size(), &value);
    napi_set_named_property(env, object, key, value);
}

napi_value state(napi_env env, napi_callback_info) {
    napi_value result;
    napi_value value;
    napi_create_object(env, &result);
    const AudioState snapshot = pipewire_state();
    set_string(env,
               result,
               "error",
               discord_audio_error().empty() ? snapshot.error : discord_audio_error());
    set_string(env, result, "selection", snapshot.selection);
    set_string(env, result, "kind", snapshot.kind);
    set_string(env, result, "hint", snapshot.hint);
    set_string(env, result, "reason", snapshot.reason);
    napi_get_boolean(env, snapshot.ready, &value);
    napi_set_named_property(env, result, "ready", value);
    napi_get_boolean(env, snapshot.matched, &value);
    napi_set_named_property(env, result, "matched", value);
    napi_get_boolean(env, snapshot.waiting, &value);
    napi_set_named_property(env, result, "waiting", value);
    napi_create_uint32(env, snapshot.linked, &value);
    napi_set_named_property(env, result, "linked", value);
    napi_create_double(env, discord_audio_recordings(), &value);
    napi_set_named_property(env, result, "recordings", value);
    napi_get_boolean(env, discord_audio_enabled(), &value);
    napi_set_named_property(env, result, "enabled", value);
    return result;
}

napi_value list_audio(napi_env env, napi_callback_info) {
    napi_value array;
    napi_create_array(env, &array);
    uint32_t index = 0;
    for (const auto &node : pipewire_nodes()) {
        napi_value item;
        napi_create_object(env, &item);
        set_string(env, item, "serial", node.serial);
        set_string(env, item, "application", node.application);
        set_string(env, item, "name", node.name);
        set_string(env, item, "description", node.description);
        set_string(env, item, "pid", node.pid);
        napi_set_element(env, array, index++, item);
    }
    return array;
}

napi_value select_audio(napi_env env, napi_callback_info info) {
    napi_value args[2];
    size_t count = 2;
    napi_get_cb_info(env, info, &count, args, nullptr, nullptr);
    std::string kind;
    std::string target;
    bool valid = count == 2;
    for (unsigned i = 0; valid && i < 2; ++i) {
        napi_valuetype type;
        size_t length;
        napi_typeof(env, args[i], &type);
        valid = type == napi_string;
        if (!valid) {
            break;
        }
        napi_get_value_string_utf8(env, args[i], nullptr, 0, &length);
        valid = length <= 256;
        if (!valid) {
            break;
        }
        char text[257];
        napi_get_value_string_utf8(env, args[i], text, sizeof(text), &length);
        (i == 0 ? kind : target).assign(text, length);
    }
    valid = valid
            && (kind == "none" || kind == "auto" || kind == "desktop" || kind == "application"
                || kind == "stream");
    if (valid && !equicord_audio_select(kind.c_str(), target.c_str())) {
        napi_value result = state(env, nullptr);
        set_string(env, result, "error", "The selected playback stream is no longer available.");
        return result;
    }
    return state(env, nullptr);
}

napi_value start_audio(napi_env env, napi_callback_info) {
    if (discord_audio_error().empty()) {
        equicord_audio_start();
    }
    return state(env, nullptr);
}

napi_value stop_audio(napi_env env, napi_callback_info) {
    equicord_audio_stop();
    return state(env, nullptr);
}
}

extern "C" bool equicord_audio_start() {
    discord_audio_enable(true);
    return pipewire_start();
}

extern "C" void equicord_audio_stop() {
    discord_audio_enable(false);
    pipewire_stop();
}

extern "C" bool equicord_audio_select(const char *kind, const char *target) {
    return kind && target && pipewire_select(kind, target);
}

void audio_capture_end() {
    pipewire_clear();
}

void audio_clear_selection() {
    pipewire_clear();
}

void audio_set_capture(uint32_t node) {
    pipewire_capture(node);
}

void audio_register(napi_env env, napi_value exports) {
    register_capture_resolver(env, exports);
    napi_property_descriptor methods[] = {
        {"startAudio", nullptr, start_audio, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"stopAudio", nullptr, stop_audio, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"audioStatus", nullptr, state, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"listAudio", nullptr, list_audio, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"selectAudio", nullptr, select_audio, nullptr, nullptr, nullptr, napi_default, nullptr}};
    napi_define_properties(env, exports, sizeof(methods) / sizeof(methods[0]), methods);
    napi_add_env_cleanup_hook(
        env,
        [](void *) {
            equicord_audio_stop();
        },
        nullptr);
}
