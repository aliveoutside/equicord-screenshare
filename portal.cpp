/*
 * Vencord, a Discord client mod
 * Copyright (c) 2026 Vendicated and contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "portal.hpp"
#include <gio/gunixfdlist.h>
#include <unistd.h>

static const char *service = "org.freedesktop.portal.Desktop";
static const char *desktop = "/org/freedesktop/portal/desktop";
static const char *screencast = "org.freedesktop.portal.ScreenCast";
static std::atomic<unsigned> sequence{0};

PortalSession::~PortalSession() {
    if (fd >= 0) close(fd);
    if (!bus) return;
    if (!path.empty()) {
        GVariant *reply = g_dbus_connection_call_sync(bus, service, path.c_str(),
            "org.freedesktop.portal.Session", "Close", nullptr, nullptr,
            G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, nullptr);
        if (reply) g_variant_unref(reply);
    }
    g_dbus_connection_close_sync(bus, nullptr, nullptr);
    g_object_unref(bus);
}

struct Response {
    GVariant *results = nullptr;
    guint code = 2;
    bool ready = false;
};

static void response_received(GDBusConnection *, const gchar *, const gchar *, const gchar *,
                              const gchar *, GVariant *parameters, gpointer data) {
    auto &response = *static_cast<Response *>(data);
    if (response.ready) return;
    g_variant_get(parameters, "(u@a{sv})", &response.code, &response.results);
    response.ready = true;
}

static std::string token() {
    return "equicord_" + std::to_string(getpid()) + "_" + std::to_string(++sequence);
}

static GVariant *request(PortalSession &session, GMainContext *context, const char *method,
                         GVariant *parameters, std::atomic<bool> &stopped,
                         std::string &error, bool &cancelled) {
    Response response;
    guint subscription = g_dbus_connection_signal_subscribe(session.bus, service,
        "org.freedesktop.portal.Request", "Response", nullptr, nullptr,
        G_DBUS_SIGNAL_FLAGS_NONE, response_received, &response, nullptr);
    GError *failure = nullptr;
    GVariant *reply = g_dbus_connection_call_sync(session.bus, service, desktop, screencast,
        method, parameters, G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, 10000, nullptr, &failure);
    if (!reply) {
        g_dbus_connection_signal_unsubscribe(session.bus, subscription);
        if (failure) g_error_free(failure);
        error = "The portal could not open the window picker.";
        return nullptr;
    }
    const gchar *request_path;
    g_variant_get(reply, "(&o)", &request_path);
    const gint64 deadline = g_get_monotonic_time() + 120 * G_TIME_SPAN_SECOND;
    while (!response.ready && !stopped.load() && g_get_monotonic_time() < deadline) {
        while (g_main_context_iteration(context, false)) {}
        if (!response.ready) g_usleep(10000);
    }
    if (!response.ready) {
        GVariant *closed = g_dbus_connection_call_sync(session.bus, service, request_path,
            "org.freedesktop.portal.Request", "Close", nullptr, nullptr,
            G_DBUS_CALL_FLAGS_NONE, 3000, nullptr, nullptr);
        if (closed) g_variant_unref(closed);
        cancelled = stopped.load();
        if (!cancelled) error = "The window picker timed out.";
    } else if (response.code != 0) {
        cancelled = response.code == 1;
        if (!cancelled) error = "The portal rejected the capture request.";
    }
    g_variant_unref(reply);
    g_dbus_connection_signal_unsubscribe(session.bus, subscription);
    if (!response.ready || response.code != 0) {
        if (response.results) g_variant_unref(response.results);
        return nullptr;
    }
    return response.results;
}

std::unique_ptr<PortalSession> choose_window(std::atomic<bool> &stopped, std::string &error, bool &cancelled) {
    GMainContext *context = g_main_context_new();
    g_main_context_push_thread_default(context);
    auto session = std::make_unique<PortalSession>();
    GError *failure = nullptr;
    gchar *address = g_dbus_address_get_for_bus_sync(G_BUS_TYPE_SESSION, nullptr, &failure);
    if (address) {
        session->bus = g_dbus_connection_new_for_address_sync(address,
            static_cast<GDBusConnectionFlags>(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
            nullptr, nullptr, &failure);
        g_free(address);
    }
    if (failure) g_error_free(failure);
    if (!session->bus) error = "Could not connect to the desktop portal.";
    GVariant *results = nullptr;
    if (session->bus) {
        GVariantBuilder options;
        g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
        g_variant_builder_add(&options, "{sv}", "handle_token", g_variant_new_string(token().c_str()));
        g_variant_builder_add(&options, "{sv}", "session_handle_token", g_variant_new_string(token().c_str()));
        results = request(*session, context, "CreateSession", g_variant_new("(a{sv})", &options), stopped, error, cancelled);
        if (results) {
            const gchar *path = nullptr;
            if (g_variant_lookup(results, "session_handle", "&o", &path)
                || g_variant_lookup(results, "session_handle", "&s", &path)) session->path = path;
            else error = "The portal did not return a capture session.";
            g_variant_unref(results);
        }
    }
    if (!session->path.empty() && error.empty() && !cancelled) {
        GVariantBuilder options;
        g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
        g_variant_builder_add(&options, "{sv}", "handle_token", g_variant_new_string(token().c_str()));
        g_variant_builder_add(&options, "{sv}", "types", g_variant_new_uint32(3));
        g_variant_builder_add(&options, "{sv}", "multiple", g_variant_new_boolean(false));
        g_variant_builder_add(&options, "{sv}", "cursor_mode", g_variant_new_uint32(2));
        results = request(*session, context, "SelectSources", g_variant_new("(oa{sv})", session->path.c_str(), &options), stopped, error, cancelled);
        if (results) g_variant_unref(results);
    }
    if (!session->path.empty() && error.empty() && !cancelled) {
        GVariantBuilder options;
        g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
        g_variant_builder_add(&options, "{sv}", "handle_token", g_variant_new_string(token().c_str()));
        results = request(*session, context, "Start", g_variant_new("(osa{sv})", session->path.c_str(), "", &options), stopped, error, cancelled);
        if (results) {
            GVariant *streams = g_variant_lookup_value(results, "streams", G_VARIANT_TYPE("a(ua{sv})"));
            if (streams && g_variant_n_children(streams) == 1) {
                GVariant *stream = g_variant_get_child_value(streams, 0);
                GVariant *node = g_variant_get_child_value(stream, 0);
                session->node = g_variant_get_uint32(node);
                g_variant_unref(node);
                g_variant_unref(stream);
            } else error = "The portal did not return the selected source.";
            if (streams) g_variant_unref(streams);
            g_variant_unref(results);
        }
    }
    if (session->node && error.empty() && !cancelled && !stopped.load()) {
        GVariantBuilder options;
        g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
        GUnixFDList *descriptors = nullptr;
        GVariant *reply = g_dbus_connection_call_with_unix_fd_list_sync(session->bus, service, desktop,
            screencast, "OpenPipeWireRemote", g_variant_new("(oa{sv})", session->path.c_str(), &options),
            G_VARIANT_TYPE("(h)"), G_DBUS_CALL_FLAGS_NONE, 10000, nullptr, &descriptors, nullptr, nullptr);
        if (reply && descriptors) {
            gint handle;
            g_variant_get(reply, "(h)", &handle);
            session->fd = g_unix_fd_list_get(descriptors, handle, nullptr);
        }
        if (reply) g_variant_unref(reply);
        if (descriptors) g_object_unref(descriptors);
        if (session->fd < 0) error = "Could not open the selected video stream.";
    }
    g_main_context_pop_thread_default(context);
    g_main_context_unref(context);
    if (stopped.load()) cancelled = true;
    if (cancelled || !error.empty() || session->fd < 0) return nullptr;
    return session;
}
