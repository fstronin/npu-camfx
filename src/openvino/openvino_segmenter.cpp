#include "npucam/openvino/openvino_segmenter.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <openvino/openvino.hpp>

#include "tensor_utils.hpp"

namespace npucam::adapters {

struct OpenVinoSegmenter::Impl {
    ov::Core core;
    ov::CompiledModel compiled;
    ov::InferRequest request;
    SegmenterConfig config;
    std::string usedDevice;

    Impl(SegmenterConfig cfg, const std::string& device)
        : config(std::move(cfg)) {
        auto model = core.read_model(config.modelPath);
        if (model->inputs().size() != 1) {
            throw std::runtime_error("segmenter must have exactly one input");
        }
        const auto shape = model->input(0).get_partial_shape();
        if (shape.is_dynamic()) {
            model->reshape(ov::PartialShape{
                1, 3, static_cast<int64_t>(config.height),
                static_cast<int64_t>(config.width)});
        } else {
            config.height = static_cast<int>(shape[2].get_length());
            config.width = static_cast<int>(shape[3].get_length());
        }

        try {
            compiled = core.compile_model(model, device);
            usedDevice = device;
        } catch (const std::exception& e) {
            std::cerr << "[npueffects] segmenter " << device
                      << " unavailable (" << e.what() << "), using CPU\n";
            compiled = core.compile_model(model, "CPU");
            usedDevice = "CPU";
        }
        request = compiled.create_infer_request();
        std::cerr << "[npueffects] segmenter on " << usedDevice << " at "
                  << config.width << "x" << config.height << "\n";
    }

    bool run(const domain::ImageView& frame, domain::PortraitMask& mask) {
        if (!frame.valid()) {
            return false;
        }
        cv::Mat rgb(frame.height, frame.width, CV_8UC3, frame.data,
                    static_cast<size_t>(frame.stride));
        auto input = detail::toNchw(
            rgb, config.width, config.height,
            config.modelOrderBgr ? detail::ModelColorOrder::BGR
                                 : detail::ModelColorOrder::RGB,
            config.mean, config.scale);
        request.set_input_tensor(input);
        request.infer();

        const ov::Tensor out = request.get_output_tensor(0);
        const auto values = detail::toFloatVector(out);
        const auto shape = out.get_shape();
        if (shape.size() < 2) {
            return false;
        }
        const int outHeight = static_cast<int>(shape[shape.size() - 2]);
        const int outWidth = static_cast<int>(shape[shape.size() - 1]);
        if (outHeight <= 0 || outWidth <= 0 ||
            values.size() < static_cast<size_t>(outHeight) * outWidth) {
            return false;
        }

        mask.width = outWidth;
        mask.height = outHeight;
        mask.values.resize(static_cast<size_t>(outWidth) * outHeight);
        for (size_t i = 0; i < mask.values.size(); ++i) {
            mask.values[i] = std::clamp(values[i], 0.0f, 1.0f);
        }
        // Remove one-pixel salt/pepper noise before the temporal filter; this
        // also makes the 256 -> 1280 upscale less jittery.
        cv::Mat1f maskMat(outHeight, outWidth);
        std::copy(mask.values.begin(), mask.values.end(), maskMat.begin());
        cv::medianBlur(maskMat, maskMat, 3);
        std::copy(maskMat.begin(), maskMat.end(), mask.values.begin());
        return true;
    }
};

OpenVinoSegmenter::OpenVinoSegmenter(SegmenterConfig config,
                                     std::string device)
    : impl_(std::make_unique<Impl>(std::move(config), device)) {}

OpenVinoSegmenter::~OpenVinoSegmenter() = default;

bool OpenVinoSegmenter::segment(const domain::ImageView& frame,
                                domain::PortraitMask& mask) {
    return impl_->run(frame, mask);
}

std::string OpenVinoSegmenter::activeDevice() const {
    return impl_->usedDevice;
}

}  // namespace npucam::adapters
