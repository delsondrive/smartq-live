#include "ui/MidiImportPanel.h"

#include <algorithm>
#include <cmath>

#include "model/SongAudio.h"
#include "ui/Format.h"
#include "ui/Helpers.h"
#include "ui/Theme.h"

namespace sqlive::ui {

namespace {

const juce::String kOpenQuote = juce::String::fromUTF8("\xe2\x80\x9c");    // “
const juce::String kCloseQuote = juce::String::fromUTF8("\xe2\x80\x9d");   // ”
const juce::String kArrow = juce::String::fromUTF8("\xe2\x86\x92");        // →
const juce::String kLeaveOut = "Leave out";
const juce::String kNoDevice = "Choose a device, or it is left out";
const juce::String kNoFile = "No MIDI file? Carry on: type or tap the tempo, and draw the sections in the editor.";
const juce::String kNoneInFile = "None in the file";

// The mockup's CSS: .box (10 px 12 px padding, 10 px corners), h4, .row (6 px margins), .val (3 px 8 px, min 80 px),
// .pills (4 px gaps, 1 px 6 px padding, 4 px corners) and the 12 px rows and hints under them.
constexpr float kPadX = 12.0f, kPadY = 10.0f;
constexpr float kHeadHeight = 14.0f, kHeadGap = 6.0f;
constexpr float kRowHeight = 24.0f, kRowGap = 6.0f, kPlainRowHeight = 18.0f, kValuePadX = 8.0f, kValueMinWidth = 80.0f;
constexpr float kPillHeight = 16.0f, kPillPadX = 6.0f, kPillGap = 4.0f;
constexpr float kGroupGap = 12.0f, kTrackHeight = 24.0f, kChoiceHeight = 22.0f, kHintGap = 10.0f;

juce::Font headFont() { return theme::tracked(theme::font(theme::sizeCaption, true), 0.8f); }
juce::Font rowFont() { return theme::font(theme::sizeBody); }
juce::Font valueFont() { return theme::font(theme::sizeBody, true); }
juce::Font smallFont() { return theme::font(theme::sizeSmall); }
juce::Font pillFont() { return theme::font(theme::sizeCaption, true); }

juce::String counted(int n, const juce::String& one, const juce::String& many) { return juce::String(n) + " " + (n == 1 ? one : many); }

juce::TextLayout wrapped(const juce::String& text, const juce::Font& font, juce::Colour colour, float width) {
    juce::AttributedString s;
    s.setWordWrap(juce::AttributedString::byWord);
    s.append(text, font, colour);
    juce::TextLayout layout;
    layout.createLayout(s, std::max(1.0f, width));
    return layout;
}

}  // namespace

// ---- The summary -------------------------------------------------------------------------------------------------------

MidiSummary makeMidiSummary(const app::Session& session, const juce::File& midiFile, const model::MidiFileContent* content,
                            const model::MidiImportChoice* choice) {
    MidiSummary summary;
    summary.fileName = midiFile.getFileName();
    if (content == nullptr) return summary;
    summary.present = true;
    const auto& band = session.model().band();
    const auto chosen = choice != nullptr ? *choice : model::defaultMidiChoice(*content, band);

    model::SongDoc song;
    if (!content->tempo.empty()) song.tempo = content->tempo;
    const auto& first = song.tempo.front();
    summary.tempo = formatBpm(first.bpm) + " BPM" + kDot + juce::String(first.numerator) + "/" + juce::String(first.denominator);
    const double seconds = model::tempoMapOf(song).secondsAt({content->bars + 1, 1.0});
    summary.length = counted(content->bars, "bar", "bars") + kDot + formatTime(std::round(seconds));
    summary.sections = content->sections.empty() ? kNoneInFile : juce::String((int) content->sections.size()) + ", from the markers";
    for (const auto& s : content->sections) summary.sectionNames.push_back(s.name);

    for (std::size_t i = 0; i < content->tracks.size(); ++i) {
        const auto& t = content->tracks[i];
        MidiSummary::Track row;
        row.name = kOpenQuote + t.name + kCloseQuote;
        if (!t.channels.empty()) {
            juce::StringArray channels;
            for (int c : t.channels) channels.add(juce::String(c));
            row.name << " (ch " << channels.joinIntoString(", ") << ")";
        }
        const auto it = chosen.trackToDevice.find(static_cast<int>(i));
        if (it != chosen.trackToDevice.end())
            for (const auto& d : band.midiDevices)
                if (d.id == it->second) row.device = d.name;
        row.needsDevice = row.device.isEmpty();
        summary.tracks.push_back(row);
    }
    summary.notes = content->notes;
    return summary;
}

// ---- The panel ---------------------------------------------------------------------------------------------------------

// Where everything goes at one width: each text (one line, shortened, or wrapped), each .val box, each pill and each
// track's Choice, and the height it all takes.
struct MidiImportPanel::Layout {
    struct Text {
        juce::Rectangle<float> area;
        juce::String text;
        juce::Font font;
        juce::Colour colour;
        bool wrap = false;
        juce::Justification justification = juce::Justification::centredLeft;
    };
    std::vector<Text> texts;
    std::vector<juce::Rectangle<float>> values;                   // the .val boxes behind their texts
    std::vector<std::pair<juce::Rectangle<float>, juce::String>> pills;
    std::vector<juce::Rectangle<int>> choices;
    int height = 0;
};

MidiImportPanel::Layout MidiImportPanel::layOut(int width) const {
    Layout l;
    const float x0 = kPadX, w = std::max(0.0f, static_cast<float>(width) - 2.0f * kPadX);
    float y = kPadY;
    const auto paragraph = [&](const juce::String& text, const juce::Font& font, juce::Colour colour) {
        const float h = std::ceil(wrapped(text, font, colour, w).getHeight());
        l.texts.push_back({{x0, y, w, h}, text, font, colour, true});
        y += h;
    };
    const auto finish = [&] {
        l.height = static_cast<int>(std::ceil(y + kPadY));
        return l;
    };

    if (summary_.fileName.isNotEmpty() && (summary_.present || problem_.isNotEmpty())) {
        l.texts.push_back({{x0, y, w, kHeadHeight}, "FROM " + kOpenQuote + summary_.fileName.toUpperCase() + kCloseQuote, headFont(), theme::dim});
        y += kHeadHeight + kHeadGap;
    }
    if (!summary_.present) {
        if (problem_.isNotEmpty()) paragraph(problem_, smallFont(), theme::danger);
        else paragraph(kNoFile, smallFont(), theme::dim);
        return finish();
    }

    // A label on the left and its value on the right: in a .val box, or as words in mint.
    const auto row = [&](const juce::String& label, const juce::String& value, bool boxed) {
        const float h = boxed ? kRowHeight : kPlainRowHeight;
        l.texts.push_back({{x0, y, w, h}, label, rowFont(), theme::mid});
        if (boxed) {
            const float vw = std::min(w / 2.0f, std::max(kValueMinWidth, std::ceil(widthOf(valueFont(), value)) + 2.0f * kValuePadX + 2.0f));
            const juce::Rectangle<float> box(x0 + w - vw, y, vw, h);
            l.values.push_back(box);
            l.texts.push_back({box.reduced(kValuePadX, 0.0f), value, valueFont(), theme::text, false, juce::Justification::centredRight});
        } else {
            l.texts.push_back({{x0 + w / 2.0f, y, w / 2.0f, h}, value, smallFont(), theme::mint, false, juce::Justification::centredRight});
        }
        y += h + kRowGap;
    };
    row("Tempo", summary_.tempo, true);
    row("Bar 1 starts at", "0:00.000", true);
    row("Length", summary_.length, true);
    row("Sections", summary_.sections, false);

    if (!summary_.sectionNames.empty()) {                         // the pills, wrapping
        float x = x0;
        for (const auto& name : summary_.sectionNames) {
            const float pw = std::min(w, std::ceil(widthOf(pillFont(), name)) + 2.0f * kPillPadX);
            if (x > x0 && x + pw > x0 + w) {
                x = x0;
                y += kPillHeight + kPillGap;
            }
            l.pills.push_back({{x, y, pw, kPillHeight}, name});
            x += pw + kPillGap;
        }
        y += kPillHeight + kRowGap;
    }

    y += kGroupGap - kRowGap;
    const int trackCount = static_cast<int>(summary_.tracks.size());
    row("Pedal changes", trackCount == 0 ? kNoneInFile : counted(trackCount, "track", "tracks"), false);
    for (std::size_t i = 0; i < summary_.tracks.size(); ++i) {
        const auto& track = summary_.tracks[i];
        const float cw = i < choices_.size() ? std::min(w / 2.0f, static_cast<float>(choices_[i]->idealWidth())) : w / 2.0f;
        l.texts.push_back({{x0, y, std::max(0.0f, w - cw - 6.0f), kTrackHeight}, track.name + " " + kArrow, smallFont(),
                           track.needsDevice ? theme::caution : theme::mid});
        l.choices.push_back(juce::Rectangle<float>(x0 + w - cw, y + (kTrackHeight - kChoiceHeight) / 2.0f, cw, kChoiceHeight).toNearestInt());
        y += kTrackHeight;
        if (track.needsDevice) {
            l.texts.push_back({{x0, y, w, kPlainRowHeight}, kNoDevice, smallFont(), theme::caution});
            y += kPlainRowHeight;
        }
        y += kRowGap / 2.0f;
    }

    if (!summary_.notes.isEmpty()) {
        y += kHintGap - kRowGap / 2.0f;
        for (const auto& note : summary_.notes) paragraph(note, smallFont(), theme::dim);
    }
    return finish();
}

MidiImportPanel::MidiImportPanel(app::Session& session) : session_(session) { update(); }

MidiImportPanel::~MidiImportPanel() = default;

void MidiImportPanel::setContent(const juce::File& midiFile, const model::MidiFileContent* content, model::MidiImportChoice choice) {
    file_ = midiFile;
    if (content != nullptr) content_ = *content;
    else content_.reset();
    choice_ = std::move(choice);
    update();
}

void MidiImportPanel::setProblem(const juce::String& problem) {
    if (problem == problem_) return;
    problem_ = problem;
    update();
}

void MidiImportPanel::setChoicesEnabled(bool enabled) {
    choicesEnabled_ = enabled;
    for (auto& c : choices_) c->setEnabled(enabled);
}

void MidiImportPanel::update() {
    summary_ = makeMidiSummary(session_, file_, content_ ? &*content_ : nullptr, &choice_);

    juce::StringArray names;
    std::vector<juce::Colour> dots;                               // each device's dot, as the mockup's .dd i
    deviceIds_.clear();
    for (const auto& d : session_.model().band().midiDevices) {
        names.add(d.name);
        dots.push_back(d.colour);
        deviceIds_.push_back(d.id);
    }
    names.add(kLeaveOut);
    dots.push_back(juce::Colours::transparentBlack);

    const auto& tracks = summary_.tracks;
    while (choices_.size() > tracks.size()) choices_.pop_back();
    while (choices_.size() < tracks.size()) {
        const int track = static_cast<int>(choices_.size());
        auto choice = std::make_unique<Choice>();
        choice->onChange = [this, track](int item) { choose(track, item); };
        addAndMakeVisible(*choice);
        choices_.push_back(std::move(choice));
    }
    for (std::size_t i = 0; i < tracks.size(); ++i) {
        const auto it = choice_.trackToDevice.find(static_cast<int>(i));
        int selected = names.size() - 1;                          // "Leave out"
        if (!tracks[i].needsDevice && it != choice_.trackToDevice.end())
            for (std::size_t d = 0; d < deviceIds_.size(); ++d)
                if (deviceIds_[d] == it->second) selected = static_cast<int>(d);
        choices_[i]->setItems(names, selected);
        choices_[i]->setDots(dots);
        choices_[i]->setNeedsAttention(tracks[i].needsDevice);
        choices_[i]->setEnabled(choicesEnabled_);
    }
    resized();
    repaint();
}

void MidiImportPanel::choose(int track, int item) {
    if (item < 0) return;
    if (static_cast<std::size_t>(item) < deviceIds_.size()) choice_.trackToDevice[track] = deviceIds_[static_cast<std::size_t>(item)];
    else choice_.trackToDevice.erase(track);
    update();
    if (onChoiceChanged) onChoiceChanged(choice_);
}

int MidiImportPanel::idealHeight(int width) const { return layOut(width).height; }

void MidiImportPanel::resized() {
    const auto l = layOut(getWidth());
    for (std::size_t i = 0; i < choices_.size(); ++i)
        choices_[i]->setBounds(i < l.choices.size() ? l.choices[i] : juce::Rectangle<int>());
}

void MidiImportPanel::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(theme::panel);
    g.fillRoundedRectangle(bounds, theme::radiusPanel);
    g.setColour(theme::hairline);
    g.drawRoundedRectangle(bounds.reduced(0.5f), theme::radiusPanel, 1.0f);

    const auto l = layOut(getWidth());
    for (const auto& box : l.values) {
        g.setColour(theme::field);
        g.fillRoundedRectangle(box, theme::radiusField);
        g.setColour(theme::hairline);
        g.drawRoundedRectangle(box.reduced(0.5f), theme::radiusField, 1.0f);
    }
    for (const auto& t : l.texts) {
        if (t.wrap) {
            wrapped(t.text, t.font, t.colour, t.area.getWidth()).draw(g, t.area);
            continue;
        }
        g.setColour(t.colour);
        g.setFont(t.font);
        g.drawText(shorten(t.text, t.font, t.area.getWidth()), t.area, t.justification, false);
    }
    for (const auto& [area, name] : l.pills) {                    // one colour until sections have types (stage 3)
        g.setColour(theme::mid);
        g.fillRoundedRectangle(area, 4.0f);
        g.setColour(theme::onAccent);
        g.setFont(pillFont());
        g.drawText(shorten(name, pillFont(), area.getWidth() - 2.0f * kPillPadX), area, juce::Justification::centred, false);
    }
}

}  // namespace sqlive::ui
