/*
 * Vencord, a Discord client mod
 * Copyright (c) 2026 Vendicated and contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <node_api.h>
#include <link.h>
#include <dlfcn.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <condition_variable>
#include <chrono>
#include <memory>
#include <vector>
#include <unistd.h>
#include "portal.hpp"
#include "audio.hpp"

struct PipewireConfig { int32_t fd; uint32_t node; };
using FrameHandler = void (*)(const void *, uint32_t, const char *, void *);
struct CaptureOptions {
    uint32_t source, fps, width, height;
    FrameHandler callback;
    void *context;
    PipewireConfig *config;
};
static_assert(sizeof(CaptureOptions) == 40);
using NewCapture = int (*)(const CaptureOptions *, void **);
using UpdateCapture = int (*)(void *, const CaptureOptions *);
using Renditions = void (*)(void *, const size_t *, const size_t *, size_t);
using DestroyCapture = int (*)(void *);
using DestroyFrame = void (*)(const void *);

static const char *names[] = {
    "cc_linux_capture_new", "cc_linux_capture_update",
    "cc_linux_capture_set_requested_renditions", "cc_linux_capture_destroy"
};
static void **slots[4] = {};
static void *originals[4] = {};
static DestroyFrame destroy_frame = nullptr;
static void (*close_original_session)() = nullptr;
static std::atomic<uint64_t> created{0}, updated{0}, resized{0}, destroyed{0}, failed{0}, switched{0};
static std::atomic<bool> choosing{false};
struct Capture;
struct Target {
    Capture *owner;
    void *native = nullptr;
    PipewireConfig config;
    std::unique_ptr<PortalSession> session;
};
struct Capture {
    std::atomic<bool> stopped{false};
    std::mutex operation, state, forwarding;
    std::condition_variable ready;
    CaptureOptions options;
    FrameHandler callback;
    void *context;
    Target *active = nullptr, *pending = nullptr;
    bool candidate_ready = false;
    bool original_session = true;
    std::string candidate_error;
    std::vector<size_t> widths, heights;
    std::vector<std::unique_ptr<Target>> targets;
};
static std::mutex handles_mutex;
static std::unordered_map<void *, std::shared_ptr<Capture>> handles;
static bool installed = false;
static unsigned matches[4] = {};
static std::string module_path;
static uintptr_t module_base = 0;

static std::shared_ptr<Capture> lookup(void *handle) {
    std::lock_guard<std::mutex> lock(handles_mutex);
    auto found = handles.find(handle);
    return found == handles.end() ? nullptr : found->second;
}

static void route_frame(const void *frame, uint32_t kind, const char *error, void *context) {
    auto &target = *static_cast<Target *>(context);
    auto &capture = *target.owner;
    std::lock_guard<std::mutex> forwarding(capture.forwarding);
    bool forward;
    {
        std::lock_guard<std::mutex> state(capture.state);
        if (!capture.stopped.load() && capture.pending == &target) {
            if (frame && !error) capture.candidate_ready = true;
            if (error) capture.candidate_error = "The new capture could not receive frames.";
            capture.ready.notify_all();
        }
        forward = !capture.stopped.load() && capture.active == &target;
    }
    if (forward) capture.callback(frame, kind, error, capture.context);
    else if (frame) destroy_frame(frame);
}

static CaptureOptions target_options(Capture &capture, Target &target) {
    auto options = capture.options;
    options.callback = route_frame;
    options.context = &target;
    options.config = &target.config;
    return options;
}

static int on_new(const CaptureOptions *options, void **output) {
    if (!options->config) return reinterpret_cast<NewCapture>(originals[0])(options, output);
    auto capture = std::make_shared<Capture>();
    capture->options = *options;
    capture->options.config = nullptr;
    capture->callback = options->callback;
    capture->context = options->context;
    auto target = std::make_unique<Target>();
    target->owner = capture.get();
    target->config = *options->config;
    capture->active = target.get();
    auto routed = target_options(*capture, *target);
    const int result = reinterpret_cast<NewCapture>(originals[0])(&routed, &target->native);
    if (result == 0 && target->native) {
        audio_set_capture(target->config.node);
        capture->targets.push_back(std::move(target));
        *output = capture.get();
        std::lock_guard<std::mutex> lock(handles_mutex);
        handles.emplace(*output, capture);
        ++created;
    } else {
        *output = nullptr;
        ++failed;
    }
    return result;
}

static int on_update(void *handle, const CaptureOptions *options) {
    ++updated;
    auto capture = lookup(handle);
    if (!capture) return reinterpret_cast<UpdateCapture>(originals[1])(handle, options);
    std::lock_guard<std::mutex> operation(capture->operation);
    if (capture->stopped.load()) return 0;
    capture->options.source = options->source;
    capture->options.fps = options->fps;
    capture->options.width = options->width;
    capture->options.height = options->height;
    int result = 0;
    for (auto &target : capture->targets) {
        auto routed = target_options(*capture, *target);
        const int update = reinterpret_cast<UpdateCapture>(originals[1])(target->native, &routed);
        if (target.get() == capture->active) result = update;
    }
    return result;
}

static void on_renditions(void *handle, const size_t *widths, const size_t *heights, size_t count) {
    ++resized;
    auto capture = lookup(handle);
    if (!capture) {
        reinterpret_cast<Renditions>(originals[2])(handle, widths, heights, count);
        return;
    }
    std::lock_guard<std::mutex> operation(capture->operation);
    if (capture->stopped.load()) return;
    capture->widths.clear();
    capture->heights.clear();
    if (count && widths && heights) {
        capture->widths.assign(widths, widths + count);
        capture->heights.assign(heights, heights + count);
    }
    for (auto &target : capture->targets)
        reinterpret_cast<Renditions>(originals[2])(target->native, widths, heights, count);
}

static void retire(Capture &capture, Target *target) {
    for (auto it = capture.targets.begin(); it != capture.targets.end(); ++it) {
        if (it->get() != target) continue;
        if (target->native) reinterpret_cast<DestroyCapture>(originals[3])(target->native);
        capture.targets.erase(it);
        return;
    }
}

static int on_destroy(void *handle) {
    auto capture = lookup(handle);
    if (!capture) return reinterpret_cast<DestroyCapture>(originals[3])(handle);
    capture->stopped.store(true);
    capture->ready.notify_all();
    {
        std::lock_guard<std::mutex> forwarding(capture->forwarding);
    }
    {
        std::lock_guard<std::mutex> operation(capture->operation);
        {
            std::lock_guard<std::mutex> state(capture->state);
            capture->active = nullptr;
            capture->pending = nullptr;
        }
        while (!capture->targets.empty()) retire(*capture, capture->targets.back().get());
    }
    audio_capture_end();
    std::lock_guard<std::mutex> lock(handles_mutex);
    handles.erase(handle);
    ++destroyed;
    return 0;
}

static bool replace_capture(const std::shared_ptr<Capture> &capture, PipewireConfig config,
                            std::unique_ptr<PortalSession> session, bool commit, std::string &error) {
    Target *candidate;
    {
        std::lock_guard<std::mutex> operation(capture->operation);
        if (capture->stopped.load()) { close(config.fd); return false; }
        auto target = std::make_unique<Target>();
        target->owner = capture.get();
        target->config = config;
        target->session = std::move(session);
        candidate = target.get();
        capture->targets.push_back(std::move(target));
        {
            std::lock_guard<std::mutex> state(capture->state);
            capture->pending = candidate;
            capture->candidate_ready = false;
            capture->candidate_error.clear();
        }
        auto options = target_options(*capture, *candidate);
        const int result = reinterpret_cast<NewCapture>(originals[0])(&options, &candidate->native);
        if (result != 0 || !candidate->native) {
            {
                std::lock_guard<std::mutex> state(capture->state);
                capture->pending = nullptr;
            }
            retire(*capture, candidate);
            error = "Could not start capturing the new window.";
            return false;
        }
        reinterpret_cast<Renditions>(originals[2])(candidate->native, capture->widths.data(),
            capture->heights.data(), capture->widths.size());
    }
    bool ready;
    {
        std::unique_lock<std::mutex> state(capture->state);
        ready = capture->ready.wait_for(state, std::chrono::seconds(15), [&] {
            return capture->candidate_ready || !capture->candidate_error.empty() || capture->stopped.load();
        });
        if (!capture->candidate_error.empty()) error = capture->candidate_error;
        ready = ready && capture->candidate_ready && error.empty();
    }
    std::unique_lock<std::mutex> forwarding(capture->forwarding, std::defer_lock);
    std::unique_lock<std::mutex> operation(capture->operation, std::defer_lock);
    std::lock(forwarding, operation);
    if (capture->stopped.load()) return false;
    Target *old = nullptr;
    {
        std::lock_guard<std::mutex> state(capture->state);
        capture->pending = nullptr;
        if (ready && commit) {
            old = capture->active;
            capture->active = candidate;
        }
    }
    forwarding.unlock();
    if (!old) {
        retire(*capture, candidate);
        if (!ready && error.empty()) error = "The new window did not provide frames. The previous capture is still active.";
        return false;
    }
    const bool close_global = capture->original_session && candidate->session;
    retire(*capture, old);
    if (close_global) {
        close_original_session();
        capture->original_session = false;
    }
    audio_set_capture(candidate->config.node);
    ++switched;
    return true;
}

extern "C" int equicord_capture_replace(void *handle, int fd, uint32_t node, bool commit) {
    auto capture = lookup(handle);
    if (!capture) { close(fd); return 0; }
    std::string error;
    return replace_capture(capture, {fd, node}, nullptr, commit, error) ? 1 : 0;
}

static void *replacements[] = {
    reinterpret_cast<void *>(on_new), reinterpret_cast<void *>(on_update),
    reinterpret_cast<void *>(on_renditions), reinterpret_cast<void *>(on_destroy)
};

static int locate(dl_phdr_info *info, size_t, void *) {
    const char *basename = std::strrchr(info->dlpi_name, '/');
    if (std::strcmp(basename ? basename + 1 : info->dlpi_name, "discord_voice.node") != 0) return 0;
    module_path = info->dlpi_name;
    module_base = info->dlpi_addr;
    const ElfW(Dyn) *dynamic = nullptr;
    for (unsigned i = 0; i < info->dlpi_phnum; ++i)
        if (info->dlpi_phdr[i].p_type == PT_DYNAMIC)
            dynamic = reinterpret_cast<const ElfW(Dyn) *>(info->dlpi_addr + info->dlpi_phdr[i].p_vaddr);
    if (!dynamic) return 1;
    const ElfW(Sym) *symbols = nullptr;
    const char *strings = nullptr;
    const ElfW(Rela) *relocations = nullptr;
    size_t bytes = 0;
    bool rela = false;
    for (auto entry = dynamic; entry->d_tag != DT_NULL; ++entry) {
        switch (entry->d_tag) {
            case DT_SYMTAB: symbols = reinterpret_cast<const ElfW(Sym) *>(entry->d_un.d_ptr); break;
            case DT_STRTAB: strings = reinterpret_cast<const char *>(entry->d_un.d_ptr); break;
            case DT_JMPREL: relocations = reinterpret_cast<const ElfW(Rela) *>(entry->d_un.d_ptr); break;
            case DT_PLTRELSZ: bytes = entry->d_un.d_val; break;
            case DT_PLTREL: rela = entry->d_un.d_val == DT_RELA; break;
        }
    }
    if (!symbols || !strings || !relocations || !rela) return 1;
    for (size_t i = 0; i < bytes / sizeof(ElfW(Rela)); ++i) {
        const auto &entry = relocations[i];
        if (ELF64_R_TYPE(entry.r_info) != R_X86_64_JUMP_SLOT) continue;
        const char *name = strings + symbols[ELF64_R_SYM(entry.r_info)].st_name;
        audio_locate(info, entry, name);
        for (unsigned j = 0; j < 4; ++j) {
            if (std::strcmp(name, names[j]) != 0) continue;
            ++matches[j];
            slots[j] = reinterpret_cast<void **>(info->dlpi_addr + entry.r_offset);
        }
    }
    return 1;
}

static bool writable(const void *address) {
    std::ifstream maps("/proc/self/maps");
    std::string line;
    const auto target = reinterpret_cast<uintptr_t>(address);
    while (std::getline(maps, line)) {
        std::istringstream fields(line);
        std::string range, permissions;
        fields >> range >> permissions;
        const auto dash = range.find('-');
        if (dash == std::string::npos || permissions.size() < 3) continue;
        const auto begin = std::stoull(range.substr(0, dash), nullptr, 16);
        const auto end = std::stoull(range.substr(dash + 1), nullptr, 16);
        if (begin <= target && target < end) return permissions[1] == 'w' && permissions[2] != 'x';
    }
    return false;
}

static void set_number(napi_env env, napi_value object, const char *key, uint64_t number) {
    napi_value value;
    napi_create_double(env, static_cast<double>(number), &value);
    napi_set_named_property(env, object, key, value);
}

static napi_value status_value(napi_env env, const char *error = "") {
    napi_value object, value;
    napi_create_object(env, &object);
    napi_get_boolean(env, installed, &value);
    napi_set_named_property(env, object, "installed", value);
    napi_create_string_utf8(env, error, NAPI_AUTO_LENGTH, &value);
    napi_set_named_property(env, object, "error", value);
    set_number(env, object, "created", created.load());
    set_number(env, object, "updated", updated.load());
    set_number(env, object, "renditions", resized.load());
    set_number(env, object, "destroyed", destroyed.load());
    set_number(env, object, "failed", failed.load());
    set_number(env, object, "switched", switched.load());
    std::lock_guard<std::mutex> lock(handles_mutex);
    set_number(env, object, "active", handles.size());
    return object;
}

extern "C" const char *equicord_capture_install() {
    if (installed) return "";
    module_path.clear();
    for (unsigned i = 0; i < 4; ++i) { slots[i] = nullptr; matches[i] = 0; }
    dl_iterate_phdr(locate, nullptr);
    if (module_path.empty()) return "The voice module is not loaded in this process.";
    void *library = dlopen(module_path.c_str(), RTLD_LAZY | RTLD_NOLOAD);
    if (!library) return "Could not access the loaded voice module.";
    destroy_frame = reinterpret_cast<DestroyFrame>(dlsym(library, "cc_video_frame_destroy"));
    close_original_session = reinterpret_cast<void (*)()>(dlsym(library, "cc_linux_picker_close_session"));
    if (!destroy_frame || !close_original_session) {
        dlclose(library);
        return "The voice module is missing capture cleanup functions.";
    }
    for (unsigned i = 0; i < 4; ++i) {
        originals[i] = dlsym(library, names[i]);
        bool owns_slot = false;
        if (slots[i] && *slots[i] == originals[i]) {
            owns_slot = true;
        } else if (slots[i] && *slots[i]) {
            Dl_info owner{};
            if (dladdr(*slots[i], &owner) && owner.dli_fname && module_path == owner.dli_fname) {
                const auto bytes = static_cast<const unsigned char *>(*slots[i]);
                owns_slot = bytes[0] == 0x68 && bytes[5] == 0xe9;
            }
        }
        if (matches[i] != 1 || !slots[i] || !originals[i] || !writable(slots[i]) || !owns_slot) {
            dlclose(library);
            return "This Discord version has an incompatible capture interface.";
        }
    }
    audio_install(module_base, writable);
    for (unsigned i = 0; i < 4; ++i) __atomic_store_n(slots[i], replacements[i], __ATOMIC_RELEASE);
    installed = true;
    dlclose(library);
    return "";
}

extern "C" void equicord_capture_counts(uint64_t *output) {
    output[0] = created.load();
    output[1] = updated.load();
    output[2] = resized.load();
    output[3] = destroyed.load();
    output[4] = failed.load();
    std::lock_guard<std::mutex> lock(handles_mutex);
    output[5] = handles.size();
}

static napi_value install(napi_env env, napi_callback_info) {
    return status_value(env, equicord_capture_install());
}

static napi_value status(napi_env env, napi_callback_info) { return status_value(env); }

struct Change {
    napi_async_work work;
    napi_deferred deferred;
    std::shared_ptr<Capture> capture;
    bool success = false, cancelled = false;
    std::string error;
};

static napi_value change_value(napi_env env, bool success, bool cancelled, const std::string &error) {
    napi_value result = status_value(env, error.c_str()), value;
    napi_get_boolean(env, success, &value);
    napi_set_named_property(env, result, "success", value);
    napi_get_boolean(env, cancelled, &value);
    napi_set_named_property(env, result, "cancelled", value);
    return result;
}

static bool choose_capture(const std::shared_ptr<Capture> &capture, std::string &error, bool &cancelled) {
    auto session = choose_window(capture->stopped, error, cancelled);
    if (!session) return false;
    PipewireConfig config{session->fd, session->node};
    session->fd = -1;
    const bool success = replace_capture(capture, config, std::move(session), true, error);
    if (capture->stopped.load()) cancelled = true;
    return success;
}

extern "C" int equicord_capture_choose(void *handle) {
    auto capture = lookup(handle);
    if (!capture) return 0;
    std::string error;
    bool cancelled = false;
    const bool success = choose_capture(capture, error, cancelled);
    if (!error.empty()) g_printerr("WaylandScreenshare: %s\n", error.c_str());
    return success ? 1 : cancelled ? 2 : 0;
}

static void execute_change(napi_env, void *data) {
    auto &change = *static_cast<Change *>(data);
    change.success = choose_capture(change.capture, change.error, change.cancelled);
}

static void complete_change(napi_env env, napi_status status, void *data) {
    auto change = std::unique_ptr<Change>(static_cast<Change *>(data));
    choosing.store(false);
    if (status != napi_ok) change->error = "Window switching was interrupted.";
    napi_resolve_deferred(env, change->deferred, change_value(env, change->success, change->cancelled, change->error));
    napi_delete_async_work(env, change->work);
}

static napi_value change_window(napi_env env, napi_callback_info) {
    auto change = std::make_unique<Change>();
    napi_value promise, name;
    napi_create_promise(env, &change->deferred, &promise);
    {
        std::lock_guard<std::mutex> lock(handles_mutex);
        if (!installed) change->error = "The capture hook is not installed.";
        else if (handles.size() != 1) change->error = "Could not identify a single active screenshare.";
        else change->capture = handles.begin()->second;
    }
    if (!change->error.empty() || choosing.exchange(true)) {
        if (change->error.empty()) change->error = "The window picker is already open.";
        napi_resolve_deferred(env, change->deferred, change_value(env, false, false, change->error));
        return promise;
    }
    napi_create_string_utf8(env, "WaylandScreenshare.ChangeWindow", NAPI_AUTO_LENGTH, &name);
    if (napi_create_async_work(env, nullptr, name, execute_change, complete_change, change.get(), &change->work) != napi_ok) {
        choosing.store(false);
        napi_resolve_deferred(env, change->deferred, change_value(env, false, false, "Could not open the window picker."));
        return promise;
    }
    if (napi_queue_async_work(env, change->work) != napi_ok) {
        choosing.store(false);
        napi_delete_async_work(env, change->work);
        napi_resolve_deferred(env, change->deferred, change_value(env, false, false, "Could not open the window picker."));
        return promise;
    }
    change.release();
    return promise;
}

NAPI_MODULE_INIT() {
    napi_property_descriptor methods[] = {
        {"install", nullptr, install, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"status", nullptr, status, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"changeWindow", nullptr, change_window, nullptr, nullptr, nullptr, napi_default, nullptr}
    };
    napi_define_properties(env, exports, 3, methods);
    audio_register(env, exports);
    return exports;
}
