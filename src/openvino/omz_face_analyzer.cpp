#include "npucam/openvino/omz_face_analyzer.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <openvino/openvino.hpp>

#include "tensor_utils.hpp"

namespace npucam::adapters {
namespace {

constexpr float kFaceConfidence = 0.5f;
constexpr size_t kMaxFaces = 3;
constexpr int kDetSize = 300;
constexpr int kLandmarkSize = 60;

ov::CompiledModel compileWithFallback(ov::Core& core,
                                      const std::shared_ptr<ov::Model>& model,
                                      const std::string& device,
                                      const char* label,
                                      std::string& usedDevice) {
    try {
        auto compiled = core.compile_model(model, device);
        usedDevice = device;
        return compiled;
    } catch (const std::exception& e) {
        std::cerr << "[npueffects] " << label << " " << device
                  << " unavailable (" << e.what() << "), using CPU\n";
        usedDevice = "CPU";
        return core.compile_model(model, "CPU");
    }
}

}  // namespace

struct OmzFaceAnalyzer::Impl {
    ov::Core core;
    ov::CompiledModel detModel;
    ov::InferRequest detRequest;
    ov::CompiledModel landmarkModel;
    ov::InferRequest landmarkRequest;
    std::string usedDevice;

    Impl(const std::string& dir, const std::string& device) {
        auto det = core.read_model(dir + "/face-detection-retail-0004.xml");
        detModel = compileWithFallback(core, det, device, "face-detection",
                                       usedDevice);
        detRequest = detModel.create_infer_request();

        auto landmarks =
            core.read_model(dir + "/facial-landmarks-35-adas-0002.xml");
        landmarkModel = compileWithFallback(core, landmarks, device,
                                            "facial-landmarks", usedDevice);
        landmarkRequest = landmarkModel.create_infer_request();
        std::cerr << "[npueffects] face analysis on " << usedDevice << "\n";
    }

    std::vector<domain::Face> detect(const cv::Mat& rgb) {
        std::vector<domain::Face> faces;
        auto input = detail::toNchw(rgb, kDetSize, kDetSize,
                                    detail::ModelColorOrder::BGR, 0.0f, 1.0f);
        detRequest.set_input_tensor(input);
        detRequest.infer();

        const ov::Tensor out = detRequest.get_output_tensor(0);
        const auto values = detail::toFloatVector(out);
        const size_t stride = 7;
        const size_t count = values.size() / stride;
        for (size_t i = 0; i < count; ++i) {
            const float* row = values.data() + i * stride;
            const float confidence = row[2];
            if (confidence < kFaceConfidence) {
                continue;
            }
            domain::RectF box;
            box.x = std::clamp(row[3], 0.0f, 1.0f);
            box.y = std::clamp(row[4], 0.0f, 1.0f);
            box.w = std::clamp(row[5], 0.0f, 1.0f) - box.x;
            box.h = std::clamp(row[6], 0.0f, 1.0f) - box.y;
            if (box.w <= 0.01f || box.h <= 0.01f) {
                continue;
            }
            faces.push_back({box, confidence, {}});
        }
        std::sort(faces.begin(), faces.end(),
                  [](const domain::Face& a, const domain::Face& b) {
                      return a.confidence > b.confidence;
                  });
        if (faces.size() > kMaxFaces) {
            faces.resize(kMaxFaces);
        }
        return faces;
    }

    void addLandmarks(const cv::Mat& rgb, domain::Face& face) {
        const int frameW = rgb.cols;
        const int frameH = rgb.rows;
        const float cx = (face.box.x + face.box.w * 0.5f) * frameW;
        const float cy = (face.box.y + face.box.h * 0.5f) * frameH;
        const float side = std::max(face.box.w * frameW, face.box.h * frameH) *
                           1.3f;
        int x0 = static_cast<int>(std::lround(cx - side * 0.5f));
        int y0 = static_cast<int>(std::lround(cy - side * 0.5f));
        int x1 = static_cast<int>(std::lround(cx + side * 0.5f));
        int y1 = static_cast<int>(std::lround(cy + side * 0.5f));
        x0 = std::clamp(x0, 0, frameW - 1);
        y0 = std::clamp(y0, 0, frameH - 1);
        x1 = std::clamp(x1, x0 + 1, frameW);
        y1 = std::clamp(y1, y0 + 1, frameH);
        if ((x1 - x0) < 32 || (y1 - y0) < 32) {
            return;
        }

        cv::Rect rect(x0, y0, x1 - x0, y1 - y0);
        cv::Mat crop = rgb(rect);
        auto input = detail::toNchw(crop, kLandmarkSize, kLandmarkSize,
                                    detail::ModelColorOrder::BGR, 0.0f, 1.0f);
        landmarkRequest.set_input_tensor(input);
        landmarkRequest.infer();
        const ov::Tensor out = landmarkRequest.get_output_tensor(0);
        const auto values = detail::toFloatVector(out);
        if (values.size() < 70) {
            return;
        }
        face.landmarks.reserve(35);
        for (int i = 0; i < 35; ++i) {
            domain::PointF p;
            p.x = (rect.x + values[2 * i] * rect.width) / frameW;
            p.y = (rect.y + values[2 * i + 1] * rect.height) / frameH;
            p.x = std::clamp(p.x, 0.0f, 1.0f);
            p.y = std::clamp(p.y, 0.0f, 1.0f);
            face.landmarks.push_back(p);
        }
    }

    std::vector<domain::Face> analyze(const domain::ImageView& frame) {
        if (!frame.valid()) {
            return {};
        }
        cv::Mat rgb(frame.height, frame.width, CV_8UC3, frame.data,
                    static_cast<size_t>(frame.stride));
        auto faces = detect(rgb);
        for (auto& face : faces) {
            addLandmarks(rgb, face);
        }
        return faces;
    }
};

OmzFaceAnalyzer::OmzFaceAnalyzer(std::string modelDir, std::string device)
    : impl_(std::make_unique<Impl>(modelDir, device)) {}

OmzFaceAnalyzer::~OmzFaceAnalyzer() = default;

std::vector<domain::Face> OmzFaceAnalyzer::analyze(
    const domain::ImageView& frame) {
    return impl_->analyze(frame);
}

std::string OmzFaceAnalyzer::activeDevice() const {
    return impl_->usedDevice;
}

}  // namespace npucam::adapters
