#include "ui/PatchListDialog.h"

#include <algorithm>

#include "model/DocumentsJson.h"
#include "model/JsonObject.h"
#include "ui/Format.h"
#include "ui/Helpers.h"
#include "ui/Theme.h"

namespace sqlive::ui {

namespace {

// The mockup's dialog CSS, as MidiImportDialog has it: .dlg (14 px corners), .dh (14 px 18 px, an 18 px title), the body's
// 16 px 18 px, .df (12 px 18 px, 10 px gaps).
constexpr float kRadius = 14.0f;
constexpr int kPadX = 18, kHeaderHeight = 58, kBodyPadY = 16, kHintHeight = 18, kHintGap = 8, kTextHeight = 280;
constexpr int kFooterPadY = 12, kButtonHeight = 30, kLineHeight = 20, kFooterGap = 10;

juce::Font titleFont() { return theme::font(18.0f, true); }
juce::Font bodyFont() { return theme::font(theme::sizeBody); }
juce::Font hintFont() { return theme::font(theme::sizeSmall); }

const juce::String kHint = "Paste a list, or type one patch per line.";

bool isDigit(juce::juce_wchar c) { return c >= '0' && c <= '9'; }

// A number of at most three digits, 0 to 127; -1 when it isn't one.
int midiNumber(const juce::String& digits) {
    if (digits.isEmpty() || digits.length() > 3) return -1;
    const int value = digits.getIntValue();
    return value <= 127 ? value : -1;
}

}  // namespace

juce::String formatPatchList(const std::vector<model::Patch>& patches) {
    juce::StringArray lines;
    for (const auto& p : patches) {
        juce::String number(p.program);
        if (p.bankMsb >= 0 || p.bankLsb >= 0)
            number = (p.bankMsb >= 0 ? juce::String(p.bankMsb) : juce::String()) + ":" + (p.bankLsb >= 0 ? juce::String(p.bankLsb) : juce::String())
                     + ":" + number;
        lines.add(number + " " + p.name);
    }
    return lines.joinIntoString("\n");
}

juce::String parsePatchList(const juce::String& text, std::vector<model::Patch>& out) {
    std::vector<model::Patch> patches;
    const auto lines = juce::StringArray::fromLines(text);
    for (int i = 0; i < lines.size(); ++i) {
        const auto line = lines[i].trim();
        if (line.isEmpty()) continue;
        const auto where = "Line " + juce::String(i + 1);
        const auto example = utf8(", like \xe2\x80\x9c" "12 Crunch\xe2\x80\x9d.");

        // The number: digits and colons, "<program>" or "<msb>:<lsb>:<program>".
        int end = 0;
        while (end < line.length() && (isDigit(line[end]) || line[end] == ':')) ++end;
        juce::StringArray parts;
        juce::String part;
        for (int c = 0; c < end; ++c) {
            if (line[c] == ':') {
                parts.add(part);
                part.clear();
            } else {
                part += line[c];
            }
        }
        parts.add(part);
        if (parts[parts.size() - 1].isEmpty()) return where + " needs a program number first" + example;
        if (parts.size() != 1 && parts.size() != 3)
            return where + utf8(": a bank is written before the program as \xe2\x80\x9c<msb>:<lsb>:<program>\xe2\x80\x9d, like "
                                "\xe2\x80\x9c" "1:0:5 Lead\xe2\x80\x9d.");

        model::Patch patch;
        patch.program = midiNumber(parts[parts.size() - 1]);
        if (patch.program < 0) return where + ": program numbers go from 0 to 127.";
        if (parts.size() == 3) {
            patch.bankMsb = parts[0].isEmpty() ? -1 : midiNumber(parts[0]);
            patch.bankLsb = parts[1].isEmpty() ? -1 : midiNumber(parts[1]);
            if ((parts[0].isNotEmpty() && patch.bankMsb < 0) || (parts[1].isNotEmpty() && patch.bankLsb < 0))
                return where + ": bank numbers go from 0 to 127.";
        }

        // The name, after a space, a tab or one of , ; = - .
        auto rest = line.substring(end).trimStart();
        if (rest.isNotEmpty() && juce::String(",;=-.").containsChar(rest[0])) rest = rest.substring(1);
        patch.name = rest.trim();
        if (patch.name.isEmpty()) return where + " needs a name after its program number" + example;
        patches.push_back(std::move(patch));
    }
    out = std::move(patches);
    return {};
}

// ---- The dialog ----------------------------------------------------------------------------------------------------------

PatchListDialog::PatchListDialog(app::Session& session, juce::String deviceId) : session_(session), deviceId_(std::move(deviceId)) {
    setWantsKeyboardFocus(true);
    std::vector<model::Patch> patches;
    for (const auto& device : session_.model().band().midiDevices)
        if (device.id == deviceId_) {
            title_ = "Patches" + kDot + device.name;
            patches = device.patches;
        }
    text_.setMultiLine(true, false);
    text_.setReturnKeyStartsNewLine(true);
    text_.setScrollbarsShown(true);
    text_.setFont(bodyFont());
    text_.setIndents(8, 6);
    text_.setText(formatPatchList(patches), juce::dontSendNotification);
    text_.setTextToShowWhenEmpty(utf8("12 Crunch"), theme::dim);
    text_.onEscapeKey = [this] {
        if (onClose) onClose();
    };
    cancel_.onClick = [this] {
        if (onClose) onClose();
    };
    save_.onClick = [this] { save(); };
    save_.setEnabled(session_.mode() == app::Mode::Edit);
    text_.setReadOnly(session_.mode() != app::Mode::Edit);
    for (auto* button : {&cancel_, &save_}) button->setWantsKeyboardFocus(false);
    for (juce::Component* child : std::initializer_list<juce::Component*>{&text_, &cancel_, &save_}) addAndMakeVisible(child);
}

void PatchListDialog::save() {
    if (session_.mode() != app::Mode::Edit) return;
    std::vector<model::Patch> patches;
    if (const auto wrong = parsePatchList(text_.getText(), patches); wrong.isNotEmpty()) {
        say(wrong);
        return;
    }
    juce::Array<juce::var> list;
    for (const auto& p : patches) list.add(model::toVar(p));
    const auto result = session_.commands().run("device.setPatches", model::jsonObject({{"deviceId", deviceId_}, {"patches", list}}));
    if (!result.ok) {
        say(result.message);
        return;
    }
    say({});
    if (onClose) onClose();                                       // last: it may close the dialog
}

void PatchListDialog::say(const juce::String& problem) {
    if (problem == problem_) return;
    problem_ = problem;
    if (auto* parent = getParentComponent()) parent->resized();  // the veil sizes it from idealHeight()
    resized();
    repaint();
}

int PatchListDialog::idealHeight() const {
    const int footer = 2 * kFooterPadY + kButtonHeight + (problem_.isNotEmpty() ? kLineHeight : 0);
    return kHeaderHeight + 2 * kBodyPadY + kHintHeight + kHintGap + kTextHeight + footer;
}

void PatchListDialog::resized() {
    auto area = getLocalBounds();
    header_ = area.removeFromTop(kHeaderHeight);
    footer_ = area.removeFromBottom(std::min(area.getHeight(), 2 * kFooterPadY + kButtonHeight + (problem_.isNotEmpty() ? kLineHeight : 0)));
    auto foot = footer_.reduced(kPadX, kFooterPadY);
    problemLine_ = foot.removeFromTop(std::max(0, foot.getHeight() - kButtonHeight));
    save_.setBounds(foot.removeFromRight(save_.idealWidth()));
    foot.removeFromRight(kFooterGap);
    cancel_.setBounds(foot.removeFromRight(cancel_.idealWidth()));

    auto body = area.reduced(kPadX, kBodyPadY);
    hint_ = body.removeFromTop(kHintHeight);
    body.removeFromTop(std::min(kHintGap, body.getHeight()));
    text_.setBounds(body);
}

void PatchListDialog::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(theme::popup);
    g.fillRoundedRectangle(bounds, kRadius);
    g.setColour(theme::hairline);
    g.drawRoundedRectangle(bounds.reduced(0.5f), kRadius, 1.0f);
    g.fillRect(1, header_.getBottom() - 1, getWidth() - 2, 1);
    g.fillRect(1, footer_.getY(), getWidth() - 2, 1);

    g.setColour(theme::text);
    g.setFont(titleFont());
    g.drawText(shorten(title_, titleFont(), static_cast<float>(header_.getWidth() - 2 * kPadX)), header_.reduced(kPadX, 0).toFloat(),
               juce::Justification::centredLeft, false);
    g.setColour(theme::dim);
    g.setFont(hintFont());
    g.drawText(kHint, hint_, juce::Justification::centredLeft, true);
    if (problem_.isNotEmpty()) {
        g.setColour(theme::caution);                              // a line to fix, not something broken
        g.setFont(hintFont());
        g.drawText(shorten(problem_, hintFont(), static_cast<float>(problemLine_.getWidth())), problemLine_, juce::Justification::centredLeft,
                   false);
    }
}

bool PatchListDialog::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey) {
        if (onClose) onClose();
        return true;
    }
    return false;
}

}  // namespace sqlive::ui
