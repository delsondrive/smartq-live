#include "ui/fx/HistoryPage.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "ui/fx/ChainHost.h"
#include "ui/fx/PageLook.h"
#include "ui/fx/Theme.h"

namespace smartq {

namespace {

constexpr float kGrabPixels = 8.0f;
constexpr float kLineInset = 2.0f;  // a line at the foot of the scale is drawn this far up, where it can be seen and taken

// The strip's readings: the value's line and the caption's under it.
constexpr float kValueTop = 12.0f, kValueHeight = 29.0f, kCaptionHeight = 14.0f;

}  // namespace

// ---- the readings ----------------------------------------------------------------------------------------

ReadingStrip::ReadingStrip() {
    setOpaque(true);
    setInterceptsMouseClicks(false, false);
    setName("readings");
}

int ReadingStrip::add(const juce::String& caption, juce::Colour colour, float valueSize, bool semibold, int width) {
    items_.push_back({caption, {}, colour, valueSize, semibold, width});
    return static_cast<int>(items_.size()) - 1;
}

void ReadingStrip::set(int index, const juce::String& value) {
    auto& item = items_.at(static_cast<size_t>(index));
    if (item.value == value) return;
    item.value = value;
    ask(index);
}

void ReadingStrip::setColour(int index, juce::Colour colour) {
    auto& item = items_.at(static_cast<size_t>(index));
    if (item.colour == colour) return;
    item.colour = colour;
    ask(index);
}

void ReadingStrip::setDimmed(bool dimmed) {
    if (dimmed == dimmed_) return;
    dimmed_ = dimmed;
    ++repaints_;
    repaint();
}

void ReadingStrip::ask(int index) {
    ++repaints_;
    repaint(area(index).getSmallestIntegerContainer());
}

juce::Rectangle<float> ReadingStrip::area(int index) const {
    int x = kLeft;
    for (int i = 0; i < index; ++i) x += items_[static_cast<size_t>(i)].width + kGap;
    return {static_cast<float>(x), kValueTop, static_cast<float>(items_.at(static_cast<size_t>(index)).width), kValueHeight + kCaptionHeight};
}

int ReadingStrip::right() const { return items_.empty() ? kLeft : juce::roundToInt(area(static_cast<int>(items_.size()) - 1).getRight()); }

void ReadingStrip::paint(juce::Graphics& g) {
    g.fillAll(theme::pane);
    const float dim = dimmed_ ? kBypassedOpacity : 1.0f;
    for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
        const auto& item = items_[static_cast<size_t>(i)];
        auto at = area(i);
        g.setColour(item.colour.withMultipliedAlpha(dim));
        g.setFont(theme::font(item.size, item.semibold));
        g.drawText(item.value, at.removeFromTop(kValueHeight), juce::Justification::centredLeft, false);
        g.setColour(theme::textDim);
        g.setFont(theme::font(theme::kSizeCaption + 0.5f));
        g.drawText(item.caption, at, juce::Justification::centredLeft, false);
    }
}

// ---- the history -----------------------------------------------------------------------------------------

HistoryPane::HistoryPane(juce::RangedAudioParameter* line, const juce::String& lineName, double topDb, double bottomDb, juce::Colour accent)
    : accent_(accent), line_(line), lineName_(lineName), topDb_(topDb), bottomDb_(bottomDb) {
    setOpaque(true);
    setName("history");
    if (line == nullptr) setInterceptsMouseClicks(false, false);
}

HistoryPane::~HistoryPane() { endDrag(); }

void HistoryPane::setHistory(const ModuleHistory* history) {
    history_ = history;
    seenVersion_ = history != nullptr ? history->version() : 0;
    rebuild();
}

void HistoryPane::follow() {
    if (history_ == nullptr || history_->version() == seenVersion_) return;
    seenVersion_ = history_->version();
    rebuild();
    ask();
}

void HistoryPane::setBypassed(bool bypassed) {
    if (bypassed == bypassed_) return;
    bypassed_ = bypassed;
    redrawKept();
}

void HistoryPane::setLineDb(double db) {
    if (db == lineDb_) return;
    lineDb_ = db;
    redrawKept();
}

float HistoryPane::yOfLevel(double db) const noexcept {
    const auto height = static_cast<float>(getHeight()) - kTopInset;
    return kTopInset + height * static_cast<float>(juce::jlimit(0.0, 1.0, (topDb_ - db) / (topDb_ - bottomDb_)));
}

double HistoryPane::levelAtY(float y) const noexcept {
    const auto height = std::max(1.0f, static_cast<float>(getHeight()) - kTopInset);
    return topDb_ - static_cast<double>((y - kTopInset) / height) * (topDb_ - bottomDb_);
}

float HistoryPane::xOfColumn(int index) const noexcept {
    return static_cast<float>(getWidth()) * static_cast<float>(index) / static_cast<float>(ModuleHistory::kColumns - 1);
}

void HistoryPane::ask() {
    ++repaints_;
    repaint();
}

void HistoryPane::redrawKept() {
    picture_.invalidate();
    ask();
}

void HistoryPane::paint(juce::Graphics& g) {
    g.fillAll(theme::pane);
    paintMoving(g, bypassed_ ? kBypassedOpacity : 1.0f);
    picture_.draw(g, getLocalBounds(), [this](juce::Graphics& kept) { paintKept(kept); });
}

void HistoryPane::resized() {
    picture_.invalidate();
    rebuild();
}

void HistoryPane::dashed(juce::Graphics& g, float y, float thickness, juce::Colour colour) const {
    juce::Path line;
    line.startNewSubPath(0.0f, y);
    line.lineTo(static_cast<float>(getWidth()), y);
    const std::array<float, 2> dashes{4.0f, 4.0f};
    juce::Path stroke;
    juce::PathStrokeType(thickness).createDashedStroke(stroke, line, dashes.data(), static_cast<int>(dashes.size()));
    g.setColour(colour);
    g.fillPath(stroke);
}

void HistoryPane::paintKeptGrid(juce::Graphics& g) const {
    const auto whole = getLocalBounds().toFloat();
    g.setColour(juce::Colours::white.withAlpha(0.055f));
    for (double db = std::ceil(bottomDb_ / 12.0) * 12.0; db < topDb_; db += 12.0) {
        const float y = yOfLevel(db);
        if (y > kTopInset + 1.0f && y < whole.getBottom() - 1.0f) g.fillRect(0.0f, y - 0.5f, whole.getWidth(), 1.0f);
    }
}

void HistoryPane::paintKept(juce::Graphics& g) const {
    ++keptPaints_;
    const float dim = bypassed_ ? kBypassedOpacity : 1.0f;
    const auto whole = getLocalBounds().toFloat();

    paintKeptGrid(g);

    const auto font = theme::font(theme::kSizeCaption);
    g.setFont(font);
    g.setColour(theme::textDim);
    const juce::String words = "Last 6 seconds";
    auto corner = cornerRow();
    g.drawText(words, corner, juce::Justification::centredRight, false);
    corner.removeFromRight(juce::GlyphArrangement::getStringWidth(font, words) + 18.0f);
    paintCornerNote(g, corner, font);

    paintKeptLines(g, dim);

    if (line_ != nullptr) paintLine(g, lineY(), hot_, dim);

    if (bypassed_) {
        const juce::String text = "Bypassed";
        const auto noticeFont = theme::font(theme::kSizeBody, true);
        const auto box = juce::Rectangle<float>(juce::GlyphArrangement::getStringWidth(noticeFont, text) + 28.0f, 28.0f).withCentre({whole.getCentreX(), 40.0f});
        g.setColour(theme::caution);
        g.fillRoundedRectangle(box, 14.0f);
        g.setColour(theme::backgroundTop);
        g.setFont(noticeFont);
        g.drawText(text, box, juce::Justification::centred, false);
    }
}

// The line, on top, with its name over it at the left, or under it where there is no room above.
void HistoryPane::paintLine(juce::Graphics& g, float y, bool hot, float dim) const {
    const auto colour = accent_.withAlpha((hot ? 0.95f : 0.6f) * dim);
    if (dashedLine()) {
        dashed(g, y, hot ? 1.5f : 1.0f, colour);
    } else {
        g.setColour(colour);
        g.fillRect(0.0f, y - (hot ? 1.0f : 0.75f), static_cast<float>(getWidth()), hot ? 2.0f : 1.5f);
    }
    const bool nameAbove = y - 18.0f >= 0.0f;
    g.setColour(theme::textDim);
    g.drawText(lineName_, juce::Rectangle<float>(14.0f, nameAbove ? y - 17.0f : y + 3.0f, 120.0f, 14.0f), juce::Justification::centredLeft, false);
}

float HistoryPane::lowestLineY() const noexcept { return std::max(kTopInset, static_cast<float>(getHeight()) - kLineInset); }
float HistoryPane::lineY() const noexcept { return juce::jlimit(kTopInset, lowestLineY(), yOfLevel(lineDb_)); }

// The row "Last 6 seconds" and the type's note are written in, at the foot of the pane.
juce::Rectangle<float> HistoryPane::cornerRow() const { return getLocalBounds().toFloat().withTrimmedRight(14.0f).withTrimmedBottom(9.0f).removeFromBottom(14.0f); }

bool HistoryPane::isOnLine(juce::Point<float> at) const noexcept {
    if (line_ == nullptr) return false;
    if (lineHandle().expanded(2.0f).contains(at)) return true;
    return at.x >= 0.0f && at.x < static_cast<float>(getWidth()) && std::abs(at.y - lineY()) <= kGrabPixels;
}

void HistoryPane::setHot(bool hot) {
    setMouseCursor(hot ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
    if (hot == hot_) return;
    hot_ = hot;
    redrawKept();
}

void HistoryPane::mouseMove(const juce::MouseEvent& e) { setHot(isOnLine(e.position)); }
void HistoryPane::mouseExit(const juce::MouseEvent&) {
    if (!grabbed_) setHot(false);
}

void HistoryPane::mouseDown(const juce::MouseEvent& e) {
    if (!e.mods.isLeftButtonDown() || !isOnLine(e.position)) return;
    grabbed_ = true;
    grabOffset_ = lineY() - e.position.y;
    setHot(true);
}

void HistoryPane::mouseDrag(const juce::MouseEvent& e) {
    if (!grabbed_ || line_ == nullptr) return;

    const float y = e.position.y + grabOffset_;
    const auto db = static_cast<float>(draggedDb(y, levelAtY(y)));
    const float wanted = line_->convertTo0to1(line_->getNormalisableRange().snapToLegalValue(db));
    if (wanted == line_->getValue()) return;

    // Opened at its first real change, so that a click that doesn't move tells the host nothing.
    if (!gestureOpen_) {
        gestureOpen_ = true;
        line_->beginChangeGesture();
    }
    line_->setValueNotifyingHost(wanted);
}

void HistoryPane::mouseUp(const juce::MouseEvent& e) {
    endDrag();
    setHot(isOnLine(e.position));
}

void HistoryPane::endDrag() {
    grabbed_ = false;
    if (gestureOpen_ && line_ != nullptr) line_->endChangeGesture();
    gestureOpen_ = false;
}

// ---- the page --------------------------------------------------------------------------------------------

HistoryPage::HistoryPage(ChainHost& processor, int slot, std::unique_ptr<HistoryPane> history, std::initializer_list<const char*> fields)
    : HistoryPage(processor, slot, std::make_unique<ControlRow>(processor, slot, fields), std::move(history)) {}

HistoryPage::HistoryPage(ChainHost& processor, int slot, std::unique_ptr<HistoryPane> history)
    : HistoryPage(processor, slot, nullptr, std::move(history)) {}

// With a row of controls, the page is laid out as a strip of readings, the history and the row. Without one, the type does it.
HistoryPage::HistoryPage(ChainHost& processor, int slot, std::unique_ptr<ControlRow> row, std::unique_ptr<HistoryPane> history)
    : processor_(processor),
      slot_(slot),
      history_(std::move(history)),
      row_(std::move(row)),
      moduleBypass_(processor.apvts().getRawParameterValue(moduleParamId(slot, kBypassId))),
      pluginBypass_(processor.apvts().getRawParameterValue(kBypassId)) {
    setOpaque(true);
    if (row_ != nullptr) {
        const auto accent = theme::moduleAccent(slotInfo(slot).type);
        readings_.add("Turned down now", accent, 24.0f, true, 112);
        readings_.set(0, reduction_.text());
        addAndMakeVisible(readings_);
    }
    addAndMakeVisible(*history_);
    if (row_ != nullptr) addAndMakeVisible(*row_);
}

HistoryPage::~HistoryPage() = default;

void HistoryPage::setHistory(const ModuleHistory* history) {
    historySource_ = history;
    history_->setHistory(history);
}

juce::Component& HistoryPage::historyView() noexcept { return *history_; }
float HistoryPage::yOfLevel(double db) const noexcept { return history_->yOfLevel(db); }
unsigned HistoryPage::repaintsAsked() const noexcept { return history_->repaints() + readings_.repaints(); }
unsigned HistoryPage::historyRepaints() const noexcept { return history_->repaints(); }
unsigned HistoryPage::layerDraws() const noexcept { return history_->layerDraws(); }
bool HistoryPage::anyLayerLost() const { return history_->layerLost(); }

void HistoryPage::endGestures() {
    history_->endDrag();
    if (row_ != nullptr) row_->endGestures();
}

void HistoryPage::followBypass() {
    const bool bypassed = moduleBypass_->load() >= 0.5f || pluginBypass_->load() >= 0.5f;
    if (bypassed == bypassed_) return;
    bypassed_ = bypassed;
    history_->setBypassed(bypassed);
    bypassChanged(bypassed);
}

void HistoryPage::update(double dtSeconds) {
    followBypass();
    followParameters(dtSeconds);
    if (row_ != nullptr) row_->update(dtSeconds);

    if (historySource_ != nullptr) {
        history_->follow();
        if (reduction_.step(historySource_->reductionNowDb(), dtSeconds)) showReduction(reduction_.text());
    }
    followReadings(dtSeconds);
}

// Whatever it was left showing is out of date, and the next step says what's so now.
void HistoryPage::pageShown() { reduction_.restart(); }

void HistoryPage::paint(juce::Graphics& g) {
    // The strip's row is the pane's, beyond the readings too.
    g.setColour(theme::pane);
    g.fillRect(0, 0, getWidth(), history_->getY());

    // Starts in the colour the window's other pages end in.
    const int panelTop = row_->getY();
    g.setGradientFill(juce::ColourGradient(theme::backgroundBottom, 0.0f, static_cast<float>(panelTop), theme::panelBottom, 0.0f, static_cast<float>(getHeight()), false));
    g.fillRect(0, panelTop, getWidth(), getHeight() - panelTop);
    g.setColour(theme::hairline);
    g.fillRect(0, panelTop, getWidth(), 1);
}

void HistoryPage::resized() {
    auto area = getLocalBounds();
    const auto underRow = area.removeFromBottom(underRowHeight());
    row_->setBounds(area.removeFromBottom(row_->heightFor(area.getWidth())));
    const auto strip = area.removeFromTop(ReadingStrip::kHeight);
    readings_.setBounds(strip.withWidth(std::min(strip.getWidth(), readings_.right() + ReadingStrip::kGap / 2)));
    const auto underReadings = area.removeFromTop(underReadingsHeight());
    history_->setBounds(area);
    placeInReadings(strip);
    placeUnderReadings(underReadings);
    placeUnderRow(underRow);
}

}  // namespace smartq
