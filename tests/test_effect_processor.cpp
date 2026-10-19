#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

#include "npucam/application/effect_processor.hpp"
#include "npucam/domain/ports.hpp"

using namespace npucam;

namespace {

struct FakeSegmenter : domain::IPortraitSegmenter {
    int calls = 0;
    bool ok = true;
    std::vector<float> values = {1.0f, 0.0f};
    bool segment(const domain::ImageView&, domain::PortraitMask& mask) override {
        ++calls;
        mask.width = 2;
        mask.height = 1;
        mask.values = values;
        return ok;
    }
};

struct FakeAnalyzer : domain::IFaceAnalyzer {
    int calls = 0;
    std::vector<domain::Face> analyze(const domain::ImageView&) override {
        ++calls;
        domain::Face face;
        face.box = {0.1f, 0.1f, 0.5f, 0.5f};
        face.confidence = 0.9f;
        face.landmarks = {{0.2f, 0.2f}};
        return {face};
    }
};

struct FakeRenderer : domain::IEffectRenderer {
    int calls = 0;
    const domain::PortraitMask* mask = nullptr;
    std::vector<float> maskValues;
    std::vector<domain::Face> faces;
    domain::EffectSettings settings;
    void render(domain::ImageView&, const domain::EffectSettings& s,
                const domain::PortraitMask* m,
                const std::vector<domain::Face>& f) override {
        ++calls;
        settings = s;
        mask = m;
        maskValues = m ? m->values : std::vector<float>{};
        faces = f;
    }
};

domain::ImageView makeFrame(std::vector<uint8_t>& pixels) {
    domain::ImageView frame;
    frame.data = pixels.data();
    frame.width = 2;
    frame.height = 1;
    frame.stride = 6;
    return frame;
}

}  // namespace

int main() {
    std::vector<uint8_t> pixels(6, 128);
    FakeSegmenter segmenter;
    FakeAnalyzer analyzer;
    FakeRenderer renderer;
    application::EffectProcessor processor(segmenter, analyzer, renderer);

    // Off + gamma 1 is a no-op.
    auto frame = makeFrame(pixels);
    domain::EffectSettings s;
    assert(processor.process(frame, s));
    assert(segmenter.calls == 0 && analyzer.calls == 0 && renderer.calls == 0);

    // Blur only.
    s.mode = domain::EffectMode::Blur;
    assert(processor.process(frame, s));
    assert(segmenter.calls == 1 && analyzer.calls == 0 && renderer.calls == 1);
    assert(renderer.mask != nullptr && renderer.faces.empty());

    // Beauty only.
    s.mode = domain::EffectMode::Beauty;
    assert(processor.process(frame, s));
    assert(segmenter.calls == 1 && analyzer.calls == 1 && renderer.calls == 2);
    assert(renderer.mask == nullptr && renderer.faces.size() == 1);

    // Both.
    s.mode = domain::EffectMode::BlurBeauty;
    assert(processor.process(frame, s));
    assert(segmenter.calls == 2 && analyzer.calls == 2 && renderer.calls == 3);
    assert(renderer.mask != nullptr && renderer.faces.size() == 1);

    // Off with gamma still renders (tone mapping).
    s = {};
    s.gamma = 0.85f;
    assert(processor.process(frame, s));
    assert(renderer.calls == 4);

    // A failed segmentation must not fail the frame.
    segmenter.ok = false;
    s.mode = domain::EffectMode::Blur;
    assert(processor.process(frame, s));
    assert(renderer.mask == nullptr);
    assert(!processor.lastError().empty());

    // Temporal mask smoothing: a large change is blended (alpha 0.65),
    // still previews bypass the history.
    s.mode = domain::EffectMode::Blur;
    segmenter.values = {1.0f, 0.0f};
    assert(processor.process(frame, s, true));
    assert(renderer.maskValues.size() == 2);
    assert(renderer.maskValues[0] == 1.0f);
    segmenter.values = {0.0f, 1.0f};
    assert(processor.process(frame, s, true));
    assert(renderer.maskValues[0] > 0.2f && renderer.maskValues[0] < 0.5f);
    assert(renderer.maskValues[1] > 0.5f && renderer.maskValues[1] < 0.8f);
    segmenter.values = {1.0f, 0.0f};
    assert(processor.process(frame, s, false));
    assert(renderer.maskValues[0] == 1.0f);

    // Invalid frame is rejected.
    domain::ImageView invalid;
    assert(!processor.process(invalid, s));

    assert(domain::toString(domain::EffectMode::BlurBeauty) == "blur-beauty");
    domain::EffectMode parsed;
    assert(domain::modeFromString("both", parsed));
    assert(parsed == domain::EffectMode::BlurBeauty);
    assert(!domain::modeFromString("nope", parsed));

    std::cout << "npucam core tests: OK\n";
    return 0;
}
