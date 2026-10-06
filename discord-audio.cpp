/*
 * Vencord, a Discord client mod
 * Copyright (c) 2026 Vendicated and contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "audio.hpp"
#include "audio-backend.hpp"
#include "audio-symbols.hpp"
#include <pulse/pulseaudio.h>
#include <atomic>
#include <cstring>
#include <dlfcn.h>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

namespace {
std::atomic<bool> enabled{false};
std::atomic<uint64_t> recordings{0};
std::mutex stream_mutex;
std::set<pa_stream *> streams;
constexpr uint32_t share_index = PA_INVALID_INDEX - 1;
using List = decltype(&pa_context_get_sink_input_info_list);
using Info = decltype(&pa_context_get_sink_input_info);
using Monitor = decltype(&pa_stream_set_monitor_stream);
using Record = decltype(&pa_stream_connect_record);
using Disconnect = decltype(&pa_stream_disconnect);
using AttachedPid = uint32_t (*)(const void *);
const char *hook_names[] = {"pa_context_get_sink_input_info_list",
                            "pa_context_get_sink_input_info",
                            "pa_stream_set_monitor_stream",
                            "pa_stream_connect_record",
                            "pa_stream_disconnect"};
void **slots[5] = {};
unsigned matches[5] = {};
List original_list = nullptr;
Info original_info = nullptr;
Monitor original_monitor = nullptr;
Record original_record = nullptr;
Disconnect original_disconnect = nullptr;
pa_sink_input_info_cb_t soundshare_callback = nullptr;
AttachedPid attached_pid = nullptr;
std::string hook_error;
decltype(&pa_cvolume_set) original_volume = nullptr;
std::vector<std::pair<uintptr_t, uintptr_t>> executable_ranges;

bool matches_code(uintptr_t address, const unsigned char *code, size_t size) {
    for (const auto &[begin, end] : executable_ranges) {
        if (address >= begin && address < end && size <= end - address) {
            return std::memcmp(reinterpret_cast<const void *>(address), code, size) == 0;
        }
    }
    return false;
}

pa_cvolume *set_volume(pa_cvolume *volume, unsigned channels, pa_volume_t value) {
    if (!pa_channels_valid(channels)) {
        return pa_cvolume_init(volume);
    }
    return original_volume(volume, channels, value);
}

struct Query {
    pa_sink_input_info_cb_t callback;
    void *userdata;
    bool list;
    bool managed;
};

void input_info(pa_context *ctx, const pa_sink_input_info *info, int end, void *data) {
    auto *query = static_cast<Query *>(data);
    if (!query->managed) {
        query->callback(ctx, info, end, query->userdata);
    } else if (end > 0 && query->list) {
        const uint32_t pid = attached_pid(query->userdata);
        if (pid) {
            pa_sink_input_info input{};
            input.index = share_index;
            input.name = "Equicord Screenshare";
            input.owner_module = PA_INVALID_INDEX;
            input.client = PA_INVALID_INDEX;
            input.sink = PA_INVALID_INDEX;
            input.sample_spec = {PA_SAMPLE_S16LE, 48000, 2};
            pa_channel_map_init_stereo(&input.channel_map);
            pa_cvolume_set(&input.volume, 2, PA_VOLUME_NORM);
            input.resample_method = "copy";
            input.driver = "Equicord";
            input.proplist = pa_proplist_new();
            const std::string process = std::to_string(pid == 1 ? 2 : pid);
            pa_proplist_sets(input.proplist, PA_PROP_APPLICATION_PROCESS_ID, process.c_str());
            query->callback(ctx, &input, 0, query->userdata);
            pa_proplist_free(input.proplist);
        }
        query->callback(ctx, nullptr, end, query->userdata);
    } else if (end) {
        query->callback(ctx, nullptr, end, query->userdata);
    }
    if (end) {
        delete query;
    }
}

pa_operation *list_inputs(pa_context *ctx, pa_sink_input_info_cb_t callback, void *data) {
    if (callback != soundshare_callback) {
        return original_list(ctx, callback, data);
    }
    auto query = std::make_unique<Query>(Query{callback, data, true, enabled.load()});
    pa_operation *operation = original_list(ctx, input_info, query.get());
    if (operation) {
        query.release();
    }
    return operation;
}

pa_operation *
get_input(pa_context *ctx, uint32_t index, pa_sink_input_info_cb_t callback, void *data) {
    if (callback != soundshare_callback) {
        return original_info(ctx, index, callback, data);
    }
    auto query = std::make_unique<Query>(Query{callback, data, false, enabled.load()});
    pa_operation *operation = original_info(ctx, index, input_info, query.get());
    if (operation) {
        query.release();
    }
    return operation;
}

int monitor_stream(pa_stream *stream, uint32_t index) {
    if (index != share_index) {
        return original_monitor(stream, index);
    }
    std::lock_guard<std::mutex> lock(stream_mutex);
    streams.insert(stream);
    return 0;
}

int connect_record(pa_stream *stream,
                   const char *device,
                   const pa_buffer_attr *attr,
                   pa_stream_flags_t flags) {
    bool managed;
    {
        std::lock_guard<std::mutex> lock(stream_mutex);
        managed = streams.count(stream);
    }
    pa_buffer_attr capture_attr{};
    capture_attr.maxlength = 48000 * 4 / 5;
    capture_attr.tlength = PA_INVALID_INDEX;
    capture_attr.prebuf = PA_INVALID_INDEX;
    capture_attr.minreq = PA_INVALID_INDEX;
    capture_attr.fragsize = 48000 * 4 / 50;
    const int result = original_record(
        stream,
        managed ? pipewire_monitor().c_str() : device,
        managed ? &capture_attr : attr,
        managed
            ? static_cast<pa_stream_flags_t>(flags | PA_STREAM_DONT_MOVE | PA_STREAM_ADJUST_LATENCY)
            : flags);
    if (managed && result == 0) {
        ++recordings;
    }
    return result;
}

int disconnect_stream(pa_stream *stream) {
    {
        std::lock_guard<std::mutex> lock(stream_mutex);
        streams.erase(stream);
    }
    return original_disconnect(stream);
}

}

void audio_locate(dl_phdr_info *info, const ElfW(Rela) & entry, const char *name) {
    if (executable_ranges.empty()) {
        for (unsigned i = 0; i < info->dlpi_phnum; ++i) {
            const auto &segment = info->dlpi_phdr[i];
            if (segment.p_type == PT_LOAD && (segment.p_flags & (PF_R | PF_X)) == (PF_R | PF_X)) {
                executable_ranges.emplace_back(info->dlpi_addr + segment.p_vaddr,
                                               info->dlpi_addr + segment.p_vaddr + segment.p_memsz);
            }
        }
    }
    for (unsigned i = 0; i < 5; ++i) {
        if (std::strcmp(name, hook_names[i]) == 0) {
            slots[i] = reinterpret_cast<void **>(info->dlpi_addr + entry.r_offset);
            ++matches[i];
        }
    }
}

std::string audio_install(const std::string &path, uintptr_t base, bool (*writable)(const void *)) {
    const auto symbols = resolve_audio_symbols(path, base);
    if (!symbols.callback || !symbols.attached_pid || !symbols.pulse_table) {
        hook_error = "Could not resolve Discord's screenshare audio functions.";
        return hook_error;
    }
    constexpr unsigned char callback_code[] = {
        0x85, 0xd2, 0x74, 0x01, 0xc3, 0x48, 0x89, 0xcf, 0xe9};
    constexpr unsigned char pid_code[] = {0x8b, 0x87, 0x80, 0x02, 0x00, 0x00, 0xc3};
    constexpr unsigned char table_code[] = {
        0x55, 0x48, 0x89, 0xe5, 0x53, 0x48, 0x83, 0xec, 0x18, 0x0f, 0xb6, 0x05};
    constexpr unsigned char table_load[] = {0x84, 0xc0, 0x74, 0x42, 0x48, 0x8b, 0x05};
    constexpr unsigned char table_check[] = {0x48, 0x83, 0xf8, 0xff, 0x74};
    if (!matches_code(symbols.callback, callback_code, sizeof(callback_code))
        || !matches_code(symbols.attached_pid, pid_code, sizeof(pid_code))
        || !matches_code(symbols.pulse_table, table_code, sizeof(table_code))
        || !matches_code(symbols.pulse_table + 16, table_load, sizeof(table_load))
        || !matches_code(symbols.pulse_table + 27, table_check, sizeof(table_check))) {
        hook_error = "This Discord version has an incompatible screenshare audio interface.";
        return hook_error;
    }
    soundshare_callback = reinterpret_cast<pa_sink_input_info_cb_t>(symbols.callback);
    attached_pid = reinterpret_cast<AttachedPid>(symbols.attached_pid);
    original_list = &pa_context_get_sink_input_info_list;
    original_info = &pa_context_get_sink_input_info;
    original_monitor = &pa_stream_set_monitor_stream;
    original_record = &pa_stream_connect_record;
    original_disconnect = &pa_stream_disconnect;
    void *replacements[] = {reinterpret_cast<void *>(list_inputs),
                            reinterpret_cast<void *>(get_input),
                            reinterpret_cast<void *>(monitor_stream),
                            reinterpret_cast<void *>(connect_record),
                            reinterpret_cast<void *>(disconnect_stream)};
    for (unsigned i = 0; i < 5; ++i) {
        if (matches[i] != 1 || !slots[i] || !writable(slots[i])) {
            hook_error = "This Discord version has an incompatible screenshare audio interface.";
            return hook_error;
        }
    }
    using PulseTable = void *(*)();
    auto *table = static_cast<unsigned char *>(reinterpret_cast<PulseTable>(symbols.pulse_table)());
    if (!table) {
        hook_error = "Could not load Discord's audio symbol table.";
        return hook_error;
    }
    auto *volume_slot = reinterpret_cast<void **>(table + 0xb8);
    Dl_info volume_symbol{};
    if (!writable(volume_slot) || !dladdr(*volume_slot, &volume_symbol) || !volume_symbol.dli_sname
        || std::strcmp(volume_symbol.dli_sname, "pa_cvolume_set") != 0) {
        hook_error = "Could not identify Discord's audio volume function.";
        return hook_error;
    }
    original_volume = reinterpret_cast<decltype(original_volume)>(*volume_slot);
    __atomic_store_n(volume_slot, reinterpret_cast<void *>(set_volume), __ATOMIC_RELEASE);
    for (unsigned i = 0; i < 5; ++i) {
        __atomic_store_n(slots[i], replacements[i], __ATOMIC_RELEASE);
    }
    return "";
}

void discord_audio_enable(bool value) {
    enabled.store(value);
}

bool discord_audio_enabled() {
    return enabled.load();
}

uint64_t discord_audio_recordings() {
    return recordings.load();
}

const std::string &discord_audio_error() {
    return hook_error;
}
