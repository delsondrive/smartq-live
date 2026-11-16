#include "ui/SetupPage.h"

#include <algorithm>
#include <cmath>
#include <iterator>

#include "app/AudioHost.h"
#include "model/JsonObject.h"
#include "ui/Format.h"
#include "ui/Helpers.h"
#include "ui/Theme.h"

namespace sqlive::ui {

using model::jsonObject;

namespace {

// The mockup's CSS: .content (padding 14 16, gap 14), .subtabs (190 wide; an item padding 8 12, 13 px semibold with an
// 11 px line under it, 8 px corners, the chosen one on a field with a 3 px mint bar inside its left edge), .col (gap 12),
// .box (padding 10 12; here a row with a gap of 18), h4 (11 px bold, letter-spaced 0.8, dim, 6 px under it).
constexpr int kPadX = 16, kPadY = 14, kGap = 14, kColumnGap = 12;
constexpr int kSubTabsWidth = 190, kSubTabHeight = 47, kSubTabPadX = 12, kSubTabPadY = 8;
constexpr int kBoxPadX = 12, kBoxPadY = 10, kGroupGap = 18, kCaptionHeight = 15, kCaptionGap = 6, kControlHeight = 28;
constexpr int kPairGap = 6, kButtonGap = 8, kPillHeight = 22, kProblemHeight = 18, kMaxProblemLines = 4;

const double kRates[] = {44100.0, 48000.0, 88200.0, 96000.0};
const int kBuffers[] = {64, 128, 256, 512, 1024, 2048};

juce::Font captionFont() { return theme::tracked(theme::font(theme::sizeCaption, true), 0.8f); }
juce::Font problemFont() { return theme::font(theme::sizeBody); }

int ceilToInt(float x) { return static_cast<int>(std::ceil(x)); }

// This computer's interface in the rig, or nullptr when it names none.
const model::ComputerBinding* bindingOf(const app::Session& session) {
    const auto* rig = session.model().rig(session.rigId());
    if (rig == nullptr) return nullptr;
    const auto found = rig->computers.find(session.settings().computerName());
    return found != rig->computers.end() && found->second.deviceName.isNotEmpty() ? &found->second : nullptr;
}

}  // namespace

SetupPage::SetupPage(app::Session& session, app::AudioHost* host)
    : session_(session), host_(host), duplicate_("Duplicate rig"), more_(juce::String::fromUTF8("\xe2\x8b\xaf")), tone_("Test tone"),
      grid_(session) {
    for (auto* child : std::initializer_list<juce::Component*>{&rig_, &types_, &devices_, &rate_, &buffer_, &connection_, &duplicate_,
                                                               &more_, &tone_, &grid_})
        addAndMakeVisible(*child);

    juce::StringArray rates, buffers;
    for (const double rate : kRates) rates.add(juce::String(rate / 1000.0, rate == std::floor(rate / 1000.0) * 1000.0 ? 0 : 1) + " kHz");
    for (const int size : kBuffers) buffers.add(juce::String(size) + (size >= 512 ? juce::String::fromUTF8(" \xc2\xb7 safest") : juce::String()));
    rate_.setItems(rates, -1);
    buffer_.setItems(buffers, -1);

    rig_.onChange = [this](int index) { chooseRig(index); };
    types_.onChange = [this](int index) { chooseType(index); };
    devices_.onChange = [this](int index) { chooseDevice(index); };
    devices_.setPlaceholder("Choose an interface" + kEllipsis);
    rate_.onChange = [this](int index) { chooseRate(index); };
    buffer_.onChange = [this](int index) { chooseBuffer(index); };
    duplicate_.onClick = [this] {
        const auto* rig = session_.model().rig(session_.rigId());
        if (rig == nullptr) return;
        askText("Duplicate the rig", rig->name + " copy", "Duplicate", [this, rigId = rig->id](const juce::String& name) {
            const auto result = session_.commands().run("rig.duplicate", jsonObject({{"rigId", rigId}, {"name", name}}));
            if (!result.ok) {
                say(result.message);
                return;
            }
            session_.chooseRig(result.value.toString());
            reopen();
        });
    };
    more_.onClick = [this] { rigMenu(); };
    tone_.onClick = [this] { setTestTone(!tone_on_); };
    tone_.setTooltip("While it is on, click an output's name to hear a tone there.");
    grid_.onOutputPressed = [this](const juce::String& outputId) { return playTone(outputId); };

    keepOffKeyboard(*this);
    session_.changes().addChangeListener(this);
    refresh(true);
}

SetupPage::~SetupPage() {
    session_.changes().removeChangeListener(this);
    if (host_ != nullptr && tone_on_) host_->setTestTone(-1);
}

bool SetupPage::editable() const { return session_.mode() == app::Mode::Edit; }

void SetupPage::changeListenerCallback(juce::ChangeBroadcaster*) { refresh(); }

void SetupPage::visibilityChanged() {
    if (!isVisible()) {
        setTestTone(false);
        return;
    }
    interfaces_for_.clear();                                      // a device may have come or gone while it was hidden
    refresh(true);
    grid_.refreshNow();                                           // it isn't told when the page is shown
}

void SetupPage::refresh(bool force) {
    // Hidden, it waits until it is shown: the session's changes come 30 times a second while a song plays.
    if (!force && !shownWithParents(*this)) {
        fresh_ = false;
        return;
    }
    fresh_ = true;
    auto next = makeRoutingState(session_, host_);
    const bool changed = !(next == state_);
    state_ = std::move(next);

    const auto& model = session_.model();
    const auto ids = model.rigIds();
    juce::StringArray names;
    for (const auto& id : ids) names.add(model.rig(id)->name);
    const int chosen = static_cast<int>(std::find(ids.begin(), ids.end(), session_.rigId()) - ids.begin());
    if (ids != rig_ids_ || names != rig_.items()) {
        rig_ids_ = ids;
        rig_.setItems(names, chosen);
    } else {
        rig_.setSelected(chosen);
    }
    if (const auto* rig = model.rig(session_.rigId())) {
        rate_.setSelected(static_cast<int>(std::find(std::begin(kRates), std::end(kRates), rig->sampleRate) - std::begin(kRates)));
        buffer_.setSelected(static_cast<int>(std::find(std::begin(kBuffers), std::end(kBuffers), rig->bufferSize) - std::begin(kBuffers)));
    }
    connection_.set(state_.connection, host_ != nullptr && host_->isOpen() ? StatusPill::Level::Ok : StatusPill::Level::Warning);
    tone_.setEnabled(host_ != nullptr && host_->isOpen());
    if (tone_on_ && !tone_.isEnabled()) setTestTone(false);

    const auto interfacesFor = session_.rigId() + "|" + state_.device + "|" + type_;
    if (interfacesFor != interfaces_for_) {
        interfaces_for_ = interfacesFor;
        refreshInterfaces();
    }
    if (changed || force) {
        resized();
        repaint();
    }
}

void SetupPage::refreshInterfaces() {
    const auto* binding = bindingOf(session_);
    juce::StringArray types = host_ != nullptr ? host_->deviceTypes() : juce::StringArray();
    if (host_ == nullptr && binding != nullptr) types.add(binding->deviceType);
    const auto wanted = type_.isNotEmpty() ? type_ : binding != nullptr ? binding->deviceType : juce::String();
    const int type = types.isEmpty() ? -1 : std::max(0, types.indexOf(wanted));
    types_.setItems(types, type);

    juce::StringArray devices;
    if (type >= 0) devices = host_ != nullptr ? host_->devicesOfType(types[type]) : juce::StringArray(binding->deviceName);
    const int device = binding != nullptr && type >= 0 && binding->deviceType == types[type] ? devices.indexOf(binding->deviceName) : -1;
    devices_.setItems(devices, device);
    devices_.setNeedsAttention(device < 0);
}

// A change refused (or asked for in Show mode) puts every control back to what the rig says: a dropdown already shows the
// choice that was made, and the interface lists are otherwise made again only when the rig's interface changes.
bool SetupPage::run(const juce::String& command, const juce::var& params) {
    const auto result = editable() ? session_.commands().run(command, params) : model::CommandResult::failure({});
    say(result.ok ? juce::String() : result.message);
    if (!result.ok) {
        type_.clear();
        interfaces_for_.clear();
    }
    refresh(true);
    return result.ok;
}

void SetupPage::say(const juce::String& problem) {
    if (problem == problem_) return;
    problem_ = problem;
    resized();
    repaint();
}

void SetupPage::reopen() {
    if (host_ == nullptr) return;
    const auto opened = host_->openFromRig();
    say(opened.failed() ? opened.getErrorMessage() : juce::String());
    interfaces_for_.clear();
    refresh(true);
}

// ---- The panel's choices -----------------------------------------------------------------------------------------------

void SetupPage::chooseRig(int index) {
    if (!editable() || !juce::isPositiveAndBelow(index, static_cast<int>(rig_ids_.size()))) {
        refresh(true);
        return;
    }
    setTestTone(false);
    type_.clear();
    session_.chooseRig(rig_ids_[static_cast<std::size_t>(index)]);
    reopen();
}

void SetupPage::chooseType(int index) {
    if (!juce::isPositiveAndBelow(index, types_.items().size())) return;
    type_ = types_.items()[index];
    refresh(true);                                                // its devices, none chosen until one is
}

void SetupPage::chooseDevice(int index) {
    if (!juce::isPositiveAndBelow(index, devices_.items().size()) || types_.selected() < 0) return;
    const auto type = types_.items()[types_.selected()];
    if (!run("rig.setDevice", jsonObject({{"rigId", session_.rigId()}, {"computerId", session_.settings().computerName()},
                                      {"deviceType", type}, {"deviceName", devices_.items()[index]}})))
        return;
    type_.clear();
    reopen();
}

void SetupPage::chooseRate(int index) {
    if (!juce::isPositiveAndBelow(index, static_cast<int>(std::size(kRates)))) return;
    if (run("rig.set", jsonObject({{"rigId", session_.rigId()}, {"sampleRate", kRates[index]}})) && bindingOf(session_) != nullptr)
        reopen();
}

void SetupPage::chooseBuffer(int index) {
    if (!juce::isPositiveAndBelow(index, static_cast<int>(std::size(kBuffers)))) return;
    if (run("rig.set", jsonObject({{"rigId", session_.rigId()}, {"bufferSize", kBuffers[index]}})) && bindingOf(session_) != nullptr)
        reopen();
}

void SetupPage::rigMenu() {
    const auto* rig = session_.model().rig(session_.rigId());
    if (rig == nullptr) return;
    const bool edit = editable();
    juce::PopupMenu menu;
    menu.addItem(1, "New rig" + kEllipsis, edit);
    menu.addItem(2, "Rename" + kEllipsis, edit);
    menu.addItem(3, "Remove", edit && session_.model().rigIds().size() > 1);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&more_),
                       [safe = juce::Component::SafePointer<SetupPage>(this), rigId = rig->id, name = rig->name](int result) {
                           if (safe == nullptr) return;
                           if (result == 1)
                               safe->askText("New rig", {}, "Make it", [safe](const juce::String& newName) {
                                   const auto made = safe->session_.commands().run("rig.create", jsonObject({{"name", newName}}));
                                   if (!made.ok) {
                                       safe->say(made.message);
                                       return;
                                   }
                                   safe->session_.chooseRig(made.value.toString());
                                   safe->reopen();
                               });
                           if (result == 2)
                               safe->askText("Rename the rig", name, "Rename", [safe, rigId](const juce::String& newName) {
                                   safe->run("rig.set", jsonObject({{"rigId", rigId}, {"name", newName}}));
                               });
                           if (result == 3 && safe->run("rig.remove", jsonObject({{"rigId", rigId}}))) safe->reopen();
                       });
}

void SetupPage::askText(const juce::String& title, const juce::String& text, const juce::String& button,
                        std::function<void(const juce::String&)> then) {
    if (!editable()) return;
    auto* window = new juce::AlertWindow(title, {}, juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor("text", text);
    window->addButton(button, 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    window->enterModalState(true,
                            juce::ModalCallbackFunction::create(
                                [safe = juce::Component::SafePointer<SetupPage>(this), window, then = std::move(then)](int result) {
                                    if (safe != nullptr && result == 1 && safe->editable()) then(window->getTextEditorContents("text").trim());
                                }),
                            true);
}

void SetupPage::setTestTone(bool on) {
    on = on && host_ != nullptr && host_->isOpen();
    if (on == tone_on_) return;
    tone_on_ = on;
    tone_.setStyle(on ? PillButton::Style::Primary : PillButton::Style::Plain);
    if (!on && host_ != nullptr) host_->setTestTone(-1);
}

bool SetupPage::playTone(const juce::String& outputId) {
    if (!tone_on_ || host_ == nullptr) return false;
    if (const auto* rig = session_.model().rig(session_.rigId()))
        for (const auto& out : rig->outputs)
            if (out.id == outputId) host_->setTestTone(out.firstChannel - 1);
    return true;
}

// ---- Layout and drawing ------------------------------------------------------------------------------------------------

std::vector<juce::String> SetupPage::problemLines() const {
    std::vector<juce::String> lines;
    if (problem_.isNotEmpty()) lines.push_back(problem_);
    lines.insert(lines.end(), state_.problems.begin(), state_.problems.end());
    if (static_cast<int>(lines.size()) > kMaxProblemLines) {
        const int more = static_cast<int>(lines.size()) - (kMaxProblemLines - 1);
        lines.resize(kMaxProblemLines - 1);
        lines.push_back(kEllipsis + "and " + juce::String(more) + " more.");
    }
    return lines;
}

void SetupPage::resized() {
    auto area = getLocalBounds().reduced(kPadX, kPadY);
    sub_tabs_ = area.removeFromLeft(std::min(kSubTabsWidth, area.getWidth()));
    sub_tab_ = sub_tabs_.withHeight(kSubTabHeight);
    area.removeFromLeft(kGap);

    // The panel's groups side by side; the pill and the buttons at its right, or on a row of their own when they don't fit.
    const int inner = std::max(0, area.getWidth() - 2 * kBoxPadX);
    const auto captionWidth = [](const juce::String& text) { return ceilToInt(widthOf(captionFont(), text)); };
    int rigW = juce::jlimit(120, 220, rig_.idealWidth());
    int typeW = juce::jlimit(90, 160, types_.idealWidth());
    int deviceW = juce::jlimit(140, 230, devices_.idealWidth());   // a longer name is shortened with "…"; the menu has it whole
    const int rateW = std::max(captionWidth("SAMPLE RATE"), rate_.idealWidth());
    const int bufferW = std::max(captionWidth("BUFFER"), buffer_.idealWidth());
    const auto groupsWidth = [&] {
        return std::max(captionWidth("RIG"), rigW) + std::max(captionWidth("AUDIO INTERFACE"), typeW + kPairGap + deviceW) + rateW + bufferW
               + 3 * kGroupGap;
    };
    if (groupsWidth() > inner) deviceW = std::max(80, deviceW - (groupsWidth() - inner));
    if (groupsWidth() > inner) rigW = std::max(80, rigW - (groupsWidth() - inner));

    const int pillW = connection_.idealWidth(), duplicateW = duplicate_.idealWidth(), moreW = more_.idealWidth(), toneW = tone_.idealWidth();
    const int clusterW = pillW + kGroupGap + duplicateW + kButtonGap + moreW + kGroupGap + toneW;
    // One row rather than two while the interface's names can give up the room: the device down to 140 px, then the driver
    // type down to 100 (each shortened with "…"; their menus show the names whole).
    if (const int short_ = groupsWidth() + kGroupGap + clusterW - inner; short_ > 0) {
        const int fromDevice = std::min(short_, std::max(0, deviceW - 140));
        const int fromType = std::min(short_ - fromDevice, std::max(0, typeW - 100));
        if (fromDevice + fromType == short_) {
            deviceW -= fromDevice;
            typeW -= fromType;
        }
    }
    const bool oneRow = groupsWidth() + kGroupGap + clusterW <= inner;
    const int groupHeight = kCaptionHeight + kCaptionGap + kControlHeight;
    const int boxHeight = kBoxPadY + groupHeight + kBoxPadY + (oneRow ? 0 : kControlHeight + kBoxPadY);
    top_ = area.removeFromTop(boxHeight);

    const int groupTop = top_.getY() + kBoxPadY, controlTop = groupTop + kCaptionHeight + kCaptionGap;
    int x = top_.getX() + kBoxPadX;
    const auto group = [&](juce::Rectangle<int>& label, const juce::String& caption, int contentWidth) {
        const int w = std::max(captionWidth(caption), contentWidth);
        label = {x, groupTop, w, kCaptionHeight};
        const int left = x;
        x += w + kGroupGap;
        return left;
    };
    rig_.setBounds(group(rig_label_, "RIG", rigW), controlTop, rigW, kControlHeight);
    const int interfaceX = group(interface_label_, "AUDIO INTERFACE", typeW + kPairGap + deviceW);
    types_.setBounds(interfaceX, controlTop, typeW, kControlHeight);
    devices_.setBounds(interfaceX + typeW + kPairGap, controlTop, deviceW, kControlHeight);
    rate_.setBounds(group(rate_label_, "SAMPLE RATE", rateW), controlTop, rateW, kControlHeight);
    buffer_.setBounds(group(buffer_label_, "BUFFER", bufferW), controlTop, bufferW, kControlHeight);

    const int clusterCentre = oneRow ? groupTop + groupHeight / 2 : controlTop + kControlHeight + kBoxPadY + kControlHeight / 2;
    int right = top_.getRight() - kBoxPadX;
    const auto fromRight = [&](juce::Component& c, int w, int h, int gapBefore) {
        c.setBounds(std::max(top_.getX() + kBoxPadX, right - w), clusterCentre - h / 2, std::min(w, right - top_.getX() - kBoxPadX), h);
        right = c.getX() - gapBefore;
    };
    fromRight(tone_, toneW, kControlHeight, kGroupGap);
    fromRight(more_, moreW, kControlHeight, kButtonGap);
    fromRight(duplicate_, duplicateW, kControlHeight, kGroupGap);
    fromRight(connection_, pillW, kPillHeight, kGroupGap);

    // The problems, then the grid in what is left.
    const int lines = static_cast<int>(problemLines().size());
    if (lines > 0) area.removeFromTop(kColumnGap);
    problems_ = area.removeFromTop(lines * kProblemHeight);
    area.removeFromTop(kColumnGap);
    grid_.setBounds(area);
    keepOffKeyboard(*this);
}

void SetupPage::paint(juce::Graphics& g) {
    // The sub-tab: "Audio & routing", chosen.
    if (!sub_tab_.isEmpty()) {
        const auto tab = sub_tab_.toFloat();
        g.setColour(theme::field);
        g.fillRoundedRectangle(tab, theme::radiusButton);
        {
            juce::Graphics::ScopedSaveState state(g);
            juce::Path shape;
            shape.addRoundedRectangle(tab, theme::radiusButton);
            g.reduceClipRegion(shape);
            g.setColour(theme::mint);
            g.fillRect(tab.withWidth(3.0f));
        }
        auto text = sub_tab_.reduced(kSubTabPadX, kSubTabPadY);
        g.setColour(theme::text);
        g.setFont(theme::font(theme::sizeBody, true));
        g.drawText("Audio & routing", text.removeFromTop(17), juce::Justification::centredLeft, true);
        g.setColour(theme::dim);
        g.setFont(theme::font(theme::sizeCaption));
        g.drawText("Rig, outputs, roles", text, juce::Justification::centredLeft, true);
    }

    theme::paintPanel(g, top_.toFloat());
    g.setColour(theme::dim);
    g.setFont(captionFont());
    g.drawText("RIG", rig_label_, juce::Justification::centredLeft, false);
    g.drawText("AUDIO INTERFACE", interface_label_, juce::Justification::centredLeft, false);
    g.drawText("SAMPLE RATE", rate_label_, juce::Justification::centredLeft, false);
    g.drawText("BUFFER", buffer_label_, juce::Justification::centredLeft, false);

    const auto lines = problemLines();
    const int offset = problem_.isNotEmpty() ? 1 : 0;             // the device's or a command's failure comes first
    g.setFont(problemFont());
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        const bool blocks = i < offset + state_.blockingProblems;
        const bool more = i == kMaxProblemLines - 1 && lines[static_cast<std::size_t>(i)].startsWith(kEllipsis);
        g.setColour(more ? theme::dim : blocks ? theme::danger : theme::caution);
        const auto line = problems_.withY(problems_.getY() + i * kProblemHeight).withHeight(kProblemHeight);
        g.drawText(shorten(lines[static_cast<std::size_t>(i)], problemFont(), static_cast<float>(line.getWidth())), line,
                   juce::Justification::centredLeft, false);
    }
}

}  // namespace sqlive::ui
