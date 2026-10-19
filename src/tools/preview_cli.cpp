#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "npucam/application/effect_processor.hpp"
#include "npucam/imaging/cv_effect_renderer.hpp"
#include "npucam/openvino/openvino_segmenter.hpp"
#include "npucam/openvino/omz_face_analyzer.hpp"
#include "npucam/preview/preview_generator.hpp"

namespace fs = std::filesystem;
using namespace npucam;

namespace {

void usage() {
    std::cerr
        << "usage: npucam-preview --image FILE [--out FILE] [--mode MODE]\n"
        << "                     [--previews DIR] [--model-dir DIR]\n"
        << "                     [--device NPU] [--gamma 0.85]\n"
        << "                     [--blur 0.6] [--smooth 0.5]\n"
        << "       npucam-preview --grab [same options]\n"
        << "MODE: off | blur | beauty | blur-beauty\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string imagePath;
    std::string outPath = "/tmp/npucam-preview.jpg";
    std::string previewsDir;
    std::string modelDir =
        std::getenv("NPUCAM_MODELS") ? std::getenv("NPUCAM_MODELS")
                                     : std::string(std::getenv("HOME")) +
                                           "/npu-models";
    std::string device = "NPU";
    std::string modeText = "blur-beauty";
    std::string segmenterName = "selfie";
    bool grab = false;
    domain::EffectSettings settings;
    settings.gamma = 0.85f;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                usage();
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "--image") {
            imagePath = next();
        } else if (arg == "--grab") {
            grab = true;
        } else if (arg == "--out") {
            outPath = next();
        } else if (arg == "--previews") {
            previewsDir = next();
        } else if (arg == "--model-dir") {
            modelDir = next();
        } else if (arg == "--device") {
            device = next();
        } else if (arg == "--mode") {
            modeText = next();
        } else if (arg == "--segmenter") {
            segmenterName = next();
        } else if (arg == "--gamma") {
            settings.gamma = std::stof(next());
        } else if (arg == "--blur") {
            settings.blurStrength = std::stof(next());
        } else if (arg == "--smooth") {
            settings.smoothStrength = std::stof(next());
        } else if (arg == "--help" || arg == "-h") {
            usage();
            return 0;
        } else {
            usage();
            return 2;
        }
    }

    if (!domain::modeFromString(modeText, settings.mode)) {
        std::cerr << "unknown mode: " << modeText << "\n";
        return 2;
    }

    if (grab) {
        imagePath = "/tmp/npucam-grab.jpg";
        const std::string cmd =
            "gst-launch-1.0 -q v4l2src device=/dev/video60 num-buffers=8 "
            "! videoconvert ! jpegenc ! filesink location=" +
            imagePath + " >/dev/null 2>&1";
        if (std::system(cmd.c_str()) != 0) {
            std::cerr << "failed to grab a frame from /dev/video60\n";
            return 1;
        }
    }
    if (imagePath.empty()) {
        usage();
        return 2;
    }

    cv::Mat image = cv::imread(imagePath, cv::IMREAD_COLOR);
    if (image.empty()) {
        std::cerr << "cannot read " << imagePath << "\n";
        return 1;
    }
    cv::cvtColor(image, image, cv::COLOR_BGR2RGB);

    try {
        adapters::SegmenterConfig segConfig;
        if (segmenterName == "modnet") {
            segConfig.modelPath = modelDir + "/modnet_fp16.onnx";
            segConfig.width = 384;
            segConfig.height = 224;
            segConfig.mean = 127.5f;
            segConfig.scale = 1.0f / 127.5f;
        } else {
            segConfig.modelPath = modelDir + "/selfie_mp_fp16.onnx";
            segConfig.width = 256;
            segConfig.height = 256;
            segConfig.mean = 0.0f;
            segConfig.scale = 1.0f / 255.0f;
        }
        adapters::OpenVinoSegmenter segmenter(segConfig, device);
        adapters::OmzFaceAnalyzer analyzer(modelDir, device);
        adapters::CvEffectRenderer renderer;
        application::EffectProcessor processor(segmenter, analyzer, renderer);

        domain::ImageView view;
        view.data = image.data;
        view.width = image.cols;
        view.height = image.rows;
        view.stride = static_cast<int>(image.step);

        if (!previewsDir.empty()) {
            fs::create_directories(previewsDir);
            preview::PreviewGenerator generator(processor, previewsDir);
            const auto paths = generator.generate(view, settings);
            for (const auto& [mode, path] : paths) {
                std::cout << mode << " " << path << "\n";
            }
            return paths.empty() ? 1 : 0;
        }

        const auto start = std::chrono::steady_clock::now();
        const bool ok = processor.process(view, settings);
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - start)
                                 .count();
        if (!ok) {
            std::cerr << "processing failed: " << processor.lastError() << "\n";
            return 1;
        }
        cv::cvtColor(image, image, cv::COLOR_RGB2BGR);
        cv::imwrite(outPath, image);
        std::cout << "saved " << outPath << " mode="
                  << domain::toString(settings.mode) << " in " << elapsed
                  << " ms; segmentation on " << segmenter.activeDevice()
                  << ", faces on " << analyzer.activeDevice() << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
