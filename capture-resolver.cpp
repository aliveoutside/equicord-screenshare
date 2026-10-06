/*
 * Vencord, a Discord client mod
 * Copyright (c) 2026 Vendicated and contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "capture-resolver.hpp"
#include <gio/gio.h>
#include <atomic>
#include <fstream>
#include <memory>
#include <sstream>

std::string audio_process_identity(const std::string &pid) {
    if (pid.empty() || pid.find_first_not_of("0123456789") != std::string::npos) {
        return "";
    }
    std::ifstream stat("/proc/" + pid + "/stat");
    std::string line;
    std::getline(stat, line);
    const auto end = line.rfind(')');
    if (end == std::string::npos) {
        return "";
    }
    std::istringstream fields(line.substr(end + 2));
    std::string field;
    for (unsigned i = 3; i <= 22; ++i) {
        if (!(fields >> field)) {
            return "";
        }
    }
    return pid + ":" + field;
}

namespace {
std::atomic<bool> picking{false};
using Connection = std::unique_ptr<GDBusConnection, decltype(&g_object_unref)>;
using Variant = std::unique_ptr<GVariant, decltype(&g_variant_unref)>;

struct Request {
    napi_async_work work = nullptr;
    napi_deferred deferred = nullptr;
    std::string application;
    std::string error;
};

void resolve(napi_env, void *data) {
    auto &request = *static_cast<Request *>(data);
    Connection bus(g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr), g_object_unref);
    if (!bus) {
        request.error = "Could not connect to the desktop window service.";
        return;
    }
    Variant owner(g_dbus_connection_call_sync(bus.get(),
                                              "org.freedesktop.DBus",
                                              "/org/freedesktop/DBus",
                                              "org.freedesktop.DBus",
                                              "NameHasOwner",
                                              g_variant_new("(s)", "org.kde.KWin"),
                                              G_VARIANT_TYPE("(b)"),
                                              G_DBUS_CALL_FLAGS_NONE,
                                              1000,
                                              nullptr,
                                              nullptr),
                  g_variant_unref);
    gboolean available = false;
    if (owner) {
        g_variant_get(owner.get(), "(b)", &available);
    }
    if (!available) {
        request.error = "Window identification is currently available on KDE only. Select an "
                        "application from the list.";
        return;
    }
    if (picking.exchange(true)) {
        request.error = "Window identification is already open. Select a window or press Escape.";
        return;
    }
    Variant reply(g_dbus_connection_call_sync(bus.get(),
                                              "org.kde.KWin",
                                              "/KWin",
                                              "org.kde.KWin",
                                              "queryWindowInfo",
                                              nullptr,
                                              G_VARIANT_TYPE("(a{sv})"),
                                              G_DBUS_CALL_FLAGS_NONE,
                                              60000,
                                              nullptr,
                                              nullptr),
                  g_variant_unref);
    picking.store(false);
    if (!reply) {
        request.error = "Window identification was cancelled or timed out.";
        return;
    }
    Variant properties(g_variant_get_child_value(reply.get(), 0), g_variant_unref);
    gint32 pid = 0;
    if (!g_variant_lookup(properties.get(), "pid", "i", &pid) || pid <= 0) {
        request.error = "This window does not expose its process. Select its audio from the list.";
        return;
    }
    request.application = audio_process_identity(std::to_string(pid));
    if (request.application.empty()) {
        request.error = "The selected application's process has exited.";
    }
}

void complete(napi_env env, napi_status status, void *data) {
    std::unique_ptr<Request> request(static_cast<Request *>(data));
    if (status != napi_ok) {
        request->error = "Window identification did not finish.";
    }
    napi_value result;
    napi_value value;
    napi_create_object(env, &result);
    for (const auto &entry :
         {std::pair{"application", &request->application}, std::pair{"error", &request->error}}) {
        napi_create_string_utf8(env, entry.second->c_str(), entry.second->size(), &value);
        napi_set_named_property(env, result, entry.first, value);
    }
    napi_resolve_deferred(env, request->deferred, result);
    if (request->work) {
        napi_delete_async_work(env, request->work);
    }
}

napi_value identify(napi_env env, napi_callback_info) {
    auto request = std::make_unique<Request>();
    napi_value promise;
    napi_value name;
    napi_create_promise(env, &request->deferred, &promise);
    napi_create_string_utf8(env, "IdentifyScreenshareWindow", NAPI_AUTO_LENGTH, &name);
    if (napi_create_async_work(env, nullptr, name, resolve, complete, request.get(), &request->work)
            != napi_ok
        || napi_queue_async_work(env, request->work) != napi_ok) {
        request->error = "Could not start window identification.";
        complete(env, napi_generic_failure, request.release());
        return promise;
    }
    request.release();
    return promise;
}
}

void register_capture_resolver(napi_env env, napi_value exports) {
    const napi_property_descriptor method{
        "identifyWindowAudio", nullptr, identify, nullptr, nullptr, nullptr, napi_default, nullptr};
    napi_define_properties(env, exports, 1, &method);
}
