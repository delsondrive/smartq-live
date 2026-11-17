#include "ui/CueVoicePage.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>

#include "model/FileNames.h"
#include "model/JsonObject.h"
#include "ui/CueVoiceRows.h"
#include "ui/Format.h"
#include "ui/Helpers.h"
#include "ui/OutputLabelPill.h"
#include "ui/Theme.h"

namespace sqlive::ui {

using model::jsonObject;
using namespace cuevoice;

namespace {

// The mockup's CSS: .col (gap 12), .box (padding 10 12; the top one a row with a gap of 18), h4 (11 px bold, letter-spaced
// 0.8, dim, 6 px under it), .val (a field, 6 px corners, padding 3 8, semibold, at least 80 wide, right-aligned), th
// (11 px bold letter-spaced 0.6 in dim, padding 7 8, a hairline under it), .addl (12 px semibold mint), .hint (12 px dim),
// the footer (padding 10 12, gap 16). The rows' measures are in CueVoiceRows.h.
constexpr int kBoxPadX = 12, kBoxPadY = 10, kGroupGap = 18, kCaptionHeight = 15, kCaptionGap = 6, kControlHeight = 28;
constexpr int kColumnGap = 12, kMessageHeight = 18, kButtonGap = 10, kValueMinWidth = 80, kValuePadX = 8;
constexpr int kHeaderHeight = 30, kFooterHeight = 38, kFooterPadX = 12, kFooterGap = 16;
constexpr int kMinColumnWidth = 560, kMinCueWidth = 200, kChoiceExtra = 34;   // a .dd: padding, the chevron and its gap
constexpr int kTickHz = 20;
constexpr double kMessageSeconds = 4.0, kRefreshEveryMs = 200.0, kBetweenClipsSeconds = 0.3;
constexpr int kLoadTicks = 40;                                    // ticks a clip may take to load before it is skipped: 2 s

const juce::String kVoice = utf8("Built-in \xc2\xb7 this computer's voice");
const juce::String kCouldNotMake = "This computer couldn't make the voice. You can still record your own clips.";
const juce::String kNotWhilePlaying = "Cues can only be heard while no song is playing.";
const juce::String kAddType = "+ Add a section type", kImport = "+ Import a recording";
const juce::StringArray kLeads{"2 beats before", "1 bar before", "2 bars before"};   // as makeCueVoiceState says them
const juce::StringArray kLeadNames{"twoBeats", "oneBar", "twoBars"};

juce::Font captionFont() { return theme::tracked(theme::font(theme::sizeCaption, true), 0.8f); }
juce::Font headFont() { return theme::tracked(theme::font(theme::sizeCaption, true), 0.6f); }
juce::Font linkFont() { return theme::font(theme::sizeSmall, true); }

// HEARD IN as drawn: "In-ears mixes only" as the In-ears pill and "mixes only"; anything else as words.
int heardWidth(const juce::String& heardIn) {
    if (!heardIn.startsWith("In-ears ")) return widthInPixels(bodyFont(), heardIn);
    return static_cast<int>(std::ceil(outputLabelPillWidth(model::OutputLabel::Monitor))) + 4
           + widthInPixels(bodyFont(), heardIn.fromFirstOccurrenceOf("In-ears ", false, false));
}

}  // namespace

// ---- The page ----------------------------------------------------------------------------------------------------------

CueVoicePage::CueVoicePage(app::Session& session)
    : session_(session), make_("Make the voice"), hearAll_(kPlay + " Hear all"), rows_(std::make_unique<Rows>(*this)) {
    for (auto* child : std::initializer_list<juce::Component*>{&make_, &hearAll_, &lead_, &viewport_}) addAndMakeVisible(*child);
    make_.onClick = [this] { makeVoice(); };
    hearAll_.onClick = [this] { hearAll(); };
    lead_.setItems(kLeads, 1);
    lead_.onChange = [this](int index) { chooseLead(index); };
    viewport_.setViewedComponent(rows_.get(), false);
    viewport_.setScrollBarsShown(true, false);
    viewport_.setScrollBarThickness(10);
    keepOffKeyboard(*this);
    session_.changes().addChangeListener(this);
    refresh(true);
}

CueVoicePage::~CueVoicePage() { session_.changes().removeChangeListener(this); }

bool CueVoicePage::editable() const { return session_.mode() == app::Mode::Edit; }

bool CueVoicePage::songPlaying() const {
    const auto now = session_.now();
    return now.playing || now.stopping || now.inGap;
}

const CueVoiceState::Row* CueVoicePage::row(int index) const {
    return juce::isPositiveAndBelow(index, static_cast<int>(state_.rows.size())) ? &state_.rows[static_cast<std::size_t>(index)] : nullptr;
}

const juce::Component* CueVoicePage::cueChoiceView(int index) const { return rows_->choice(index); }

CueChoice CueVoicePage::cueChoice(int index) const {
    const auto* r = row(index);
    return r != nullptr ? makeCueChoice(state_, *r) : CueChoice{};
}

void CueVoicePage::changeListenerCallback(juce::ChangeBroadcaster*) { refresh(); }

void CueVoicePage::visibilityChanged() {
    if (isVisible()) refresh(true);
    else hearQueue_.clear();                                      // nothing more is said for a page no one sees
    updateTimer();
}

void CueVoicePage::refresh(bool force) {
    // Hidden, it waits until it is shown. Shown, at most five times a second: it looks at the clips on disk, and the
    // session's changes come 30 times a second while a song plays.
    if (!force && !shownWithParents(*this)) return;
    const auto now = juce::Time::getMillisecondCounterHiRes();
    if (!force && now - last_refresh_ms_ < kRefreshEveryMs) {
        dirty_ = true;
        updateTimer();
        return;
    }
    dirty_ = false;
    last_refresh_ms_ = now;
    auto next = makeCueVoiceState(session_);
    const bool changed = !(next == state_);
    state_ = std::move(next);

    const bool edit = editable();
    making_ = session_.makingVoice() && making_;
    make_.setButtonText(makeButtonText());
    make_.setEnabled(edit && !session_.makingVoice());
    lead_.setSelected(state_.leadIndex);
    lead_.setEnabled(edit);
    hearAll_.setEnabled(!state_.rows.empty());
    if (changed || force) {
        rows_->update();
        resized();
        repaint();
    }
    updateTimer();
}

void CueVoicePage::updateTimer() {
    const bool needed = isVisible() && (dirty_ || message_until_ > 0.0 || hearing());
    if (needed && !isTimerRunning()) startTimerHz(kTickHz);
    if (!needed && isTimerRunning()) stopTimer();
}

void CueVoicePage::timerCallback() {
    const auto now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    if (message_until_ > 0.0 && now >= message_until_) {
        message_.clear();
        message_until_ = 0.0;
        resized();
        repaint();
    }
    if (dirty_) refresh(true);
    stepHearing();
    updateTimer();
}

bool CueVoicePage::run(const juce::String& command, const juce::var& params, const juce::String& coalesceKey) {
    const auto result = editable() ? session_.commands().run(command, params, coalesceKey)
                                   : model::CommandResult::failure("Show mode never changes the band folder. Change it after the show.");
    if (!result.ok) say(result.message, true);
    refresh(true);                                                // a refused choice goes back to what the band says
    return result.ok;
}

void CueVoicePage::say(const juce::String& message, bool problem, bool stays) {
    message_ = message;
    message_problem_ = problem;
    message_until_ = message.isEmpty() || stays ? 0.0 : juce::Time::getMillisecondCounterHiRes() / 1000.0 + kMessageSeconds;
    resized();
    repaint();
    updateTimer();
}

// ---- Making the voice --------------------------------------------------------------------------------------------------

juce::String CueVoicePage::makeButtonText() const { return state_.madeBuiltIns.isEmpty() ? "Make the voice" : "Make it again"; }

juce::String CueVoicePage::makeAgainQuestion() const {
    juce::StringArray names;
    for (const auto& name : state_.madeBuiltIns) names.add(quoted(name));
    return "Making it again replaces these built-in clips, which everyone in the band hears: " + names.joinIntoString(", ")
           + ". Replace them, or only make the ones that are missing?";
}

void CueVoicePage::makeVoice() {
    if (!editable() || session_.makingVoice()) return;
    if (state_.madeBuiltIns.isEmpty()) {
        makeVoice(false);
        return;
    }
    auto* window = new juce::AlertWindow("Make the voice again?", makeAgainQuestion(), juce::MessageBoxIconType::NoIcon, this);
    window->addButton("Replace them", 1);
    window->addButton("Only make the missing ones", 2, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    window->enterModalState(true,
                            juce::ModalCallbackFunction::create([safe = juce::Component::SafePointer<CueVoicePage>(this)](int result) {
                                if (safe != nullptr && result != 0) safe->makeVoice(result == 1);
                            }),
                            true);
}

void CueVoicePage::makeVoice(bool replace) {
    if (!editable() || session_.makingVoice()) return;
    progress_ = 0.0;
    const auto started = session_.makeVoice(
        replace,
        [safe = juce::Component::SafePointer<CueVoicePage>(this)](double progress) {
            if (safe == nullptr) return;
            safe->progress_ = progress;
            safe->repaint(safe->message_line_);
        },
        [safe = juce::Component::SafePointer<CueVoicePage>(this)](juce::Result result) {
            if (safe != nullptr) safe->voiceDone(result);
        });
    if (started.failed()) {
        say(started.getErrorMessage(), true);
        return;
    }
    making_ = true;
    say({}, false);
    refresh(true);
}

void CueVoicePage::voiceDone(juce::Result result) {
    making_ = false;
    if (result.wasOk()) say("The voice is made.", false);
    else say(session_.mode() == app::Mode::Show ? result.getErrorMessage() : kCouldNotMake, true, true);
    refresh(true);
}

// ---- SAY IT, LEVEL and hearing -----------------------------------------------------------------------------------------

void CueVoicePage::chooseLead(int index) {
    if (!juce::isPositiveAndBelow(index, kLeadNames.size())) return;
    run("band.setCueLead", jsonObject({{"lead", kLeadNames[index]}}));
}

void CueVoicePage::setLevel(float db) {
    session_.setLevel({app::LevelTarget::Kind::BandCues, {}, {}, {}, {}}, db, juce::Uuid().toString());
    refresh(true);
}

void CueVoicePage::askLevel() {
    const auto level = session_.level({app::LevelTarget::Kind::BandCues, {}, {}, {}, {}});
    askUserText(*this, "How loud are the cues, in dB?", formatDbNumber(level.value_or(session_.model().band().cuesDb)), "Set",
                [safe = juce::Component::SafePointer<CueVoicePage>(this)](const juce::String& text) {
                    if (safe == nullptr) return;
                    if (const auto db = parseDb(text)) safe->setLevel(*db);
                    else safe->say("Type a level in dB, like -9 or +3.", true);
                });
}

bool CueVoicePage::hearAll() {
    if (songPlaying()) {
        hearQueue_.clear();
        say(kNotWhilePlaying, true);
        return false;
    }
    hearQueue_.clear();
    for (const auto& r : state_.rows)
        if (r.hasClip && !r.clipMissing) {
            session_.cues().clip(r.clip);                         // each starts loading now, so the next is ready in time
            hearQueue_.push_back(r.clip);
        }
    if (hearQueue_.empty()) {
        say(state_.voiceMade ? "No section type has a cue to hear." : "Make the voice first, or choose your own recordings.", true);
        return false;
    }
    hear_until_ = 0.0;
    hear_tries_ = 0;
    stepHearing();
    updateTimer();
    return true;
}

void CueVoicePage::hear(int index) {
    const auto* r = row(index);
    if (r == nullptr || !r->hasClip) return;
    if (r->clipMissing) {
        say(isBuiltInClip(r->clip) ? "This cue hasn't been made yet: click " + makeButtonText() + "."
                                         : "This recording isn't in the band folder on this computer.",
            true);
        return;
    }
    if (songPlaying()) {
        say(kNotWhilePlaying, true);
        return;
    }
    hearQueue_.clear();
    hearQueue_.push_back(r->clip);
    hear_until_ = 0.0;
    hear_tries_ = 0;
    stepHearing();
    updateTimer();
}

// One clip after another: the next once the one heard has ended and a short breath has passed; a clip still loading is
// waited for up to 2 s, then skipped. A song starting ends it.
void CueVoicePage::stepHearing() {
    const auto now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    hearBusy_ = now < hear_until_;
    if (hearQueue_.empty() || hearBusy_) return;
    if (songPlaying()) {
        hearQueue_.clear();
        return;
    }
    const auto clip = hearQueue_.front();
    if (session_.hearCue(clip)) {
        hearQueue_.pop_front();
        hear_tries_ = 0;
        const auto audio = session_.cues().clip(clip);
        const double seconds = audio != nullptr ? static_cast<double>(audio->left.size()) / session_.sampleRate() : 1.0;
        hear_until_ = now + seconds + kBetweenClipsSeconds;
        hearBusy_ = true;
    } else if (++hear_tries_ > kLoadTicks) {
        hearQueue_.pop_front();                                   // it couldn't be read: on to the next
        hear_tries_ = 0;
    }
}

// ---- The table's gestures ----------------------------------------------------------------------------------------------

void CueVoicePage::chooseCue(int index, int item) {
    const auto* r = row(index);
    if (r == nullptr) return;
    const auto choice = makeCueChoice(state_, *r);
    if (!juce::isPositiveAndBelow(item, choice.items.size())) return;
    if (item == choice.importItem) {
        rows_->update();                                          // the type's cue until a recording is brought in
        askImport(r->typeId);
        return;
    }
    run("sectionType.set", jsonObject({{"typeId", r->typeId}, {"cueClip", choice.clips[item]}}));
}

void CueVoicePage::askImport(const juce::String& typeId) {
    if (!editable()) return;
    chooser_ = std::make_unique<juce::FileChooser>("Import a recording", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe = juce::Component::SafePointer<CueVoicePage>(this), typeId](const juce::FileChooser& chooser) {
                              if (safe != nullptr && chooser.getResult() != juce::File())
                                  safe->importRecording(chooser.getResult(), typeId);
                          });
}

void CueVoicePage::importRecording(const juce::File& file, const juce::String& typeId) {
    if (!editable()) {
        say("Show mode never changes the band folder. Import it after the show.", true);
        return;
    }
    const auto path = session_.importCueClip(file);
    if (path.isEmpty()) {
        say(file.getFileName() + " couldn't be brought in: it couldn't be read as audio, or the band folder couldn't be written.",
            true);
        return;
    }
    if (typeId.isNotEmpty()) {
        if (run("sectionType.set", jsonObject({{"typeId", typeId}, {"cueClip", path}}))) say({}, false);
        return;
    }
    refresh(true);
    askTypeFor(path);
}

// "+ Import a recording": once it is in, which section type it announces (or none yet: it is kept, and offered in every
// type's CUE choice).
void CueVoicePage::askTypeFor(const juce::String& path) {
    if (!isShowing()) return;
    juce::PopupMenu menu;
    menu.addSectionHeader("Use " + quoted(clipName(path)) + " as the cue for");
    for (int i = 0; i < static_cast<int>(state_.rows.size()); ++i) menu.addItem(i + 1, state_.rows[static_cast<std::size_t>(i)].name);
    menu.addSeparator();
    menu.addItem(-1, "None yet");
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(localAreaToGlobal(import_)),
                       [safe = juce::Component::SafePointer<CueVoicePage>(this), path](int result) {
                           if (safe == nullptr || result <= 0) return;
                           if (const auto* r = safe->row(result - 1))
                               safe->run("sectionType.set", jsonObject({{"typeId", r->typeId}, {"cueClip", path}}));
                       });
}

void CueVoicePage::renameType(int index, const juce::String& name) {
    const auto* r = row(index);
    if (r == nullptr) return;
    const auto typeId = r->typeId, oldName = r->name, clip = r->clip;   // copied: the command rebuilds the state
    // A type whose cue is the built-in clip of its name gets the built-in clip of its new name, so the voice says that.
    auto* params = new juce::DynamicObject();
    const juce::var holder(params);
    params->setProperty("typeId", typeId);
    params->setProperty("name", name.trim());
    if (const auto renamed = builtInClip(name.trim()); clip == builtInClip(oldName) && model::isPathInsideFolder(renamed))
        params->setProperty("cueClip", renamed);
    run("sectionType.set", holder);
}

void CueVoicePage::setTypeColour(int index, juce::Colour colour) {
    const auto* r = row(index);
    if (r == nullptr) return;
    run("sectionType.set", jsonObject({{"typeId", r->typeId}, {"colour", colour.toDisplayString(false)}}));
}

void CueVoicePage::addType(const juce::String& name) {
    const auto key = juce::Uuid().toString();                     // the type and its cue are one undo step
    const auto added = editable() ? session_.commands().run("sectionType.add", jsonObject({{"name", name.trim()}}), key)
                                  : model::CommandResult::failure("Show mode never changes the band folder. Change it after the show.");
    if (!added.ok) {
        say(added.message, true);
        return;
    }
    if (const auto clip = builtInClip(name.trim()); model::isPathInsideFolder(clip))
        run("sectionType.set", jsonObject({{"typeId", added.value.toString()}, {"cueClip", clip}}), key);
    else
        refresh(true);
}

void CueVoicePage::colourMenu(int index) {
    const auto* r = row(index);
    if (r == nullptr || !editable()) return;
    juce::PopupMenu menu;
    const auto& colours = sectionTypeColours();
    for (int i = 0; i < static_cast<int>(colours.size()); ++i)
        menu.addColouredItem(i + 1, colours[static_cast<std::size_t>(i)].first, colours[static_cast<std::size_t>(i)].second, true,
                             r->colour == colours[static_cast<std::size_t>(i)].second);
    menu.showMenuAsync(juce::PopupMenu::Options().withMousePosition(),
                       [safe = juce::Component::SafePointer<CueVoicePage>(this), typeId = r->typeId](int result) {
                           if (safe == nullptr || result <= 0) return;
                           for (int i = 0; i < static_cast<int>(safe->state_.rows.size()); ++i)
                               if (safe->state_.rows[static_cast<std::size_t>(i)].typeId == typeId)
                                   safe->setTypeColour(i, sectionTypeColours()[static_cast<std::size_t>(result - 1)].second);
                       });
}

// ---- Layout and drawing ------------------------------------------------------------------------------------------------

int CueVoicePage::idealWidth() const {
    const auto caption = [](const juce::String& text) { return widthInPixels(captionFont(), text); };
    const int voiceW = std::max(caption("VOICE"), widthInPixels(bodyFont(), kVoice) + kButtonGap + make_.idealWidth());
    const int levelW = std::max({caption("LEVEL"), kValueMinWidth, widthInPixels(nameFont(), state_.level) + 2 * kValuePadX});
    const int heardW = std::max(caption("HEARD IN"), heardWidth(state_.heardIn));
    const int top = 2 * kBoxPadX + voiceW + std::max(caption("SAY IT"), lead_.idealWidth()) + levelW + heardW + hearAll_.idealWidth()
                    + 4 * kGroupGap + kGroupGap;
    const int table = name_width_ + colour_width_ + cue_width_ + kPlayWidth + 12;   // and the scroll bar's room
    return std::max({kMinColumnWidth, top, table});
}

void CueVoicePage::resized() {
    // The table's columns first, from what they hold: the column is as wide as the widest of the panel and the table.
    int nameW = widthInPixels(headFont(), "SECTION TYPE"), pillW = widthInPixels(headFont(), "COLOUR"), cueW = kMinCueWidth;
    for (const auto& r : state_.rows) {
        const int yours = r.builtIn ? 0 : 4 + widthInPixels(hintFont(), kMiddleDot + " yours");
        nameW = std::max(nameW, widthInPixels(nameFont(), r.name) + yours);
        pillW = std::max(pillW, widthInPixels(pillFont(), r.name) + 2 * kPillPadX);
        const auto choice = makeCueChoice(state_, r);
        int widest = 0;
        for (const auto& item : choice.items) widest = std::max(widest, widthInPixels(bodyFont(), item));
        cueW = std::max(cueW, std::min(widest, 260) + kChoiceExtra + (r.clipMissing ? kHintGap + Rows::hintWidth(r) : 0));
    }
    name_width_ = std::min(nameW, 220) + 2 * kCellPadX;
    colour_width_ = std::min(pillW, 160) + 2 * kCellPadX;
    cue_width_ = cueW + 2 * kCellPadX;

    auto area = getLocalBounds();
    column_ = area.withWidth(std::min(area.getWidth(), idealWidth()));
    area = column_;
    // When the window is too narrow for the table at its widths, the cue column gives up the room.
    const int tableRoom = column_.getWidth() - name_width_ - colour_width_ - kPlayWidth - 12;
    cue_width_ = std::max(0, std::min(cue_width_, tableRoom));

    // The panel: VOICE (the words and the button), SAY IT, LEVEL, HEARD IN, then ▶ Hear all at its right.
    top_ = area.removeFromTop(2 * kBoxPadY + kCaptionHeight + kCaptionGap + kControlHeight);
    const int groupTop = top_.getY() + kBoxPadY, controlTop = groupTop + kCaptionHeight + kCaptionGap;
    const auto caption = [](const juce::String& text) { return widthInPixels(captionFont(), text); };
    const int hearW = hearAll_.idealWidth();
    hearAll_.setBounds(top_.getRight() - kBoxPadX - hearW, controlTop, hearW, kControlHeight);
    int x = top_.getX() + kBoxPadX;
    const int levelW = std::max({caption("LEVEL"), kValueMinWidth, widthInPixels(nameFont(), state_.level) + 2 * kValuePadX});
    const int heardW = std::max(caption("HEARD IN"), heardWidth(state_.heardIn));
    const int leadW = std::max(caption("SAY IT"), lead_.idealWidth());
    const int makeW = make_.idealWidth();
    // The voice's words give up room first, shortened with "…".
    const int fixed = leadW + levelW + heardW + hearW + 4 * kGroupGap + kGroupGap + kButtonGap + makeW + 2 * kBoxPadX;
    const int textW = std::clamp(top_.getWidth() - fixed, 0, widthInPixels(bodyFont(), kVoice));
    const int voiceW = std::max(caption("VOICE"), textW + kButtonGap + makeW);
    voice_label_ = {x, groupTop, voiceW, kCaptionHeight};
    voice_text_ = {x, controlTop, textW, kControlHeight};
    make_.setBounds(x + textW + kButtonGap, controlTop, makeW, kControlHeight);
    x += voiceW + kGroupGap;
    lead_label_ = {x, groupTop, leadW, kCaptionHeight};
    lead_.setBounds(x, controlTop, std::max(lead_.idealWidth(), leadW), kControlHeight);
    x += leadW + kGroupGap;
    level_label_ = {x, groupTop, levelW, kCaptionHeight};
    level_box_ = {x, controlTop + 2, levelW, kControlHeight - 4};
    x += levelW + kGroupGap;
    heard_label_ = {x, groupTop, heardW, kCaptionHeight};
    heard_area_ = {x, controlTop, std::max(0, std::min(heardW, hearAll_.getX() - kGroupGap - x)), kControlHeight};

    // The progress or what went wrong, under the panel.
    const bool line = making_ || message_.isNotEmpty();
    area.removeFromTop(kColumnGap);
    message_line_ = line ? area.removeFromTop(kMessageHeight) : juce::Rectangle<int>(area.getX(), area.getY(), area.getWidth(), 0);
    if (line) area.removeFromTop(kColumnGap);

    // The table's box: the header, the rows (scrolling when they don't fit), and the footer straight under them.
    table_ = area;
    header_ = table_.withHeight(kHeaderHeight);
    const int rowsH = static_cast<int>(state_.rows.size()) * kRowHeight;
    const int roomForRows = std::max(0, table_.getHeight() - kHeaderHeight - kFooterHeight);
    viewport_.setBounds(table_.getX(), header_.getBottom(), table_.getWidth(), std::min(rowsH, roomForRows));
    rows_->setSize(viewport_.getWidth() - (rowsH > roomForRows ? viewport_.getScrollBarThickness() : 0), rowsH);
    rows_->layout();
    footer_ = {table_.getX(), viewport_.getBottom(), table_.getWidth(), kFooterHeight};
    auto links = footer_.reduced(kFooterPadX, 0);
    add_type_ = links.removeFromLeft(widthInPixels(linkFont(), kAddType));
    links.removeFromLeft(kFooterGap);
    import_ = links.removeFromLeft(widthInPixels(linkFont(), kImport));
    links.removeFromLeft(kFooterGap);
    others_ = links;
    keepOffKeyboard(*this);
}

void CueVoicePage::paint(juce::Graphics& g) {
    // The panel.
    theme::paintPanel(g, top_.toFloat());
    g.setColour(theme::dim);
    g.setFont(captionFont());
    g.drawText("VOICE", voice_label_, juce::Justification::centredLeft, false);
    g.drawText("SAY IT", lead_label_, juce::Justification::centredLeft, false);
    g.drawText("LEVEL", level_label_, juce::Justification::centredLeft, false);
    g.drawText("HEARD IN", heard_label_, juce::Justification::centredLeft, false);
    g.setColour(theme::text);
    g.setFont(bodyFont());
    g.drawText(shorten(kVoice, bodyFont(), static_cast<float>(voice_text_.getWidth())), voice_text_,
               juce::Justification::centredLeft, false);

    const auto level = level_box_.toFloat();
    g.setColour(theme::field);
    g.fillRoundedRectangle(level, theme::radiusField);
    g.setColour(theme::hairline);
    g.drawRoundedRectangle(level.reduced(0.5f), theme::radiusField, 1.0f);
    g.setColour(theme::text);
    g.setFont(nameFont());
    g.drawText(state_.level, level_box_.reduced(kValuePadX, 0), juce::Justification::centredRight, false);

    if (state_.heardIn.startsWith("In-ears ")) {
        const float pillW = outputLabelPillWidth(model::OutputLabel::Monitor);
        const auto pill = juce::Rectangle<float>(static_cast<float>(heard_area_.getX()),
                                                 heard_area_.getCentreY() - kOutputLabelPillHeight / 2.0f,
                                                 std::min(pillW, static_cast<float>(heard_area_.getWidth())), kOutputLabelPillHeight);
        drawOutputLabelPill(g, model::OutputLabel::Monitor, pill);
        const auto rest = heard_area_.withTrimmedLeft(juce::roundToInt(pillW) + 4);
        g.setColour(theme::mid);
        g.setFont(bodyFont());
        const auto words = state_.heardIn.fromFirstOccurrenceOf("In-ears ", false, false);
        g.drawText(shorten(words, bodyFont(), static_cast<float>(rest.getWidth())),
                   rest, juce::Justification::centredLeft, false);
    } else {
        g.setColour(theme::caution);
        g.setFont(bodyFont());
        g.drawText(shorten(state_.heardIn, bodyFont(), static_cast<float>(heard_area_.getWidth())), heard_area_,
                   juce::Justification::centredLeft, false);
    }

    // The progress line, or what went wrong.
    if (!message_line_.isEmpty()) {
        auto line = message_line_;
        if (making_) {
            const juce::String words = "Making the voice" + kEllipsis + " " + juce::String(juce::roundToInt(progress_ * 100.0)) + "%";
            const int w = widthInPixels(hintFont(), words);
            g.setColour(theme::mid);
            g.setFont(hintFont());
            g.drawText(words, line.removeFromLeft(w), juce::Justification::centredLeft, false);
            line.removeFromLeft(kHintGap);
            const int barW = std::min(160, line.getWidth());
            const auto bar = line.removeFromLeft(barW).withSizeKeepingCentre(barW, 4).toFloat();
            g.setColour(theme::field);
            g.fillRoundedRectangle(bar, 2.0f);
            g.setColour(theme::mint);
            g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * static_cast<float>(std::clamp(progress_, 0.0, 1.0))), 2.0f);
        } else {
            g.setColour(message_problem_ ? theme::caution : theme::mint);
            g.setFont(hintFont());
            g.drawText(shorten(message_, hintFont(), static_cast<float>(line.getWidth())), line, juce::Justification::centredLeft, false);
        }
    }

    // The table's box, its header and footer.
    theme::paintPanel(g, table_.toFloat());
    g.setColour(theme::hairline);
    g.fillRect(header_.getX() + 1, header_.getBottom() - 1, header_.getWidth() - 2, 1);
    g.setColour(theme::dim);
    g.setFont(headFont());
    int x = header_.getX();
    for (const auto& [title, w] : {std::pair{"SECTION TYPE", name_width_}, std::pair{"COLOUR", colour_width_},
                                   std::pair{"CUE", cue_width_}}) {
        g.drawText(title, x + kCellPadX, header_.getY(), std::max(0, w - 2 * kCellPadX), header_.getHeight(),
                   juce::Justification::centredLeft, false);
        x += w;
    }

    const bool edit = editable();
    g.setFont(linkFont());
    g.setColour(theme::mint.withAlpha(edit ? 1.0f : 0.4f));
    g.drawText(kAddType, add_type_, juce::Justification::centredLeft, false);
    g.drawText(kImport, import_, juce::Justification::centredLeft, false);
    g.setColour(theme::dim);
    g.setFont(hintFont());
    const auto others = "Other clips: " + state_.otherClips.joinIntoString(", ");
    g.drawText(shorten(others, hintFont(), static_cast<float>(others_.getWidth())), others_,
               juce::Justification::centredLeft, false);
}

void CueVoicePage::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) return;
    const auto at = e.getPosition();
    if (level_box_.contains(at)) {
        askLevel();
    } else if (add_type_.contains(at) && editable()) {
        askUserText(*this, "Add a section type", {}, "Add",
                    [safe = juce::Component::SafePointer<CueVoicePage>(this)](const juce::String& name) {
            if (safe != nullptr) safe->addType(name);
        });
    } else if (import_.contains(at) && editable()) {
        askImport({});
    }
}

void CueVoicePage::mouseMove(const juce::MouseEvent& e) {
    const auto at = e.getPosition();
    const bool link = level_box_.contains(at) || (editable() && (add_type_.contains(at) || import_.contains(at)));
    setMouseCursor(link ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

}  // namespace sqlive::ui
