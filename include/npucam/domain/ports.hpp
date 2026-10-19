#pragma once

#include <vector>

#include "npucam/domain/types.hpp"

namespace npucam::domain {

// Port: NPU portrait matting. Implementations own their compiled model.
class IPortraitSegmenter {
public:
    virtual ~IPortraitSegmenter() = default;
    virtual bool segment(const ImageView& frame, PortraitMask& mask) = 0;
};

// Port: NPU face analysis. Landmarks are optional and filled when available.
class IFaceAnalyzer {
public:
    virtual ~IFaceAnalyzer() = default;
    virtual std::vector<Face> analyze(const ImageView& frame) = 0;
};

// Port: pixel operations. Takes the optional mask/faces and renders in place.
class IEffectRenderer {
public:
    virtual ~IEffectRenderer() = default;
    virtual void render(ImageView& frame,
                        const EffectSettings& settings,
                        const PortraitMask* mask,
                        const std::vector<Face>& faces) = 0;
};

}  // namespace npucam::domain
