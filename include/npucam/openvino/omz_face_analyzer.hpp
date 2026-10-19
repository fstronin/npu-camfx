#pragma once

#include <memory>
#include <string>
#include <vector>

#include "npucam/domain/ports.hpp"

namespace npucam::adapters {

// OMZ face-detection-retail-0004 + facial-landmarks-35-adas-0002 on OpenVINO.
class OmzFaceAnalyzer final : public domain::IFaceAnalyzer {
public:
    OmzFaceAnalyzer(std::string modelDir, std::string device);
    ~OmzFaceAnalyzer() override;

    std::vector<domain::Face> analyze(const domain::ImageView& frame) override;

    std::string activeDevice() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace npucam::adapters
