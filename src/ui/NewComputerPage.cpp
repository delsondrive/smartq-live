#include "ui/NewComputerPage.h"

#include <algorithm>

#include "app/MidiPorts.h"
#include "ui/Format.h"
#include "ui/Helpers.h"
#include "ui/Logo.h"
#include "ui/Theme.h"

namespace sqlive::ui {

namespace {

constexpr int kPadX = 22, kPadY = 18, kBoxTop = kPadY + 96, kMaxWidth = 760, kRowHeight = 28, kButtonHeight = 30, kBoxPad = 12;
// The MIDI table, as the mockup's table CSS: th 29 px high, rows 36, each cell padded 8 either side.
constexpr int kHeadRow = 29, kMidiRow = 36, kCellPad = 8, kChoiceHeight = 24, kStatusWidth = 110, kBoxGap = 12;

const juce::String kNotHere = "Not on this computer";


juce::var rigDeviceParams(const juce::String& rigId, const juce::String& computer, const juce::String& type, const juce::String& name) {
    auto* object = new juce::DynamicObject();
    object->setProperty("rigId", rigId);
    object->setProperty("computerId", computer);
    object->setProperty("deviceType", type);
    object->setProperty("deviceName", name);
    return juce::var(object);
}

// The MIDI table's rows: the band's devices, each with the port chosen on the page, else the rig's for this computer.
std::vector<NewComputerState::MidiRow> midiRowsFor(const app::Session& session, const juce::String& rigId,
                                                   const std::map<juce::String, juce::String>& chosen) {
    std::vector<NewComputerState::MidiRow> rows;
    const auto* rig = session.model().rig(rigId);
    const model::ComputerBinding* here = nullptr;
    if (rig != nullptr)
        if (const auto found = rig->computers.find(session.settings().computerName()); found != rig->computers.end()) here = &found->second;
    const auto outputs = session.midi() != nullptr ? session.midi()->availableOutputs() : juce::StringArray();
    for (const auto& device : session.model().band().midiDevices) {
        NewComputerState::MidiRow row;
        row.deviceId = device.id;
        row.name = device.name;
        row.colour = device.colour;
        row.ports = outputs;
        row.ports.add(kNotHere);
        juce::String port;
        const auto picked = chosen.find(device.id);
        if (picked != chosen.end()) port = picked->second;
        else if (here != nullptr)
            if (const auto found = here->midiPorts.find(device.id); found != here->midiPorts.end()) port = found->second;
        if (picked != chosen.end() && port.isEmpty()) {
            row.port = row.ports.size() - 1;
            row.status = "skipped";
        } else if (port.isNotEmpty() && outputs.contains(port)) {
            row.port = outputs.indexOf(port);
            row.status = juce::String::fromUTF8("\xe2\x9c\x93 found");
        } else {
            row.status = "needs a port";
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

}  // namespace


NewComputerState makeNewComputerState(const app::Session& session, app::AudioHost& host, const juce::String& rigId,
                                      const juce::String& type, const std::map<juce::String, juce::String>& midiPorts) {
    NewComputerState state;
    state.computer = session.settings().computerName();
    const auto& model = session.model();
    const auto wanted = rigId.isNotEmpty() ? rigId : session.rigId();
    state.midi = midiRowsFor(session, wanted, midiPorts);
    int at = 0;
    for (const auto& id : model.rigIds()) {
        if (id == wanted) state.rig = at;
        state.rigs.add(model.rig(id)->name);
        ++at;
    }

    const model::ComputerBinding* binding = nullptr;
    if (const auto* rig = model.rig(wanted)) {
        const auto found = rig->computers.find(state.computer);
        if (found != rig->computers.end() && found->second.deviceName.isNotEmpty()) binding = &found->second;
    }

    state.types = host.deviceTypes();
    const auto typeName = type.isNotEmpty() ? type : (binding != nullptr ? binding->deviceType : juce::String());
    state.type = std::max(0, state.types.indexOf(typeName));
    if (state.types.isEmpty()) return state;
    state.devices = host.devicesOfType(state.types[state.type]);
    if (binding != nullptr && binding->deviceType == state.types[state.type]) state.device = state.devices.indexOf(binding->deviceName);
    state.canFinish = state.device >= 0;
    return state;
}

namespace {
constexpr int kBoxHeight = kBoxPad + kRowHeight + 12 + kRowHeight + 10 + 34 + kBoxPad;
constexpr int kProblemHeight = 34;
}  // namespace

// ---- The MIDI table's rows ---------------------------------------------------------------------------------------------

// The rows under the MIDI table's column titles, in the page's viewport: each device's colour bar and name, its port and
// what that means.
class NewComputerPage::MidiTable : public juce::Component {
public:
    explicit MidiTable(NewComputerPage& page) : page_(page) {}

    void update() {
        const auto& rows = page_.state_.midi;
        while (choices_.size() < rows.size()) {
            const int index = static_cast<int>(choices_.size());
            auto choice = std::make_unique<Choice>();
            choice->setPlaceholder("Choose a port" + kEllipsis);
            choice->onChange = [this, index](int chosen) { page_.chooseMidiPort(index, chosen); };
            addAndMakeVisible(*choice);
            choices_.push_back(std::move(choice));
        }
        while (choices_.size() > rows.size()) choices_.pop_back();
        for (std::size_t i = 0; i < rows.size(); ++i) {
            choices_[i]->setItems(rows[i].ports, rows[i].port);
            choices_[i]->setNeedsAttention(rows[i].port < 0);
        }
        resized();
        repaint();
    }

    void resized() override {
        for (int i = 0; i < static_cast<int>(choices_.size()); ++i) {
            const auto cell = juce::Rectangle<int>(nameWidth(), i * kMidiRow, portWidth(), kMidiRow).reduced(kCellPad, 0);
            const int w = std::min(cell.getWidth(), std::max(160, choices_[static_cast<std::size_t>(i)]->idealWidth()));
            choices_[static_cast<std::size_t>(i)]->setBounds(cell.getX(), cell.getCentreY() - kChoiceHeight / 2, w, kChoiceHeight);
        }
    }

    void paint(juce::Graphics& g) override {
        const auto& rows = page_.state_.midi;
        const auto nameFont = theme::font(theme::sizeBody, true);
        for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
            const auto& row = rows[static_cast<std::size_t>(i)];
            const int y = i * kMidiRow;
            g.setColour(theme::rowLine);
            g.fillRect(0, y + kMidiRow - 1, getWidth(), 1);
            auto name = juce::Rectangle<int>(kCellPad, y, std::max(0, nameWidth() - 2 * kCellPad), kMidiRow);
            g.setColour(row.colour);
            g.fillRoundedRectangle(juce::Rectangle<float>(static_cast<float>(name.getX()), static_cast<float>(y) + (kMidiRow - 14) / 2.0f, 4.0f, 14.0f),
                                   2.0f);
            name.removeFromLeft(std::min(12, name.getWidth()));
            g.setColour(theme::text);
            g.setFont(nameFont);
            g.drawText(shorten(row.name, nameFont, static_cast<float>(name.getWidth())), name, juce::Justification::centredLeft, false);
            // "✓ found" in mint, "needs a port" in caution, "skipped" as a hint.
            const auto status = juce::Rectangle<int>(nameWidth() + portWidth() + kCellPad, y, kStatusWidth - 2 * kCellPad, kMidiRow);
            const bool found = row.status.endsWith("found"), skipped = row.status == "skipped";
            g.setColour(found ? theme::mint : skipped ? theme::dim : theme::caution);
            g.setFont(theme::font(skipped ? theme::sizeSmall : theme::sizeBody));
            g.drawText(row.status, status, juce::Justification::centredLeft, false);
        }
    }

    int nameWidth() const { return std::max(120, (getWidth() - kStatusWidth) * 2 / 5); }
    int portWidth() const { return std::max(0, getWidth() - kStatusWidth - nameWidth()); }

private:
    NewComputerPage& page_;
    std::vector<std::unique_ptr<Choice>> choices_;
};

// ---- The page ----------------------------------------------------------------------------------------------------------

NewComputerPage::NewComputerPage(app::Session& session, app::AudioHost& host)
    : session_(session), host_(host), tone_("Test tone"), done_("Done", PillButton::Style::Primary),
      midiTable_(std::make_unique<MidiTable>(*this)) {
    rigId_ = session.rigId();
    for (auto* child : std::initializer_list<juce::Component*>{&rig_, &type_, &device_, &tone_, &done_}) addAndMakeVisible(*child);
    midiView_.setViewedComponent(midiTable_.get(), false);
    midiView_.setScrollBarsShown(true, false);
    midiView_.setScrollBarThickness(10);
    addChildComponent(midiView_);                                 // shown when the band has MIDI devices
    device_.setPlaceholder("Choose an interface" + kEllipsis);

    rig_.onChange = [this](int index) {
        if (!juce::isPositiveAndBelow(index, static_cast<int>(rigIds_.size()))) return;
        rigId_ = rigIds_[static_cast<std::size_t>(index)];
        say({});
        rebuild({});
    };
    type_.onChange = [this](int index) {
        say({});
        rebuild(state_.types[index]);
    };
    device_.onChange = [this](int index) { chooseDevice(index); };
    tone_.onStateChange = [this] {
        const auto* rig = session_.model().rig(rigId_);
        const int channel = rig != nullptr && !rig->outputs.empty() ? std::max(0, rig->outputs.front().firstChannel - 1) : 0;
        host_.setTestTone(tone_.isDown() ? channel : -1);
    };
    done_.onClick = [this] { finish(); };
    session_.changes().addChangeListener(this);
    rebuild({});
}

NewComputerPage::~NewComputerPage() {
    session_.changes().removeChangeListener(this);
    host_.setTestTone(-1);
    midiView_.setViewedComponent(nullptr, false);
}

// A port plugged in or out (MidiPorts fires the session's changes), or the band's devices changed.
void NewComputerPage::changeListenerCallback(juce::ChangeBroadcaster*) { rebuildMidi(); }

void NewComputerPage::rebuild(const juce::String& type) {
    rigIds_ = session_.model().rigIds();
    state_ = makeNewComputerState(session_, host_, rigId_, type, midiPorts_);
    rig_.setItems(state_.rigs, state_.rig);
    type_.setItems(state_.types, state_.type);
    device_.setItems(state_.devices, state_.device);
    device_.setNeedsAttention(state_.device < 0);
    done_.setEnabled(state_.canFinish);
    tone_.setEnabled(state_.canFinish);
    midiTable_->update();
    resized();
    repaint();
}

void NewComputerPage::rebuildMidi() {
    auto rows = midiRowsFor(session_, rigId_, midiPorts_);
    if (rows == state_.midi) return;
    state_.midi = std::move(rows);
    midiTable_->update();
    resized();
    repaint();
}

void NewComputerPage::chooseMidiPort(int row, int index) {
    if (!juce::isPositiveAndBelow(row, static_cast<int>(state_.midi.size()))) return;
    const auto& r = state_.midi[static_cast<std::size_t>(row)];
    if (!juce::isPositiveAndBelow(index, r.ports.size())) return;
    midiPorts_[r.deviceId] = index == r.ports.size() - 1 ? juce::String() : r.ports[index];   // the last: not on this computer
    rebuildMidi();
}

void NewComputerPage::chooseDevice(int index) {
    if (!juce::isPositiveAndBelow(index, state_.devices.size())) return;
    state_.device = index;
    state_.canFinish = true;
    device_.setNeedsAttention(false);
    done_.setEnabled(true);
    tone_.setEnabled(true);
    const auto* rig = session_.model().rig(rigId_);
    const auto opened = host_.open({state_.types[state_.type], state_.devices[index], rig != nullptr ? rig->sampleRate : 48000.0,
                                    rig != nullptr ? rig->bufferSize : 512});
    say(opened.failed() ? opened.getErrorMessage() : juce::String());
}

void NewComputerPage::finish() {
    if (!state_.canFinish) return;
    const auto result = session_.commands().run(
        "rig.setDevice", rigDeviceParams(rigId_, state_.computer, state_.types[state_.type], state_.devices[state_.device]));
    if (!result.ok) {
        say(result.message);
        return;
    }
    if (!saveMidiPorts()) return;
    session_.chooseRig(rigId_);
    const auto opened = host_.openFromRig();
    if (opened.failed()) {
        say(opened.getErrorMessage());
        return;
    }
    say({});
    if (onDone) onDone();
}

// Each MIDI port chosen here, for this computer in the rig ("" for a device skipped: not on this computer); a device removed
// meanwhile is left out.
bool NewComputerPage::saveMidiPorts() {
    const auto& devices = session_.model().band().midiDevices;
    for (const auto& [deviceId, port] : midiPorts_) {
        const auto& id = deviceId;
        if (std::none_of(devices.begin(), devices.end(), [&id](const model::MidiDeviceDoc& d) { return d.id == id; })) continue;
        auto* object = new juce::DynamicObject();
        object->setProperty("rigId", rigId_);
        object->setProperty("deviceId", deviceId);
        object->setProperty("computerId", state_.computer);
        object->setProperty("port", port);
        const auto saved = session_.commands().run("rig.setMidi", juce::var(object));
        if (!saved.ok) {
            say(saved.message);
            return false;
        }
    }
    return true;
}

void NewComputerPage::say(const juce::String& problem) {
    if (problem == problem_) return;
    problem_ = problem;
    resized();                                                    // the MIDI table moves down under it
    repaint();
}

// The page is one column, at most 760 px wide, centred in a wider window.
int NewComputerPage::columnWidth() const { return std::min(getWidth() - 2 * kPadX, kMaxWidth); }
int NewComputerPage::columnX() const { return (getWidth() - columnWidth()) / 2; }

juce::String NewComputerPage::rigHint() const {
    const auto* rig = session_.model().rig(rigId_);
    if (rig == nullptr) return {};
    const int n = static_cast<int>(rig->outputs.size());
    juce::StringArray names;
    for (int i = 0; i < std::min(n, 3); ++i) names.add(rig->outputs[static_cast<std::size_t>(i)].name);
    return juce::String(n) + (n == 1 ? " output: " : " outputs: ") + names.joinIntoString(", ") + (n > 3 ? kEllipsis : juce::String());
}

void NewComputerPage::resized() {
    const int w = columnWidth(), x = columnX();
    const int top = kBoxTop + kBoxPad;
    const int right = x + w - kBoxPad;
    auto place = [&](Choice& choice, int rightEdge, int y, int minWidth) {
        const int cw = std::max(minWidth, choice.idealWidth());
        choice.setBounds(rightEdge - cw, y, cw, kRowHeight);
        return rightEdge - cw;
    };
    place(rig_, right, top, 160);
    const int y2 = top + kRowHeight + 12;
    const int left = place(device_, right, y2, 220);
    place(type_, left - 8, y2, 140);
    const int buttonsY = getHeight() - kPadY - kButtonHeight;
    const int doneW = done_.idealWidth(), toneW = tone_.idealWidth();
    done_.setBounds(x + w - doneW, buttonsY, doneW, kButtonHeight);
    tone_.setBounds(done_.getX() - 10 - toneW, buttonsY, toneW, kButtonHeight);

    // The MIDI table: under the box (and the problem, when there is one), its rows scrolling above the buttons.
    const int midiTop = kBoxTop + kBoxHeight + kBoxGap + (problem_.isNotEmpty() ? kProblemHeight : 0);
    const int rows = static_cast<int>(state_.midi.size());
    const int room = std::max(kMidiRow, buttonsY - kBoxGap - midiTop - kHeadRow);
    midiBox_ = {x, midiTop, w, kHeadRow + std::max(kMidiRow, std::min(rows * kMidiRow, room))};
    midiColumns_ = midiBox_.withHeight(kHeadRow);
    midiView_.setBounds(midiBox_.withTrimmedTop(kHeadRow).withTrimmedBottom(1).reduced(1, 0));
    midiView_.setVisible(rows > 0);
    const bool scrolls = rows * kMidiRow > midiView_.getHeight();
    midiTable_->setSize(std::max(0, midiView_.getWidth() - (scrolls ? midiView_.getScrollBarThickness() : 0)), rows * kMidiRow);
}

void NewComputerPage::paint(juce::Graphics& g) {
    theme::paintAppBackground(g, getLocalBounds());
    const int w = columnWidth(), x = columnX();

    drawLogo(g, juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(kPadY), 300.0f, 26.0f), true);
    g.setColour(theme::dim);
    g.setFont(theme::font(theme::sizeSmall));
    g.drawText(session_.model().band().name, x, kPadY, w, 26, juce::Justification::centredRight, true);

    int y = kPadY + 26 + 14;
    g.setColour(theme::text);
    g.setFont(theme::font(19.0f, true));
    g.drawText("Set up this computer", x, y, w, 26, juce::Justification::centredLeft, true);
    y += 26;

    juce::AttributedString sentence;
    sentence.setJustification(juce::Justification::topLeft);
    sentence.append("This band folder hasn't been used on ", theme::font(theme::sizeBody), theme::dim);
    sentence.append(state_.computer, theme::font(theme::sizeBody, true), theme::mid);
    sentence.append(" before. Tell it what's plugged in here.", theme::font(theme::sizeBody), theme::dim);
    sentence.draw(g, juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), 36.0f));

    const int boxTop = kBoxTop;
    theme::paintPanel(g, juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(boxTop), static_cast<float>(w),
                                                static_cast<float>(kBoxHeight)));

    g.setColour(theme::mid);
    g.setFont(theme::font(theme::sizeBody));
    g.drawText("Rig", x + kBoxPad, rig_.getY(), 160, kRowHeight, juce::Justification::centredLeft, true);
    g.drawText("Audio interface", x + kBoxPad, device_.getY(), 200, kRowHeight, juce::Justification::centredLeft, true);

    g.setColour(theme::dim);
    g.setFont(theme::font(theme::sizeSmall));
    g.drawFittedText(rigHint(), x + kBoxPad, device_.getBottom() + 10, w - 2 * kBoxPad, 34, juce::Justification::topLeft, 2);

    if (problem_.isNotEmpty()) {
        g.setColour(theme::danger);
        g.setFont(theme::font(theme::sizeSmall));
        g.drawFittedText(problem_, x, boxTop + kBoxHeight + 10, w, kProblemHeight, juce::Justification::topLeft, 2);
    }

    // The MIDI table (the mockup's): MIDI DEVICE, PORT ON THIS COMPUTER, and a column for what that means.
    if (!state_.midi.empty()) {
        theme::paintPanel(g, midiBox_.toFloat());
        g.setColour(theme::dim);
        g.setFont(theme::tracked(theme::font(theme::sizeCaption, true), 0.6f));
        const int left = midiView_.getX();
        g.drawText("MIDI DEVICE", left + kCellPad, midiColumns_.getY(), std::max(0, midiTable_->nameWidth() - 2 * kCellPad), kHeadRow,
                   juce::Justification::centredLeft, false);
        g.drawText("PORT ON THIS COMPUTER", left + midiTable_->nameWidth() + kCellPad, midiColumns_.getY(),
                   std::max(0, midiTable_->portWidth() - 2 * kCellPad), kHeadRow, juce::Justification::centredLeft, false);
        g.setColour(theme::hairline);
        g.fillRect(midiColumns_.getX(), midiColumns_.getBottom() - 1, midiColumns_.getWidth(), 1);
    }
}

}  // namespace sqlive::ui
