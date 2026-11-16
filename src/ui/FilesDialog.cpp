#include "ui/FilesDialog.h"

#include <algorithm>
#include <cmath>

#include "app/FolderWatch.h"
#include "ui/Theme.h"

namespace sqlive::ui {

namespace {

// The mockup's .dlg (popup, hairline border, 14 px corners), .dh (an 18 px title in a 52 px header), .df (12 px by 18 px,
// 10 px between its parts), and its list lines (8 px above and below 13 px words in mid, a faint line under each).
constexpr float kRadius = 14.0f;
constexpr int kPadX = 18, kHeaderHeight = 52, kListPadY = 4, kLinePadY = 8;
constexpr int kFooterPadY = 12, kButtonHeight = 30, kFooterGap = 10;
// A conflict: 10 px above and below; its title (20 px), 6 px, then two columns 12 px apart, each a file name (18 px), its
// time (16 px), 8 px, and "Keep this one".
constexpr int kRowPadY = 10, kTitleHeight = 20, kTitleGap = 6, kColumnGap = 12, kNameHeight = 18, kTimeHeight = 16, kButtonGap = 8;
constexpr int kConflictHeight = kRowPadY + kTitleHeight + kTitleGap + kNameHeight + kTimeHeight + kButtonGap + kButtonHeight + kRowPadY + 1;

// The mockup's amber banner: caution at 96%, ink #160f10, 18 px either side.
const juce::Colour kInk{0xff160f10};

juce::Font titleFont() { return theme::font(18.0f, true); }
juce::Font bodyFont() { return theme::font(theme::sizeBody); }
juce::Font boldFont() { return theme::font(theme::sizeBody, true); }
juce::Font smallFont() { return theme::font(theme::sizeSmall); }

juce::String timeOf(const juce::File& file) {
    if (!file.existsAsFile()) return {};
    return file.getLastModificationTime().formatted("%d %b %Y, %H:%M");
}

juce::TextLayout noteLayout(const juce::String& note, int w) {
    juce::AttributedString s;
    s.setWordWrap(juce::AttributedString::byWord);
    s.append(note, bodyFont(), theme::mid);
    juce::TextLayout layout;
    layout.createLayout(s, static_cast<float>(std::max(1, w)));
    return layout;
}

int noteHeight(const juce::String& note, int w) {
    const auto text = std::max(static_cast<int>(std::ceil(noteLayout(note, w).getHeight())),
                               static_cast<int>(std::ceil(bodyFont().getHeight())));
    return kLinePadY + text + kLinePadY + 1;
}

void fit(juce::Viewport& view, juce::Component& body, int idealHeight) {
    const bool scrolls = idealHeight > view.getHeight();
    const int w = std::max(0, view.getWidth() - (scrolls ? view.getScrollBarThickness() : 0));
    body.setSize(w, std::max(idealHeight, view.getHeight()));
}

}  // namespace

// ---- What it shows -----------------------------------------------------------------------------------------------------

bool FilesState::operator==(const FilesState& o) const {
    const auto same = [](const Conflict& a, const Conflict& b) {
        return a.title == b.title && a.original == b.original && a.copy == b.copy && a.conflict.original == b.conflict.original
               && a.conflict.copy == b.conflict.copy && a.conflict.id == b.conflict.id;
    };
    return notes == o.notes && std::equal(conflicts.begin(), conflicts.end(), o.conflicts.begin(), o.conflicts.end(), same);
}

FilesState makeFilesState(const app::Session& session) {
    FilesState state;
    state.notes = session.loadReport().notes;
    for (const auto& copy : session.conflictCopies()) {
        FilesState::Conflict row;
        row.conflict = copy;
        const auto key = app::conflictDocument(session.folder().root(), copy);
        auto name = key.has_value() ? app::documentName(session.model(), *key) : juce::String();
        if (name.isEmpty()) name = copy.original.getFileName();
        row.title = name + " was changed on two computers";
        row.original = {copy.original.getFileName(), timeOf(copy.original)};
        row.copy = {copy.copy.getFileName(), timeOf(copy.copy)};
        state.conflicts.push_back(std::move(row));
    }
    return state;
}

juce::String outsideChangesSentence(const std::vector<app::OutsideChange>& changes) {
    juce::StringArray names;
    for (const auto& change : changes)
        names.add(change.name.isNotEmpty() ? change.name : change.file.getFileNameWithoutExtension());
    if (names.isEmpty()) return {};
    auto list = names[names.size() - 1];
    if (names.size() > 1) {
        names.remove(names.size() - 1);
        list = names.joinIntoString(", ") + " and " + list;
    }
    return list + " changed on another computer.";
}

// ---- The list ----------------------------------------------------------------------------------------------------------

// The notes, then the conflicts, each with its two versions and a "Keep this one" under each.
class FilesDialog::Body : public juce::Component {
public:
    std::function<void(std::size_t conflict, bool keepTheCopy)> onKeep;

    void setState(const FilesState& state) {
        state_ = state;
        buttons_.clear();
        for (std::size_t i = 0; i < state_.conflicts.size(); ++i) {
            for (const bool keepTheCopy : {false, true}) {
                auto button = std::make_unique<PillButton>("Keep this one");
                // Later, not from inside the click: keeping one changes the list, and with it these buttons.
                button->onClick = [safe = juce::Component::SafePointer<Body>(this), i, keepTheCopy] {
                    juce::MessageManager::callAsync([safe, i, keepTheCopy] {
                        if (safe != nullptr && safe->onKeep) safe->onKeep(i, keepTheCopy);
                    });
                };
                addAndMakeVisible(*button);
                buttons_.push_back(std::move(button));
            }
        }
        resized();
        repaint();
    }

    int idealHeight(int w) const {
        int h = 0;
        for (const auto& note : state_.notes) h += noteHeight(note, w);
        return h + static_cast<int>(state_.conflicts.size()) * kConflictHeight;
    }

    void resized() override {
        int y = 0;
        for (const auto& note : state_.notes) y += noteHeight(note, getWidth());
        const int columnWidth = (getWidth() - kColumnGap) / 2;
        for (std::size_t i = 0; i < state_.conflicts.size() && 2 * i + 1 < buttons_.size(); ++i) {
            const int buttonY = y + kRowPadY + kTitleHeight + kTitleGap + kNameHeight + kTimeHeight + kButtonGap;
            for (int column = 0; column < 2; ++column) {
                auto& button = *buttons_[2 * i + static_cast<std::size_t>(column)];
                const int x = column * (columnWidth + kColumnGap);
                button.setBounds(x, buttonY, std::min(columnWidth, button.idealWidth()), kButtonHeight);
            }
            y += kConflictHeight;
        }
    }

    void paint(juce::Graphics& g) override {
        const int w = getWidth();
        int y = 0;
        for (const auto& note : state_.notes) {
            const int h = noteHeight(note, w);
            const auto layout = noteLayout(note, w);
            layout.draw(g, juce::Rectangle<float>(0.0f, static_cast<float>(y + kLinePadY), static_cast<float>(w), layout.getHeight()));
            g.setColour(theme::rowLine);
            g.fillRect(0, y + h - 1, w, 1);
            y += h;
        }
        const int columnWidth = (w - kColumnGap) / 2;
        for (const auto& row : state_.conflicts) {
            int top = y + kRowPadY;
            g.setColour(theme::text);
            g.setFont(boldFont());
            g.drawFittedText(row.title, 0, top, w, kTitleHeight, juce::Justification::centredLeft, 1, 1.0f);
            top += kTitleHeight + kTitleGap;
            int column = 0;
            for (const auto* version : {&row.original, &row.copy}) {
                const int x = column++ * (columnWidth + kColumnGap);
                g.setColour(theme::text);
                g.setFont(bodyFont());
                g.drawFittedText(version->fileName, x, top, columnWidth, kNameHeight, juce::Justification::centredLeft, 1, 1.0f);
                g.setColour(theme::dim);
                g.setFont(smallFont());
                g.drawFittedText(version->time, x, top + kNameHeight, columnWidth, kTimeHeight, juce::Justification::centredLeft, 1, 1.0f);
            }
            g.setColour(theme::rowLine);
            g.fillRect(0, y + kConflictHeight - 1, w, 1);
            y += kConflictHeight;
        }
    }

private:
    FilesState state_;
    std::vector<std::unique_ptr<PillButton>> buttons_;           // two per conflict: the original's, then the copy's
};

// ---- The dialog --------------------------------------------------------------------------------------------------------

FilesDialog::FilesDialog(app::Session& session) : session_(session), body_(std::make_unique<Body>()) {
    setWantsKeyboardFocus(true);
    body_->onKeep = [this](std::size_t conflict, bool keepTheCopy) { keep(conflict, keepTheCopy); };
    view_.setViewedComponent(body_.get(), false);
    view_.setScrollBarsShown(true, false);
    addAndMakeVisible(view_);
    close_.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible(close_);
    state_ = makeFilesState(session_);
    body_->setState(state_);
}

FilesDialog::~FilesDialog() = default;

void FilesDialog::refresh() {
    auto next = makeFilesState(session_);
    if (next == state_) return;
    state_ = std::move(next);
    body_->setState(state_);
    if (state_.isEmpty() && error_.isEmpty()) {
        if (onClose) onClose();
        return;
    }
    if (auto* parent = getParentComponent()) parent->resized();  // a veil sizes this to idealHeight()
    resized();
}

void FilesDialog::keep(std::size_t conflict, bool keepTheCopy) {
    if (conflict >= state_.conflicts.size()) return;
    const auto result = session_.resolveConflict(state_.conflicts[conflict].conflict, keepTheCopy);
    error_ = result.failed() ? result.getErrorMessage() : juce::String();
    repaint();
    refresh();
}

int FilesDialog::idealHeight() const {
    const int w = getWidth() > 0 ? getWidth() : width;
    return kHeaderHeight + 2 * kListPadY + body_->idealHeight(w - 2 * kPadX) + 2 * kFooterPadY + kButtonHeight;
}

void FilesDialog::resized() {
    auto area = getLocalBounds();
    header_ = area.removeFromTop(kHeaderHeight);
    footer_ = area.removeFromBottom(2 * kFooterPadY + kButtonHeight);
    auto foot = footer_.reduced(kPadX, kFooterPadY);
    close_.setBounds(foot.removeFromRight(close_.idealWidth()));
    view_.setBounds(area.reduced(kPadX, kListPadY));
    fit(view_, *body_, body_->idealHeight(view_.getWidth()));
    if (body_->getHeight() > view_.getHeight())                   // a scroll bar takes some width: wrap to what is left
        fit(view_, *body_, body_->idealHeight(body_->getWidth()));
}

void FilesDialog::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(theme::popup);
    g.fillRoundedRectangle(bounds, kRadius);
    g.setColour(theme::hairline);
    g.drawRoundedRectangle(bounds.reduced(0.5f), kRadius, 1.0f);
    g.fillRect(1, header_.getBottom() - 1, getWidth() - 2, 1);
    g.fillRect(1, footer_.getY(), getWidth() - 2, 1);

    g.setColour(theme::text);
    g.setFont(titleFont());
    g.drawText("Check files", header_.reduced(kPadX, 0), juce::Justification::centredLeft, true);

    if (error_.isNotEmpty()) {
        auto foot = footer_.reduced(kPadX, kFooterPadY);
        foot.removeFromRight(close_.idealWidth() + kFooterGap);
        g.setColour(theme::danger);
        g.setFont(smallFont());
        g.drawFittedText(error_, foot, juce::Justification::centredLeft, 2, 1.0f);
    }
}

bool FilesDialog::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey) {
        if (onClose) onClose();
        return true;
    }
    return false;
}

// ---- The banner --------------------------------------------------------------------------------------------------------

OutsideChangesBanner::OutsideChangesBanner(app::Session& session) : session_(session) {
    setWantsKeyboardFocus(false);
    load_.onClick = [this] { session_.takeOutsideChanges(); };
    keep_.onClick = [this] { session_.keepMine(); };
    addAndMakeVisible(load_);
    addAndMakeVisible(keep_);
    refresh();
}

void OutsideChangesBanner::refresh() {
    const auto sentence = session_.mode() == app::Mode::Edit ? outsideChangesSentence(session_.outsideChanges()) : juce::String();
    if (sentence == sentence_) return;
    sentence_ = sentence;
    repaint();
}

void OutsideChangesBanner::resized() {
    auto area = getLocalBounds().reduced(kPadX, (height - kButtonHeight) / 2);
    keep_.setBounds(area.removeFromRight(keep_.idealWidth()));
    area.removeFromRight(kFooterGap);
    load_.setBounds(area.removeFromRight(load_.idealWidth()));
}

void OutsideChangesBanner::paint(juce::Graphics& g) {
    g.fillAll(theme::caution.withAlpha(0.96f));
    auto area = getLocalBounds().reduced(kPadX, 0);
    area.setRight(load_.getX() - kPadX);
    g.setColour(kInk);
    g.setFont(theme::font(theme::sizeBase, true));
    g.drawFittedText(sentence_, area, juce::Justification::centredLeft, 2, 1.0f);
}

}  // namespace sqlive::ui
