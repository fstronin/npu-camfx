#pragma once

#include <map>
#include <string>

#include "npucam/application/effect_processor.hpp"

namespace npucam::preview {

// Renders one still through every effect mode and writes PNGs for the GNOME
// menu. It reuses the same EffectProcessor as the live pipeline (DRY).
class PreviewGenerator {
public:
    PreviewGenerator(application::EffectProcessor& processor,
                     std::string outputDir);

    std::map<std::string, std::string> generate(
        const domain::ImageView& source,
        const domain::EffectSettings& baseSettings,
        int maxWidth = 640);

private:
    application::EffectProcessor& processor_;
    std::string outputDir_;
};

}  // namespace npucam::preview
