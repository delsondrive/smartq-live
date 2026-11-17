#include "ui/RemoteSetupPage.h"

#include <algorithm>

#include "ui/Format.h"
#include "ui/Helpers.h"
#include "ui/QrCode.h"
#include "ui/Theme.h"

namespace sqlive::ui {

using app::PhoneAccess;

namespace {

// The mockup's screen 14: .box (padding 10 12; the QR code and the column 18 apart), .qr (150 px on white, 8 px corners),
// .row (13 px mid, 6 px apart, a gap of 10), .val (a field with a hairline border, 6 px corners, padding 3 8, semibold
// text on the right, at least 80 wide), the PIN letter-spaced 4 px, th (11 px bold, letter-spaced 0.6, dim, padding 7 8)
// and td (padding 7 8), .addl (12 px semibold, mint) for "New PIN", td.bad for "Refuse", .hint (12 px, dim).
constexpr int kBoxPadX = 12, kBoxPadY = 10, kQrSize = 150, kQrGap = 18, kTitleRow = 28, kValueRow = 34, kValueHeight = 24;
constexpr int kValueMin = 80, kValuePadX = 8, kLinkGap = 12, kLineHeight = 18, kTableGap = 6, kHeadRow = 29, kRow = 36;
constexpr int kCellPad = 8, kButtonHeight = 26, kButtonGap = 8, kChoiceHeight = 24, kChoiceMin = 130;
constexpr int kCanWidth = 230, kActionWidth = 80, kBelowGap = 12, kSwitchW = 34, kSwitchH = 18, kSwitchGap = 6;
constexpr int kDotSize = 8, kRefreshMs = 2000;

const juce::String kWaiting = "waiting", kConnected = "connected", kNotConnected = "not connected";
const juce::String kFullControl = "Full control", kViewOnly = "View only";
const juce::String kRefuse = "Refuse", kForget = "Forget", kNewPin = "New PIN";
const juce::String kNotInShow = "New phones can be let in when the show is over.";
const juce::String kNoPhones = "No phones yet. Scan the code with a phone on the same Wi-Fi.";
const juce::String kNoInternet = "No internet is needed.";

juce::Font titleFont() { return theme::font(15.0f, true); }
juce::Font rowFont() { return theme::font(theme::sizeBody); }
juce::Font valueFont() { return theme::font(theme::sizeBody, true); }
juce::Font pinFont() { return theme::tracked(theme::font(theme::sizeBody, true), 4.0f); }
juce::Font thFont() { return theme::tracked(theme::font(theme::sizeCaption, true), 0.6f); }
juce::Font nameFont() { return theme::font(theme::sizeBody, true); }
juce::Font linkFont() { return theme::font(theme::sizeSmall, true); }
juce::Font hintFont() { return theme::font(theme::sizeSmall); }

// "4719" as the mockup writes it, "4 7 1 9" (the letter-spacing does the rest).
juce::String spacedPin(const juce::String& pin) {
    juce::StringArray digits;
    for (int i = 0; i < pin.length(); ++i) digits.add(pin.substring(i, i + 1));
    return digits.joinIntoString(" ");
}

// The mockup's .val, its words on the right.
void drawValue(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text, const juce::Font& font) {
    const auto box = area.toFloat();
    g.setColour(theme::field);
    g.fillRoundedRectangle(box, theme::radiusField);
    g.setColour(theme::hairline);
    g.drawRoundedRectangle(box.reduced(0.5f), theme::radiusField, 1.0f);
    g.setColour(theme::text);
    g.setFont(font);
    g.drawText(text, area.reduced(kValuePadX, 0), juce::Justification::centredRight, false);
}

int valueWidth(const juce::String& text, const juce::Font& font, int most) {
    return juce::jlimit(std::min(kValueMin, most), std::max(0, most), widthInPixels(font, text) + 2 * kValuePadX + 2);
}

}  // namespace

RemoteSetupState makeRemoteSetupState(const app::Session& session, const app::RemoteAccess& access,
                                      const app::RemoteServer& server) {
    RemoteSetupState state;
    state.on = server.isRunning();
    if (state.on) {
        state.address = server.address();
        state.pin = access.pin();
    } else {
        state.problem = server.lastError();
    }
    bool anyWaiting = false;
    for (const auto& phone : access.phones()) {
        RemoteSetupState::Row row;
        row.id = phone.id;
        row.name = phone.name;
        row.access = phone.access;
        row.waiting = phone.access == PhoneAccess::Waiting;
        row.connected = phone.connected;
        row.status = row.waiting ? kWaiting : row.connected ? kConnected : kNotConnected;
        anyWaiting = anyWaiting || row.waiting;
        state.rows.push_back(std::move(row));
    }
    if (anyWaiting && session.mode() != app::Mode::Edit) state.note = kNotInShow;
    return state;
}

// A row's controls: the two buttons of a waiting phone, or the choice of one already let in.
struct RemoteSetupPage::RowControls {
    PillButton full{kFullControl, PillButton::Style::Primary}, view{kViewOnly};
    Choice choice;
};

RemoteSetupPage::RemoteSetupPage(app::Session& session, app::RemoteAccess& access, app::RemoteServer& server,
                                 juce::String bindAddress)
    : session_(session), access_(access), server_(server), bindAddress_(std::move(bindAddress)), mode_(session.mode()) {
    switch_.onChange = [this](bool on) { setOn(on); };
    addAndMakeVisible(switch_);
    access_.addChangeListener(this);
    session_.changes().addChangeListener(this);
    refresh(true);
}

RemoteSetupPage::~RemoteSetupPage() {
    stopTimer();
    session_.changes().removeChangeListener(this);
    access_.removeChangeListener(this);
}

void RemoteSetupPage::changeListenerCallback(juce::ChangeBroadcaster* source) {
    // The session's changes come 30 times a second while a song plays; only a change of mode matters here (the note).
    if (source == &session_.changes() && session_.mode() == mode_) return;
    refresh();
}

void RemoteSetupPage::timerCallback() { refresh(); }

void RemoteSetupPage::visibilityChanged() {
    if (isVisible()) {
        refresh(true);
        startTimer(kRefreshMs);
    } else {
        stopTimer();
    }
}

void RemoteSetupPage::refresh(bool force) {
    mode_ = session_.mode();
    if (!force && !shownWithParents(*this)) return;               // made again when it is shown
    auto next = makeRemoteSetupState(session_, access_, server_);
    if (next == state_ && !force) return;
    state_ = std::move(next);
    apply();
}

void RemoteSetupPage::showState(const RemoteSetupState& state) {
    state_ = state;
    apply();
}

const RemoteSetupState::Row* RemoteSetupPage::rowAt(int row) const {
    return juce::isPositiveAndBelow(row, static_cast<int>(state_.rows.size())) ? &state_.rows[static_cast<std::size_t>(row)] : nullptr;
}

// ---- The gestures ------------------------------------------------------------------------------------------------------

void RemoteSetupPage::setOn(bool on) {
    if (on) {
        if (server_.start(access_.port(), bindAddress_).wasOk()) {   // a failure is said from server_.lastError()
            access_.setEnabled(true);
            access_.setPort(server_.port());                      // the address stays the same next time, if it is free
        }
    } else {
        server_.stop();
        access_.setEnabled(false);
    }
    refresh(true);
}

void RemoteSetupPage::newPin() {
    access_.newPin();
    refresh(true);
}

void RemoteSetupPage::letIn(int row, PhoneAccess access) {
    if (const auto* r = rowAt(row); r != nullptr && (access == PhoneAccess::FullControl || access == PhoneAccess::ViewOnly))
        access_.decide(r->id, access, session_.mode() == app::Mode::Edit);   // a waiting phone only in Edit mode
    refresh(true);
}

void RemoteSetupPage::refuse(int row) {
    if (const auto* r = rowAt(row)) access_.decide(r->id, PhoneAccess::Refused, session_.mode() == app::Mode::Edit);
    refresh(true);
}

void RemoteSetupPage::forget(int row) {
    if (const auto* r = rowAt(row)) access_.forget(r->id);
    refresh(true);
}

// ---- Layout and drawing ------------------------------------------------------------------------------------------------

void RemoteSetupPage::apply() {
    switch_.setOn(state_.on);
    if (state_.address != qrFor_) {
        qrFor_ = state_.address;
        qrImage_ = state_.address.isNotEmpty() ? makeQrImage(state_.address, kQrSize) : juce::Image();
    }
    // Controls are only ever added, and hidden when not needed: one may be calling back into this as it is made again.
    while (rowControls_.size() < state_.rows.size()) {
        auto controls = std::make_unique<RowControls>();
        const int row = static_cast<int>(rowControls_.size());
        controls->full.onClick = [this, row] { letIn(row, PhoneAccess::FullControl); };
        controls->view.onClick = [this, row] { letIn(row, PhoneAccess::ViewOnly); };
        controls->choice.onChange = [this, row](int index) { letIn(row, index == 0 ? PhoneAccess::FullControl : PhoneAccess::ViewOnly); };
        for (auto* child : std::initializer_list<juce::Component*>{&controls->full, &controls->view, &controls->choice})
            addChildComponent(*child);
        rowControls_.push_back(std::move(controls));
    }
    const bool edit = session_.mode() == app::Mode::Edit;
    for (std::size_t i = 0; i < state_.rows.size(); ++i) {
        const auto& row = state_.rows[i];
        auto& controls = *rowControls_[i];
        controls.full.setEnabled(edit);                           // a waiting phone is let in only in Edit mode
        controls.view.setEnabled(edit);
        controls.choice.setItems({kFullControl, kViewOnly}, row.access == PhoneAccess::FullControl ? 0 : 1);
    }
    resized();
    repaint();
}

void RemoteSetupPage::resized() {
    auto area = getLocalBounds();
    const bool on = state_.on;
    const int rowCount = static_cast<int>(state_.rows.size());
    const bool table = on || rowCount > 0;                        // off, the phones still listed so one can be forgotten

    // The panel as high as its column, or the QR code when that is higher.
    int column = kTitleRow;
    if (on) column += 2 * kValueRow;
    if (state_.problem.isNotEmpty()) column += kLineHeight;
    if (table && state_.note.isNotEmpty()) column += kLineHeight;
    if (table) column += kTableGap + kHeadRow + std::max(1, rowCount) * kRow;
    box_ = area.removeFromTop(std::min(area.getHeight(), std::max(on ? kQrSize : 0, column) + 2 * kBoxPadY));
    area.removeFromTop(std::min(kBelowGap, area.getHeight()));
    internet_ = area.removeFromTop(std::min(kLineHeight, area.getHeight()));

    auto content = box_.reduced(kBoxPadX, kBoxPadY);
    qr_ = on ? content.removeFromLeft(std::min(kQrSize, content.getWidth())).withHeight(kQrSize) : juce::Rectangle<int>();
    if (on) content.removeFromLeft(std::min(kQrGap, content.getWidth()));

    // "Phone remote", and at the right its switch and "On" or "Off".
    title_ = content.removeFromTop(kTitleRow);
    auto right = title_;
    onLabel_ = right.removeFromRight(widthInPixels(rowFont(), "Off"));
    right.removeFromRight(kSwitchGap);
    switch_.setBounds(right.removeFromRight(kSwitchW).withSizeKeepingCentre(kSwitchW, kSwitchH));

    address_ = addressValue_ = pin_ = pinValue_ = newPin_ = noteLine_ = head_ = empty_ = {};
    if (on) {
        address_ = content.removeFromTop(kValueRow);
        const int addressW = valueWidth(state_.address, valueFont(), address_.getWidth());
        addressValue_ = address_.removeFromRight(addressW).withSizeKeepingCentre(addressW, kValueHeight);
        pin_ = content.removeFromTop(kValueRow);
        const int pinW = valueWidth(spacedPin(state_.pin), pinFont(), pin_.getWidth());
        pinValue_ = pin_.removeFromRight(pinW).withSizeKeepingCentre(pinW, kValueHeight);
        pin_.removeFromRight(kLinkGap);
        newPin_ = pin_.removeFromRight(widthInPixels(linkFont(), kNewPin)).withSizeKeepingCentre(widthInPixels(linkFont(), kNewPin), kLineHeight);
    }
    problemLine_ = state_.problem.isNotEmpty() ? content.removeFromTop(kLineHeight) : juce::Rectangle<int>();
    if (table && state_.note.isNotEmpty()) noteLine_ = content.removeFromTop(kLineHeight);

    rows_.clear();
    links_.clear();
    if (table) {
        content.removeFromTop(kTableGap);
        head_ = content.removeFromTop(kHeadRow);
        actionX_ = head_.getRight() - std::min(kActionWidth, head_.getWidth());
        canX_ = std::max(head_.getX(), actionX_ - kCanWidth);
        if (rowCount == 0) empty_ = content.removeFromTop(kRow);
        for (int i = 0; i < rowCount; ++i) {
            auto row = content.removeFromTop(kRow);
            if (row.getHeight() < kRow) row = {};                 // no room for it in the panel: not drawn, nothing to click
            rows_.push_back(row);
            const auto& r = state_.rows[static_cast<std::size_t>(i)];
            const auto link = r.waiting ? kRefuse : kForget;
            const int linkW = widthInPixels(r.waiting ? rowFont() : hintFont(), link);
            links_.push_back(row.isEmpty() ? juce::Rectangle<int>() : juce::Rectangle<int>(actionX_ + kCellPad, row.getCentreY() - 10, linkW, 20));
        }
    }

    // Each row's controls in its CAN cell, the others hidden.
    for (std::size_t i = 0; i < rowControls_.size(); ++i) {
        auto& controls = *rowControls_[i];
        const bool shown = i < rows_.size() && !rows_[i].isEmpty();
        const bool waiting = shown && state_.rows[i].waiting;
        controls.full.setVisible(waiting);
        controls.view.setVisible(waiting);
        controls.choice.setVisible(shown && !waiting);
        if (!shown) continue;
        const int centreY = rows_[i].getCentreY(), left = canX_ + kCellPad, most = actionX_ - kCellPad - left;
        if (waiting) {
            const int fullW = std::min(controls.full.idealWidth(), std::max(0, most));
            controls.full.setBounds(left, centreY - kButtonHeight / 2, fullW, kButtonHeight);
            const int viewX = controls.full.getRight() + kButtonGap;
            controls.view.setBounds(viewX, centreY - kButtonHeight / 2, std::max(0, std::min(controls.view.idealWidth(), actionX_ - kCellPad - viewX)), kButtonHeight);
        } else {
            const int w = std::min(std::max(kChoiceMin, controls.choice.idealWidth()), std::max(0, most));
            controls.choice.setBounds(left, centreY - kChoiceHeight / 2, w, kChoiceHeight);
        }
    }
    keepOffKeyboard(*this);
}

void RemoteSetupPage::paint(juce::Graphics& g) {
    theme::paintPanel(g, box_.toFloat());

    // The QR code on white, its corners rounded.
    if (!qr_.isEmpty()) {
        juce::Graphics::ScopedSaveState saved(g);
        juce::Path shape;
        shape.addRoundedRectangle(qr_.toFloat(), theme::radiusButton);
        g.reduceClipRegion(shape);
        g.fillAll(juce::Colours::white);
        g.setOpacity(1.0f);                                       // an image is drawn at the current colour's opacity
        if (qrImage_.isValid()) g.drawImageAt(qrImage_, qr_.getX(), qr_.getY());
    }

    g.setColour(theme::text);
    g.setFont(titleFont());
    g.drawText("Phone remote", title_, juce::Justification::centredLeft, true);
    g.setColour(theme::mid);
    g.setFont(rowFont());
    g.drawText(state_.on ? "On" : "Off", onLabel_, juce::Justification::centredLeft, false);

    if (!address_.isEmpty() || !addressValue_.isEmpty()) {
        g.setColour(theme::mid);
        g.setFont(rowFont());
        g.drawText("Address", address_, juce::Justification::centredLeft, true);
        drawValue(g, addressValue_, shorten(state_.address, valueFont(), static_cast<float>(addressValue_.getWidth() - 2 * kValuePadX)), valueFont());
        g.setColour(theme::mid);
        g.setFont(rowFont());
        g.drawText("PIN", pin_, juce::Justification::centredLeft, true);
        g.setColour(theme::mint);
        g.setFont(linkFont());
        g.drawText(kNewPin, newPin_, juce::Justification::centredRight, false);
        drawValue(g, pinValue_, spacedPin(state_.pin), pinFont());
    }

    g.setFont(rowFont());
    g.setColour(theme::caution);
    if (!problemLine_.isEmpty())
        g.drawText(shorten(state_.problem, rowFont(), static_cast<float>(problemLine_.getWidth())), problemLine_, juce::Justification::centredLeft, false);
    if (!noteLine_.isEmpty())
        g.drawText(shorten(state_.note, rowFont(), static_cast<float>(noteLine_.getWidth())), noteLine_, juce::Justification::centredLeft, false);

    if (!head_.isEmpty()) {
        // PHONE / CAN / (action), a hairline under them.
        g.setColour(theme::dim);
        g.setFont(thFont());
        g.drawText("PHONE", head_.withRight(canX_).reduced(kCellPad, 0), juce::Justification::centredLeft, false);
        g.drawText("CAN", head_.withLeft(canX_).withRight(actionX_).reduced(kCellPad, 0), juce::Justification::centredLeft, false);
        g.setColour(theme::hairline);
        g.fillRect(head_.getX(), head_.getBottom() - 1, head_.getWidth(), 1);
    }
    if (!empty_.isEmpty()) {
        g.setColour(theme::dim);
        g.setFont(hintFont());
        g.drawText(shorten(kNoPhones, hintFont(), static_cast<float>(empty_.getWidth() - 2 * kCellPad)), empty_.reduced(kCellPad, 0),
                   juce::Justification::centredLeft, false);
    }
    for (std::size_t i = 0; i < rows_.size() && i < state_.rows.size(); ++i) {
        const auto& area = rows_[i];
        if (area.isEmpty()) continue;
        const auto& row = state_.rows[i];
        if (row.waiting) {
            g.setColour(theme::ember.withAlpha(0.08f));
            g.fillRect(area);
        }
        g.setColour(theme::rowLine);
        g.fillRect(area.getX(), area.getBottom() - 1, area.getWidth(), 1);

        // PHONE: a mint dot when it is connected, its name, and "· waiting".
        auto name = area.withRight(canX_).reduced(kCellPad, 0);
        if (row.connected) {
            g.setColour(theme::mint);
            g.fillEllipse(juce::Rectangle<int>(name.getX(), name.getCentreY() - kDotSize / 2, kDotSize, kDotSize).toFloat());
            name.removeFromLeft(kDotSize + kCellPad);
        }
        const auto after = row.waiting ? kDot + kWaiting : juce::String();
        const int afterW = after.isEmpty() ? 0 : widthInPixels(hintFont(), after);
        const auto shown = shorten(row.name, nameFont(), static_cast<float>(std::max(0, name.getWidth() - afterW)));
        g.setColour(theme::text);
        g.setFont(nameFont());
        g.drawText(shown, name, juce::Justification::centredLeft, false);
        if (afterW > 0) {
            g.setColour(theme::dim);
            g.setFont(hintFont());
            g.drawText(after, name.withTrimmedLeft(widthInPixels(nameFont(), shown)), juce::Justification::centredLeft, false);
        }

        // Refuse (danger) or Forget (dim).
        if (i < links_.size() && !links_[i].isEmpty()) {
            g.setColour(row.waiting ? theme::danger : theme::dim);
            g.setFont(row.waiting ? rowFont() : hintFont());
            g.drawText(row.waiting ? kRefuse : kForget, links_[i], juce::Justification::centredLeft, false);
        }
    }

    g.setColour(theme::dim);
    g.setFont(hintFont());
    g.drawText(kNoInternet, internet_, juce::Justification::centredLeft, true);
}

void RemoteSetupPage::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) return;
    const auto at = e.getPosition();
    if (newPin_.contains(at)) {
        newPin();
        return;
    }
    for (int i = 0; i < static_cast<int>(links_.size()); ++i)
        if (links_[static_cast<std::size_t>(i)].contains(at)) {
            if (const auto* row = rowAt(i); row != nullptr && row->waiting) refuse(i);
            else forget(i);
            return;
        }
}

void RemoteSetupPage::mouseMove(const juce::MouseEvent& e) {
    const auto at = e.getPosition();
    const bool link = newPin_.contains(at) || std::any_of(links_.begin(), links_.end(), [&](const auto& r) { return r.contains(at); });
    setMouseCursor(link ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

}  // namespace sqlive::ui
