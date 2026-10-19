#pragma once

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <openvino/openvino.hpp>

namespace npucam::adapters::detail {

enum class ModelColorOrder { RGB, BGR };

// Resize an RGB8 image and pack it as NCHW float32, applying (v - mean) * scale.
inline ov::Tensor toNchw(const cv::Mat& rgb, int width, int height,
                         ModelColorOrder order, float mean, float scale) {
    cv::Mat resized;
    cv::resize(rgb, resized, cv::Size(width, height), 0, 0, cv::INTER_LINEAR);

    ov::Tensor tensor(ov::element::f32,
                      ov::Shape{1, 3, static_cast<size_t>(height),
                                static_cast<size_t>(width)});
    float* dst = tensor.data<float>();
    const int plane = width * height;
    for (int y = 0; y < height; ++y) {
        const uint8_t* row = resized.ptr<uint8_t>(y);
        for (int x = 0; x < width; ++x) {
            float r = row[3 * x + 0];
            float g = row[3 * x + 1];
            float b = row[3 * x + 2];
            if (order == ModelColorOrder::BGR) {
                std::swap(r, b);
            }
            const int i = y * width + x;
            dst[i] = (r - mean) * scale;
            dst[plane + i] = (g - mean) * scale;
            dst[2 * plane + i] = (b - mean) * scale;
        }
    }
    return tensor;
}

// Copy an OpenVINO output tensor to float32 and return a row-major buffer.
inline std::vector<float> toFloatVector(const ov::Tensor& tensor) {
    ov::Tensor f32(ov::element::f32, tensor.get_shape());
    tensor.copy_to(f32);
    const float* data = f32.data<float>();
    return std::vector<float>(data, data + f32.get_size());
}

}  // namespace npucam::adapters::detail
