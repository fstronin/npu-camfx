#pragma once

#include <memory>
#include <string>

#include "npucam/domain/ports.hpp"

namespace npucam::adapters {

struct SegmenterConfig {
    std::string modelPath;
    int width = 256;
    int height = 256;
    float mean = 0.0f;
    float scale = 1.0f / 255.0f;
    bool modelOrderBgr = false;
};

// OpenVINO portrait matting. The config makes the adapter usable for both
// MediaPipe Selfie Segmentation (256x256, [0,1]) and MODNet (dynamic, [-1,1]).
class OpenVinoSegmenter final : public domain::IPortraitSegmenter {
public:
    OpenVinoSegmenter(SegmenterConfig config, std::string device);
    ~OpenVinoSegmenter() override;

    bool segment(const domain::ImageView& frame,
                 domain::PortraitMask& mask) override;

    std::string activeDevice() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace npucam::adapters
