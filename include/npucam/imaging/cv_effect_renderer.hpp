#pragma once

#include <vector>

#include "npucam/domain/ports.hpp"

namespace npucam::adapters {

// CPU/OpenCV pixel work: luma gamma, bokeh-style background blur and
// face-aware skin smoothing. Inference is not done here.
class CvEffectRenderer final : public domain::IEffectRenderer {
public:
    void render(domain::ImageView& frame,
                const domain::EffectSettings& settings,
                const domain::PortraitMask* mask,
                const std::vector<domain::Face>& faces) override;
};

}  // namespace npucam::adapters
