// GStreamer element: npueffects
//
// RGB in, RGB out. Inference runs on the NPU through OpenVINO; pixel work
// (gamma, blur, skin smoothing) runs on the CPU. The element exposes an
// action signal "generate-previews" so a control daemon can render menu
// thumbnails without linking against the element implementation.
#include <gst/gst.h>
#include <gst/video/gstvideofilter.h>
#include <gst/video/video.h>

#ifndef PACKAGE
#define PACKAGE "npu-camfx"
#endif

#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "npucam/application/effect_processor.hpp"
#include "npucam/domain/types.hpp"
#include "npucam/imaging/cv_effect_renderer.hpp"
#include "npucam/openvino/openvino_segmenter.hpp"
#include "npucam/openvino/omz_face_analyzer.hpp"
#include "npucam/preview/preview_generator.hpp"

typedef struct _GstNpuEffects GstNpuEffects;
typedef struct _GstNpuEffectsClass GstNpuEffectsClass;

#define NPUCAM_TYPE_EFFECTS (gst_npu_effects_get_type())
#define NPUCAM_EFFECTS(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), NPUCAM_TYPE_EFFECTS, GstNpuEffects))

struct NpuEffectsImpl {
    std::mutex lock;
    npucam::domain::EffectSettings settings;
    std::string modelDir;
    std::string device = "NPU";

    std::unique_ptr<npucam::adapters::OpenVinoSegmenter> segmenter;
    std::unique_ptr<npucam::adapters::OmzFaceAnalyzer> analyzer;
    std::unique_ptr<npucam::adapters::CvEffectRenderer> renderer;
    std::unique_ptr<npucam::application::EffectProcessor> processor;

    std::vector<uint8_t> snapshot;
    int snapshotWidth = 0;
    int snapshotHeight = 0;
    int snapshotStride = 0;
    gint64 lastSnapshotUs = 0;
};

struct _GstNpuEffects {
    GstVideoFilter parent;
    NpuEffectsImpl* impl;
};

struct _GstNpuEffectsClass {
    GstVideoFilterClass parent_class;
};

G_DEFINE_TYPE(GstNpuEffects, gst_npu_effects, GST_TYPE_VIDEO_FILTER)

GVariant* gst_npu_effects_generate_previews(GstNpuEffects* self,
                                            const gchar* dir,
                                            gpointer user_data);

static void gst_npu_effects_init(GstNpuEffects* self) {
    self->impl = new NpuEffectsImpl();
    self->impl->modelDir = std::string(g_get_home_dir()) + "/npu-models";
    self->impl->settings.gamma = 1.0f;
    self->impl->settings.blurStrength = 0.6f;
    self->impl->settings.smoothStrength = 0.7f;
    g_signal_connect(self, "generate-previews",
                     G_CALLBACK(gst_npu_effects_generate_previews), nullptr);
}

static void gst_npu_effects_finalize(GObject* object) {
    auto* self = NPUCAM_EFFECTS(object);
    delete self->impl;
    self->impl = nullptr;
    G_OBJECT_CLASS(gst_npu_effects_parent_class)->finalize(object);
}

static void gst_npu_effects_set_property(GObject* object, guint propId,
                                         const GValue* value,
                                         GParamSpec* spec) {
    auto* self = NPUCAM_EFFECTS(object);
    auto* impl = self->impl;
    switch (propId) {
        case 1: {  // mode
            npucam::domain::EffectMode mode;
            const gchar* text = g_value_get_string(value);
            if (text && npucam::domain::modeFromString(text, mode)) {
                std::lock_guard<std::mutex> lock(impl->lock);
                impl->settings.mode = mode;
            } else {
                GST_WARNING_OBJECT(self, "ignoring unknown mode '%s'",
                                   text ? text : "(null)");
            }
            break;
        }
        case 2: {  // gamma
            std::lock_guard<std::mutex> lock(impl->lock);
            impl->settings.gamma =
                static_cast<float>(g_value_get_double(value));
            break;
        }
        case 3: {  // blur-strength
            std::lock_guard<std::mutex> lock(impl->lock);
            impl->settings.blurStrength =
                static_cast<float>(g_value_get_double(value));
            break;
        }
        case 4: {  // smooth-strength
            std::lock_guard<std::mutex> lock(impl->lock);
            impl->settings.smoothStrength =
                static_cast<float>(g_value_get_double(value));
            break;
        }
        case 5: {  // model-dir
            std::lock_guard<std::mutex> lock(impl->lock);
            impl->modelDir = g_value_get_string(value);
            break;
        }
        case 6: {  // device
            std::lock_guard<std::mutex> lock(impl->lock);
            impl->device = g_value_get_string(value);
            break;
        }
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, propId, spec);
    }
}

static void gst_npu_effects_get_property(GObject* object, guint propId,
                                         GValue* value, GParamSpec* spec) {
    auto* self = NPUCAM_EFFECTS(object);
    auto* impl = self->impl;
    std::lock_guard<std::mutex> lock(impl->lock);
    switch (propId) {
        case 1:
            g_value_set_string(
                value, npucam::domain::toString(impl->settings.mode).c_str());
            break;
        case 2:
            g_value_set_double(value, impl->settings.gamma);
            break;
        case 3:
            g_value_set_double(value, impl->settings.blurStrength);
            break;
        case 4:
            g_value_set_double(value, impl->settings.smoothStrength);
            break;
        case 5:
            g_value_set_string(value, impl->modelDir.c_str());
            break;
        case 6:
            g_value_set_string(value, impl->device.c_str());
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID(object, propId, spec);
    }
}

static gboolean gst_npu_effects_start(GstBaseTransform* trans) {
    auto* self = NPUCAM_EFFECTS(trans);
    auto* impl = self->impl;
    std::string modelDir;
    std::string device;
    {
        std::lock_guard<std::mutex> lock(impl->lock);
        modelDir = impl->modelDir;
        device = impl->device;
    }
    if (impl->processor) {
        // READY -> PAUSED after on-demand resume: models are still loaded.
        impl->processor->resetTemporalState();
        return TRUE;
    }
    try {
        npucam::adapters::SegmenterConfig segConfig;
        segConfig.modelPath = modelDir + "/selfie_mp_fp16.onnx";
        segConfig.width = 256;
        segConfig.height = 256;
        segConfig.mean = 0.0f;
        segConfig.scale = 1.0f / 255.0f;
        segConfig.modelOrderBgr = false;
        impl->segmenter =
            std::make_unique<npucam::adapters::OpenVinoSegmenter>(segConfig,
                                                                  device);
        impl->analyzer =
            std::make_unique<npucam::adapters::OmzFaceAnalyzer>(modelDir,
                                                                device);
        impl->renderer =
            std::make_unique<npucam::adapters::CvEffectRenderer>();
        impl->processor =
            std::make_unique<npucam::application::EffectProcessor>(
                *impl->segmenter, *impl->analyzer, *impl->renderer);
    } catch (const std::exception& e) {
        GST_ERROR_OBJECT(self,
                         "model initialization failed: %s; "
                         "effects will pass through",
                         e.what());
        impl->processor.reset();
        impl->segmenter.reset();
        impl->analyzer.reset();
        impl->renderer.reset();
    }
    return TRUE;
}

static gboolean gst_npu_effects_stop(GstBaseTransform* trans) {
    // Keep compiled models across READY/PAUSED: the daemon goes READY when
    // no application uses the camera, and recompiling the NPU graph would
    // add seconds to every camera open. finalize() releases them.
    (void)trans;
    return TRUE;
}

static GstFlowReturn gst_npu_effects_transform_ip(GstVideoFilter* filter,
                                                  GstVideoFrame* frame) {
    auto* self = NPUCAM_EFFECTS(filter);
    auto* impl = self->impl;
    if (GST_VIDEO_INFO_FORMAT(&frame->info) != GST_VIDEO_FORMAT_RGB) {
        GST_WARNING_OBJECT(self, "only RGB is supported");
        return GST_FLOW_NOT_NEGOTIATED;
    }

    const int width = GST_VIDEO_FRAME_WIDTH(frame);
    const int height = GST_VIDEO_FRAME_HEIGHT(frame);
    const int stride = GST_VIDEO_FRAME_PLANE_STRIDE(frame, 0);
    auto* data =
        static_cast<uint8_t*>(GST_VIDEO_FRAME_PLANE_DATA(frame, 0));

    npucam::domain::EffectSettings settings;
    {
        std::lock_guard<std::mutex> lock(impl->lock);
        settings = impl->settings;
    }

    // Keep a raw copy before the renderer mutates the buffer in place.
    // Previews must start from an untouched frame, otherwise an active blur
    // would be baked into every preview (and applied twice).
    const gint64 now = g_get_monotonic_time();
    if (now - impl->lastSnapshotUs >= 500000) {
        std::lock_guard<std::mutex> lock(impl->lock);
        if (now - impl->lastSnapshotUs >= 500000) {
            impl->snapshotWidth = width;
            impl->snapshotHeight = height;
            impl->snapshotStride = width * 3;
            impl->snapshot.resize(static_cast<size_t>(width) * height * 3);
            for (int y = 0; y < height; ++y) {
                std::memcpy(impl->snapshot.data() +
                                static_cast<size_t>(y) * width * 3,
                            data + static_cast<size_t>(y) * stride,
                            static_cast<size_t>(width) * 3);
            }
            impl->lastSnapshotUs = now;
        }
    }

    if (impl->processor) {
        npucam::domain::ImageView view{data, width, height, stride};
        if (!impl->processor->process(view, settings)) {
            GST_WARNING_OBJECT(self, "%s",
                               impl->processor->lastError().c_str());
        }
    }
    return GST_FLOW_OK;
}

GVariant* gst_npu_effects_generate_previews(GstNpuEffects* self,
                                            const gchar* dir,
                                            gpointer user_data) {
    (void)user_data;
    auto* impl = self->impl;
    if (!impl->processor || !dir) {
        return g_variant_new_array(G_VARIANT_TYPE("{ss}"), nullptr, 0);
    }

    std::vector<uint8_t> pixels;
    int width = 0;
    int height = 0;
    int stride = 0;
    npucam::domain::EffectSettings settings;
    {
        std::lock_guard<std::mutex> lock(impl->lock);
        pixels = impl->snapshot;
        width = impl->snapshotWidth;
        height = impl->snapshotHeight;
        stride = impl->snapshotStride;
        settings = impl->settings;
    }
    if (pixels.empty() || width <= 0 || height <= 0) {
        return g_variant_new_array(G_VARIANT_TYPE("{ss}"), nullptr, 0);
    }

    g_mkdir_with_parents(dir, 0700);
    npucam::domain::ImageView view{pixels.data(), width, height, stride};
    npucam::preview::PreviewGenerator generator(*impl->processor, dir);
    const auto paths = generator.generate(view, settings);

    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a{ss}"));
    for (const auto& [mode, path] : paths) {
        g_variant_builder_add(&builder, "{ss}", mode.c_str(), path.c_str());
    }
    return g_variant_builder_end(&builder);
}

static void gst_npu_effects_class_init(GstNpuEffectsClass* klass) {
    GObjectClass* gobject = G_OBJECT_CLASS(klass);
    gobject->finalize = gst_npu_effects_finalize;
    gobject->set_property = gst_npu_effects_set_property;
    gobject->get_property = gst_npu_effects_get_property;

    g_object_class_install_property(
        gobject, 1,
        g_param_spec_string("mode", "Effect mode",
                            "off, blur, beauty or blur-beauty", "off",
                            static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject, 2,
        g_param_spec_double("gamma", "Gamma",
                            "Luma gamma (GStreamer convention: 1/gamma)",
                            0.5, 2.0, 1.0,
                            static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject, 3,
        g_param_spec_double("blur-strength", "Blur strength",
                            "Background blur strength", 0.0, 1.0, 0.6,
                            static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject, 4,
        g_param_spec_double("smooth-strength", "Smooth strength",
                            "Skin smoothing strength", 0.0, 1.0, 0.7,
                            static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject, 5,
        g_param_spec_string("model-dir", "Model directory",
                            "Directory with MODNet and OMZ face models", "",
                            static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
    g_object_class_install_property(
        gobject, 6,
        g_param_spec_string("device", "OpenVINO device",
                            "OpenVINO device: NPU or CPU", "NPU",
                            static_cast<GParamFlags>(G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

    gst_element_class_set_static_metadata(
        GST_ELEMENT_CLASS(klass), "NPU camera effects", "Filter/Video",
        "Portrait blur and skin smoothing with NPU inference",
        "npu-camfx");

    GstCaps* caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING,
                                        "RGB", nullptr);
    GstPadTemplate* sinkTemplate = gst_pad_template_new(
        "sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps);
    GstPadTemplate* srcTemplate = gst_pad_template_new(
        "src", GST_PAD_SRC, GST_PAD_ALWAYS, caps);
    gst_element_class_add_pad_template(GST_ELEMENT_CLASS(klass), sinkTemplate);
    gst_element_class_add_pad_template(GST_ELEMENT_CLASS(klass), srcTemplate);
    gst_caps_unref(caps);

    GstBaseTransformClass* base = GST_BASE_TRANSFORM_CLASS(klass);
    base->start = gst_npu_effects_start;
    base->stop = gst_npu_effects_stop;
    GstVideoFilterClass* video = GST_VIDEO_FILTER_CLASS(klass);
    video->transform_frame_ip = gst_npu_effects_transform_ip;

    g_signal_new("generate-previews", G_TYPE_FROM_CLASS(klass),
                 static_cast<GSignalFlags>(G_SIGNAL_RUN_LAST |
                                           G_SIGNAL_ACTION),
                 0, nullptr, nullptr, nullptr, G_TYPE_VARIANT, 1,
                 G_TYPE_STRING);
}

static gboolean plugin_init(GstPlugin* plugin) {
    return gst_element_register(plugin, "npueffects", GST_RANK_NONE,
                                NPUCAM_TYPE_EFFECTS);
}

GST_PLUGIN_DEFINE(GST_VERSION_MAJOR, GST_VERSION_MINOR, npueffects,
                  "NPU camera effects (portrait blur, beauty)",
                  plugin_init, "0.1.0", "MIT", "npu-camfx",
                  "https://github.com/fstronin/npu-camfx")
