// npueffectsd: owns the camera pipeline and exposes a small D-Bus API.
//
// The element is loaded as a normal GStreamer plugin. The daemon talks to it
// only through public contracts: GObject properties and the "generate-previews"
// action signal.
#include <dirent.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <gio/gio.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>

namespace {

constexpr const char* kService = "ru.fstronin.NpuEffects";
constexpr const char* kObjectPath = "/ru/fstronin/NpuEffects";
constexpr const char* kDevice = "/dev/video60";

const char kIntrospectionXml[] = R"XML(
<node>
  <interface name="ru.fstronin.NpuEffects">
    <method name="SetMode">
      <arg name="mode" type="s" direction="in"/>
    </method>
    <method name="SetGamma">
      <arg name="gamma" type="d" direction="in"/>
    </method>
    <method name="SetBlurStrength">
      <arg name="strength" type="d" direction="in"/>
    </method>
    <method name="SetSmoothStrength">
      <arg name="strength" type="d" direction="in"/>
    </method>
    <method name="GetState">
      <arg name="state" type="a{sv}" direction="out"/>
    </method>
    <method name="RefreshPreviews">
      <arg name="paths" type="a{ss}" direction="out"/>
    </method>
    <method name="Quit"/>
  </interface>
</node>
)XML";

struct Daemon {
    GstElement* pipeline = nullptr;
    GstElement* effects = nullptr;
    GstElement* appsrc = nullptr;
    GstElement* cameraPipeline = nullptr;
    GstElement* cameraSink = nullptr;
    GMainLoop* loop = nullptr;
    GDBusConnection* connection = nullptr;
    guint registrationId = 0;
    guint ownerId = 0;
    guint activeSourceId = 0;
    gboolean active = FALSE;
    std::atomic<bool> cameraStreaming{false};
    gint64 idleSinceUs = 0;
    gint64 nextRetryUs = 0;
    gint64 consumerSinceUs = 0;
    guint blackSourceId = 0;
    bool failed = false;
    bool testMode = false;
    std::string mode = "off";
    std::string device = "NPU";
    std::string models;
    double gamma = 0.85;
    double blur = 0.6;
    double smooth = 0.7;
};

bool camera_in_use() {
    DIR* proc = opendir("/proc");
    if (!proc) {
        return false;
    }
    const pid_t self = getpid();
    bool found = false;
    while (dirent* entry = readdir(proc)) {
        const pid_t pid = std::atoi(entry->d_name);
        if (pid <= 0 || pid == self) {
            continue;
        }
        const std::string fdDir =
            "/proc/" + std::to_string(pid) + "/fd";
        DIR* fds = opendir(fdDir.c_str());
        if (!fds) {
            continue;
        }
        while (dirent* fd = readdir(fds)) {
            if (fd->d_name[0] == '.') {
                continue;
            }
            const std::string link =
                fdDir + "/" + fd->d_name;
            char target[4096];
            const ssize_t len =
                readlink(link.c_str(), target, sizeof(target) - 1);
            if (len <= 0) {
                continue;
            }
            target[len] = '\0';
            if (std::strcmp(target, kDevice) == 0) {
                found = true;
                break;
            }
        }
        closedir(fds);
        if (found) {
            break;
        }
    }
    closedir(proc);
    return found;
}

void apply_effects(Daemon& daemon, bool live) {
    if (!daemon.effects) {
        return;
    }
    if (live) {
        g_object_set(daemon.effects,
                     "mode", daemon.mode.c_str(),
                     "gamma", daemon.gamma,
                     "blur-strength", daemon.blur,
                     "smooth-strength", daemon.smooth,
                     nullptr);
    } else {
        g_object_set(daemon.effects, "mode", "off", "gamma", 1.0, nullptr);
    }
}

gboolean push_black(gpointer data) {
    auto* daemon = static_cast<Daemon*>(data);
    if (!daemon->appsrc || daemon->cameraStreaming) {
        return G_SOURCE_CONTINUE;
    }
    const gsize size = 1280u * 720u * 3u;
    GstBuffer* buffer = gst_buffer_new_allocate(nullptr, size, nullptr);
    gst_buffer_memset(buffer, 0, 0, size);
    const GstFlowReturn ret =
        gst_app_src_push_buffer(GST_APP_SRC(daemon->appsrc), buffer);
    if (ret != GST_FLOW_OK) {
        g_printerr("[npueffectsd] appsrc push failed: %d\n", ret);
    }
    return G_SOURCE_CONTINUE;
}

void start_black_timer(Daemon& daemon) {
    if (daemon.appsrc && daemon.blackSourceId == 0) {
        daemon.blackSourceId = g_timeout_add(33, push_black, &daemon);
    }
}

void stop_black_timer(Daemon& daemon) {
    if (daemon.blackSourceId) {
        g_source_remove(daemon.blackSourceId);
        daemon.blackSourceId = 0;
    }
}

GstFlowReturn on_camera_sample(GstAppSink* sink, gpointer data) {
    auto* daemon = static_cast<Daemon*>(data);
    GstSample* sample = gst_app_sink_pull_sample(sink);
    if (!sample) {
        return GST_FLOW_OK;
    }
    if (daemon->cameraStreaming && daemon->appsrc) {
        GstBuffer* buffer = gst_sample_get_buffer(sample);
        if (buffer) {
            // libcamera PTS belongs to a different clock; main appsrc uses
            // its running time. Clear it (make_writable keeps this legal on
            // a shared sample buffer), then appsrc adds a timestamp.
            GstBuffer* copy =
                gst_buffer_make_writable(gst_buffer_ref(buffer));
            GST_BUFFER_PTS(copy) = GST_CLOCK_TIME_NONE;
            GST_BUFFER_DTS(copy) = GST_CLOCK_TIME_NONE;
            const GstFlowReturn ret = gst_app_src_push_buffer(
                GST_APP_SRC(daemon->appsrc), copy);
            if (ret != GST_FLOW_OK) {
                g_printerr("[npueffectsd] camera appsrc push: %d\n", ret);
            }
        }
    }
    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

bool start_camera(Daemon& daemon) {
    if (!daemon.cameraPipeline || !daemon.appsrc) {
        return false;
    }
    apply_effects(daemon, true);
    stop_black_timer(daemon);
    daemon.cameraStreaming = true;
    GstStateChangeReturn ret =
        gst_element_set_state(daemon.cameraPipeline, GST_STATE_PLAYING);
    bool ok = true;
    if (ret == GST_STATE_CHANGE_FAILURE) {
        ok = false;
    } else if (ret == GST_STATE_CHANGE_ASYNC) {
        ret = gst_element_get_state(daemon.cameraPipeline, nullptr, nullptr,
                                    3 * GST_SECOND);
        if (ret != GST_STATE_CHANGE_SUCCESS &&
            ret != GST_STATE_CHANGE_NO_PREROLL) {
            ok = false;
        }
    }
    if (!ok) {
        daemon.cameraStreaming = false;
        gst_element_set_state(daemon.cameraPipeline, GST_STATE_READY);
        start_black_timer(daemon);
        daemon.active = FALSE;
        g_printerr("[npueffectsd] camera start failed; black continues\n");
        return false;
    }
    daemon.active = TRUE;
    g_print("[npueffectsd] camera stream ON\n");
    return true;
}

void stop_camera(Daemon& daemon) {
    daemon.cameraStreaming = false;
    if (daemon.cameraPipeline) {
        gst_element_set_state(daemon.cameraPipeline, GST_STATE_READY);
    }
    apply_effects(daemon, false);
    start_black_timer(daemon);
    daemon.active = FALSE;
    // Cooldown: ignore short WirePlumber/PipeWire probes right after stop.
    daemon.nextRetryUs = g_get_monotonic_time() + G_USEC_PER_SEC;
    g_print("[npueffectsd] camera stream OFF (idle black)\n");
}

gboolean on_camera_bus(GstBus*, GstMessage* message, gpointer data) {
    auto* daemon = static_cast<Daemon*>(data);
    if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
        GError* error = nullptr;
        gchar* debug = nullptr;
        gst_message_parse_error(message, &error, &debug);
        g_printerr("[npueffectsd] camera pipeline error: %s (%s)\n",
                   error ? error->message : "unknown",
                   debug ? debug : "no debug");
        g_clear_error(&error);
        g_free(debug);
        if (daemon->cameraStreaming) {
            stop_camera(*daemon);
        }
        daemon->nextRetryUs = g_get_monotonic_time() + 2 * G_USEC_PER_SEC;
    } else if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS) {
        g_print("[npueffectsd] camera pipeline EOS\n");
        if (daemon->cameraStreaming) {
            stop_camera(*daemon);
        }
        daemon->nextRetryUs = g_get_monotonic_time() + 2 * G_USEC_PER_SEC;
    }
    return TRUE;
}

gboolean update_active(gpointer data) {
    auto* daemon = static_cast<Daemon*>(data);
    if (daemon->testMode) {
        daemon->active = TRUE;
        return G_SOURCE_CONTINUE;
    }
    if (!daemon->cameraPipeline) {
        return G_SOURCE_CONTINUE;
    }
    const bool consumer = camera_in_use();
    const gint64 now = g_get_monotonic_time();
    if (consumer) {
        if (daemon->consumerSinceUs == 0) {
            daemon->consumerSinceUs = now;
        }
        daemon->idleSinceUs = 0;
        // Ignore short PipeWire/WirePlumber probes; a real application holds
        // the device open. 600 ms filters device probes to the user.
        if (!daemon->cameraStreaming && now >= daemon->nextRetryUs &&
            now - daemon->consumerSinceUs >= 600 * 1000) {
            if (!start_camera(*daemon)) {
                daemon->nextRetryUs = now + 2 * G_USEC_PER_SEC;
            }
        }
    } else {
        daemon->consumerSinceUs = 0;
        if (daemon->cameraStreaming) {
            if (daemon->idleSinceUs == 0) {
                daemon->idleSinceUs = now;
            } else if (now - daemon->idleSinceUs >= G_USEC_PER_SEC) {
                stop_camera(*daemon);
                daemon->idleSinceUs = 0;
            }
        } else {
            daemon->active = FALSE;
        }
    }
    return G_SOURCE_CONTINUE;
}

void on_name_acquired(GDBusConnection*, const gchar*, gpointer) {
    g_print("[npueffectsd] D-Bus name %s acquired\n", kService);
}

void on_name_lost(GDBusConnection*, const gchar*, gpointer) {
    g_printerr("[npueffectsd] D-Bus name %s lost\n", kService);
}

GVariant* empty_paths() {
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a{ss}"));
    return g_variant_builder_end(&builder);
}

GVariant* empty_state(const Daemon& daemon) {
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&builder, "{sv}", "active",
                          g_variant_new_boolean(daemon.active));
    g_variant_builder_add(&builder, "{sv}", "mode",
                          g_variant_new_string(daemon.mode.c_str()));
    g_variant_builder_add(&builder, "{sv}", "gamma",
                          g_variant_new_double(daemon.gamma));
    g_variant_builder_add(&builder, "{sv}", "blur",
                          g_variant_new_double(daemon.blur));
    g_variant_builder_add(&builder, "{sv}", "smooth",
                          g_variant_new_double(daemon.smooth));
    g_variant_builder_add(&builder, "{sv}", "device",
                          g_variant_new_string(daemon.device.c_str()));
    return g_variant_builder_end(&builder);
}

void on_method_call(GDBusConnection*, const gchar*, const gchar*,
                    const gchar*, const gchar* method,
                    GVariant* parameters, GDBusMethodInvocation* invocation,
                    gpointer data) {
    auto* daemon = static_cast<Daemon*>(data);

    if (g_strcmp0(method, "SetMode") == 0) {
        const gchar* mode = nullptr;
        g_variant_get(parameters, "(&s)", &mode);
        daemon->mode = mode;
        if (daemon->effects &&
            (daemon->cameraStreaming || daemon->testMode)) {
            g_object_set(daemon->effects, "mode", daemon->mode.c_str(),
                         nullptr);
        }
        g_dbus_method_invocation_return_value(invocation, nullptr);
        return;
    }
    if (g_strcmp0(method, "SetGamma") == 0) {
        g_variant_get(parameters, "(d)", &daemon->gamma);
        if (daemon->effects &&
            (daemon->cameraStreaming || daemon->testMode)) {
            g_object_set(daemon->effects, "gamma", daemon->gamma, nullptr);
        }
        g_dbus_method_invocation_return_value(invocation, nullptr);
        return;
    }
    if (g_strcmp0(method, "SetBlurStrength") == 0) {
        g_variant_get(parameters, "(d)", &daemon->blur);
        if (daemon->effects &&
            (daemon->cameraStreaming || daemon->testMode)) {
            g_object_set(daemon->effects, "blur-strength", daemon->blur,
                         nullptr);
        }
        g_dbus_method_invocation_return_value(invocation, nullptr);
        return;
    }
    if (g_strcmp0(method, "SetSmoothStrength") == 0) {
        g_variant_get(parameters, "(d)", &daemon->smooth);
        if (daemon->effects &&
            (daemon->cameraStreaming || daemon->testMode)) {
            g_object_set(daemon->effects, "smooth-strength", daemon->smooth,
                         nullptr);
        }
        g_dbus_method_invocation_return_value(invocation, nullptr);
        return;
    }
    if (g_strcmp0(method, "GetState") == 0) {
        GVariant* state = empty_state(*daemon);
        g_dbus_method_invocation_return_value(
            invocation, g_variant_new_tuple(&state, 1));
        return;
    }
    if (g_strcmp0(method, "RefreshPreviews") == 0) {
        if (!daemon->effects) {
            GVariant* paths = empty_paths();
            g_dbus_method_invocation_return_value(
                invocation, g_variant_new_tuple(&paths, 1));
            return;
        }
        const std::string dir =
            std::string(g_get_user_runtime_dir()) + "/npu-effects";
        GVariant* paths = nullptr;
        g_signal_emit_by_name(daemon->effects, "generate-previews",
                              dir.c_str(), &paths);
        if (!paths) {
            paths = empty_paths();
        }
        g_dbus_method_invocation_return_value(
            invocation, g_variant_new_tuple(&paths, 1));
        return;
    }
    if (g_strcmp0(method, "Quit") == 0) {
        g_dbus_method_invocation_return_value(invocation, nullptr);
        if (daemon->loop) {
            g_main_loop_quit(daemon->loop);
        }
        return;
    }
    g_dbus_method_invocation_return_dbus_error(
        invocation, "org.freedesktop.DBus.Error.UnknownMethod",
        "Unknown method");
}

const GDBusInterfaceVTable kVtable = {
    on_method_call,
    nullptr,
    nullptr,
    {0},
};

gboolean on_bus_message(GstBus*, GstMessage* message, gpointer data) {
    auto* daemon = static_cast<Daemon*>(data);
    switch (GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_ERROR: {
            GError* error = nullptr;
            gchar* debug = nullptr;
            gst_message_parse_error(message, &error, &debug);
            g_printerr("[npueffectsd] pipeline error: %s (%s)\n",
                       error ? error->message : "unknown",
                       debug ? debug : "no debug");
            g_clear_error(&error);
            g_free(debug);
            daemon->failed = true;
            if (daemon->loop) {
                g_main_loop_quit(daemon->loop);
            }
            break;
        }
        case GST_MESSAGE_EOS:
            g_print("[npueffectsd] pipeline EOS\n");
            daemon->failed = true;
            if (daemon->loop) {
                g_main_loop_quit(daemon->loop);
            }
            break;
        case GST_MESSAGE_WARNING: {
            GError* error = nullptr;
            gchar* debug = nullptr;
            gst_message_parse_warning(message, &error, &debug);
            g_printerr("[npueffectsd] pipeline warning: %s\n",
                       error ? error->message : "unknown");
            g_clear_error(&error);
            g_free(debug);
            break;
        }
        default:
            break;
    }
    return TRUE;
}

std::string build_pipeline(const Daemon& daemon) {
    if (daemon.testMode) {
        return "videotestsrc is-live=true ! "
               "video/x-raw,width=1280,height=720,framerate=30/1 ! "
               "videoconvert ! video/x-raw,format=RGB ! npueffects "
               "name=effects mode=" +
               daemon.mode + " gamma=" + std::to_string(daemon.gamma) +
               " blur-strength=" + std::to_string(daemon.blur) +
               " smooth-strength=" + std::to_string(daemon.smooth) +
               " model-dir=" + daemon.models + " device=" + daemon.device +
               " ! fakesink sync=false";
    }
    // The main pipeline is always PLAYING with a black placeholder, so a
    // V4L2 consumer can preroll before the physical camera starts. The camera
    // source is explicitly switched to READY while idle; that is what turns
    // off the sensor and its hardware LED.
    return "appsrc name=src is-live=true format=time do-timestamp=true "
           "block=false max-buffers=2 "
           "caps=video/x-raw,format=RGB,width=1280,height=720,framerate=30/1 ! "
           "npueffects name=effects mode=off gamma=1.0 blur-strength=" +
           std::to_string(daemon.blur) + " smooth-strength=" +
           std::to_string(daemon.smooth) + " model-dir=" + daemon.models +
           " device=" + daemon.device +
           " ! queue max-size-buffers=2 leaky=downstream ! videoconvert ! "
           "video/x-raw,format=YUY2 ! v4l2sink device=" +
           std::string(kDevice) + " sync=false";
}

void print_usage() {
    g_printerr(
        "usage: npueffectsd [--test] [--pipeline PIPELINE] "
        "[--models DIR] [--device NPU] [--mode off|blur|beauty|blur-beauty] "
        "[--gamma 0.85] [--blur 0.6] [--smooth 0.5]\n");
}

}  // namespace

int main(int argc, char** argv) {
    gst_init(&argc, &argv);

    Daemon daemon;
    daemon.models =
        std::string(g_get_home_dir()) + "/npu-models";
    std::string pipelineOverride;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                print_usage();
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "--test") {
            daemon.testMode = true;
        } else if (arg == "--pipeline") {
            pipelineOverride = next();
        } else if (arg == "--models") {
            daemon.models = next();
        } else if (arg == "--device") {
            daemon.device = next();
        } else if (arg == "--mode") {
            daemon.mode = next();
        } else if (arg == "--gamma") {
            daemon.gamma = std::stod(next());
        } else if (arg == "--blur") {
            daemon.blur = std::stod(next());
        } else if (arg == "--smooth") {
            daemon.smooth = std::stod(next());
        } else if (arg == "--help" || arg == "-h") {
            print_usage();
            return 0;
        } else {
            print_usage();
            return 2;
        }
    }

    const std::string pipelineDesc =
        pipelineOverride.empty() ? build_pipeline(daemon) : pipelineOverride;
    GError* error = nullptr;
    daemon.pipeline = gst_parse_launch(pipelineDesc.c_str(), &error);
    if (!daemon.pipeline) {
        g_printerr("[npueffectsd] cannot build pipeline: %s\n",
                   error ? error->message : "unknown");
        g_clear_error(&error);
        return 1;
    }
    daemon.effects =
        gst_bin_get_by_name(GST_BIN(daemon.pipeline), "effects");
    if (!daemon.effects) {
        g_printerr("[npueffectsd] pipeline has no 'effects' element\n");
        gst_object_unref(daemon.pipeline);
        return 1;
    }
    if (!daemon.testMode) {
        daemon.appsrc =
            gst_bin_get_by_name(GST_BIN(daemon.pipeline), "src");
        if (!daemon.appsrc) {
            g_printerr("[npueffectsd] pipeline lacks appsrc\n");
            gst_object_unref(daemon.effects);
            gst_object_unref(daemon.pipeline);
            return 1;
        }

        // Separate camera pipeline: its appsink feeds the always-running
        // appsrc. Starting/stopping libcamera never renegotiates v4l2sink.
        GError* camError = nullptr;
        daemon.cameraPipeline = gst_parse_launch(
            "libcamerasrc name=camera ! "
            "video/x-raw,width=1280,height=720,framerate=30/1,"
            "pixel-aspect-ratio=1/1 ! videoconvert ! "
            "video/x-raw,format=RGB,width=1280,height=720,framerate=30/1,"
            "pixel-aspect-ratio=1/1 ! "
            "appsink name=camsink emit-signals=true max-buffers=2 "
            "drop=true sync=false", &camError);
        if (!daemon.cameraPipeline) {
            g_printerr("[npueffectsd] cannot build camera pipeline: %s\n",
                       camError ? camError->message : "unknown");
            g_clear_error(&camError);
        } else {
            daemon.cameraSink =
                gst_bin_get_by_name(GST_BIN(daemon.cameraPipeline),
                                    "camsink");
            if (daemon.cameraSink) {
                g_signal_connect(daemon.cameraSink, "new-sample",
                                 G_CALLBACK(on_camera_sample), &daemon);
            }
            GstBus* camBus = gst_element_get_bus(daemon.cameraPipeline);
            gst_bus_add_watch(camBus, on_camera_bus, &daemon);
            gst_object_unref(camBus);
            gst_element_set_state(daemon.cameraPipeline, GST_STATE_READY);
        }
    }

    GstBus* bus = gst_element_get_bus(daemon.pipeline);
    gst_bus_add_watch(bus, on_bus_message, &daemon);
    gst_object_unref(bus);

    daemon.loop = g_main_loop_new(nullptr, FALSE);

    GError* dbusError = nullptr;
    daemon.connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &dbusError);
    if (!daemon.connection) {
        g_printerr("[npueffectsd] cannot connect to session bus: %s\n",
                   dbusError ? dbusError->message : "unknown");
        g_clear_error(&dbusError);
        gst_element_set_state(daemon.pipeline, GST_STATE_NULL);
        gst_object_unref(daemon.effects);
        gst_object_unref(daemon.pipeline);
        g_main_loop_unref(daemon.loop);
        return 1;
    }

    GDBusNodeInfo* nodeInfo =
        g_dbus_node_info_new_for_xml(kIntrospectionXml, &dbusError);
    if (!nodeInfo) {
        g_printerr("[npueffectsd] bad introspection XML: %s\n",
                   dbusError ? dbusError->message : "unknown");
        g_clear_error(&dbusError);
        return 1;
    }
    daemon.registrationId = g_dbus_connection_register_object(
        daemon.connection, kObjectPath, nodeInfo->interfaces[0], &kVtable,
        &daemon, nullptr, &dbusError);
    if (!daemon.registrationId) {
        g_printerr("[npueffectsd] cannot register D-Bus object: %s\n",
                   dbusError ? dbusError->message : "unknown");
        g_clear_error(&dbusError);
        g_dbus_node_info_unref(nodeInfo);
        return 1;
    }
    daemon.ownerId = g_bus_own_name_on_connection(
        daemon.connection, kService, G_BUS_NAME_OWNER_FLAGS_NONE,
        on_name_acquired, on_name_lost, &daemon, nullptr);

    // Seed the appsrc before the state change: a live appsrc with no buffer
    // cannot preroll, and the blocking get_state() below would time out.
    if (!daemon.testMode) {
        push_black(&daemon);
    }

    // Main pipeline is always PLAYING with black placeholder frames.
    // The physical camera is a separate source inside it; READY there stops
    // libcamera and the hardware LED while keeping the virtual camera alive.
    GstStateChangeReturn state =
        gst_element_set_state(daemon.pipeline, GST_STATE_PLAYING);
    if (state == GST_STATE_CHANGE_FAILURE) {
        g_printerr("[npueffectsd] pipeline failed to start\n");
        daemon.failed = true;
        g_main_loop_quit(daemon.loop);
    } else if (state == GST_STATE_CHANGE_ASYNC) {
        state = gst_element_get_state(daemon.pipeline, nullptr, nullptr,
                                      15 * GST_SECOND);
        if (state == GST_STATE_CHANGE_FAILURE) {
            g_printerr("[npueffectsd] pipeline did not reach PLAYING\n");
            daemon.failed = true;
            g_main_loop_quit(daemon.loop);
        }
    }
    if (!daemon.testMode) {
        apply_effects(daemon, false);
        daemon.cameraStreaming = false;
        daemon.active = FALSE;
        start_black_timer(daemon);
        if (daemon.cameraPipeline && camera_in_use()) {
            start_camera(daemon);
        }
    } else {
        daemon.active = TRUE;
    }
    g_print("[npueffectsd] pipeline %s (mode=%s, device=%s, %s)\n",
            daemon.testMode ? "test" : "camera", daemon.mode.c_str(),
            daemon.device.c_str(),
            daemon.cameraStreaming ? "camera" : "idle black");
    daemon.activeSourceId = g_timeout_add(200, update_active, &daemon);

    g_main_loop_run(daemon.loop);

    g_source_remove(daemon.activeSourceId);
    if (daemon.ownerId) {
        g_bus_unown_name(daemon.ownerId);
    }
    if (daemon.registrationId) {
        g_dbus_connection_unregister_object(daemon.connection,
                                            daemon.registrationId);
    }
    stop_black_timer(daemon);
    if (daemon.cameraPipeline) {
        gst_element_set_state(daemon.cameraPipeline, GST_STATE_READY);
    }
    gst_element_set_state(daemon.pipeline, GST_STATE_NULL);
    gst_object_unref(daemon.effects);
    if (daemon.appsrc) {
        gst_object_unref(daemon.appsrc);
    }
    if (daemon.cameraSink) {
        gst_object_unref(daemon.cameraSink);
    }
    if (daemon.cameraPipeline) {
        gst_object_unref(daemon.cameraPipeline);
    }
    gst_object_unref(daemon.pipeline);
    g_dbus_node_info_unref(nodeInfo);
    g_object_unref(daemon.connection);
    g_main_loop_unref(daemon.loop);
    return daemon.failed ? 1 : 0;
}
