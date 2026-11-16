#include "ui/MixerPanel.h"

#include <algorithm>
#include <cmath>

#include "model/Routing.h"
#include "model/Units.h"
#include "ui/Format.h"
#include "ui/Theme.h"

namespace sqlive::ui {

using app::LevelTarget;

namespace {

// The mockup's CSS: .mixer (padding 14 and gap 10 on screen 10; 10 and 6 in the main view), .ch (a column, gap 6),
// .ch .nm (11 px semibold, mid), .hint (12 px, dim), .fader (an 8 px track and a 32 px cap), .ms b (18 x 16, 3 px
// corners, 10 px bold), .sep (1 px, hairline).
constexpr int kPad = 14, kGap = 10, kCompactPad = 10, kCompactGap = 6;
constexpr int kMinStripWidth = 56, kStripGap = 6, kSeparatorWidth = 1;
constexpr int kNameHeight = 15, kHintHeight = 16, kFaderWidth = 32, kMuteWidth = 18, kMuteHeight = 16;

const juce::String kInEarsMark = juce::String::fromUTF8(" \xe2\x97\x89");     // " ◉"
const juce::Colour kOnRed{0xff160f10};                                       // .ms b.m text

juce::Font nameFont() { return theme::font(theme::sizeCaption, true); }
juce::Font hintFont() { return theme::font(theme::sizeSmall); }
juce::Font muteFont() { return theme::font(theme::sizeTag, true); }

float widthOf(const juce::Font& font, const juce::String& text) { return juce::GlyphArrangement::getStringWidth(font, text); }

// Nothing in the mixer takes the keyboard, not even with a click: in Show mode Space and the arrows must reach the window.
void keepOffKeyboard(juce::Component& component) {
    component.setWantsKeyboardFocus(false);
    component.setMouseClickGrabsKeyboardFocus(false);
    for (auto* child : component.getChildren()) keepOffKeyboard(*child);
}

juce::String soundName(const juce::String& sound) {
    if (sound == "beep") return "Beep";
    if (sound == "rim") return "Rim";
    return "Wood block";
}

juce::String labelName(model::OutputLabel label) {
    switch (label) {
    case model::OutputLabel::Monitor: return "In-ears";
    case model::OutputLabel::Foh: return "PA";
    case model::OutputLabel::Timecode: return "Timecode";
    case model::OutputLabel::Unused: return "Not used";
    }
    return {};
}

int groupOf(model::RoleKind kind) {
    switch (kind) {
    case model::RoleKind::Click:
    case model::RoleKind::Cues: return 0;
    case model::RoleKind::Part: return 1;
    case model::RoleKind::Pad: return 2;
    }
    return 1;
}

// A level as the band folder has it, without tonight's changes; nullopt = not sent, or no such thing.
std::optional<float> savedLevel(const app::Session& session, const LevelTarget& target) {
    const auto& model = session.model();
    switch (target.kind) {
    case LevelTarget::Kind::Part:
        if (const auto* song = model.song(target.songId))
            for (const auto& part : song->parts)
                if (part.id == target.partId) return part.gainDb;
        return std::nullopt;
    case LevelTarget::Kind::SongClick:
        if (const auto* song = model.song(target.songId)) return song->click.offsetDb;
        return std::nullopt;
    case LevelTarget::Kind::Mix:
    case LevelTarget::Kind::OutputTrim:
        if (const auto* rig = model.rig(session.rigId()))
            for (const auto& out : rig->outputs) {
                if (out.id != target.outputId) continue;
                if (target.kind == LevelTarget::Kind::OutputTrim) return out.trimDb;
                const auto sent = out.mix.find(target.roleId);
                return sent != out.mix.end() ? std::optional<float>(sent->second) : std::nullopt;
            }
        return std::nullopt;
    case LevelTarget::Kind::BandClick: return model.band().clickDb;
    case LevelTarget::Kind::BandCues: return model.band().cuesDb;
    case LevelTarget::Kind::BandPad: return model.band().padDb;
    }
    return std::nullopt;
}

bool savedMute(const app::Session& session, const LevelTarget& target) {
    if (target.kind != LevelTarget::Kind::Part) return false;
    if (const auto* song = session.model().song(target.songId))
        for (const auto& part : song->parts)
            if (part.id == target.partId) return part.mute;
    return false;
}

// What changed tonight, for the footer: "+2 dB", "-1.5 dB", "off" (no longer sent), "was off", "muted", "unmuted".
juce::String tonightChange(const app::Session& session, const MixerState::Strip& strip) {
    const auto saved = savedLevel(session, strip.target);
    if (!strip.db) return "off";
    if (!saved) return "was off";
    const float change = std::round((*strip.db - *saved) * 10.0f) / 10.0f;
    if (change == 0.0f && strip.target.kind == LevelTarget::Kind::Part && strip.muted != savedMute(session, strip.target))
        return strip.muted ? "muted" : "unmuted";
    return formatDb(change) + " dB";
}

}  // namespace

// ---- The state ---------------------------------------------------------------------------------------------------------

MixerState makeMixerState(const app::Session& session, const juce::String& songId, const juce::String& outputId) {
    using Strip = MixerState::Strip;
    MixerState state;
    const auto& model = session.model();
    const bool show = session.mode() == app::Mode::Show;

    state.options.push_back({{}, "This song"});
    const model::OutputDoc* output = nullptr;
    if (const auto* rig = model.rig(session.rigId()))
        for (const auto& out : rig->outputs) {
            state.options.push_back({out.id, out.name});
            if (outputId.isNotEmpty() && out.id == outputId) {
                output = &out;
                state.chosen = static_cast<int>(state.options.size()) - 1;
            }
        }

    if (output == nullptr) {
        state.panelTitle = "THIS SONG";
        state.panelText = "How loud each part is in this song, for everyone.";
        if (model.song(songId) != nullptr) {
            const auto heard = session.songAsHeard(songId);
            Strip click;
            click.target = show ? LevelTarget{LevelTarget::Kind::BandClick, {}, {}, {}, {}}
                                : LevelTarget{LevelTarget::Kind::SongClick, songId, {}, {}, {}};
            click.name = "Click";
            click.colour = theme::ember;
            click.db = session.level(click.target);
            click.inEarsOnly = true;
            click.group = 0;
            if (!show) click.footer = soundName(heard.click.sound);
            state.strips.push_back(click);

            for (const auto& part : heard.parts) {
                Strip strip;
                strip.target = {LevelTarget::Kind::Part, songId, part.id, {}, {}};
                strip.name = part.name;
                const auto* role = model.role(part.roleId);
                strip.colour = role != nullptr ? role->colour : theme::mid;
                strip.db = part.gainDb;
                strip.muted = part.mute;
                strip.canMute = true;
                if (role != nullptr && role->name != part.name) strip.footer = role->name;
                state.strips.push_back(strip);
            }
        }
    } else {
        state.panelTitle = output->name.toUpperCase();
        state.panelLabel = labelName(output->label);
        state.panelLabelKind = output->label;
        state.panelChannels = "OUT " + formatChannels(output->firstChannel, output->stereo);

        const auto rig = session.rigAsHeard();
        const auto heard = std::find_if(rig.outputs.begin(), rig.outputs.end(),
                                        [&](const model::OutputDoc& o) { return o.id == output->id; });
        const auto& mix = heard != rig.outputs.end() ? heard->mix : output->mix;

        // Click and cues first, then the parts, then the pad, each in the band's order.
        std::vector<const model::Role*> roles;
        for (const auto& role : model.band().roles)
            if (role.kind != model::RoleKind::Cues && role.kind != model::RoleKind::Pad && model::mayRoute(role.kind, output->label))
                roles.push_back(&role);
        std::stable_sort(roles.begin(), roles.end(),
                         [](const model::Role* a, const model::Role* b) { return groupOf(a->kind) < groupOf(b->kind); });

        juce::StringArray said;
        for (const auto* role : roles) {
            Strip strip;
            strip.target = {LevelTarget::Kind::Mix, {}, {}, output->id, role->id};
            strip.name = role->name;
            strip.colour = role->kind == model::RoleKind::Click ? theme::ember : role->colour;
            if (const auto sent = mix.find(role->id); sent != mix.end()) strip.db = sent->second;
            strip.muted = !strip.db.has_value();
            strip.canMute = true;
            strip.inEarsOnly = !model::mayRoute(role->kind, model::OutputLabel::Foh);
            strip.group = groupOf(role->kind);
            state.strips.push_back(strip);

            if (!strip.db) said.add("no " + role->name.toLowerCase());
            else if (*strip.db > 0.0f) said.add("plenty of " + role->name.toLowerCase());
        }
        const juce::String always = "This mix is the same in every song.";
        state.panelText = said.isEmpty() ? always
                                         : "What the " + output->name.toLowerCase() + " hears: " + said.joinIntoString(", ") + ". " + always;
    }

    for (auto& strip : state.strips) {
        strip.value = strip.db ? formatStripDb(*strip.db) : "off";
        strip.changedTonight = session.changedTonight(strip.target);
        if (strip.changedTonight) {
            strip.footer = tonightChange(session, strip);
            strip.footerIsWarning = true;
        }
    }
    return state;
}

// ---- A strip -----------------------------------------------------------------------------------------------------------

namespace {

// The mockup's .ms b: "M" on a field in dim, or on danger in near-black when on. A press flips it.
class MuteButton : public juce::Component {
public:
    void setOn(bool on) {
        if (on == on_) return;
        on_ = on;
        repaint();
    }
    std::function<void()> onPress;

    void paint(juce::Graphics& g) override {
        const auto r = getLocalBounds().toFloat();
        g.setColour(on_ ? theme::danger : (isMouseOver() ? theme::fieldHover : theme::field));
        g.fillRoundedRectangle(r, 3.0f);
        g.setColour(on_ ? kOnRed : theme::dim);
        g.setFont(muteFont());
        g.drawText("M", getLocalBounds(), juce::Justification::centred, false);
    }
    void mouseDown(const juce::MouseEvent& e) override {
        if (!e.mods.isPopupMenu() && onPress) onPress();
    }
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }

private:
    bool on_ = false;
};

}  // namespace

class MixerPanel::StripView : public juce::Component {
public:
    StripView(MixerPanel& panel, int index) : panel_(panel), index_(index) {
        addAndMakeVisible(fader_);
        addChildComponent(mute_);
        fader_.onDrag = [this](float db, bool start, bool end) { panel_.faderMoved(index_, db, start, end); };
        mute_.onPress = [this] { panel_.toggleMute(index_); };
    }

    void set(const MixerState::Strip& strip, bool compact, bool metering) {
        const bool layoutChanged = compact != compact_ || strip.canMute != strip_.canMute;
        strip_ = strip;
        compact_ = compact;
        fader_.setValue(strip.db);
        fader_.setColour(strip.colour);
        fader_.setChangedTonight(strip.changedTonight);
        fader_.setMeter(metering && strip.db && !strip.muted ? model::dbToGain(*strip.db) : 0.0f);
        mute_.setVisible(strip.canMute);
        mute_.setOn(strip.muted);
        if (layoutChanged) resized();
        repaint();
    }

    void paint(juce::Graphics& g) override {
        const float w = static_cast<float>(getWidth());
        // The name, with the in-ears mark after it, centred together and shortened to fit.
        const auto nf = nameFont();
        const float markWidth = strip_.inEarsOnly ? widthOf(nf, kInEarsMark) : 0.0f;
        const auto name = shorten(strip_.name, nf, std::max(0.0f, w - markWidth));
        const float nameWidth = widthOf(nf, name);
        const float x = std::max(0.0f, 0.5f * (w - nameWidth - markWidth));
        g.setFont(nf);
        g.setColour(theme::mid);
        g.drawText(name, juce::Rectangle<float>(x, 0.0f, nameWidth + 1.0f, kNameHeight), juce::Justification::centredLeft, false);
        if (strip_.inEarsOnly) {
            g.setColour(theme::dim);
            g.drawText(kInEarsMark, juce::Rectangle<float>(x + nameWidth, 0.0f, markWidth + 1.0f, kNameHeight),
                       juce::Justification::centredLeft, false);
        }

        g.setFont(hintFont());
        if (!compact_) {
            g.setColour(theme::dim);
            g.drawText(shorten(strip_.value, hintFont(), w), value_, juce::Justification::centred, false);
        }
        if (strip_.footer.isNotEmpty() && (!compact_ || strip_.footerIsWarning)) {
            g.setColour(strip_.footerIsWarning ? theme::caution : theme::dim);
            g.drawText(shorten(strip_.footer, hintFont(), w), footer_, juce::Justification::centred, false);
        }
    }

    void resized() override {
        auto area = getLocalBounds();
        area.removeFromTop(kNameHeight + kStripGap);
        value_ = compact_ ? juce::Rectangle<int>() : area.removeFromTop(kHintHeight);
        if (!compact_) area.removeFromTop(kStripGap);
        footer_ = area.removeFromBottom(kHintHeight);
        area.removeFromBottom(kStripGap);
        const auto muteRow = area.removeFromBottom(kMuteHeight);
        area.removeFromBottom(kStripGap);
        mute_.setBounds(muteRow.withSizeKeepingCentre(kMuteWidth, kMuteHeight));
        fader_.setBounds(area.withSizeKeepingCentre(std::min(kFaderWidth, area.getWidth()), std::max(0, area.getHeight())));
    }

    LevelFader& fader() { return fader_; }

private:
    MixerPanel& panel_;
    const int index_;
    MixerState::Strip strip_;
    bool compact_ = false;
    LevelFader fader_;
    MuteButton mute_;
    juce::Rectangle<int> value_, footer_;
};

// The strips side by side, with a thin line between groups; what the viewport scrolls.
class MixerPanel::Row : public juce::Component {
public:
    Row() { setInterceptsMouseClicks(false, true); }
    std::vector<int> separators;                                  // x of each line

    void paint(juce::Graphics& g) override {
        g.setColour(theme::hairline);
        for (const int x : separators) g.fillRect(x, 0, kSeparatorWidth, getHeight());
    }
};

// ---- The panel ---------------------------------------------------------------------------------------------------------

MixerPanel::MixerPanel(app::Session& session) : session_(session), row_(std::make_unique<Row>()) {
    viewport_.setViewedComponent(row_.get(), false);
    viewport_.setScrollBarsShown(false, true, false, true);
    addAndMakeVisible(viewport_);
    keepOffKeyboard(*this);
    session_.changes().addChangeListener(this);
}

MixerPanel::~MixerPanel() { session_.changes().removeChangeListener(this); }

void MixerPanel::show(const juce::String& songId, const juce::String& outputId) {
    songId_ = songId;
    outputId_ = outputId;
    refresh(true);
}

void MixerPanel::setCompact(bool compact) {
    if (compact == compact_) return;
    compact_ = compact;
    for (std::size_t i = 0; i < strips_.size(); ++i) strips_[i]->set(state_.strips[i], compact_, metering_);
    resized();
    repaint();
}

const juce::Viewport& MixerPanel::viewport() const { return viewport_; }

void MixerPanel::toggleMute(int stripIndex) {
    if (!juce::isPositiveAndBelow(stripIndex, static_cast<int>(state_.strips.size()))) return;
    const auto strip = state_.strips[static_cast<std::size_t>(stripIndex)];
    if (!strip.canMute) return;
    if (strip.target.kind == LevelTarget::Kind::Part)
        session_.setMute(strip.target.songId, strip.target.partId, !strip.muted);
    else if (strip.target.kind == LevelTarget::Kind::Mix)
        session_.setLevel(strip.target, strip.muted ? std::optional<float>(0.0f) : std::nullopt, juce::Uuid().toString());
    refresh(true);
}

// A press starts a gesture and a release ends it; neither is a level of its own (a fader that isn't sent reports -60
// there, which isn't "off"). Each move, and a double-click or wheel notch (a gesture of its own), is, except one that
// leaves a fader that isn't sent at the bottom: a pixel's wobble, or a notch down, doesn't start sending it at -60.
void MixerPanel::faderMoved(int stripIndex, float db, bool gestureStart, bool gestureEnd) {
    if (!juce::isPositiveAndBelow(stripIndex, static_cast<int>(state_.strips.size()))) return;
    if (gestureStart) gesture_ = juce::Uuid().toString();
    if (gestureStart != gestureEnd) return;
    const auto& strip = state_.strips[static_cast<std::size_t>(stripIndex)];
    if (!strip.db && db <= LevelFader::kMinDb) {
        strips_[static_cast<std::size_t>(stripIndex)]->set(strip, compact_, metering_);   // the cap back to "not sent"
        return;
    }
    session_.setLevel(strip.target, db, gesture_);
    refresh(true);
}

void MixerPanel::changeListenerCallback(juce::ChangeBroadcaster*) { refresh(); }

void MixerPanel::visibilityChanged() {
    if (isVisible() && !fresh_) refresh();
}

bool MixerPanel::shownWithParents() const {
    for (const auto* c = static_cast<const juce::Component*>(this); c->getParentComponent() != nullptr; c = c->getParentComponent())
        if (!c->isVisible()) return false;
    return true;
}

void MixerPanel::refresh(bool force) {
    // Hidden, it waits until it is shown: the session's changes come 30 times a second while a song plays.
    if (!force && !shownWithParents()) {
        fresh_ = false;
        return;
    }
    fresh_ = true;
    ++states_made_;
    auto next = makeMixerState(session_, songId_, outputId_);
    const auto now = session_.now();
    const bool metering = now.playing && (outputId_.isNotEmpty() || now.playingSongId == songId_);   // drawn at each fader's level
    const bool stateChanged = !(next == state_);
    if (!stateChanged && metering == metering_ && strips_.size() == state_.strips.size()) return;

    const bool sameStrips = next.strips.size() == strips_.size()
                            && std::equal(next.strips.begin(), next.strips.end(), state_.strips.begin(),
                                          [](const MixerState::Strip& a, const MixerState::Strip& b) {
                                              return a.target == b.target && a.group == b.group;
                                          });
    state_ = std::move(next);
    metering_ = metering;
    if (!sameStrips) {
        // Made again only when the strips themselves change, never during a drag of one of them.
        strips_.clear();
        for (std::size_t i = 0; i < state_.strips.size(); ++i) {
            strips_.push_back(std::make_unique<StripView>(*this, static_cast<int>(i)));
            row_->addAndMakeVisible(*strips_.back());
        }
        keepOffKeyboard(*row_);
    }
    for (std::size_t i = 0; i < strips_.size(); ++i) strips_[i]->set(state_.strips[i], compact_, metering_);
    if (!sameStrips) layoutRow();
    if (stateChanged && onStateChanged) onStateChanged();
}

void MixerPanel::paint(juce::Graphics& g) { theme::paintPanel(g, getLocalBounds().toFloat()); }

void MixerPanel::resized() {
    viewport_.setBounds(getLocalBounds().reduced(compact_ ? kCompactPad : kPad));
    keepOffKeyboard(viewport_);                                   // the scroll bars, made again when the look changes
    layoutRow();
}

void MixerPanel::layoutRow() {
    const int gap = compact_ ? kCompactGap : kGap;
    const int count = static_cast<int>(strips_.size());
    int separators = 0;
    for (int i = 1; i < count; ++i)
        if (state_.strips[static_cast<std::size_t>(i)].group != state_.strips[static_cast<std::size_t>(i) - 1].group) ++separators;

    const int fixed = std::max(0, count - 1) * gap + separators * (kSeparatorWidth + gap);
    const int available = viewport_.getWidth();
    const bool scrolls = count * kMinStripWidth + fixed > available;
    const int stripsWidth = scrolls ? count * kMinStripWidth : available - fixed;
    const int rowWidth = scrolls ? stripsWidth + fixed : available;
    const int rowHeight = std::max(0, viewport_.getHeight() - (scrolls ? viewport_.getScrollBarThickness() : 0));
    row_->setSize(rowWidth, rowHeight);

    row_->separators.clear();
    int x = 0;
    for (int i = 0; i < count; ++i) {
        if (i > 0) {
            x += gap;
            if (state_.strips[static_cast<std::size_t>(i)].group != state_.strips[static_cast<std::size_t>(i) - 1].group) {
                row_->separators.push_back(x);
                x += kSeparatorWidth + gap;
            }
        }
        // The width shared out evenly, the pixels left over going to the first strips.
        const int width = count > 0 ? stripsWidth / count + (i < stripsWidth % count ? 1 : 0) : 0;
        strips_[static_cast<std::size_t>(i)]->setBounds(x, 0, width, rowHeight);
        x += width;
    }
    row_->repaint();
}

}  // namespace sqlive::ui
