#include "ui/QrCode.h"

#include <exception>
#include <optional>

#include "qrcodegen.hpp"

namespace sqlive::ui {

namespace {
constexpr int kBorderModules = 4;
}

juce::Image makeQrImage(const juce::String& text, int pixelSize) {
    std::optional<qrcodegen::QrCode> code;
    try {
        code.emplace(qrcodegen::QrCode::encodeText(text.toRawUTF8(), qrcodegen::QrCode::Ecc::MEDIUM));
    } catch (const std::exception&) {                             // qrcodegen::data_too_long: more than a code can hold
        return {};
    }
    const int modules = code->getSize();
    const int scale = pixelSize / (modules + 2 * kBorderModules);
    if (scale < 1) return {};

    juce::Image image(juce::Image::RGB, pixelSize, pixelSize, false);
    juce::Graphics g(image);
    g.fillAll(juce::Colours::white);
    g.setColour(juce::Colours::black);
    const int origin = (pixelSize - modules * scale) / 2;         // the code centred, its border included in what is left
    for (int y = 0; y < modules; ++y)
        for (int x = 0; x < modules; ++x)
            if (code->getModule(x, y)) g.fillRect(origin + x * scale, origin + y * scale, scale, scale);
    return image;
}

}  // namespace sqlive::ui
