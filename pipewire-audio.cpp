/*
 * Vencord, a Discord client mod
 * Copyright (c) 2026 Vendicated and contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "audio-backend.hpp"
#include "audio-selection.hpp"
#include "capture-resolver.hpp"
#include <pipewire/pipewire.h>
#include <pipewire/impl.h>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <unistd.h>

namespace {
struct Client {
    pw_client *proxy = nullptr;
    spa_hook listener{};
    std::string pid, name, binary, app_id;
    ~Client() {
        spa_hook_remove(&listener);
        if (proxy) pw_proxy_destroy(reinterpret_cast<pw_proxy *>(proxy));
    }
};
struct Node {
    uint32_t id, client = PW_ID_ANY;
    std::string serial, application, name, description, pid, binary, app_id;
    bool video = false;
    pw_node *proxy = nullptr;
    spa_hook listener{};
    ~Node() {
        spa_hook_remove(&listener);
        if (proxy) pw_proxy_destroy(reinterpret_cast<pw_proxy *>(proxy));
    }
};
struct Loopback {
    pw_impl_module *module = nullptr;
    spa_hook listener{};
    ~Loopback() {
        spa_hook_remove(&listener);
        if (module) pw_impl_module_destroy(module);
    }
};
pw_thread_loop *loop = nullptr;
pw_context *context = nullptr;
pw_core *core = nullptr;
pw_registry *registry = nullptr;
pw_proxy *sink = nullptr;
spa_hook core_listener{}, registry_listener{};
spa_source *retry = nullptr;
std::unordered_map<uint32_t, std::unique_ptr<Client>> clients;
std::unordered_map<uint32_t, std::unique_ptr<Node>> nodes;
std::unordered_map<std::string, std::unique_ptr<Loopback>> loopbacks;
std::string selection, selection_kind, backend_error, capture_hint, match_reason;
uint32_t capture_node = PW_ID_ANY;
std::string sink_name = "equicord-share-" + std::to_string(getpid());
std::string monitor_name = sink_name + ".monitor";
std::atomic<bool> ready{false};
int sync_sequence = 0;
bool synchronized = false, auto_confident = false, desktop_capture = false;
std::string property(const spa_dict *props, const char *key) {
    const char *value = props ? spa_dict_lookup(props, key) : nullptr;
    return value ? value : "";
}

void clear_loopbacks() {
    loopbacks.clear();
}

void module_destroyed(void *data) {
    static_cast<Loopback *>(data)->module = nullptr;
}

bool selected(const Node &node) {
    if (node.video || node.serial.empty()) return false;
    if (selection_kind == "desktop" || (selection_kind == "auto" && desktop_capture)) return true;
    return !selection.empty() && (selection_kind != "auto" || auto_confident)
        && (selection_kind == "application" || selection_kind == "auto" ? node.application == selection : node.serial == selection);
}

std::vector<AudioNode> snapshot_nodes() {
    std::vector<AudioNode> result;
    for (const auto &[id, node] : nodes) {
        if (node->video || node->serial.empty()) continue;
        result.push_back({node->serial, node->application, node->name, node->description, node->pid, node->binary, node->app_id});
    }
    return result;
}

void reconcile() {
    if (selection_kind == "auto") {
        const auto video = nodes.find(capture_node);
        capture_hint.clear();
        if (video != nodes.end() && video->second->video) {
            const std::string prefix = "kwin-screencast-";
            const auto &description = video->second->description;
            if (description.find(prefix) == 0) capture_hint = description.substr(prefix.size());
        }
        desktop_capture = false;
        for (const char *prefix : {"DP-", "HDMI-", "eDP-", "DVI-", "VGA-"})
            if (capture_hint.find(prefix) == 0) desktop_capture = true;
        const auto match = match_audio_application(capture_hint, snapshot_nodes());
        if (selection.empty()) selection = match.application;
        auto_confident = !selection.empty() && selection == match.application;
        match_reason = desktop_capture ? "Sharing desktop playback audio." : !match.application.empty() && !auto_confident
            ? "The matched application changed. Reselect Automatic to use the new instance." : match.reason;
    }
    for (auto it = loopbacks.begin(); it != loopbacks.end();) {
        bool keep = false;
        for (const auto &[id, node] : nodes) if (node->serial == it->first && selected(*node)) keep = true;
        if (keep && it->second->module) { ++it; continue; }
        it = loopbacks.erase(it);
    }
    if (!ready.load()) return;
    static const pw_impl_module_events events = [] {
        pw_impl_module_events value{};
        value.version = PW_VERSION_IMPL_MODULE_EVENTS;
        value.destroy = module_destroyed;
        return value;
    }();
    for (const auto &[id, node] : nodes) {
        if (node->video || !selected(*node) || node->serial.empty() || loopbacks.count(node->serial)) continue;
        const std::string group = sink_name + "." + node->serial;
        const std::string args = "{ audio.position = [ FL FR ] capture.props = { node.name = " + group +
            ".capture target.object = " + node->serial +
            " node.passive = true node.dont-fallback = true node.dont-reconnect = true node.dont-move = true"
            " node.linger = true stream.dont-remix = false } playback.props = { node.name = " + group +
            ".playback target.object = " + sink_name +
            " node.passive = true node.dont-fallback = true node.dont-reconnect = true node.dont-move = true"
            " node.linger = true } }";
        auto capture = std::make_unique<Loopback>();
        capture->module = pw_context_load_module(context, "libpipewire-module-loopback", args.c_str(), nullptr);
        if (!capture->module) { backend_error = "Could not connect the selected playback stream."; continue; }
        pw_impl_module_add_listener(capture->module, &capture->listener, &events, capture.get());
        loopbacks.emplace(node->serial, std::move(capture));
    }
}

void identify(Node &node) {
    auto client = clients.find(node.client);
    if (client != clients.end()) {
        if (node.pid.empty()) node.pid = client->second->pid;
        if (node.name.empty()) node.name = client->second->name;
        if (node.binary.empty()) node.binary = client->second->binary;
        if (node.app_id.empty()) node.app_id = client->second->app_id;
    }
    if (node.video) return;
    node.application = audio_process_identity(node.pid);
    if (node.application.empty()) node.application = "node:" + node.serial;
    if (node.pid == std::to_string(getpid()) || (client != clients.end()
        && (client->second->binary == "Discord" || client->second->app_id == "com.discordapp.Discord"))) {
        node.serial.clear();
        node.application.clear();
    }
}

void client_info(void *data, const pw_client_info *info) {
    if (!info->props) return;
    auto &client = *static_cast<Client *>(data);
    for (auto pair : {std::pair{PW_KEY_APP_PROCESS_ID, &client.pid}, std::pair{PW_KEY_APP_NAME, &client.name},
                      std::pair{PW_KEY_APP_PROCESS_BINARY, &client.binary}, std::pair{PW_KEY_APP_ID, &client.app_id}}) {
        const char *value = spa_dict_lookup(info->props, pair.first);
        if (value) *pair.second = value;
    }
    for (auto &[id, node] : nodes) if (node->client == info->id) identify(*node);
    reconcile();
}

void node_info(void *data, const pw_node_info *info) {
    auto &node = *static_cast<Node *>(data);
    if (!info->props) return;
    for (auto pair : {std::pair{PW_KEY_OBJECT_SERIAL, &node.serial}, std::pair{PW_KEY_APP_PROCESS_ID, &node.pid},
                      std::pair{PW_KEY_APP_NAME, &node.name}, std::pair{PW_KEY_MEDIA_NAME, &node.description},
                      std::pair{PW_KEY_APP_PROCESS_BINARY, &node.binary}, std::pair{PW_KEY_APP_ID, &node.app_id}}) {
        const char *value = spa_dict_lookup(info->props, pair.first);
        if (value) *pair.second = value;
    }
    if (node.name.empty()) node.name = property(info->props, PW_KEY_NODE_NAME);
    identify(node);
    if (property(info->props, PW_KEY_APP_PROCESS_BINARY) == "Discord"
        || property(info->props, PW_KEY_APP_ID) == "com.discordapp.Discord") {
        node.serial.clear();
        node.application.clear();
    }
    reconcile();
}

void global(void *, uint32_t id, uint32_t, const char *type, uint32_t version, const spa_dict *props) {
    if (std::strcmp(type, PW_TYPE_INTERFACE_Client) == 0) {
        auto client = std::make_unique<Client>();
        client->proxy = static_cast<pw_client *>(pw_registry_bind(registry, id, type, std::min(version, uint32_t(PW_VERSION_CLIENT)), 0));
        if (!client->proxy) return;
        static const pw_client_events events = [] {
            pw_client_events value{};
            value.version = PW_VERSION_CLIENT_EVENTS;
            value.info = client_info;
            return value;
        }();
        pw_client_add_listener(client->proxy, &client->listener, &events, client.get());
        clients.emplace(id, std::move(client));
        return;
    }
    if (std::strcmp(type, PW_TYPE_INTERFACE_Node)) return;
    const std::string media_class = property(props, PW_KEY_MEDIA_CLASS);
    if (media_class != "Stream/Output/Audio" && media_class != "Stream/Output/Video" && media_class != "Video/Source") return;
    if (property(props, PW_KEY_NODE_NAME).find(sink_name) == 0) return;
    auto node = std::make_unique<Node>();
    node->id = id;
    node->video = media_class != "Stream/Output/Audio";
    node->serial = property(props, PW_KEY_OBJECT_SERIAL);
    node->pid = property(props, PW_KEY_APP_PROCESS_ID);
    node->name = property(props, PW_KEY_APP_NAME);
    node->description = property(props, PW_KEY_MEDIA_NAME);
    node->binary = property(props, PW_KEY_APP_PROCESS_BINARY);
    node->app_id = property(props, PW_KEY_APP_ID);
    const std::string client_id = property(props, PW_KEY_CLIENT_ID);
    if (!client_id.empty()) node->client = static_cast<uint32_t>(std::strtoul(client_id.c_str(), nullptr, 10));
    identify(*node);
    node->proxy = static_cast<pw_node *>(pw_registry_bind(registry, id, type, std::min(version, uint32_t(PW_VERSION_NODE)), 0));
    if (!node->proxy) return;
    static const pw_node_events events = [] {
        pw_node_events value{};
        value.version = PW_VERSION_NODE_EVENTS;
        value.info = node_info;
        return value;
    }();
    pw_node_add_listener(node->proxy, &node->listener, &events, node.get());
    nodes.emplace(id, std::move(node));
}

void global_remove(void *, uint32_t id) {
    auto client = clients.find(id);
    if (client != clients.end()) {
        clients.erase(client);
    }
    auto found = nodes.find(id);
    if (found == nodes.end()) return;
    if (id == capture_node) capture_node = PW_ID_ANY;
    nodes.erase(found);
    reconcile();
}

void disconnect_graph() {
    ready.store(false);
    clear_loopbacks();
    nodes.clear();
    clients.clear();
    if (sink) { pw_proxy_destroy(sink); sink = nullptr; }
    if (registry) {
        spa_hook_remove(&registry_listener);
        pw_proxy_destroy(reinterpret_cast<pw_proxy *>(registry));
        registry = nullptr;
    }
    if (core) {
        spa_hook_remove(&core_listener);
        pw_core_disconnect(core);
        core = nullptr;
    }
}

void connect_graph();
void reconnect(void *, uint64_t) {
    disconnect_graph();
    connect_graph();
}

void schedule_retry() {
    timespec delay{1, 0};
    pw_loop_update_timer(pw_thread_loop_get_loop(loop), retry, &delay, nullptr, false);
}

void core_error(void *, uint32_t id, int, int result, const char *message) {
    std::fprintf(stderr, "[WaylandScreenshare] PipeWire error on object %u: %d (%s)\n", id, result, message ? message : "Unknown error");
    if (id != PW_ID_CORE || result != -EPIPE) return;
    ready.store(false);
    backend_error = "PipeWire disconnected. Waiting for it to return.";
    pw_thread_loop_signal(loop, false);
    schedule_retry();
}

void core_done(void *, uint32_t id, int sequence) {
    if (id != PW_ID_CORE || sequence != sync_sequence) return;
    synchronized = true;
    ready.store(true);
    backend_error.clear();
    reconcile();
    pw_thread_loop_signal(loop, false);
}

void connect_graph() {
    synchronized = false;
    core = pw_context_connect(context, nullptr, 0);
    if (!core) { backend_error = "Could not connect to PipeWire."; schedule_retry(); return; }
    static const pw_core_events events = [] {
        pw_core_events value{};
        value.version = PW_VERSION_CORE_EVENTS;
        value.done = core_done;
        value.error = core_error;
        return value;
    }();
    pw_core_add_listener(core, &core_listener, &events, nullptr);
    registry = pw_core_get_registry(core, PW_VERSION_REGISTRY, 0);
    static const pw_registry_events registry_events = [] {
        pw_registry_events value{};
        value.version = PW_VERSION_REGISTRY_EVENTS;
        value.global = global;
        value.global_remove = global_remove;
        return value;
    }();
    pw_registry_add_listener(registry, &registry_listener, &registry_events, nullptr);
    pw_properties *props = pw_properties_new("factory.name", "support.null-audio-sink",
        PW_KEY_NODE_NAME, sink_name.c_str(), PW_KEY_NODE_DESCRIPTION, "Equicord Screenshare",
        PW_KEY_MEDIA_CLASS, "Audio/Sink", PW_KEY_NODE_VIRTUAL, "true", "audio.position", "[ FL FR ]",
        "audio.rate", "48000", "node.autoconnect", "false", "node.pause-on-idle", "false",
        "adapter.auto-port-config", "{ mode = dsp monitor = true position = preserve }", nullptr);
    sink = static_cast<pw_proxy *>(pw_core_create_object(core, "adapter", PW_TYPE_INTERFACE_Node,
        PW_VERSION_NODE, &props->dict, 0));
    pw_properties_free(props);
    if (!sink) { backend_error = "Could not create the screenshare audio sink."; schedule_retry(); return; }
    sync_sequence = pw_core_sync(core, PW_ID_CORE, 0);
}

void stop_backend() {
    if (!loop) return;
    pw_thread_loop_stop(loop);
    disconnect_graph();
    if (retry) { pw_loop_destroy_source(pw_thread_loop_get_loop(loop), retry); retry = nullptr; }
    if (context) { pw_context_destroy(context); context = nullptr; }
    pw_thread_loop_destroy(loop);
    loop = nullptr;
    selection.clear();
    selection_kind.clear();
}

bool start_backend() {
    if (loop) return ready.load();
    pw_init(nullptr, nullptr);
    loop = pw_thread_loop_new("equicord-share", nullptr);
    if (!loop) { backend_error = "Could not start the PipeWire audio loop."; return false; }
    context = pw_context_new(pw_thread_loop_get_loop(loop), nullptr, 0);
    if (!context) { backend_error = "Could not create the PipeWire audio context."; stop_backend(); return false; }
    retry = pw_loop_add_timer(pw_thread_loop_get_loop(loop), reconnect, nullptr);
    if (!retry || pw_thread_loop_start(loop) < 0) {
        backend_error = "Could not start the PipeWire audio loop.";
        stop_backend();
        return false;
    }
    pw_thread_loop_lock(loop);
    connect_graph();
    timespec deadline{};
    pw_thread_loop_get_time(loop, &deadline, 3000000000LL);
    while (!synchronized && backend_error.empty()) {
        if (pw_thread_loop_timed_wait_full(loop, &deadline) < 0) {
            backend_error = "PipeWire did not finish connecting.";
            break;
        }
    }
    const bool connected = ready.load();
    pw_thread_loop_unlock(loop);
    return connected;
}

}

bool pipewire_start() { return start_backend(); }
void pipewire_stop() { stop_backend(); }
const std::string &pipewire_monitor() { return monitor_name; }
bool pipewire_select(const std::string &kind_value, const std::string &target_value) {
    const char *kind = kind_value.c_str(), *target = target_value.c_str();
    if (!loop || kind_value.size() > 16 || target_value.size() > 256) return false;
    if (std::strcmp(kind, "none") && std::strcmp(kind, "auto") && std::strcmp(kind, "desktop") && std::strcmp(kind, "application") && std::strcmp(kind, "stream")) return false;
    pw_thread_loop_lock(loop);
    bool exists = std::strcmp(kind, "none") == 0 || std::strcmp(kind, "auto") == 0 || std::strcmp(kind, "desktop") == 0;
    for (const auto &[id, node] : nodes)
        if (!node->video && !node->serial.empty() && (std::strcmp(kind, "application") == 0 ? node->application == target : node->serial == target)) exists = true;
    if (!exists && kind_value == "application") {
        const auto separator = target_value.find(':');
        if (separator != std::string::npos)
            exists = audio_process_identity(target_value.substr(0, separator)) == target_value;
    }
    if (exists) {
        selection_kind = kind;
        selection = selection_kind == "none" || selection_kind == "auto" || selection_kind == "desktop" ? "" : target;
        match_reason.clear();
        if (ready.load()) backend_error.clear();
        clear_loopbacks();
        reconcile();
    }
    pw_thread_loop_unlock(loop);
    return exists;
}
void pipewire_clear() {
    if (!loop) return;
    pw_thread_loop_lock(loop);
    selection.clear();
    selection_kind = "none";
    capture_node = PW_ID_ANY;
    capture_hint.clear();
    match_reason.clear();
    clear_loopbacks();
    pw_thread_loop_unlock(loop);
}

AudioState pipewire_state() {
    if (loop) pw_thread_loop_lock(loop);
    AudioState state{ready.load(), backend_error, selection, selection_kind, capture_hint, match_reason, 0};
    for (const auto &[serial, capture] : loopbacks) if (capture->module) ++state.linked;
    if (loop) pw_thread_loop_unlock(loop);
    return state;
}

std::vector<AudioNode> pipewire_nodes() {
    std::vector<AudioNode> result;
    if (!loop) return result;
    pw_thread_loop_lock(loop);
    result = snapshot_nodes();
    pw_thread_loop_unlock(loop);
    return result;
}

void pipewire_capture(uint32_t node) {
    if (!loop) return;
    pw_thread_loop_lock(loop);
    capture_node = node;
    capture_hint.clear();
    selection.clear();
    selection_kind = "auto";
    clear_loopbacks();
    reconcile();
    pw_thread_loop_unlock(loop);
}
