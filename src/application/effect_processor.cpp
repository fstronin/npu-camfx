#include "npucam/application/effect_processor.hpp"

#include <cmath>
#include <exception>

namespace npucam::application {

EffectProcessor::EffectProcessor(domain::IPortraitSegmenter& segmenter,
                                 domain::IFaceAnalyzer& faceAnalyzer,
                                 domain::IEffectRenderer& renderer)
    : segmenter_(segmenter), faceAnalyzer_(faceAnalyzer), renderer_(renderer) {}

bool EffectProcessor::process(domain::ImageView& frame,
                              const domain::EffectSettings& settings,
                              bool temporal) {
    if (!frame.valid()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    lastError_.clear();

    const bool wantBlur = domain::hasBlur(settings.mode);
    const bool wantBeauty = domain::hasBeauty(settings.mode);
    const bool wantGamma = std::fabs(settings.gamma - 1.0f) > 0.001f;
    if (temporal && !wantBlur) {
        // Leaving blur mode must not blend the next blur session with a stale
        // mask from minutes ago.
        hasLastMask_ = false;
        lastMask_ = {};
    }
    if (!wantBlur && !wantBeauty && !wantGamma) {
        return true;
    }

    domain::PortraitMask mask;
    const domain::PortraitMask* maskPtr = nullptr;
    if (wantBlur) {
        try {
            if (segmenter_.segment(frame, mask)) {
                if (temporal && mask.width > 0 && mask.height > 0) {
                    if (hasLastMask_ &&
                        lastMask_.width == mask.width &&
                        lastMask_.height == mask.height &&
                        lastMask_.values.size() == mask.values.size()) {
                        double diff = 0.0;
                        for (size_t i = 0; i < mask.values.size(); ++i) {
                            diff += std::fabs(mask.values[i] -
                                              lastMask_.values[i]);
                        }
                        const double mad =
                            diff / static_cast<double>(mask.values.size());
                        // Strong smoothing while stationary, faster adaptation
                        // for gestures/motion, no ghosting on a scene cut.
                        const float alpha =
                            mad > 0.2 ? 1.0f : (mad > 0.03 ? 0.65f : 0.2f);
                        for (size_t i = 0; i < mask.values.size(); ++i) {
                            mask.values[i] =
                                lastMask_.values[i] * (1.0f - alpha) +
                                mask.values[i] * alpha;
                        }
                    }
                    lastMask_ = mask;
                    hasLastMask_ = true;
                }
                maskPtr = &mask;
            } else {
                lastError_ = "portrait segmentation failed";
            }
        } catch (const std::exception& e) {
            lastError_ = std::string("portrait segmentation: ") + e.what();
        }
    }

    std::vector<domain::Face> faces;
    if (wantBeauty) {
        try {
            faces = faceAnalyzer_.analyze(frame);
        } catch (const std::exception& e) {
            lastError_ = std::string("face analysis: ") + e.what();
        }
    }

    try {
        renderer_.render(frame, settings, maskPtr, faces);
    } catch (const std::exception& e) {
        lastError_ = std::string("render: ") + e.what();
        return false;
    }
    return true;
}

void EffectProcessor::resetTemporalState() {
    std::lock_guard<std::mutex> lock(mutex_);
    hasLastMask_ = false;
    lastMask_ = {};
}

std::string EffectProcessor::lastError() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastError_;
}

}  // namespace npucam::application
