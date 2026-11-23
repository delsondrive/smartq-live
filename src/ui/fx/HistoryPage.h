#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <atomic>
#include <initializer_list>
#include <memory>
#include <vector>

#include "ui/fx/ControlRow.h"
#include "ui/fx/LayerImage.h"
#include "ui/fx/ModuleHistory.h"
#include "ui/fx/ModulePage.h"

namespace smartq {

class ChainHost;

// A row of readings across the top of a page, each a value over a caption, such as the big "-5.2 dB" over "Turned down now". Each
// is drawn again only when what it says has changed.
class ReadingStrip : public juce::Component {
public:
    ReadingStrip();

    // A reading, after those added before it, as wide as `width`. Its index.
    int add(const juce::String& caption, juce::Colour, float valueSize, bool semibold, int width);

    void set(int index, const juce::String& value);
    void setColour(int index, juce::Colour);
    const juce::String& value(int index) const { return items_.at(static_cast<size_t>(index)).value; }

    // Where a reading is written, in the strip, and where the readings end.
    juce::Rectangle<float> area(int index) const;
    int right() const;

    // Drawn faint, for a module that is bypassed.
    void setDimmed(bool);

    unsigned repaints() const noexcept { return repaints_; }

    void paint(juce::Graphics&) override;

    static constexpr int kHeight = 62, kLeft = 18, kGap = 28;

private:
    struct Item {
        juce::String caption, value;
        juce::Colour colour;
        float size = 0.0f;
        bool semibold = false;
        int width = 0;
    };

    void ask(int index);

    std::vector<Item> items_;
    bool dimmed_ = false;
    unsigned repaints_ = 0;
};

// The last six seconds of a module drawn across a pane, on a scale of dB with `topDb` at the top and `bottomDb` at the bottom, and one
// of the module's parameters in dB as a line across it, which can be dragged up and down as one host gesture. What seldom changes is
// kept as an image: the grid, the line and its name, and the corner's words. A type draws its history over that in its own way.
//
// `line` can be null, for a type whose parameter is dragged elsewhere: there is then no line, and the pane takes no mouse.
class HistoryPane : public juce::Component {
public:
    HistoryPane(juce::RangedAudioParameter* line, const juce::String& lineName, double topDb, double bottomDb, juce::Colour accent);
    ~HistoryPane() override;

    void setHistory(const ModuleHistory*);

    // A step of the display: draws again if the history has moved.
    void follow();

    void setBypassed(bool);

    // Where the line is, as the page reads its parameter.
    void setLineDb(double);

    float yOfLevel(double db) const noexcept;
    double levelAtY(float y) const noexcept;
    float lineY() const noexcept;  // held inside the pane, where it can be seen and taken
    float lowestLineY() const noexcept;  // the lowest it is drawn, a little over the foot

    // The row at the foot where "Last 6 seconds" and the type's note are written.
    juce::Rectangle<float> cornerRow() const;

    unsigned repaints() const noexcept { return repaints_; }
    unsigned layerDraws() const noexcept { return keptPaints_; }
    bool layerLost() const { return picture_.wasLost(); }
    void setImageForTest(const juce::Image& image) { picture_.setImageForTest(image); }

    // Closes the drag's gesture, if one is open.
    void endDrag();

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    static constexpr float kTopInset = 3.0f;

protected:
    // The type's paths from the history, at this size, and their drawing under what is kept.
    virtual void rebuild() = 0;
    virtual void paintMoving(juce::Graphics&, float dim) const = 0;

    // The grid in what is kept: a line every 12 dB, as quiet as it can be.
    virtual void paintKeptGrid(juce::Graphics&) const;

    // What the type says in the corner, beside "Last 6 seconds", in `beside`, which is the room to its left, with the corner's
    // font and colour already set.
    virtual void paintCornerNote(juce::Graphics&, juce::Rectangle<float> /*beside*/, const juce::Font&) const {}

    // The type's own lines in what is kept, under the dragged one.
    virtual void paintKeptLines(juce::Graphics&, float /*dim*/) const {}

    // How the line is drawn: dashed, as a threshold, or solid, as a ceiling.
    virtual bool dashedLine() const { return true; }

    // The line at y, on top of what is kept, lit while it is hot: by default as dashedLine says, with its name over it at the left. A
    // type that draws its own may give it a handle, which can be taken as the line can.
    virtual void paintLine(juce::Graphics&, float y, bool hot, float dim) const;
    virtual juce::Rectangle<float> lineHandle() const { return {}; }

    double lineDb() const noexcept { return lineDb_; }

    // What a drag to y, which reads as db on the scale, sets the line to, before the parameter's range holds it: db, unless the type
    // reads some places otherwise.
    virtual double draggedDb(float /*y*/, double db) const { return db; }

    void ask();
    void redrawKept();
    float xOfColumn(int index) const noexcept;
    void dashed(juce::Graphics&, float y, float thickness, juce::Colour) const;

    const juce::Colour accent_;
    const ModuleHistory* history_ = nullptr;
    bool bypassed_ = false;

private:
    bool isOnLine(juce::Point<float>) const noexcept;
    void setHot(bool);
    void paintKept(juce::Graphics&) const;

    juce::RangedAudioParameter* line_;
    const juce::String lineName_;
    const double topDb_, bottomDb_;
    double lineDb_ = 0.0;
    unsigned seenVersion_ = 0, repaints_ = 0;
    KeptPicture picture_{KeptPicture::Scales::Any};
    mutable unsigned keptPaints_ = 0;  // how many times what stays has been drawn, into its image or not
    bool hot_ = false, grabbed_ = false, gestureOpen_ = false;
    float grabOffset_ = 0.0f;
};

// The page of a module drawn as its history: its readings across the top, then the last six seconds with a line to drag, over its
// controls. Everything it shows is read in update, on the message thread, and it asks for a repaint only of what has moved. Destroying
// the page closes a drag's gesture, as closing the window does. A type adds its own readings and lines.
//
// A type that lays out its own readings and controls, as Dynamic's page does, makes the page without a row of controls, and has no
// use for the strip of readings: it overrides paint and resized, and says the big reading where it likes.
class HistoryPage : public ModulePage {
public:
    ~HistoryPage() override;

    int slot() const noexcept { return slot_; }

    // Where the page reads the last few seconds from, or null for none. Not owned. The column steps it.
    void setHistory(const ModuleHistory*);

    void update(double dtSeconds) override;
    bool anyLayerLost() const override;
    void pageShown() override;
    void endGestures() override;

    // The row of controls under the history. Only a page made with its fields has one: a page that places its own controls, as Dynamic's
    // does, has none, and must not be asked.
    ControlRow& controls() noexcept {
        jassert(row_ != nullptr);
        return *row_;
    }
    juce::Component& historyView() noexcept;

    // Where a level falls in the history, and the dragged line's place there.
    float yOfLevel(double db) const noexcept;

    // What the big reading says, such as "-5.2 dB", and where it is written, in the page.
    juce::String reductionText() const { return reduction_.text(); }
    virtual juce::Rectangle<float> readingArea() const { return readings_.area(0) + readings_.getPosition().toFloat(); }

    // Whether it's drawn as bypassed: this module's own bypass or the plug-in's is on.
    bool isBypassed() const noexcept { return bypassed_; }

    // How many repaints the page has asked for, how many of those were for the history, and how many times what stays has been
    // drawn, for tests.
    virtual unsigned repaintsAsked() const noexcept;
    unsigned historyRepaints() const noexcept;
    virtual unsigned layerDraws() const noexcept;

    void paint(juce::Graphics&) override;
    void resized() override;

protected:
    HistoryPage(ChainHost&, int slot, std::unique_ptr<HistoryPane>, std::initializer_list<const char*> fields);

    // For a page that places its own readings and controls.
    HistoryPage(ChainHost&, int slot, std::unique_ptr<HistoryPane>);

    // The type's settings: what it shows from them, at every step. The bypass is looked after by followBypass, before this.
    virtual void followParameters(double dtSeconds) = 0;

    // The type's readings, at every step after the history's.
    virtual void followReadings(double /*dtSeconds*/) {}

    // Places what the type has in the strip of readings, which has been laid out.
    virtual void placeInReadings(juce::Rectangle<int> /*strip*/) {}

    // For a type with a line under its readings: the strip grows by this much, and the type places the line in what it grew by, on
    // the strip's colour, once the strip has been laid out.
    virtual int underReadingsHeight() const { return 0; }
    virtual void placeUnderReadings(juce::Rectangle<int>) {}

    // For a type with notes under its row of controls: the room kept for them at the foot of the page, under the row, and their
    // places in it, once the row has been laid out.
    virtual int underRowHeight() const { return 0; }
    virtual void placeUnderRow(juce::Rectangle<int>) {}

    // Reads the bypass, this module's own or the plug-in's, and tells the history and bypassChanged when it has changed. At every
    // step, and where the type wants it known from the start.
    void followBypass();

    // What the type shows of the bypass besides the history: the strip dims by default.
    virtual void bypassChanged(bool bypassed) { readings_.setDimmed(bypassed); }

    // The big reading has changed to say `text`: the strip says it by default.
    virtual void showReduction(const juce::String& text) { readings_.set(0, text); }

    ChainHost& processor_;
    const int slot_;
    const ModuleHistory* historySource_ = nullptr;
    std::unique_ptr<HistoryPane> history_;
    std::unique_ptr<ControlRow> row_;  // null for a page that places its own controls
    ReadingStrip readings_;
    ReductionReading reduction_;
    bool bypassed_ = false;

private:
    HistoryPage(ChainHost&, int slot, std::unique_ptr<ControlRow>, std::unique_ptr<HistoryPane>);

    std::atomic<float>* moduleBypass_ = nullptr;
    std::atomic<float>* pluginBypass_ = nullptr;
};

}  // namespace smartq
