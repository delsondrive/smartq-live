#include "ui/NewComputerPage.h"

#include <algorithm>

#include "ui/Logo.h"
#include "ui/Theme.h"

namespace sqlive::ui {

namespace {

constexpr int kPadX = 22, kPadY = 18, kBoxTop = kPadY + 96, kMaxWidth = 760, kRowHeight = 28, kButtonHeight = 30, kBoxPad = 12;

const juce::String kEllipsis = juce::String::fromUTF8("\xe2\x80\xa6");     // …

juce::var rigDeviceParams(const juce::String& rigId, const juce::String& computer, const juce::String& type, const juce::String& name) {
    auto* object = new juce::DynamicObject();
    object->setProperty("rigId", rigId);
    object->setProperty("computerId", computer);
    object->setProperty("deviceType", type);
    object->setProperty("deviceName", name);
    return juce::var(object);
}

}  // namespace

NewComputerState makeNewComputerState(const app::Session& session, app::AudioHost& host, const juce::String& rigId,
                                      const juce::String& type) {
    NewComputerState state;
    state.computer = session.settings().computerName();
    const auto& model = session.model();
    const auto wanted = rigId.isNotEmpty() ? rigId : session.rigId();
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

NewComputerPage::NewComputerPage(app::Session& session, app::AudioHost& host)
    : session_(session), host_(host), tone_("Test tone"), done_("Done", PillButton::Style::Primary) {
    rigId_ = session.rigId();
    for (auto* child : std::initializer_list<juce::Component*>{&rig_, &type_, &device_, &tone_, &done_}) addAndMakeVisible(*child);

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
    rebuild({});
}

NewComputerPage::~NewComputerPage() { host_.setTestTone(-1); }

void NewComputerPage::rebuild(const juce::String& type) {
    rigIds_ = session_.model().rigIds();
    state_ = makeNewComputerState(session_, host_, rigId_, type);
    rig_.setItems(state_.rigs, state_.rig);
    type_.setItems(state_.types, state_.type);
    device_.setItems(state_.devices, state_.device);
    device_.setNeedsAttention(state_.device < 0);
    done_.setEnabled(state_.canFinish);
    tone_.setEnabled(state_.canFinish);
    resized();
    repaint();
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
    session_.chooseRig(rigId_);
    const auto opened = host_.openFromRig();
    if (opened.failed()) {
        say(opened.getErrorMessage());
        return;
    }
    say({});
    if (onDone) onDone();
}

void NewComputerPage::say(const juce::String& problem) {
    problem_ = problem;
    repaint();
}

juce::String NewComputerPage::rigHint() const {
    const auto* rig = session_.model().rig(rigId_);
    if (rig == nullptr) return {};
    const int n = static_cast<int>(rig->outputs.size());
    juce::StringArray names;
    for (int i = 0; i < std::min(n, 3); ++i) names.add(rig->outputs[static_cast<std::size_t>(i)].name);
    return juce::String(n) + (n == 1 ? " output: " : " outputs: ") + names.joinIntoString(", ") + (n > 3 ? kEllipsis : juce::String());
}

void NewComputerPage::resized() {
    const int w = std::min(getWidth() - 2 * kPadX, kMaxWidth);
    const int x = kPadX;
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
}

void NewComputerPage::paint(juce::Graphics& g) {
    theme::paintAppBackground(g, getLocalBounds());
    const int w = std::min(getWidth() - 2 * kPadX, kMaxWidth);

    drawLogo(g, juce::Rectangle<float>(static_cast<float>(kPadX), static_cast<float>(kPadY), 300.0f, 26.0f), true);
    g.setColour(theme::dim);
    g.setFont(theme::font(theme::sizeSmall));
    g.drawText(session_.model().band().name, kPadX, kPadY, w, 26, juce::Justification::centredRight, true);

    int y = kPadY + 26 + 14;
    g.setColour(theme::text);
    g.setFont(theme::font(19.0f, true));
    g.drawText("Set up this computer", kPadX, y, w, 26, juce::Justification::centredLeft, true);
    y += 26;

    juce::AttributedString sentence;
    sentence.setJustification(juce::Justification::topLeft);
    sentence.append("This band folder hasn't been used on ", theme::font(theme::sizeBody), theme::dim);
    sentence.append(state_.computer, theme::font(theme::sizeBody, true), theme::mid);
    sentence.append(" before. Tell it what's plugged in here.", theme::font(theme::sizeBody), theme::dim);
    sentence.draw(g, juce::Rectangle<float>(static_cast<float>(kPadX), static_cast<float>(y), static_cast<float>(w), 36.0f));

    const int boxTop = kBoxTop;
    const int boxHeight = kBoxPad + kRowHeight + 12 + kRowHeight + 10 + 34 + kBoxPad;
    theme::paintPanel(g, juce::Rectangle<float>(static_cast<float>(kPadX), static_cast<float>(boxTop), static_cast<float>(w),
                                                static_cast<float>(boxHeight)));

    g.setColour(theme::mid);
    g.setFont(theme::font(theme::sizeBody));
    g.drawText("Rig", kPadX + kBoxPad, rig_.getY(), 160, kRowHeight, juce::Justification::centredLeft, true);
    g.drawText("Audio interface", kPadX + kBoxPad, device_.getY(), 200, kRowHeight, juce::Justification::centredLeft, true);

    g.setColour(theme::dim);
    g.setFont(theme::font(theme::sizeSmall));
    g.drawFittedText(rigHint(), kPadX + kBoxPad, device_.getBottom() + 10, w - 2 * kBoxPad, 34, juce::Justification::topLeft, 2);

    if (problem_.isNotEmpty()) {
        g.setColour(theme::danger);
        g.setFont(theme::font(theme::sizeSmall));
        g.drawFittedText(problem_, kPadX, boxTop + boxHeight + 10, w, 34, juce::Justification::topLeft, 2);
    }
}

}  // namespace sqlive::ui
