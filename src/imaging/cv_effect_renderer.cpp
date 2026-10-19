#include "npucam/imaging/cv_effect_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include <opencv2/core.hpp>
#include <opencv2/core/utility.hpp>
#include <opencv2/imgproc.hpp>

namespace npucam::adapters {
namespace {

// The element's gamma property follows the GStreamer gamma convention:
// output = input^(1/gamma), so gamma < 1 darkens the midtones.
void applyGamma(cv::Mat& rgb, float gamma) {
    const float exponent = 1.0f / std::clamp(gamma, 0.5f, 2.0f);
    float gain[256];
    gain[0] = 0.0f;
    for (int i = 1; i < 256; ++i) {
        const float normalized = i / 255.0f;
        gain[i] = 255.0f * std::pow(normalized, exponent) / i;
    }

    cv::parallel_for_(cv::Range(0, rgb.rows), [&](const cv::Range& range) {
        for (int y = range.start; y < range.end; ++y) {
            uint8_t* row = rgb.ptr<uint8_t>(y);
            for (int x = 0; x < rgb.cols; ++x) {
                uint8_t* p = row + 3 * x;
                const int luma = (77 * p[0] + 150 * p[1] + 29 * p[2]) >> 8;
                const float k = gain[luma];
                p[0] = cv::saturate_cast<uint8_t>(p[0] * k);
                p[1] = cv::saturate_cast<uint8_t>(p[1] * k);
                p[2] = cv::saturate_cast<uint8_t>(p[2] * k);
            }
        }
    });
}

void applyBackgroundBlur(cv::Mat& rgb, const domain::PortraitMask& mask,
                         float strength) {
    if (mask.values.empty() || mask.width <= 0 || mask.height <= 0) {
        return;
    }
    strength = std::clamp(strength, 0.0f, 1.0f);

    cv::Mat1f alpha(mask.height, mask.width);
    std::copy(mask.values.begin(), mask.values.end(), alpha.begin());
    if (alpha.size() != rgb.size()) {
        cv::resize(alpha, alpha, rgb.size(), 0, 0, cv::INTER_LINEAR);
    }
    cv::GaussianBlur(alpha, alpha, cv::Size(0, 0), 2.0);

    const int factor = 2 + static_cast<int>(strength * 4.0f);
    const int smallW = std::max(8, rgb.cols / factor);
    const int smallH = std::max(8, rgb.rows / factor);
    cv::Mat small;
    cv::resize(rgb, small, cv::Size(smallW, smallH), 0, 0, cv::INTER_AREA);
    cv::GaussianBlur(small, small, cv::Size(0, 0), 2.5 + 8.0 * strength);
    cv::Mat blurred;
    cv::resize(small, blurred, rgb.size(), 0, 0, cv::INTER_LINEAR);

    cv::parallel_for_(cv::Range(0, rgb.rows), [&](const cv::Range& range) {
        for (int y = range.start; y < range.end; ++y) {
            const float* m = alpha.ptr<float>(y);
            const uint8_t* fg = rgb.ptr<uint8_t>(y);
            const uint8_t* bg = blurred.ptr<uint8_t>(y);
            uint8_t* out = rgb.ptr<uint8_t>(y);
            for (int x = 0; x < rgb.cols; ++x) {
                const float a = std::clamp(m[x], 0.0f, 1.0f);
                for (int c = 0; c < 3; ++c) {
                    out[3 * x + c] = cv::saturate_cast<uint8_t>(
                        fg[3 * x + c] * a + bg[3 * x + c] * (1.0f - a));
                }
            }
        }
    });
}

cv::Point2f landmarkPixel(const domain::Face& face, const cv::Rect& rect,
                          int index, const cv::Mat& frame) {
    const auto& p = face.landmarks[index];
    return {p.x * frame.cols - rect.x, p.y * frame.rows - rect.y};
}

cv::Point2f meanLandmarks(const domain::Face& face, const cv::Rect& rect,
                          int first, int last, const cv::Mat& frame) {
    cv::Point2f sum(0.0f, 0.0f);
    for (int i = first; i <= last; ++i) {
        sum += landmarkPixel(face, rect, i, frame);
    }
    return sum * (1.0f / static_cast<float>(last - first + 1));
}

void smoothFace(cv::Mat& rgb, const domain::Face& face, float strength) {
    strength = std::clamp(strength, 0.0f, 1.0f);
    const int frameW = rgb.cols;
    const int frameH = rgb.rows;
    const float cx = (face.box.x + face.box.w * 0.5f) * frameW;
    const float cy = (face.box.y + face.box.h * 0.5f) * frameH;
    const float side = std::max(face.box.w * frameW, face.box.h * frameH) *
                       1.25f;
    int x0 = static_cast<int>(std::lround(cx - side * 0.5f));
    int y0 = static_cast<int>(std::lround(cy - side * 0.5f));
    int x1 = static_cast<int>(std::lround(cx + side * 0.5f));
    int y1 = static_cast<int>(std::lround(cy + side * 0.5f));
    x0 = std::clamp(x0, 0, frameW - 1);
    y0 = std::clamp(y0, 0, frameH - 1);
    x1 = std::clamp(x1, x0 + 1, frameW);
    y1 = std::clamp(y1, y0 + 1, frameH);
    if ((x1 - x0) < 32 || (y1 - y0) < 32) {
        return;
    }

    cv::Rect rect(x0, y0, x1 - x0, y1 - y0);
    cv::Mat crop = rgb(rect);
    const int w = rect.width;
    const int h = rect.height;

    // Frequency split: bilateral removes texture, a light glow softens the
    // remaining skin without touching eyes/mouth/hair.
    cv::Mat smoothed;
    cv::bilateralFilter(crop, smoothed, 9, 30.0 + 60.0 * strength, 9.0);
    cv::Mat glow;
    cv::GaussianBlur(crop, glow, cv::Size(0, 0), 2.0 + 3.0 * strength);
    cv::Mat soft;
    cv::addWeighted(smoothed, 0.6, glow, 0.4, 0.0, soft);

    // Skin region: a wide face ellipse clipped to the visible skin.
    // The luminance floor drops dark glasses frames/hair; the lower band and
    // the side inset drop the beard/sideburns; eyes and mouth are excluded
    // with the 35 landmarks. This gives a large, symmetric forehead/cheek
    // area for a visible (but not plastic) beauty effect.
    const int faceW = std::max(1, static_cast<int>(face.box.w * frameW));
    const int faceH = std::max(1, static_cast<int>(face.box.h * frameH));
    const cv::Point2f faceCenter(
        (face.box.x + face.box.w * 0.5f) * frameW - rect.x,
        (face.box.y + face.box.h * 0.5f) * frameH - rect.y);

    cv::Mat1f skin(h, w, 0.0f);
    cv::ellipse(skin, faceCenter,
                cv::Size(static_cast<int>(faceW * 0.58f),
                         static_cast<int>(faceH * 0.62f)),
                0, 0, 360, cv::Scalar(1.0f), cv::FILLED);

    cv::Mat gray;
    cv::cvtColor(crop, gray, cv::COLOR_RGB2GRAY);
    const int hairline = static_cast<int>(face.box.y * frameH - rect.y +
                                          faceH * 0.02f);
    cv::Point2f eyeMid(faceCenter.x, faceCenter.y - faceH * 0.02f);
    cv::Point2f mouth(faceCenter.x, faceCenter.y + faceH * 0.28f);
    if (face.landmarks.size() == 35) {
        const cv::Point2f rightEye =
            meanLandmarks(face, rect, 10, 14, rgb);
        const cv::Point2f leftEye =
            meanLandmarks(face, rect, 15, 19, rgb);
        eyeMid = (rightEye + leftEye) * 0.5f;
        mouth = meanLandmarks(face, rect, 25, 34, rgb);
    }
    for (int y = 0; y < h; ++y) {
        const uint8_t* grow = gray.ptr<uint8_t>(y);
        float* mrow = skin.ptr<float>(y);
        for (int x = 0; x < w; ++x) {
            bool keep = grow[x] > 80 && y >= hairline;
            if (keep && y > eyeMid.y &&
                std::fabs(x - faceCenter.x) > faceW * 0.40f) {
                keep = false;  // sideburns / beard at the cheeks
            }
            if (keep && y > mouth.y) {
                keep = false;  // chin / beard below the mouth
            }
            mrow[x] = keep ? mrow[x] : 0.0f;
        }
    }

    // Eyes and lips: explicit exclusion, even inside the skin region.
    const float eyeDist = face.landmarks.size() == 35
                              ? cv::norm(meanLandmarks(face, rect, 10, 14, rgb) -
                                         meanLandmarks(face, rect, 15, 19, rgb))
                              : faceW * 0.42f;
    cv::Point2f leftEye(eyeMid.x - eyeDist * 0.5f, eyeMid.y);
    cv::Point2f rightEye(eyeMid.x + eyeDist * 0.5f, eyeMid.y);
    if (face.landmarks.size() == 35) {
        leftEye = meanLandmarks(face, rect, 15, 19, rgb);
        rightEye = meanLandmarks(face, rect, 10, 14, rgb);
    }
    cv::circle(skin, leftEye, static_cast<int>(faceW * 0.09f),
               cv::Scalar(0.0f), cv::FILLED);
    cv::circle(skin, rightEye, static_cast<int>(faceW * 0.09f),
               cv::Scalar(0.0f), cv::FILLED);
    cv::ellipse(skin, mouth,
                cv::Size(static_cast<int>(faceW * 0.22f),
                         static_cast<int>(faceH * 0.10f)),
                0, 0, 360, cv::Scalar(0.0f), cv::FILLED);
    cv::GaussianBlur(skin, skin, cv::Size(0, 0), 2.0);

    const float alpha = std::clamp(strength, 0.0f, 0.9f);
    const float gain = 1.0f + 0.08f * strength;
    const float lift = 10.0f * strength;
    cv::parallel_for_(cv::Range(0, h), [&](const cv::Range& range) {
        for (int y = range.start; y < range.end; ++y) {
            const float* m = skin.ptr<float>(y);
            uint8_t* dst = crop.ptr<uint8_t>(y);
            const uint8_t* src = soft.ptr<uint8_t>(y);
            for (int x = 0; x < w; ++x) {
                const float a = alpha * std::clamp(m[x], 0.0f, 1.0f);
                for (int c = 0; c < 3; ++c) {
                    const float target = src[3 * x + c] * gain + lift;
                    dst[3 * x + c] = cv::saturate_cast<uint8_t>(
                        dst[3 * x + c] * (1.0f - a) + target * a);
                }
            }
        }
    });
}
}  // namespace

void CvEffectRenderer::render(domain::ImageView& frame,
                              const domain::EffectSettings& settings,
                              const domain::PortraitMask* mask,
                              const std::vector<domain::Face>& faces) {
    if (!frame.valid()) {
        return;
    }
    cv::Mat rgb(frame.height, frame.width, CV_8UC3, frame.data,
                static_cast<size_t>(frame.stride));

    if (std::fabs(settings.gamma - 1.0f) > 0.001f) {
        applyGamma(rgb, settings.gamma);
    }
    if (domain::hasBlur(settings.mode) && mask != nullptr) {
        applyBackgroundBlur(rgb, *mask, settings.blurStrength);
    }
    if (domain::hasBeauty(settings.mode) && !faces.empty()) {
        size_t count = std::min<size_t>(faces.size(), 3);
        for (size_t i = 0; i < count; ++i) {
            smoothFace(rgb, faces[i], settings.smoothStrength);
        }
    }
}

}  // namespace npucam::adapters
