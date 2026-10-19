#include "npucam/preview/preview_generator.hpp"

#include <algorithm>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace npucam::preview {

PreviewGenerator::PreviewGenerator(application::EffectProcessor& processor,
                                   std::string outputDir)
    : processor_(processor), outputDir_(std::move(outputDir)) {}

std::map<std::string, std::string> PreviewGenerator::generate(
    const domain::ImageView& source,
    const domain::EffectSettings& baseSettings, int maxWidth) {
    std::map<std::string, std::string> paths;
    if (!source.valid()) {
        return paths;
    }

    cv::Mat rgb(source.height, source.width, CV_8UC3, source.data,
                static_cast<size_t>(source.stride));
    cv::Mat scaled;
    if (maxWidth > 0 && rgb.cols > maxWidth) {
        const double scale = static_cast<double>(maxWidth) / rgb.cols;
        cv::resize(rgb, scaled, cv::Size(), scale, scale, cv::INTER_AREA);
    } else {
        scaled = rgb.clone();
    }

    std::vector<domain::EffectMode> modes = {
        domain::EffectMode::Off,
        domain::EffectMode::Blur,
        domain::EffectMode::Beauty,
        domain::EffectMode::BlurBeauty,
    };
    for (auto mode : modes) {
        std::vector<uint8_t> buffer(
            static_cast<size_t>(scaled.cols) * scaled.rows * 3);
        cv::Mat copy(scaled.rows, scaled.cols, CV_8UC3, buffer.data());
        scaled.copyTo(copy);

        domain::ImageView view;
        view.data = buffer.data();
        view.width = scaled.cols;
        view.height = scaled.rows;
        view.stride = scaled.cols * 3;

        auto settings = baseSettings;
        settings.mode = mode;
        processor_.process(view, settings, /*temporal=*/false);

        const std::string name = "preview-" + domain::toString(mode) + ".png";
        const std::string path = outputDir_ + "/" + name;
        // OpenCV writes BGR; the pipeline and EffectProcessor work in RGB.
        cv::Mat bgr;
        cv::cvtColor(copy, bgr, cv::COLOR_RGB2BGR);
        if (cv::imwrite(path, bgr)) {
            paths[domain::toString(mode)] = path;
        }
    }
    return paths;
}

}  // namespace npucam::preview
