#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace npucam::domain {

enum class EffectMode : int {
    Off = 0,
    Blur = 1,
    Beauty = 2,
    BlurBeauty = 3,
};

inline std::string toString(EffectMode mode) {
    switch (mode) {
        case EffectMode::Off: return "off";
        case EffectMode::Blur: return "blur";
        case EffectMode::Beauty: return "beauty";
        case EffectMode::BlurBeauty: return "blur-beauty";
    }
    return "off";
}

inline bool modeFromString(const std::string& text, EffectMode& mode) {
    if (text == "off") { mode = EffectMode::Off; return true; }
    if (text == "blur") { mode = EffectMode::Blur; return true; }
    if (text == "beauty") { mode = EffectMode::Beauty; return true; }
    if (text == "blur-beauty" || text == "both") {
        mode = EffectMode::BlurBeauty;
        return true;
    }
    return false;
}

inline bool hasBlur(EffectMode mode) {
    return mode == EffectMode::Blur || mode == EffectMode::BlurBeauty;
}

inline bool hasBeauty(EffectMode mode) {
    return mode == EffectMode::Beauty || mode == EffectMode::BlurBeauty;
}

// Non-owning view of an RGB24 image. `stride` is in bytes and may be larger
// than width * 3 (GStreamer pads do this).
struct ImageView {
    uint8_t* data = nullptr;
    int width = 0;
    int height = 0;
    int stride = 0;

    bool valid() const {
        return data != nullptr && width > 0 && height > 0 &&
               stride >= width * 3;
    }
};

// Soft portrait mask, 0 = background, 1 = foreground.
struct PortraitMask {
    std::vector<float> values;
    int width = 0;
    int height = 0;
};

// All coordinates are normalized to the full frame (0..1).
struct RectF {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

struct PointF {
    float x = 0.0f;
    float y = 0.0f;
};

struct Face {
    RectF box;
    float confidence = 0.0f;
    // Optional 35-point OMZ landmark set, normalized to the full frame.
    std::vector<PointF> landmarks;
};

struct EffectSettings {
    EffectMode mode = EffectMode::Off;
    float gamma = 1.0f;
    float blurStrength = 0.6f;
    float smoothStrength = 0.7f;
};

}  // namespace npucam::domain
