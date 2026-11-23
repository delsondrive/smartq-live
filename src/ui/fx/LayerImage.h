#pragma once

#include <juce_graphics/juce_graphics.h>

#include <algorithm>
#include <cmath>

namespace smartq {

// An image that a component draws itself from vectors and lays over its other drawing. JUCE keeps a copy of a Direct2D image
// in main memory, read back from the graphics card after each time it is drawn, which costs far more than the drawing. That
// copy is only there for the card's being lost, and this kind of image can simply be drawn again.
inline juce::Image makeLayerImage(juce::Image::PixelFormat format, int width, int height, const juce::ImageType& type) {
    juce::Image layer(format, width, height, true, type);
    layer.setBackupEnabled(false);
    return layer;
}

// Whether a layer's picture went with the graphics card, such as when its driver restarts, so that it must be drawn again.
inline bool layerWasLost(const juce::Image& layer) {
    const auto data = layer.getPixelData();
    if (data == nullptr) return false;
    const auto* backup = data->getBackupExtensions();
    return backup != nullptr && backup->needsBackup() && !backup->canBackup();
}

// Part of a component's drawing that seldom changes, kept as an image at the screen's own pixel scale, over an area of the
// component. The picture is drawn into the same image for as long as its size and the scale stay.
//
// At a scale such as 1.25 a component can start part of the way across a pixel of the screen. Kept at any scale, the image is laid
// over its area there as the EQ graph's layers are, which the screen resamples by up to half a pixel. Kept at whole scales only, as
// the EQ's miniature is, it is drawn straight onto the screen each time instead.
class KeptPicture {
public:
    enum class Scales { WholeOnly, Any };

    explicit KeptPicture(Scales scales = Scales::WholeOnly) noexcept : scales_(scales) {}

    void invalidate() noexcept { dirty_ = true; }

    // `paint` takes a juce::Graphics, and draws in the component's own units, inside `area`.
    template <typename Paint>
    void draw(juce::Graphics& g, juce::Rectangle<int> area, Paint&& paint) {
        if (area.isEmpty()) return;
        const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
        const bool whole = scale == std::round(scale);
        if (!whole && scales_ == Scales::WholeOnly) {
            paint(g);
            return;
        }

        const int pixelWidth = std::max(1, juce::roundToInt(static_cast<float>(area.getWidth()) * scale));
        const int pixelHeight = std::max(1, juce::roundToInt(static_cast<float>(area.getHeight()) * scale));
        const bool newImage = !image_.isValid() || image_.getWidth() != pixelWidth || image_.getHeight() != pixelHeight || layerWasLost(image_);
        if (newImage || dirty_ || scale != scale_) {
            dirty_ = false;
            scale_ = scale;
            ++draws_;
            if (newImage) {
                ++makes_;
                image_ = makeLayerImage(juce::Image::ARGB, pixelWidth, pixelHeight, *g.getInternalContext().getPreferredImageTypeForTemporaryImages());
            }

            juce::Graphics picture(image_);
            if (!newImage) {
                picture.getInternalContext().setFill(juce::Colours::transparentBlack);
                picture.getInternalContext().fillRect(image_.getBounds(), true);
            }
            picture.addTransform(juce::AffineTransform::translation(static_cast<float>(-area.getX()), static_cast<float>(-area.getY()))
                                     .scaled(static_cast<float>(pixelWidth) / static_cast<float>(area.getWidth()),
                                             static_cast<float>(pixelHeight) / static_cast<float>(area.getHeight())));
            paint(picture);
        }

        g.setOpacity(1.0f);
        if (whole) {
            g.drawImageTransformed(image_, juce::AffineTransform::scale(1.0f / scale).translated(static_cast<float>(area.getX()), static_cast<float>(area.getY())));
        } else {
            // Graphics can't say what quality it had, so the state is kept, and what is drawn after is resampled as it was before.
            juce::Graphics::ScopedSaveState quality(g);
            g.setImageResamplingQuality(juce::Graphics::lowResamplingQuality);
            g.drawImage(image_, area.toFloat());
        }
    }

    bool wasLost() const { return layerWasLost(image_); }
    int imageWidth() const noexcept { return image_.getWidth(); }
    int imageHeight() const noexcept { return image_.getHeight(); }

    unsigned draws() const noexcept { return draws_; }  // how many times the picture has been drawn into an image
    unsigned makes() const noexcept { return makes_; }  // how many images it has taken

    // Puts another image in the place of this one, for tests.
    void setImageForTest(const juce::Image& image) { image_ = image; }

private:
    Scales scales_;
    juce::Image image_;
    float scale_ = 0.0f;
    bool dirty_ = true;
    unsigned draws_ = 0, makes_ = 0;
};

}  // namespace smartq
