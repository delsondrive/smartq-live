#include "ui/SnapshotsDialog.h"

#include <algorithm>
#include <cmath>

#include "ui/Format.h"
#include "ui/Helpers.h"
#include "ui/Theme.h"

namespace sqlive::ui {

namespace {

// The mockup's .dlg (popup, hairline border, 14 px corners), .dh (14 px by 18 px around an 18 px title and a 30 px button),
// .df (12 px by 18 px, 10 px between its parts); the list's column (8 px above and below) with its table (th: 11 px bold
// capitals, 7 px by 8 px, a hairline under; td: 13 px, 7 px by 8 px, a faint line under); the right column's 14 px by 18 px.
constexpr float kRadius = 14.0f;
constexpr int kPadX = 18, kHeaderHeight = 58, kFooterPadY = 12, kButtonHeight = 30, kFooterGap = 10;
constexpr int kListPadY = 8, kCellPadX = 8, kHeadRowHeight = 29, kRowHeight = 31, kWhenWidth = 140, kMarkWidth = 76;
constexpr int kRightPadX = 18, kRightPadY = 14, kChangeHeadHeight = 22, kChangeRowHeight = 22, kChangeGap = 12, kDotSize = 6;
constexpr int kFooterHeight = 2 * kFooterPadY + kButtonHeight;

const juce::String kGoingBack = "Going back replaces every song, setlist and setting with how it was then. Audio files are never changed.";
const juce::String kFooterHint = "Your current state is saved as a snapshot before going back.";

juce::Font titleFont() { return theme::font(18.0f, true); }
juce::Font bodyFont() { return theme::font(theme::sizeBody); }
juce::Font nameFont() { return theme::font(theme::sizeBody, true); }
juce::Font smallFont() { return theme::font(theme::sizeSmall); }
juce::Font headFont() { return theme::tracked(theme::font(theme::sizeCaption, true), 0.6f); }

juce::TextLayout wrapped(const juce::String& text, const juce::Font& font, juce::Colour colour, int w) {
    juce::AttributedString s;
    s.setWordWrap(juce::AttributedString::byWord);
    s.append(text, font, colour);
    juce::TextLayout layout;
    layout.createLayout(s, static_cast<float>(std::max(1, w)));
    return layout;
}

// Whole calendar days from one day to another, by their local midnights (a change of clocks between them rounds away).
int daysBetween(juce::Time from, juce::Time to) {
    const auto midnight = [](juce::Time t) { return juce::Time(t.getYear(), t.getMonth(), t.getDayOfMonth(), 0, 0, 0, 0, true); };
    return static_cast<int>(std::lround((midnight(to) - midnight(from)).inDays()));
}

}  // namespace

juce::String snapshotWhen(juce::Time when, juce::Time now) {
    const int days = daysBetween(when, now);
    if (days == 0) return "Today " + when.formatted("%H:%M");
    if (days > 0 && days < 7) return when.formatted("%a %H:%M");
    return when.formatted("%d %b %Y, %H:%M");
}

SnapshotsState makeSnapshotsState(const app::Session& session, juce::Time now) {
    SnapshotsState state;
    for (const auto& info : session.snapshots().list())
        state.rows.push_back({info.id, info.name, snapshotWhen(info.when, now), info.automatic});
    return state;
}

// ---- The list ----------------------------------------------------------------------------------------------------------

// The table's rows: a click selects one, a right-click on a named one opens its menu.
class SnapshotsDialog::List : public juce::Component {
public:
    explicit List(SnapshotsDialog& owner) : owner_(owner) {}

    void paint(juce::Graphics& g) override {
        const auto& rows = owner_.state_.rows;
        if (rows.empty()) {
            g.setColour(theme::dim);
            g.setFont(bodyFont());
            g.drawText("No snapshots yet.", juce::Rectangle<int>(kCellPadX, 0, getWidth() - 2 * kCellPadX, kRowHeight),
                       juce::Justification::centredLeft, true);
            return;
        }
        const int w = getWidth();
        const int markX = w - kMarkWidth, whenX = markX - kWhenWidth;
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const auto& row = rows[i];
            const juce::Rectangle<int> r(0, static_cast<int>(i) * kRowHeight, w, kRowHeight);
            const bool selected = row.id == owner_.selected_;
            if (selected) {
                g.setColour(theme::mint.withAlpha(0.06f));
                g.fillRect(r);
            }
            g.setColour(theme::rowLine);
            g.fillRect(r.withTop(r.getBottom() - 1));

            const auto text = r.withTrimmedBottom(1);
            // The name, and " · automatic" in dim after it, cut to the column.
            const int nameRoom = whenX - 2 * kCellPadX;
            const auto suffix = row.automatic ? kDot + "automatic" : juce::String();
            const int suffixWidth = row.automatic ? widthInPixels(smallFont(), suffix) : 0;
            const auto name = shorten(row.name, nameFont(), static_cast<float>(std::max(0, nameRoom - suffixWidth)));
            const int nameWidth = widthInPixels(nameFont(), name);
            g.setColour(theme::text);
            g.setFont(nameFont());
            g.drawText(name, text.withX(kCellPadX).withWidth(nameWidth), juce::Justification::centredLeft, false);
            if (row.automatic) {
                g.setColour(theme::dim);
                g.setFont(smallFont());
                g.drawText(suffix, text.withX(kCellPadX + nameWidth).withWidth(std::max(0, nameRoom - nameWidth)),
                           juce::Justification::centredLeft, true);
            }
            g.setColour(theme::mid);
            g.setFont(bodyFont());
            g.drawText(row.when, text.withX(whenX + kCellPadX).withWidth(kWhenWidth - 2 * kCellPadX), juce::Justification::centredLeft, true);
            if (selected) {
                g.setColour(theme::mint);
                g.drawText("selected", text.withX(markX + kCellPadX).withWidth(kMarkWidth - 2 * kCellPadX), juce::Justification::centredLeft,
                           true);
            }
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        const auto& rows = owner_.state_.rows;
        const int index = e.y / kRowHeight;
        if (e.y < 0 || index >= static_cast<int>(rows.size())) return;
        const auto& row = rows[static_cast<std::size_t>(index)];
        owner_.select(row.id);
        if (e.mods.isPopupMenu() && !row.automatic) owner_.showMenu(row.id);
    }

private:
    SnapshotsDialog& owner_;
};

// ---- The dialog --------------------------------------------------------------------------------------------------------

SnapshotsDialog::SnapshotsDialog(app::Session& session) : session_(session), list_(std::make_unique<List>(*this)) {
    setWantsKeyboardFocus(true);
    view_.setViewedComponent(list_.get(), false);
    view_.setScrollBarsShown(true, false);
    addAndMakeVisible(view_);
    save_.onClick = [this] { askName(); };
    close_.onClick = [this] {
        if (onClose) onClose();
    };
    goBack_.onClick = [this] { goBack(); };
    for (auto* button : {&save_, &close_, &goBack_}) {
        button->setWantsKeyboardFocus(false);
        addAndMakeVisible(*button);
    }
    refresh();
}

SnapshotsDialog::~SnapshotsDialog() = default;

bool SnapshotsDialog::editable() const { return session_.mode() == app::Mode::Edit; }

void SnapshotsDialog::refresh() {
    auto next = makeSnapshotsState(session_, juce::Time::getCurrentTime());
    const auto there = [&](const juce::String& id) {
        return std::any_of(next.rows.begin(), next.rows.end(), [&](const SnapshotsState::Row& r) { return r.id == id; });
    };
    if (!there(selected_)) selected_ = next.rows.empty() ? juce::String() : next.rows.front().id;
    const auto changesBefore = changes_;
    const bool readBefore = changesRead_;
    const bool differs = !(next == state_);
    state_ = std::move(next);
    readChanges();
    const bool changesDiffer = readBefore != changesRead_ || changesBefore.size() != changes_.size()
                               || !std::equal(changesBefore.begin(), changesBefore.end(), changes_.begin(),
                                              [](const model::ChangeLine& a, const model::ChangeLine& b) { return a.where == b.where && a.what == b.what; });
    goBack_.setEnabled(editable() && selected_.isNotEmpty());
    save_.setEnabled(editable());
    if (differs || changesDiffer) {
        if (auto* parent = getParentComponent()) parent->resized();   // the veil sizes it from idealHeight()
        resized();
    }
    list_->repaint();
}

void SnapshotsDialog::select(const juce::String& id) {
    if (id == selected_) return;
    selected_ = id;
    goBack_.setEnabled(editable() && selected_.isNotEmpty());
    readChanges();
    if (auto* parent = getParentComponent()) parent->resized();   // the right side's height depends on its lines
    resized();
    list_->repaint();
    repaint();
}

void SnapshotsDialog::readChanges() {
    changes_.clear();
    changesRead_ = false;
    if (selected_.isEmpty()) return;
    model::BandModel then;
    if (session_.snapshots().load(selected_, then).failed()) return;
    changes_ = model::describeChanges(then, session_.model(), kMaxChanges);
    changesRead_ = true;
}

void SnapshotsDialog::say(const juce::String& problem) {
    if (problem == problem_) return;
    problem_ = problem;
    repaint();
}

void SnapshotsDialog::askName() {
    if (!editable()) return;
    askUserText(*this, "Name this snapshot", {}, "Save", [safe = juce::Component::SafePointer<SnapshotsDialog>(this)](const juce::String& name) {
        if (safe != nullptr) safe->saveNow(name);
    });
}

void SnapshotsDialog::saveNow(const juce::String& name) {
    if (!editable()) return;
    if (const auto made = session_.snapshots().create(session_.model(), name, false); made.failed()) {
        say(made.getErrorMessage());
        return;
    }
    say({});
    refresh();
    if (!state_.rows.empty()) select(state_.rows.front().id);   // the one just made
}

void SnapshotsDialog::goBack() {
    if (!editable() || selected_.isEmpty()) return;
    if (const auto went = session_.snapshots().restore(selected_, session_.commands()); went.failed()) {
        say(went.getErrorMessage());
        refresh();
        return;
    }
    say({});
    if (onClose) onClose();                                       // last: it may close the dialog
}

void SnapshotsDialog::rename(const juce::String& id, const juce::String& name) {
    if (!editable()) return;
    const auto renamed = session_.snapshots().rename(id, name);
    say(renamed.failed() ? renamed.getErrorMessage() : juce::String());
    refresh();
}

void SnapshotsDialog::remove(const juce::String& id) {
    if (!editable()) return;
    const auto removed = session_.snapshots().remove(id);
    say(removed.failed() ? removed.getErrorMessage() : juce::String());
    refresh();
}

void SnapshotsDialog::showMenu(const juce::String& id) {
    if (!editable()) return;
    const auto row = std::find_if(state_.rows.begin(), state_.rows.end(), [&](const SnapshotsState::Row& r) { return r.id == id; });
    if (row == state_.rows.end() || row->automatic) return;
    juce::PopupMenu menu;
    menu.addItem(1, "Rename" + kEllipsis);
    menu.addItem(2, "Remove");
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(list_.get()).withMousePosition(),
        [safe = juce::Component::SafePointer<SnapshotsDialog>(this), id, name = row->name](int result) {
            if (safe == nullptr) return;
            if (result == 1)
                askUserText(*safe, "Rename the snapshot", name, "Rename", [safe, id](const juce::String& newName) {
                    if (safe != nullptr) safe->rename(id, newName);
                });
            if (result == 2) {
                // A snapshot removed can't be brought back: asked first.
                safe->box_ = juce::AlertWindow::showScopedAsync(
                    juce::MessageBoxOptions()
                        .withIconType(juce::MessageBoxIconType::NoIcon)
                        .withAssociatedComponent(safe.getComponent())
                        .withTitle(utf8("Remove the snapshot \xe2\x80\x9c") + name + utf8("\xe2\x80\x9d?"))
                        .withMessage("It can't be brought back.")
                        .withButton("Remove")
                        .withButton("Cancel"),
                    [safe, id](int answer) {
                        if (safe != nullptr && answer == 1) safe->remove(id);
                    });
            }
        });
}

int SnapshotsDialog::changesHeight() const {
    if (selected_.isEmpty()) return 0;
    const int lines = changesRead_ && !changes_.empty() ? static_cast<int>(changes_.size()) : 1;
    return kChangeHeadHeight + lines * kChangeRowHeight + kChangeGap;
}

int SnapshotsDialog::bodyHeight() const {
    const int rows = std::max<int>(1, static_cast<int>(state_.rows.size()));
    const int left = 2 * kListPadY + kHeadRowHeight + rows * kRowHeight;
    const int textWidth = width / 2 - 2 * kRightPadX;
    const int right = 2 * kRightPadY + changesHeight() + static_cast<int>(std::ceil(wrapped(kGoingBack, bodyFont(), theme::mid, textWidth).getHeight()));
    return std::max(left, right);
}

int SnapshotsDialog::idealHeight() const { return kHeaderHeight + bodyHeight() + kFooterHeight; }

void SnapshotsDialog::resized() {
    auto area = getLocalBounds();
    header_ = area.removeFromTop(kHeaderHeight);
    footer_ = area.removeFromBottom(std::min(area.getHeight(), kFooterHeight));
    auto head = header_.reduced(kPadX, (kHeaderHeight - kButtonHeight) / 2);
    save_.setBounds(head.removeFromRight(save_.idealWidth()));

    auto foot = footer_.reduced(kPadX, kFooterPadY);
    goBack_.setBounds(foot.removeFromRight(goBack_.idealWidth()));
    foot.removeFromRight(kFooterGap);
    close_.setBounds(foot.removeFromRight(close_.idealWidth()));
    foot.removeFromRight(kFooterGap);
    hint_ = foot;

    left_ = area.removeFromLeft(area.getWidth() / 2);
    right_ = area;
    auto list = left_.reduced(0, kListPadY);
    columns_ = list.removeFromTop(kHeadRowHeight);
    view_.setBounds(list);
    const int ideal = std::max(1, static_cast<int>(state_.rows.size())) * kRowHeight;
    const bool scrolls = ideal > list.getHeight();
    list_->setSize(std::max(0, list.getWidth() - (scrolls ? view_.getScrollBarThickness() : 0)), ideal);
}

void SnapshotsDialog::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(theme::popup);
    g.fillRoundedRectangle(bounds, kRadius);
    g.setColour(theme::hairline);
    g.drawRoundedRectangle(bounds.reduced(0.5f), kRadius, 1.0f);
    g.fillRect(1, header_.getBottom() - 1, getWidth() - 2, 1);
    g.fillRect(1, footer_.getY(), getWidth() - 2, 1);
    g.fillRect(left_.getRight() - 1, left_.getY(), 1, left_.getHeight());   // the list's border on its right

    g.setColour(theme::text);
    g.setFont(titleFont());
    g.drawText("Snapshots", header_.withTrimmedLeft(kPadX).withRight(save_.getX() - 12), juce::Justification::centredLeft, true);

    // The table's heads, with the hairline under them.
    const int listWidth = list_->getWidth();
    const int markX = columns_.getX() + listWidth - kMarkWidth, whenX = markX - kWhenWidth;
    g.setColour(theme::dim);
    g.setFont(headFont());
    g.drawText("SNAPSHOT", columns_.withX(columns_.getX() + kCellPadX).withRight(whenX), juce::Justification::centredLeft, true);
    g.drawText("WHEN", columns_.withX(whenX + kCellPadX).withRight(markX), juce::Justification::centredLeft, true);
    g.setColour(theme::hairline);
    g.fillRect(columns_.getX(), columns_.getBottom() - 1, listWidth, 1);

    auto text = right_.reduced(kRightPadX, kRightPadY);
    if (selected_.isNotEmpty()) {
        const auto row = std::find_if(state_.rows.begin(), state_.rows.end(), [&](const SnapshotsState::Row& r) { return r.id == selected_; });
        const auto name = row == state_.rows.end() ? juce::String() : row->name;
        g.setColour(theme::dim);
        g.setFont(headFont());
        g.drawText(shorten(utf8("CHANGED SINCE \xe2\x80\x9c") + name.toUpperCase() + utf8("\xe2\x80\x9d"), headFont(), static_cast<float>(text.getWidth())),
                   text.removeFromTop(kChangeHeadHeight), juce::Justification::centredLeft, false);
        if (!changesRead_ || changes_.empty()) {
            g.setColour(theme::mid);
            g.setFont(bodyFont());
            g.drawText(changesRead_ ? "Nothing has changed since this snapshot." : "This snapshot couldn't be read.",
                       text.removeFromTop(kChangeRowHeight), juce::Justification::centredLeft, true);
        }
        for (const auto& line : changes_) {
            auto r = text.removeFromTop(kChangeRowHeight);
            if (line.where.isNotEmpty()) {                          // a cut list's last line has no where, and no dot
                g.setColour(theme::caution);
                g.fillEllipse(static_cast<float>(r.getX()), static_cast<float>(r.getCentreY()) - kDotSize / 2.0f, static_cast<float>(kDotSize),
                              static_cast<float>(kDotSize));
            }
            r.removeFromLeft(kDotSize + 8);
            const auto where = line.where.isNotEmpty() ? line.where : juce::String();
            const int whereWidth = std::min(r.getWidth() / 2, where.isEmpty() ? 0 : widthInPixels(nameFont(), where));
            g.setColour(theme::text);
            g.setFont(nameFont());
            g.drawText(shorten(where, nameFont(), static_cast<float>(whereWidth)), r.withWidth(whereWidth), juce::Justification::centredLeft, false);
            r.removeFromLeft(whereWidth);
            g.setColour(theme::mid);
            g.setFont(bodyFont());
            const auto what = where.isEmpty() ? line.what : kDot + line.what;
            g.drawText(shorten(what, bodyFont(), static_cast<float>(r.getWidth())), r, juce::Justification::centredLeft, false);
        }
        text.removeFromTop(kChangeGap);
    }
    wrapped(kGoingBack, bodyFont(), theme::mid, text.getWidth()).draw(g, text.toFloat());

    // The footer's sentence, or what went wrong in its place (in caution: something to try again, not something broken).
    const bool problem = problem_.isNotEmpty();
    g.setColour(problem ? theme::caution : theme::dim);
    g.setFont(smallFont());
    g.drawText(shorten(problem ? problem_ : kFooterHint, smallFont(), static_cast<float>(hint_.getWidth())), hint_,
               juce::Justification::centredLeft, false);
}

bool SnapshotsDialog::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey) {
        if (onClose) onClose();
        return true;
    }
    return false;
}

}  // namespace sqlive::ui
