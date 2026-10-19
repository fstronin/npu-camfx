#pragma once

#include <mutex>
#include <string>

#include "npucam/domain/ports.hpp"

namespace npucam::application {

// Application use case: one frame in, one frame out.
// It only knows domain ports; OpenVINO/GStreamer/OpenCV live in adapters.
class EffectProcessor {
public:
    EffectProcessor(domain::IPortraitSegmenter& segmenter,
                    domain::IFaceAnalyzer& faceAnalyzer,
                    domain::IEffectRenderer& renderer);

    // temporal=true smooths the portrait mask between video frames.
    // Still previews must pass temporal=false so they do not contaminate
    // the live mask history.
    bool process(domain::ImageView& frame,
                 const domain::EffectSettings& settings,
                 bool temporal = true);

    // Clears EMA history; called when streaming resumes after READY.
    void resetTemporalState();

    std::string lastError() const;

private:
    domain::IPortraitSegmenter& segmenter_;
    domain::IFaceAnalyzer& faceAnalyzer_;
    domain::IEffectRenderer& renderer_;
    mutable std::mutex mutex_;
    std::string lastError_;
    domain::PortraitMask lastMask_;
    bool hasLastMask_ = false;
};

}  // namespace npucam::application
