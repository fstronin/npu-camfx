// Small throughput probe used by scripts/bench.sh and the verification notes.
// It counts buffers after the effects element (or after v4l2src) and prints
// frames per second.
#include <gst/gst.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

guint g_count = 0;

GstPadProbeReturn countBuffer(GstPad*, GstPadProbeInfo*, gpointer) {
    ++g_count;
    return GST_PAD_PROBE_OK;
}

}  // namespace

int main(int argc, char** argv) {
    gst_init(&argc, &argv);

    std::string pipeline =
        "videotestsrc is-live=true ! "
        "video/x-raw,width=1280,height=720,framerate=30/1 ! "
        "videoconvert ! video/x-raw,format=RGB ! "
        "npueffects mode=blur ! fakesink name=sink sync=false";
    int seconds = 6;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--vcam") {
            pipeline =
                "v4l2src device=/dev/video60 ! fakesink name=sink sync=false";
        } else if (arg == "--pipeline" && i + 1 < argc) {
            pipeline = argv[++i];
        } else if (arg == "--seconds" && i + 1 < argc) {
            seconds = std::atoi(argv[++i]);
        } else {
            std::fprintf(stderr,
                         "usage: %s [--vcam] [--pipeline STR] [--seconds N]\n",
                         argv[0]);
            return 2;
        }
    }

    GError* error = nullptr;
    GstElement* pipelineElement = gst_parse_launch(pipeline.c_str(), &error);
    if (!pipelineElement) {
        std::fprintf(stderr, "pipeline: %s\n",
                     error ? error->message : "unknown");
        g_clear_error(&error);
        return 1;
    }
    GstElement* sink =
        gst_bin_get_by_name(GST_BIN(pipelineElement), "sink");
    if (!sink) {
        std::fprintf(stderr, "pipeline needs a fakesink named 'sink'\n");
        gst_object_unref(pipelineElement);
        return 1;
    }
    GstPad* pad = gst_element_get_static_pad(sink, "sink");
    gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_BUFFER, countBuffer, nullptr,
                      nullptr);
    gst_element_set_state(pipelineElement, GST_STATE_PLAYING);
    g_usleep(static_cast<gulong>(seconds) * 1000000);
    gst_element_set_state(pipelineElement, GST_STATE_NULL);
    std::printf("frames=%u seconds=%d fps=%.1f\n", g_count, seconds,
                g_count / static_cast<double>(seconds));

    gst_object_unref(pad);
    gst_object_unref(sink);
    gst_object_unref(pipelineElement);
    return 0;
}
