#include "ui/MidiSetupPage.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>

#include "app/ControlInput.h"
#include "app/MidiPorts.h"
#include "app/WallClock.h"
#include "model/DocumentsJson.h"
#include "model/JsonObject.h"
#include "ui/AppShell.h"
#include "ui/Format.h"
#include "ui/Helpers.h"
#include "ui/Theme.h"
#include "ui/Veil.h"

namespace sqlive::ui {

using model::jsonObject;

namespace {

// The mockup's CSS: .box (no padding here; its header 10 12 0, h4 11 px bold letter-spaced 0.8, dim), th (11 px bold,
// letter-spaced 0.6, dim, padding 7 8, a hairline under it), td (13 px, padding 7 8, mid, a faint line under it), td.f
// (text, semibold), .bar4 (4 x 14, 2 px corners, 8 px to its right), .addl (12 px semibold, mint), .val (a field with a
// hairline border, 6 px corners, padding 3 8, semibold text on the right, at least 80 wide), .learn (12 px bold on a field
// with a hairline border, padding 2 8, 6 px corners) and .learn.wait (ember on its tint, pulsing to 40% every 1.2 s).
constexpr int kBoxGap = 12, kHeaderTop = 10, kHeaderHeight = 18, kPadX = 12, kCellPad = 8;
constexpr int kHeadRow = 29, kDeviceRow = 42, kControlRow = 36;
constexpr int kChoiceHeight = 24, kButtonHeight = 28, kSwitchW = 34, kSwitchH = 18, kValueHeight = 24, kValueMin = 80;
constexpr int kLearnHeight = 20, kLearnPadX = 8, kClearSize = 16, kActionWidth = 240, kKeyWidth = 240, kMessageGap = 16;
constexpr double kMessageSeconds = 4.0, kPulseSeconds = 1.2;
constexpr int kMaxOffsetMs = 200;

enum Column { Name, Port, Channel, Clock, StartStop, Timing, Patches, Status, Test, ColumnCount };
const char* const kColumnTitles[ColumnCount] = {"NAME", "PORT", "CH", "CLOCK", "START/STOP", "TIMING", "PATCHES", "STATUS", ""};
// Each cell's width with its padding; NAME and PORT share what is left, PORT between 150 and 230, NAME at least 110.
constexpr int kFixed[ColumnCount] = {0, 0, 62, 64, 88, 112, 112, 104, 70};
constexpr int kNameMin = 110, kPortMin = 150, kPortMax = 230;

// The left edge of each column, and the right edge of the last, for a table that wide.
std::array<int, ColumnCount + 1> columnEdges(int width) {
    int fixed = 0;
    for (int c = Channel; c < ColumnCount; ++c) fixed += kFixed[c];
    const int rest = std::max(0, width - fixed);
    const int port = std::min(kPortMax, std::max(rest - kNameMin, std::min(kPortMin, rest / 2)));
    std::array<int, ColumnCount + 1> edges{};
    for (int c = 0; c < ColumnCount; ++c) edges[static_cast<std::size_t>(c) + 1] = edges[static_cast<std::size_t>(c)] + (c == Name ? rest - port : c == Port ? port : kFixed[c]);
    return edges;
}

juce::Font h4Font() { return theme::tracked(theme::font(theme::sizeCaption, true), 0.8f); }
juce::Font thFont() { return theme::tracked(theme::font(theme::sizeCaption, true), 0.6f); }
juce::Font tdFont() { return theme::font(theme::sizeBody); }
juce::Font nameFont() { return theme::font(theme::sizeBody, true); }
juce::Font linkFont() { return theme::font(theme::sizeSmall, true); }
juce::Font learnFont() { return theme::font(theme::sizeSmall, true); }
juce::Font hintFont() { return theme::font(theme::sizeSmall); }

const juce::String kNone = "None";
const juce::String kLearn = "Learn";
const juce::String kAddDevice = "+ Add a device";
const juce::String kClear = utf8("\xe2\x9c\x95");                             // ✕
const juce::String kStatusDot = utf8("\xe2\x97\x8f ");                        // "● "
juce::String pressAPedal() { return "Press a pedal" + kEllipsis; }
juce::String pressAKey() { return "Press a key" + kEllipsis; }
juce::String chooseAPort() { return "Choose a port" + kEllipsis; }
const juce::String kMomentary = "Set a latching switch to momentary.";
const juce::String kTapTwice = "tap twice";

// What a device's Colour menu offers: its five first colours (the mockup's --d1, --d2, --d3, then two more), then others
// that mean nothing else (never mint, ember, caution or danger).
struct NamedColour { const char* name; juce::Colour colour; };
const NamedColour kDeviceColours[] = {{"Teal", juce::Colour(0xff4fcabd)},  {"Pink", juce::Colour(0xffff7d93)},
                                      {"Blue", juce::Colour(0xff9fb4ff)},  {"Yellow", juce::Colour(0xffe9d568)},
                                      {"Lilac", juce::Colour(0xffb69dff)}, {"Green", juce::Colour(0xffa8dc68)},
                                      {"Sky", juce::Colour(0xff6cc8ff)},   {"Rose", juce::Colour(0xffe889c8)}};
constexpr int kColourCount = static_cast<int>(std::size(kDeviceColours));

float textWidth(const juce::Font& font, const juce::String& text) { return widthOf(font, text); }

// The mockup's .learn pill, or .learn.wait (ember, at that opacity) when waiting.
void drawLearnPill(juce::Graphics& g, juce::Rectangle<float> pill, const juce::String& text, bool waiting, float alpha) {
    const auto fill = waiting ? theme::ember.withAlpha(0.15f) : theme::field;
    const auto border = waiting ? theme::ember.withAlpha(0.4f) : theme::hairline;
    const auto ink = waiting ? theme::ember : theme::mid;
    g.setColour(fill.withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(pill, theme::radiusField);
    g.setColour(border.withMultipliedAlpha(alpha));
    g.drawRoundedRectangle(pill.reduced(0.5f), theme::radiusField, 1.0f);
    g.setColour(ink.withMultipliedAlpha(alpha));
    g.setFont(learnFont());
    g.drawText(text, pill, juce::Justification::centred, false);
}

int learnPillWidth(const juce::String& text) { return static_cast<int>(std::ceil(textWidth(learnFont(), text))) + 2 * kLearnPadX + 2; }

}  // namespace

// ---- The state ---------------------------------------------------------------------------------------------------------

MidiSetupState makeMidiSetupState(const app::Session& session, const app::ControlInput* input) {
    MidiSetupState state;
    const auto& model = session.model();
    const auto* rig = model.rig(session.rigId());
    const auto* ports = session.midi();
    if (rig != nullptr) state.rigName = rig->name;
    if (ports != nullptr) state.ports = ports->availableOutputs();
    const auto* here = session.thisComputer();

    for (const auto& doc : model.band().midiDevices) {
        MidiSetupState::Device device;
        device.id = doc.id;
        device.name = doc.name;
        device.colour = doc.colour;
        device.clock = doc.clock;
        device.startStop = doc.startStop;
        device.offsetMs = doc.offsetMs;
        juce::String port;
        if (here != nullptr)
            if (const auto found = here->midiPorts.find(doc.id); found != here->midiPorts.end()) port = found->second;
        device.hasPort = port.isNotEmpty();
        device.port = device.hasPort ? port : chooseAPort();
        int channel = 1;
        if (rig != nullptr)
            if (const auto found = rig->midiChannels.find(doc.id); found != rig->midiChannels.end()) channel = found->second;
        device.channel = juce::String(channel);
        device.timing = timingText(doc.offsetMs);
        device.patches = doc.patches.empty() ? juce::String("Name them") : juce::String(static_cast<int>(doc.patches.size())) + " named";
        // Without MidiPorts (tests, screenshots) no port is listed, so one that is chosen isn't found.
        const auto status = ports != nullptr ? ports->status(doc.id)
                                             : (device.hasPort ? app::MidiPorts::Status::NotFound : app::MidiPorts::Status::NoPort);
        device.status = status == app::MidiPorts::Status::Connected ? kStatusDot + "Connected"
                        : status == app::MidiPorts::Status::NotFound ? kStatusDot + "Not found"
                                                                       : juce::String("No port");
        device.statusLevel = status == app::MidiPorts::Status::Connected ? 0 : status == app::MidiPorts::Status::NotFound ? 1 : 2;
        device.canTest = ports != nullptr && status == app::MidiPorts::Status::Connected;
        state.devices.push_back(std::move(device));
    }

    const app::ControlMap map(model.band().controls);
    const auto learning = input != nullptr ? input->learning() : std::nullopt;
    for (const auto action : app::allActions()) {
        const auto binding = map.bindingFor(action);
        MidiSetupState::Control control;
        control.action = app::actionName(action);
        control.title = app::actionTitle(action);
        control.key = app::ControlMap::describeKey(binding);
        control.midi = app::ControlMap::describeMidi(binding);
        if (action == app::Action::Stop) {
            // A key on Stop, or else Start's, held for a second; Esc twice always. A pedal only when one is learned for
            // Stop, and then tapped twice: no pedal is ever held to stop.
            const auto startKey = app::ControlMap::describeKey(map.bindingFor(app::Action::Start));
            if (control.key.isNotEmpty() && startKey.isNotEmpty())
                control.keyNote = "Holding " + startKey + ", Start's key, for a second during a song stops it too.";
            const auto held = control.key.isNotEmpty() ? control.key : startKey;
            control.key = (held.isNotEmpty() ? "hold " + held + kDot : juce::String()) + "Esc Esc";
            if (control.midi.isNotEmpty()) control.hint = kTapTwice;
        }
        control.learning = learning.has_value() && *learning == action;
        if (control.learning) control.hint = kMomentary;
        state.controls.push_back(std::move(control));
    }
    return state;
}

// ---- The device rows ---------------------------------------------------------------------------------------------------

// The rows of the MIDI DEVICES table, in the page's viewport: each row's controls, its name, timing and status drawn.
class MidiSetupPage::DeviceList : public juce::Component {
public:
    explicit DeviceList(MidiSetupPage& page) : page_(page) {}

    void update() {
        const auto& devices = page_.state_.devices;
        while (rows_.size() < devices.size()) addRow(static_cast<int>(rows_.size()));
        while (rows_.size() > devices.size()) rows_.pop_back();
        juce::StringArray channels;
        for (int c = 1; c <= 16; ++c) channels.add(juce::String(c));
        const bool edit = page_.editable();
        for (std::size_t i = 0; i < devices.size(); ++i) {
            const auto& d = devices[i];
            auto& r = *rows_[i];
            auto ports = page_.state_.ports;
            if (d.hasPort && !ports.contains(d.port)) ports.add(d.port);   // chosen here, not plugged in now
            ports.add(kNone);
            r.port.setItems(ports, d.hasPort ? ports.indexOf(d.port) : -1);
            r.port.setNeedsAttention(!d.hasPort);
            r.channel.setItems(channels, d.channel.getIntValue() - 1);
            r.clock.setOn(d.clock);
            r.startStop.setOn(d.startStop);
            r.patches.setButtonText(d.patches);
            r.test.setEnabled(d.canTest);
            r.test.setTooltip(d.canTest ? "Sends " + d.name + "'s first patch" : d.name + " isn't connected");
            for (juce::Component* c : std::initializer_list<juce::Component*>{&r.port, &r.channel, &r.clock, &r.startStop, &r.patches})
                c->setEnabled(edit);
        }
        keepOffKeyboard(*this);
        layout();
        repaint();
    }

    juce::Rectangle<int> cell(int row, Column column) const {
        const auto edges = columnEdges(getWidth());
        return {edges[static_cast<std::size_t>(column)], row * kDeviceRow, edges[static_cast<std::size_t>(column) + 1] - edges[static_cast<std::size_t>(column)],
                kDeviceRow};
    }

    void resized() override { layout(); }

    void paint(juce::Graphics& g) override {
        const auto& devices = page_.state_.devices;
        for (int i = 0; i < static_cast<int>(devices.size()); ++i) {
            const auto& d = devices[static_cast<std::size_t>(i)];
            const int y = i * kDeviceRow;
            g.setColour(theme::rowLine);
            g.fillRect(0, y + kDeviceRow - 1, getWidth(), 1);

            // NAME: its colour bar, then its name.
            auto name = cell(i, Name).reduced(kCellPad, 0);
            g.setColour(d.colour);
            g.fillRoundedRectangle(juce::Rectangle<float>(static_cast<float>(name.getX()), static_cast<float>(y) + (kDeviceRow - 14) / 2.0f, 4.0f, 14.0f), 2.0f);
            name.removeFromLeft(std::min(12, name.getWidth()));
            g.setColour(theme::text);
            g.setFont(nameFont());
            g.drawText(shorten(d.name, nameFont(), static_cast<float>(name.getWidth())), name, juce::Justification::centredLeft, false);

            // TIMING: the value on a field, on the right of its box.
            const auto value = valueBox(i).toFloat();
            g.setColour(theme::field);
            g.fillRoundedRectangle(value, theme::radiusField);
            g.setColour(theme::hairline);
            g.drawRoundedRectangle(value.reduced(0.5f), theme::radiusField, 1.0f);
            g.setColour(theme::text);
            g.setFont(nameFont());
            g.drawText(d.timing, value.reduced(8.0f, 0.0f), juce::Justification::centredRight, false);

            // STATUS.
            const auto status = cell(i, Status).reduced(kCellPad, 0);
            g.setColour(d.statusLevel == 0 ? theme::mint : d.statusLevel == 1 ? theme::caution : theme::dim);
            g.setFont(tdFont());
            g.drawText(shorten(d.status, tdFont(), static_cast<float>(status.getWidth())), status, juce::Justification::centredLeft, false);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        drag_row_ = -1;
        const int row = rowAt(e.position);
        if (row < 0) return;
        const auto id = page_.state_.devices[static_cast<std::size_t>(row)].id;
        if (cell(row, Name).contains(e.getPosition())) {
            if (e.mods.isPopupMenu()) page_.deviceMenu(row);
            else if (page_.editable()) askRename(id);
            return;
        }
        if (!e.mods.isPopupMenu() && valueBox(row).expanded(4).contains(e.getPosition()) && page_.editable()) {
            drag_row_ = row;
            drag_from_ = page_.state_.devices[static_cast<std::size_t>(row)].offsetMs;
            gesture_ = juce::Uuid().toDashedString();
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (drag_row_ < 0) return;
        // Right or up is later, 1 ms a pixel.
        const double delta = e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY();
        page_.setTiming(drag_row_, drag_from_ + delta, gesture_);
    }

    void mouseUp(const juce::MouseEvent&) override { drag_row_ = -1; }

    void mouseDoubleClick(const juce::MouseEvent& e) override {
        const int row = rowAt(e.position);
        if (row >= 0 && !e.mods.isPopupMenu() && valueBox(row).expanded(4).contains(e.getPosition())) page_.setTiming(row, 0.0);
    }

    void mouseMove(const juce::MouseEvent& e) override {
        const int row = rowAt(e.position);
        const bool edit = page_.editable() && row >= 0;
        setMouseCursor(edit && valueBox(row).expanded(4).contains(e.getPosition()) ? juce::MouseCursor::LeftRightResizeCursor
                       : edit && cell(row, Name).contains(e.getPosition())       ? juce::MouseCursor::PointingHandCursor
                                                                                   : juce::MouseCursor::NormalCursor);
    }

private:
    struct Row {
        Choice port, channel;
        Switch clock, startStop;
        PillButton patches{juce::String()}, test{"Test"};
    };

    void addRow(int index) {
        auto row = std::make_unique<Row>();
        row->port.setPlaceholder(chooseAPort());
        row->port.onChange = [this, index](int chosen) {
            const auto& items = rows_[static_cast<std::size_t>(index)]->port.items();
            page_.choosePort(index, chosen == items.size() - 1 ? juce::String() : items[chosen]);   // the last is "None"
        };
        row->channel.onChange = [this, index](int chosen) { page_.chooseChannel(index, chosen + 1); };
        row->clock.onChange = [this, index](bool on) { page_.setClock(index, on); };
        row->startStop.onChange = [this, index](bool on) { page_.setStartStop(index, on); };
        row->patches.onClick = [this, index] { page_.openPatches(index); };
        row->test.onClick = [this, index] { page_.testDevice(index); };
        for (juce::Component* c : std::initializer_list<juce::Component*>{&row->port, &row->channel, &row->clock, &row->startStop,
                                                                          &row->patches, &row->test})
            addAndMakeVisible(c);
        rows_.push_back(std::move(row));
    }

    void layout() {
        for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
            auto& r = *rows_[static_cast<std::size_t>(i)];
            const auto inside = [&](Column column) { return cell(i, column).reduced(kCellPad, 0); };
            const auto centred = [&](Column column, int w, int h) {
                const auto c = inside(column);
                return juce::Rectangle<int>(c.getX(), c.getCentreY() - h / 2, std::min(w, c.getWidth()), h);
            };
            r.port.setBounds(centred(Port, inside(Port).getWidth(), kChoiceHeight));
            r.channel.setBounds(centred(Channel, inside(Channel).getWidth(), kChoiceHeight));
            r.clock.setBounds(centred(Clock, kSwitchW, kSwitchH));
            r.startStop.setBounds(centred(StartStop, kSwitchW, kSwitchH));
            r.patches.setBounds(centred(Patches, r.patches.idealWidth(), kButtonHeight));
            r.test.setBounds(centred(Test, r.test.idealWidth(), kButtonHeight));
        }
    }

    // The timing value's field: at least 80 wide, as wide as "200 ms early" needs.
    juce::Rectangle<int> valueBox(int row) const {
        const auto c = cell(row, Timing).reduced(kCellPad, 0);
        const int w = std::min(c.getWidth(), std::max(kValueMin, static_cast<int>(std::ceil(textWidth(nameFont(), "200 ms early"))) + 16));
        return {c.getX(), c.getCentreY() - kValueHeight / 2, w, kValueHeight};
    }

    int rowAt(juce::Point<float> at) const {
        const int row = static_cast<int>(std::floor(at.y / kDeviceRow));
        return at.y >= 0.0f && row < static_cast<int>(page_.state_.devices.size()) ? row : -1;
    }

    void askRename(const juce::String& id) {
        const int row = page_.rowOf(id);
        if (row < 0) return;
        askUserText(page_, "Rename the device", page_.state_.devices[static_cast<std::size_t>(row)].name, "Rename",
                    [page = &page_, id](const juce::String& name) {
                        if (const int now = page->rowOf(id); now >= 0) page->renameDevice(now, name);
                    });
    }

    MidiSetupPage& page_;
    std::vector<std::unique_ptr<Row>> rows_;
    int drag_row_ = -1;
    double drag_from_ = 0.0;
    juce::String gesture_;
};

// ---- The page ----------------------------------------------------------------------------------------------------------

MidiSetupPage::MidiSetupPage(app::Session& session, app::ControlInput* input)
    : session_(session), input_(input), mode_(session.mode()), list_(std::make_unique<DeviceList>(*this)) {
    viewport_.setViewedComponent(list_.get(), false);
    viewport_.setScrollBarsShown(true, false);
    viewport_.setScrollBarThickness(10);
    addAndMakeVisible(viewport_);
    keepOffKeyboard(*this);
    session_.changes().addChangeListener(this);
    refresh(true);
}

MidiSetupPage::~MidiSetupPage() {
    session_.changes().removeChangeListener(this);
    stopWaiting();
    patches_.reset();
    viewport_.setViewedComponent(nullptr, false);
}

bool MidiSetupPage::editable() const { return session_.mode() == app::Mode::Edit; }

void MidiSetupPage::changeListenerCallback(juce::ChangeBroadcaster*) { refresh(); }

void MidiSetupPage::visibilityChanged() {
    if (!isVisible()) {
        cancelGestures();
        return;
    }
    refresh(true);
}

void MidiSetupPage::refresh(bool force) {
    if (session_.mode() != mode_) {                               // waiting for a key or a pedal ends with Edit mode
        mode_ = session_.mode();
        stopWaiting();
    }
    // Hidden, it waits until it is shown: the session's changes come 30 times a second while a song plays.
    if (!force && !shownWithParents(*this)) {
        fresh_ = false;
        return;
    }
    fresh_ = true;
    auto next = makeMidiSetupState(session_, input_);
    const bool changed = !(next == state_);
    state_ = std::move(next);
    if (changed || force) {
        list_->update();
        resized();
        repaint();
    }
    updateTimer();
}

void MidiSetupPage::stopWaiting() {
    if (input_ != nullptr && input_->learning()) input_->cancelLearning();
    choosing_key_.reset();
    takeKeyboard(false);
}

void MidiSetupPage::cancelGestures() {
    stopWaiting();
    refresh(true);
}

bool MidiSetupPage::waiting() const {
    return choosing_key_.has_value() || std::any_of(state_.controls.begin(), state_.controls.end(), [](const auto& c) { return c.learning; });
}

void MidiSetupPage::updateTimer() {
    if (waiting() || message_.isNotEmpty()) {
        if (!isTimerRunning()) startTimerHz(30);
    } else {
        stopTimer();
        pulse_ = 1.0f;
    }
}

void MidiSetupPage::timerCallback() {
    const double now = app::wallClockSeconds();
    if (message_.isNotEmpty() && now >= message_until_) {
        message_.clear();
        repaint();
    }
    // Learning ends when the pedal is pressed (learned() refreshes), or elsewhere: what the rows say follows it.
    const auto learning = input_ != nullptr ? input_->learning() : std::nullopt;
    const bool shown = std::any_of(state_.controls.begin(), state_.controls.end(), [](const auto& c) { return c.learning; });
    if (learning.has_value() != shown) refresh(true);
    if (waiting()) {
        pulse_ = 0.7f + 0.3f * static_cast<float>(std::cos(juce::MathConstants<double>::twoPi * now / kPulseSeconds));
        repaint(controls_box_);
    }
    updateTimer();
}

void MidiSetupPage::takeKeyboard(bool take) {
    setWantsKeyboardFocus(take);
    if (take && isShowing()) grabKeyboardFocus();
    if (!take && hasKeyboardFocus(false)) giveAwayKeyboardFocus();
}

void MidiSetupPage::focusLost(FocusChangeType) {
    // Waiting for a key ends when the keyboard goes elsewhere; a pedal is listened for whichever window has it.
    if (choosing_key_) {
        choosing_key_.reset();
        setWantsKeyboardFocus(waiting());
        updateTimer();
        repaint();
    }
}

bool MidiSetupPage::run(const juce::String& command, const juce::var& params, bool inControls, const juce::String& coalesceKey) {
    if (!editable()) return false;
    const auto result = session_.commands().run(command, params, coalesceKey);
    if (!result.ok) say(result.message, inControls, true);
    else if (result.message.isNotEmpty()) say(result.message, inControls, false);
    refresh(true);                                                // a refused choice shows what is saved again
    return result.ok;
}

void MidiSetupPage::say(const juce::String& message, bool inControls, bool problem) {
    message_ = message;
    message_in_controls_ = inControls;
    message_problem_ = problem;
    message_until_ = app::wallClockSeconds() + kMessageSeconds;
    repaint();
    updateTimer();
}

const MidiSetupState::Device* MidiSetupPage::device(int row) const {
    return juce::isPositiveAndBelow(row, static_cast<int>(state_.devices.size())) ? &state_.devices[static_cast<std::size_t>(row)] : nullptr;
}

int MidiSetupPage::rowOf(const juce::String& deviceId) const {
    for (int i = 0; i < static_cast<int>(state_.devices.size()); ++i)
        if (state_.devices[static_cast<std::size_t>(i)].id == deviceId) return i;
    return -1;
}

std::optional<app::Action> MidiSetupPage::actionOf(int row) const {
    const auto actions = app::allActions();
    if (!juce::isPositiveAndBelow(row, static_cast<int>(actions.size()))) return std::nullopt;
    return actions[static_cast<std::size_t>(row)];
}

model::ControlBinding MidiSetupPage::bindingOf(app::Action action) const {
    return app::ControlMap(session_.model().band().controls).bindingFor(action);
}

// ---- The devices -------------------------------------------------------------------------------------------------------

void MidiSetupPage::addDevice(const juce::String& name) { run("device.add", jsonObject({{"name", name.trim()}}), false); }

void MidiSetupPage::renameDevice(int row, const juce::String& name) {
    if (const auto* d = device(row)) run("device.set", jsonObject({{"deviceId", d->id}, {"name", name.trim()}}), false);
}

void MidiSetupPage::setDeviceColour(int row, juce::Colour colour) {
    if (const auto* d = device(row)) run("device.set", jsonObject({{"deviceId", d->id}, {"colour", colour.toDisplayString(false)}}), false);
}

void MidiSetupPage::removeDevice(int row) {
    if (const auto* d = device(row)) run("device.remove", jsonObject({{"deviceId", d->id}}), false);
}

void MidiSetupPage::choosePort(int row, const juce::String& port) {
    const auto* d = device(row);
    if (d == nullptr) return;
    run("rig.setMidi",
        jsonObject({{"rigId", session_.rigId()}, {"deviceId", d->id}, {"computerId", session_.settings().computerName()}, {"port", port}}), false);
}

void MidiSetupPage::chooseChannel(int row, int channel) {
    if (const auto* d = device(row)) run("rig.setMidi", jsonObject({{"rigId", session_.rigId()}, {"deviceId", d->id}, {"channel", channel}}), false);
}

void MidiSetupPage::setClock(int row, bool on) {
    if (const auto* d = device(row)) run("device.set", jsonObject({{"deviceId", d->id}, {"clock", on}}), false);
}

void MidiSetupPage::setStartStop(int row, bool on) {
    if (const auto* d = device(row)) run("device.set", jsonObject({{"deviceId", d->id}, {"startStop", on}}), false);
}

void MidiSetupPage::setTiming(int row, double offsetMs, const juce::String& gestureId) {
    const auto* d = device(row);
    if (d == nullptr) return;
    const double ms = juce::jlimit(-static_cast<double>(kMaxOffsetMs), static_cast<double>(kMaxOffsetMs), std::round(offsetMs));
    if (ms == d->offsetMs) return;
    run("device.set", jsonObject({{"deviceId", d->id}, {"offsetMs", ms}}), false,
        gestureId.isEmpty() ? juce::String() : "timing " + d->id + " " + gestureId);
}

void MidiSetupPage::testDevice(int row) {
    const auto* d = device(row);
    auto* ports = session_.midi();
    if (d == nullptr || ports == nullptr || !d->canTest) return;
    model::MidiMessageDoc message;                                // its first patch, or program 0, on its channel
    for (const auto& doc : session_.model().band().midiDevices)
        if (doc.id == d->id && !doc.patches.empty()) {
            message.program = doc.patches.front().program;
            message.bankMsb = doc.patches.front().bankMsb;
            message.bankLsb = doc.patches.front().bankLsb;
        }
    ports->sendNow(d->id, {message});
}

void MidiSetupPage::openPatches(int row) {
    const auto* d = device(row);
    if (d == nullptr || !editable() || patches_ != nullptr) return;
    auto dialog = std::make_unique<PatchListDialog>(session_, d->id);
    auto* raw = dialog.get();
    // Often asked from inside the dialog's own button: hidden at once, deleted once that call has returned.
    raw->onClose = [safe = juce::Component::SafePointer<MidiSetupPage>(this)] {
        if (safe == nullptr || safe->patches_ == nullptr) return;
        safe->patches_->setVisible(false);
        juce::MessageManager::callAsync([safe] {
            if (safe != nullptr) safe->patches_.reset();
        });
    };
    auto veil = std::make_unique<Veil>(std::move(dialog), PatchListDialog::width, [raw] { return raw->idealHeight(); });
    auto* shell = findParentComponentOfClass<AppShell>();         // so the veil covers the top bar too
    veil->coverAndFollow(shell != nullptr ? static_cast<juce::Component&>(*shell) : *getTopLevelComponent());
    patches_ = std::move(veil);
    if (raw->text().isShowing()) raw->text().grabKeyboardFocus();
}

void MidiSetupPage::deviceMenu(int row) {
    const auto* d = device(row);
    if (d == nullptr) return;
    const bool edit = editable();
    juce::PopupMenu menu, colours;
    menu.addItem(1, "Rename" + kEllipsis, edit);
    for (int i = 0; i < kColourCount; ++i)
        colours.addColouredItem(10 + i, kDeviceColours[i].name, kDeviceColours[i].colour, edit, d->colour == kDeviceColours[i].colour);
    menu.addSubMenu("Colour", colours, edit);
    menu.addSeparator();
    menu.addItem(2, "Remove", edit);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(list_.get()).withMousePosition(),
                       [safe = juce::Component::SafePointer<MidiSetupPage>(this), id = d->id, name = d->name](int result) {
                           if (safe == nullptr) return;
                           const int now = safe->rowOf(id);
                           if (now < 0) return;
                           if (result == 1)
                               askUserText(*safe, "Rename the device", name, "Rename", [safe, id](const juce::String& newName) {
                                   if (safe != nullptr) safe->renameDevice(safe->rowOf(id), newName);
                               });
                           if (result == 2) safe->removeDevice(now);
                           if (result >= 10 && result < 10 + kColourCount) safe->setDeviceColour(now, kDeviceColours[result - 10].colour);
                       });
}

// ---- The controls ------------------------------------------------------------------------------------------------------

void MidiSetupPage::chooseKey(int row) {
    const auto action = actionOf(row);
    if (!action || !editable()) return;
    const bool again = choosing_key_ == action;
    stopWaiting();
    if (!again) {
        choosing_key_ = action;
        takeKeyboard(true);
    }
    refresh(true);
    repaint();
}

void MidiSetupPage::learn(int row) {
    const auto action = actionOf(row);
    if (!action || !editable() || input_ == nullptr) return;
    const bool again = input_->learning() == action;
    stopWaiting();
    if (!again) {
        input_->learn(*action, [safe = juce::Component::SafePointer<MidiSetupPage>(this), a = *action](model::ControlBinding binding) {
            if (safe != nullptr) safe->learned(a, binding);
        });
        takeKeyboard(true);                                       // for Esc
    }
    refresh(true);
}

void MidiSetupPage::learned(app::Action action, model::ControlBinding binding) {
    takeKeyboard(false);
    run("controls.set", jsonObject({{"action", app::actionName(action)}, {"binding", model::toVar(binding)}}), true);
    refresh(true);
}

void MidiSetupPage::clearPedal(int row) {
    const auto action = actionOf(row);
    if (!action || !editable()) return;
    auto binding = bindingOf(*action);
    if (binding.midiKind == model::MidiKind::None) return;
    binding.midiKind = model::MidiKind::None;
    binding.midiChannel = 0;
    binding.midiNumber = 0;
    run("controls.set", jsonObject({{"action", app::actionName(*action)}, {"binding", model::toVar(binding)}}), true);
}

bool MidiSetupPage::pressKey(const juce::KeyPress& key) {
    if (choosing_key_) {
        const auto action = *choosing_key_;
        stopWaiting();
        if (key.getKeyCode() != juce::KeyPress::escapeKey && key.getKeyCode() != 0) {
            auto binding = bindingOf(action);
            binding.keyCode = key.getKeyCode();
            binding.keyModifiers = key.getModifiers().getRawFlags() & juce::ModifierKeys::allKeyboardModifiers;
            run("controls.set", jsonObject({{"action", app::actionName(action)}, {"binding", model::toVar(binding)}}), true);
        }
        refresh(true);
        repaint();
        return true;
    }
    if (input_ != nullptr && input_->learning() && key.getKeyCode() == juce::KeyPress::escapeKey) {
        cancelGestures();
        return true;
    }
    return false;
}

bool MidiSetupPage::keyPressed(const juce::KeyPress& key) { return pressKey(key); }

// ---- Gestures on the page ----------------------------------------------------------------------------------------------

void MidiSetupPage::mouseDown(const juce::MouseEvent& e) {
    const auto at = e.getPosition();
    if (e.mods.isPopupMenu()) return;
    if (add_device_.contains(at)) {
        if (editable())
            askUserText(*this, "Add a device", {}, "Add", [this](const juce::String& name) {
                if (name.isNotEmpty()) addDevice(name);
            });
        return;
    }
    for (int row = 0; row < static_cast<int>(control_rows_.size()); ++row) {
        const auto& r = control_rows_[static_cast<std::size_t>(row)];
        if (r.clear.contains(at)) {
            clearPedal(row);
            return;
        }
        if (r.learn.contains(at)) {
            learn(row);
            return;
        }
        if (r.key.contains(at)) {
            chooseKey(row);
            return;
        }
    }
    if (choosing_key_) cancelGestures();                          // a click elsewhere stops waiting for a key
}

void MidiSetupPage::mouseMove(const juce::MouseEvent& e) {
    const auto at = e.getPosition();
    bool hot = add_device_.contains(at);
    for (const auto& r : control_rows_) hot = hot || r.clear.contains(at) || r.learn.contains(at) || r.key.contains(at);
    setMouseCursor(hot && editable() ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

juce::String MidiSetupPage::getTooltip() {
    const auto at = getMouseXYRelative();
    for (int row = 0; row < static_cast<int>(control_rows_.size()) && row < static_cast<int>(state_.controls.size()); ++row) {
        const auto& r = control_rows_[static_cast<std::size_t>(row)];
        const auto& c = state_.controls[static_cast<std::size_t>(row)];
        if (r.clear.contains(at)) return "Forget this pedal";
        if (r.learn.contains(at) && input_ == nullptr) return {};
        if (r.learn.contains(at))
            return c.learning ? "Press the pedal or pad now, or click again to stop."
                              : "Click, then press the pedal or pad. A latching switch or a toggling pad should be set to "
                                "momentary, or it acts on every other tap. A pedal that sends the same value when pressed and "
                                "when let go counts as two taps: set it to momentary, 127 when pressed and 0 when let go.";
        if (r.pedal.contains(at) && c.action == "stop")
            return "A pedal stops the song when tapped twice, 150 ms to 1 s apart. Only a key can be held to stop.";
        if (r.key.contains(at)) {
            const juce::String edit = editable() ? "Click, then press the key. Esc cancels." : "";
            return c.keyNote.isEmpty() ? edit : (edit + " " + c.keyNote).trim();
        }
    }
    return {};
}

juce::Rectangle<int> MidiSetupPage::keyCell(int row) const {
    return juce::isPositiveAndBelow(row, static_cast<int>(control_rows_.size())) ? control_rows_[static_cast<std::size_t>(row)].key : juce::Rectangle<int>();
}
juce::Rectangle<int> MidiSetupPage::learnButton(int row) const {
    return juce::isPositiveAndBelow(row, static_cast<int>(control_rows_.size())) ? control_rows_[static_cast<std::size_t>(row)].learn : juce::Rectangle<int>();
}
juce::Rectangle<int> MidiSetupPage::clearButton(int row) const {
    return juce::isPositiveAndBelow(row, static_cast<int>(control_rows_.size())) ? control_rows_[static_cast<std::size_t>(row)].clear : juce::Rectangle<int>();
}

// ---- Layout and drawing ------------------------------------------------------------------------------------------------

void MidiSetupPage::resized() {
    auto area = getLocalBounds();
    const int controls = static_cast<int>(state_.controls.size());
    const int controlsHeight = kHeaderTop + kHeaderHeight + kHeadRow + controls * kControlRow;
    const int devicesFixed = kHeaderTop + kHeaderHeight + kHeadRow;
    const int rowsWanted = static_cast<int>(state_.devices.size()) * kDeviceRow;
    // As many device rows as fit above the controls (at least two, when there are), the rest scrolled to.
    const int room = area.getHeight() - controlsHeight - kBoxGap - devicesFixed - 1;
    const int rowsHeight = std::min(rowsWanted, std::max(room, std::min(rowsWanted, 2 * kDeviceRow)));
    devices_box_ = area.removeFromTop(std::min(area.getHeight(), devicesFixed + rowsHeight + 1));   // and its border
    area.removeFromTop(std::min(kBoxGap, area.getHeight()));
    controls_box_ = area;

    auto devices = devices_box_;
    devices.removeFromTop(kHeaderTop);
    devices_header_ = devices.removeFromTop(kHeaderHeight).reduced(kPadX, 0);
    const int addWidth = static_cast<int>(std::ceil(textWidth(linkFont(), kAddDevice)));
    add_device_ = devices_header_.withLeft(devices_header_.getRight() - addWidth);
    devices_columns_ = devices.removeFromTop(kHeadRow);
    viewport_.setBounds(devices.withTrimmedBottom(1));
    const bool scrolls = rowsWanted > viewport_.getHeight();
    list_->setSize(std::max(0, viewport_.getWidth() - (scrolls ? viewport_.getScrollBarThickness() : 0)), rowsWanted);

    auto controlsArea = controls_box_;
    controlsArea.removeFromTop(kHeaderTop);
    controls_header_ = controlsArea.removeFromTop(kHeaderHeight).reduced(kPadX, 0);
    controls_columns_ = controlsArea.removeFromTop(kHeadRow);
    layoutControls();
    keepOffKeyboard(viewport_);
}

void MidiSetupPage::layoutControls() {
    control_rows_.clear();
    const int x = controls_box_.getX();
    const int width = controls_box_.getWidth();
    action_width_ = std::min(kActionWidth, width / 3);
    key_width_ = std::min(kKeyWidth, width / 3);
    for (int i = 0; i < static_cast<int>(state_.controls.size()); ++i) {
        const auto& c = state_.controls[static_cast<std::size_t>(i)];
        const int y = controls_columns_.getBottom() + i * kControlRow;
        ControlRow row;
        row.key = {x + action_width_, y, key_width_, kControlRow};
        row.pedal = {x + action_width_ + key_width_, y, std::max(0, width - action_width_ - key_width_), kControlRow};
        const int left = row.pedal.getX() + kCellPad, centreY = y + kControlRow / 2;
        if (c.learning || c.midi.isEmpty()) {
            const auto text = c.learning ? pressAPedal() : kLearn;
            row.learn = {left, centreY - kLearnHeight / 2, learnPillWidth(text), kLearnHeight};
        } else {
            int end = left + static_cast<int>(std::ceil(textWidth(tdFont(), c.midi)));
            if (c.hint.isNotEmpty()) end += static_cast<int>(std::ceil(textWidth(hintFont(), kDot + c.hint)));
            row.clear = {end + 8, centreY - kClearSize / 2, kClearSize, kClearSize};
        }
        if (!controls_box_.contains(row.key)) row = {};                 // no room for it: nothing there to click
        control_rows_.push_back(row);
    }
}

void MidiSetupPage::paint(juce::Graphics& g) {
    const auto drawHeader = [&](juce::Rectangle<int> header, const juce::String& title, bool controls, int rightEdge) {
        g.setColour(theme::dim);
        g.setFont(h4Font());
        g.drawText(title, header, juce::Justification::centredLeft, false);
        if (message_.isEmpty() || message_in_controls_ != controls) return;
        const int left = header.getX() + static_cast<int>(std::ceil(textWidth(h4Font(), title))) + kMessageGap;
        const auto line = juce::Rectangle<int>(left, header.getY(), std::max(0, rightEdge - kMessageGap - left), header.getHeight());
        g.setColour(message_problem_ ? theme::caution : theme::mid);
        g.setFont(hintFont());
        g.drawText(shorten(message_, hintFont(), static_cast<float>(line.getWidth())), line, juce::Justification::centredRight, false);
    };
    const auto drawColumns = [&](juce::Rectangle<int> row, const std::vector<std::pair<int, juce::String>>& columns) {
        g.setColour(theme::dim);
        g.setFont(thFont());
        for (std::size_t i = 0; i < columns.size(); ++i) {
            const int right = i + 1 < columns.size() ? columns[i + 1].first : row.getRight();
            g.drawText(columns[i].second, juce::Rectangle<int>(columns[i].first + kCellPad, row.getY(), std::max(0, right - columns[i].first - 2 * kCellPad), row.getHeight()),
                       juce::Justification::centredLeft, false);
        }
        g.setColour(theme::hairline);
        g.fillRect(row.getX(), row.getBottom() - 1, row.getWidth(), 1);
    };

    // MIDI DEVICES · <RIG>, "+ Add a device", and the column titles over the rows.
    theme::paintPanel(g, devices_box_.toFloat());
    drawHeader(devices_header_, ("MIDI DEVICES" + kDot + state_.rigName).toUpperCase(), false, add_device_.getX());
    g.setColour(theme::mint.withMultipliedAlpha(editable() ? 1.0f : 0.4f));
    g.setFont(linkFont());
    g.drawText(kAddDevice, add_device_, juce::Justification::centredRight, false);
    {
        const auto edges = columnEdges(list_->getWidth());
        std::vector<std::pair<int, juce::String>> columns;
        for (int c = 0; c < ColumnCount; ++c) columns.emplace_back(devices_columns_.getX() + edges[static_cast<std::size_t>(c)], kColumnTitles[c]);
        drawColumns(devices_columns_, columns);
    }

    // CONTROLS.
    theme::paintPanel(g, controls_box_.toFloat());
    drawHeader(controls_header_, "CONTROLS", true, controls_header_.getRight());
    drawColumns(controls_columns_, {{controls_box_.getX(), "ACTION"},
                                    {controls_box_.getX() + action_width_, "KEY"},
                                    {controls_box_.getX() + action_width_ + key_width_, "PEDAL OR PAD"}});
    paintControls(g);
}

void MidiSetupPage::paintControls(juce::Graphics& g) {
    juce::Graphics::ScopedSaveState clip(g);
    g.reduceClipRegion(controls_box_.reduced(1));
    const bool canLearn = editable() && input_ != nullptr;
    for (int i = 0; i < static_cast<int>(state_.controls.size()) && i < static_cast<int>(control_rows_.size()); ++i) {
        const auto& c = state_.controls[static_cast<std::size_t>(i)];
        const auto& r = control_rows_[static_cast<std::size_t>(i)];
        const auto rowArea = r.key.isEmpty() ? juce::Rectangle<int>() : juce::Rectangle<int>(controls_box_.getX(), r.key.getY(), controls_box_.getWidth(), kControlRow);
        if (rowArea.isEmpty()) continue;
        g.setColour(theme::rowLine);
        g.fillRect(rowArea.getX(), rowArea.getBottom() - 1, rowArea.getWidth(), 1);
        const float centreY = static_cast<float>(rowArea.getCentreY());

        // ACTION.
        g.setColour(theme::text);
        g.setFont(nameFont());
        g.drawText(shorten(c.title, nameFont(), static_cast<float>(action_width_ - 2 * kCellPad)),
                   juce::Rectangle<int>(rowArea.getX() + kCellPad, rowArea.getY(), action_width_ - 2 * kCellPad, kControlRow),
                   juce::Justification::centredLeft, false);

        // KEY: its keys as the mockup's kbd, "hold" and "·" as words; or "Press a key…" while one is awaited.
        float x = static_cast<float>(r.key.getX() + kCellPad);
        if (choosing_key_ && app::actionName(*choosing_key_) == c.action) {
            const auto text = pressAKey();
            drawLearnPill(g, {x, centreY - kLearnHeight / 2.0f, static_cast<float>(learnPillWidth(text)), static_cast<float>(kLearnHeight)}, text, true, pulse_);
        } else if (c.key.isEmpty()) {
            g.setColour(theme::dim);
            g.setFont(tdFont());
            g.drawText(utf8("\xe2\x80\x94"), juce::Rectangle<float>(x, centreY - 10.0f, 30.0f, 20.0f), juce::Justification::centredLeft, false);
        } else {
            const auto word = [&](const juce::String& text) {
                g.setColour(theme::mid);
                g.setFont(tdFont());
                const float w = textWidth(tdFont(), text);
                g.drawText(text, juce::Rectangle<float>(x, centreY - 10.0f, w + 1.0f, 20.0f), juce::Justification::centredLeft, false);
                x += w + 4.0f;
            };
            juce::StringArray parts;                              // Stop's "hold Space · Esc Esc" in its parts
            for (auto rest = c.key;;) {
                const int at = c.action == "stop" ? rest.indexOf(kDot) : -1;
                if (at < 0) {
                    parts.add(rest);
                    break;
                }
                parts.add(rest.substring(0, at));
                rest = rest.substring(at + kDot.length());
            }
            for (int p = 0; p < parts.size(); ++p) {
                if (p > 0) word(kMiddleDot);
                auto part = parts[p].trim();
                if (c.action == "stop" && part.startsWith("hold ")) {
                    word("hold");
                    part = part.fromFirstOccurrenceOf("hold ", false, false);
                }
                if (c.action == "stop" && part == "Esc Esc") {
                    x += drawKey(g, "Esc", x, centreY) + 3.0f;
                    x += drawKey(g, "Esc", x, centreY) + 4.0f;
                } else {
                    x += drawKey(g, part, x, centreY) + 4.0f;
                }
            }
        }

        // PEDAL OR PAD: the learned control with ✕; "Learn"; or "Press a pedal…", pulsing, with the tip on momentary switches.
        const float left = static_cast<float>(r.pedal.getX() + kCellPad);
        if (!r.learn.isEmpty()) {
            const auto text = c.learning ? pressAPedal() : kLearn;
            drawLearnPill(g, r.learn.toFloat(), text, c.learning, c.learning ? pulse_ : (canLearn ? 1.0f : 0.4f));
            if (c.hint.isNotEmpty()) {
                g.setColour(theme::dim);
                g.setFont(hintFont());
                const float hx = static_cast<float>(r.learn.getRight() + kCellPad);
                g.drawText(shorten(c.hint, hintFont(), static_cast<float>(r.pedal.getRight()) - hx - kCellPad),
                           juce::Rectangle<float>(hx, centreY - 10.0f, static_cast<float>(r.pedal.getRight()) - hx - kCellPad, 20.0f),
                           juce::Justification::centredLeft, false);
            }
        } else {
            const float w = textWidth(tdFont(), c.midi);
            g.setColour(theme::mid);
            g.setFont(tdFont());
            g.drawText(c.midi, juce::Rectangle<float>(left, centreY - 10.0f, w + 1.0f, 20.0f), juce::Justification::centredLeft, false);
            if (c.hint.isNotEmpty()) {
                g.setColour(theme::dim);
                g.setFont(hintFont());
                g.drawText(kDot + c.hint, juce::Rectangle<float>(left + w, centreY - 10.0f, textWidth(hintFont(), kDot + c.hint) + 1.0f, 20.0f),
                           juce::Justification::centredLeft, false);
            }
            g.setColour(theme::dim.withMultipliedAlpha(editable() ? 1.0f : 0.4f));
            g.setFont(hintFont());
            g.drawText(kClear, r.clear.toFloat(), juce::Justification::centred, false);
        }
    }
}

}  // namespace sqlive::ui
