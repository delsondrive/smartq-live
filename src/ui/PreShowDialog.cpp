#include "ui/PreShowDialog.h"

#include <algorithm>
#include <cmath>

#include "app/AudioHost.h"
#include "app/PartFiles.h"
#include "app/Power.h"
#include "model/CloudFiles.h"
#include "ui/Helpers.h"
#include "ui/Theme.h"

namespace sqlive::ui {

namespace {

using Level = app::CheckItem::Level;

// The mockup's .dlg (popup, hairline border, 14 px corners), .dh (14 px by 18 px, an 18 px title, a 12 px hint),
// .checklist (4 px by 18 px; each line 8 px above and below, 10 px between the 20 px mark and 13 px words in mid, with a
// faint line under it), .addl (12 px semibold mint) and .df (12 px by 18 px, 10 px between its parts).
constexpr float kRadius = 14.0f;
constexpr int kPadX = 18, kHeaderHeight = 52, kListPadY = 4, kLinePadY = 8, kMarkWidth = 20, kMarkGap = 10;
constexpr int kFooterPadY = 12, kButtonHeight = 30, kFooterGap = 10;

juce::Font titleFont() { return theme::font(18.0f, true); }
juce::Font hintFont() { return theme::font(theme::sizeSmall); }
juce::Font bodyFont() { return theme::font(theme::sizeBody); }
juce::Font boldFont() { return theme::font(theme::sizeBody, true); }
juce::Font markFont() { return theme::font(theme::sizeBody, true); }
juce::Font linkFont() { return theme::font(theme::sizeSmall, true); }

const juce::String kTick = juce::String::fromUTF8("\xe2\x9c\x93");
const juce::String kCross = juce::String::fromUTF8("\xe2\x9c\x95");

juce::String markOf(Level level) { return level == Level::Ok ? kTick : level == Level::Warning ? juce::String("!") : kCross; }
juce::Colour colourOf(Level level) { return level == Level::Ok ? theme::mint : level == Level::Warning ? theme::caution : theme::danger; }

bool same(const std::vector<app::CheckItem>& a, const std::vector<app::CheckItem>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].level != b[i].level || a[i].id != b[i].id || a[i].title != b[i].title || a[i].detail != b[i].detail
            || a[i].fixLabel != b[i].fixLabel)
            return false;
    return true;
}

void fit(juce::Viewport& view, juce::Component& body, int idealHeight) {
    const bool scrolls = idealHeight > view.getHeight();
    const int w = std::max(0, view.getWidth() - (scrolls ? view.getScrollBarThickness() : 0));
    body.setSize(w, std::max(idealHeight, view.getHeight()));
}

}  // namespace

// ---- The lines -------------------------------------------------------------------------------------------------------

// The checklist itself: one line per item, its words wrapped to the width, the link found in the laid-out words so a
// click on it (and only on it) asks for the fix.
class PreShowDialog::Lines : public juce::Component {
public:
    std::function<void(const app::CheckItem&)> onFix;

    void setItems(const std::vector<app::CheckItem>& items) {
        items_ = items;
        layOut(getWidth());
        repaint();
    }

    int idealHeight(int w) const {
        int h = 0;
        for (const auto& item : items_) h += lineHeight(layoutFor(item, textWidth(w)));
        return h;
    }

    void resized() override { layOut(getWidth()); }

    void paint(juce::Graphics& g) override {
        int y = 0;
        for (size_t i = 0; i < items_.size(); ++i) {
            const auto& item = items_[i];
            const auto& line = lines_[i];
            const int h = lineHeight(line.layout);
            g.setColour(colourOf(item.level));
            g.setFont(markFont());
            g.drawText(markOf(item.level), juce::Rectangle<int>(0, y + kLinePadY, kMarkWidth, firstLineHeight(line.layout)),
                       juce::Justification::centred, false);
            line.layout.draw(g, juce::Rectangle<float>(static_cast<float>(kMarkWidth + kMarkGap), static_cast<float>(y + kLinePadY),
                                                       static_cast<float>(textWidth(getWidth())), line.layout.getHeight()));
            g.setColour(theme::rowLine);
            g.fillRect(0, y + h - 1, getWidth(), 1);
            y += h;
        }
    }

    void mouseMove(const juce::MouseEvent& e) override {
        setMouseCursor(linkAt(e.position) != nullptr ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }

    void mouseUp(const juce::MouseEvent& e) override {
        if (const auto* item = linkAt(e.position); item != nullptr && onFix) onFix(*item);
    }

private:
    struct Line {
        juce::TextLayout layout;
        juce::Rectangle<float> link;                              // in this component; empty when the item has no fix
    };

    static int textWidth(int w) { return std::max(1, w - kMarkWidth - kMarkGap); }

    static juce::TextLayout layoutFor(const app::CheckItem& item, int w) {
        juce::AttributedString s;
        s.setWordWrap(juce::AttributedString::byWord);
        s.append(item.title, boldFont(), theme::text);
        if (item.detail.isNotEmpty()) s.append(kDot + item.detail, bodyFont(), theme::mid);
        if (item.fixLabel.isNotEmpty()) {
            s.append(" ", bodyFont(), theme::mid);
            s.append(item.fixLabel, linkFont(), theme::mint);
        }
        juce::TextLayout layout;
        layout.createLayout(s, static_cast<float>(w));
        return layout;
    }

    static int firstLineHeight(const juce::TextLayout& layout) {
        if (layout.getNumLines() == 0) return static_cast<int>(std::ceil(bodyFont().getHeight()));
        return static_cast<int>(std::ceil(layout.getLine(0).getLineBoundsY().getEnd()));
    }

    static int lineHeight(const juce::TextLayout& layout) {
        const int text = std::max(static_cast<int>(std::ceil(layout.getHeight())), firstLineHeight(layout));
        return kLinePadY + text + kLinePadY + 1;
    }

    // The mint runs' area, from the laid-out words: the link may wrap, so it is the union of its pieces.
    static juce::Rectangle<float> linkArea(const juce::TextLayout& layout, juce::Point<float> origin) {
        juce::Rectangle<float> area;
        for (int i = 0; i < layout.getNumLines(); ++i) {
            const auto& line = layout.getLine(i);
            for (const auto* run : line.runs) {
                if (run->colour != theme::mint || run->glyphs.isEmpty()) continue;
                const auto x = run->getRunBoundsX() + line.lineOrigin.x;
                const auto y = line.getLineBoundsY();
                const juce::Rectangle<float> piece(x.getStart(), y.getStart(), x.getLength(), y.getLength());
                area = area.isEmpty() ? piece : area.getUnion(piece);
            }
        }
        return area.isEmpty() ? area : area.translated(origin.x, origin.y);
    }

    void layOut(int w) {
        lines_.clear();
        int y = 0;
        for (const auto& item : items_) {
            Line line;
            line.layout = layoutFor(item, textWidth(w));
            if (item.fixLabel.isNotEmpty())
                line.link = linkArea(line.layout, {static_cast<float>(kMarkWidth + kMarkGap), static_cast<float>(y + kLinePadY)});
            y += lineHeight(line.layout);
            lines_.push_back(std::move(line));
        }
    }

    const app::CheckItem* linkAt(juce::Point<float> p) const {
        for (size_t i = 0; i < lines_.size() && i < items_.size(); ++i)
            if (!lines_[i].link.isEmpty() && lines_[i].link.expanded(2.0f).contains(p)) return &items_[i];
        return nullptr;
    }

    std::vector<app::CheckItem> items_;
    std::vector<Line> lines_;
};

// ---- The dialog ------------------------------------------------------------------------------------------------------

PreShowDialog::PreShowDialog(app::Session& session, app::AudioHost* host)
    : session_(session), host_(host), lines_(std::make_unique<Lines>()) {
    setWantsKeyboardFocus(true);

    const auto* setlist = session_.model().setlist(session_.setlistId());
    const auto* rig = session_.model().rig(session_.rigId());
    juce::StringArray names;
    if (setlist != nullptr && setlist->name.isNotEmpty()) names.add(setlist->name);
    if (rig != nullptr && rig->name.isNotEmpty()) names.add(rig->name);
    names_ = names.joinIntoString(kDot);

    lines_->onFix = [this](const app::CheckItem& item) { fix(item); };
    view_.setViewedComponent(lines_.get(), false);
    view_.setScrollBarsShown(true, false);
    addAndMakeVisible(view_);

    back_.onClick = [this] { if (onBack) onBack(); };
    go_.onClick = [this] { go(); };
    addAndMakeVisible(back_);
    addAndMakeVisible(go_);

    recheck();
    startTimer(1000);
}

PreShowDialog::~PreShowDialog() { stopTimer(); }

void PreShowDialog::recheck() {
    app::CheckInputs in;
    if (host_ != nullptr && host_->isOpen()) {
        in.deviceOpen = true;
        in.deviceName = host_->deviceName();
        in.deviceRate = host_->sampleRate();
        in.deviceBuffer = host_->bufferSize();
        in.deviceChannels = host_->outputChannels();
    }
    in.power = app::powerState();
    auto items = app::runPreShowCheck(session_, in);
    go_.setEnabled(app::mayEnterShow(items));
    if (same(items, items_)) return;
    items_ = std::move(items);
    lines_->setItems(items_);
    if (auto* parent = getParentComponent()) parent->resized();   // a veil sizes this to idealHeight()
    resized();
}

bool PreShowDialog::canGo() const { return app::mayEnterShow(items_); }

void PreShowDialog::downloadNow() {
    const auto* setlist = session_.model().setlist(session_.setlistId());
    if (setlist == nullptr) return;
    juce::StringArray seen;
    for (const auto& entry : setlist->entries) {
        if (seen.contains(entry.songId)) continue;
        seen.add(entry.songId);
        for (const auto& problem : app::partFileProblems(session_, entry.songId))
            if (problem.inCloud) model::startDownload(problem.file);
    }
}

void PreShowDialog::fix(const app::CheckItem& item) {
    if (item.id == "files") downloadNow();
    recheck();
}

void PreShowDialog::go() {
    recheck();                                                    // the last word: a problem may have come since the last second
    if (canGo() && onGo) onGo();
}

int PreShowDialog::idealHeight() const {
    const int w = getWidth() > 0 ? getWidth() : width;
    const int footer = 2 * kFooterPadY + kButtonHeight;
    return kHeaderHeight + 2 * kListPadY + lines_->idealHeight(w - 2 * kPadX) + footer;
}

void PreShowDialog::resized() {
    auto area = getLocalBounds();
    header_ = area.removeFromTop(kHeaderHeight);
    footer_ = area.removeFromBottom(2 * kFooterPadY + kButtonHeight);
    auto foot = footer_.reduced(kPadX, kFooterPadY);
    go_.setBounds(foot.removeFromRight(go_.idealWidth()));
    foot.removeFromRight(kFooterGap);
    back_.setBounds(foot.removeFromRight(back_.idealWidth()));
    foot.removeFromRight(kFooterGap);
    legend_ = foot;
    view_.setBounds(area.reduced(kPadX, kListPadY));
    fit(view_, *lines_, lines_->idealHeight(view_.getWidth()));
    if (lines_->getHeight() > view_.getHeight())                  // a scroll bar takes some width: wrap to what is left
        fit(view_, *lines_, lines_->idealHeight(lines_->getWidth()));
}

void PreShowDialog::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(theme::popup);
    g.fillRoundedRectangle(bounds, kRadius);
    g.setColour(theme::hairline);
    g.drawRoundedRectangle(bounds.reduced(0.5f), kRadius, 1.0f);
    g.fillRect(1, header_.getBottom() - 1, getWidth() - 2, 1);
    g.fillRect(1, footer_.getY(), getWidth() - 2, 1);

    // The header: the title, and the setlist and rig on the right.
    auto head = header_.reduced(kPadX, 0);
    const juce::String title = "Ready for the show?";
    const int titleWidth = static_cast<int>(std::ceil(widthOf(titleFont(), title)));
    g.setColour(theme::text);
    g.setFont(titleFont());
    g.drawText(title, head.removeFromLeft(titleWidth), juce::Justification::centredLeft, false);
    head.removeFromLeft(12);
    g.setColour(theme::dim);
    g.setFont(hintFont());
    g.drawFittedText(names_, head, juce::Justification::centredRight, 1, 1.0f);

    // The legend: "✕ must be fixed · ! is your call", the marks in their colours.
    float x = static_cast<float>(legend_.getX());
    const auto legendY = legend_.toFloat();
    const auto piece = [&](const juce::String& text, juce::Colour colour) {
        const float w = widthOf(hintFont(), text);
        if (x + w > legendY.getRight()) return;
        g.setColour(colour);
        g.setFont(hintFont());
        g.drawText(text, juce::Rectangle<float>(x, legendY.getY(), w + 1.0f, legendY.getHeight()), juce::Justification::centredLeft, false);
        x += w;
    };
    piece(kCross, theme::danger);
    piece(" must be fixed" + kDot, theme::dim);
    piece("!", theme::caution);
    piece(" is your call", theme::dim);
}

bool PreShowDialog::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey) {
        if (onBack) onBack();
        return true;
    }
    return false;
}

}  // namespace sqlive::ui
