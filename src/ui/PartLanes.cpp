#include "ui/PartLanes.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include "app/SongCache.h"
#include "model/JsonObject.h"
#include "ui/BarScale.h"
#include "ui/Format.h"
#include "ui/Helpers.h"
#include "ui/Theme.h"

namespace sqlive::ui {

using model::jsonObject;

namespace {

const juce::StringArray kSubdivisionNames{"none", "eighths", "triplets", "sixteenths"};   // engine::Subdivision's order

juce::Font smallFont() { return theme::font(theme::sizeSmall); }

void say(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& words, juce::Colour colour) {
    g.setColour(colour);
    g.setFont(smallFont());
    g.drawText(words, area, juce::Justification::centredLeft, true);
}

}  // namespace

// ---- A part ------------------------------------------------------------------------------------------------------------

PartLane::PartLane(app::Session& session, juce::String songId, juce::String partId)
    : session_(session), songId_(std::move(songId)), partId_(std::move(partId)) {
    refresh();
}

void PartLane::setMissing(bool missing) {
    if (missing == missing_) return;
    missing_ = missing;
    repaint();
}

void PartLane::refresh() {
    const auto* song = session_.model().song(songId_);
    if (song == nullptr) return;                                  // undone away: the page is about to hear
    const auto part = std::find_if(song->parts.begin(), song->parts.end(), [&](const auto& p) { return p.id == partId_; });
    if (part != song->parts.end()) {
        const auto* role = session_.model().role(part->roleId);
        name_ = part->name;
        colour_ = role != nullptr ? role->colour : theme::dim;
    }
    const auto state = session_.cache().state(songId_);
    loading_ = state == app::SongCache::State::Loading || state == app::SongCache::State::NotLoaded;
    progress_ = loading_ ? session_.cache().progress(songId_) : 0.0;
    auto loaded = session_.cache().loaded(songId_);
    if (loaded != loaded_ || song->audioStartSeconds != barOne_) {
        loaded_ = std::move(loaded);
        barOne_ = song->audioStartSeconds;
        built_ = false;
    }
    repaint();
}

void PartLane::paint(juce::Graphics& g) {
    if (scale() == nullptr) return;
    const auto words = getLocalBounds().reduced(10, 0);
    if (missing_) return say(g, words, "The audio file is missing", theme::caution);
    if (loading_) return say(g, words, "Loading" + kEllipsis + " " + juce::String(juce::roundToInt(progress_ * 100.0)) + "%", theme::dim);
    if (!built_) build();                                         // a pixel's columns: the summary is made already
    if (!hasWave_) return say(g, words, "The audio file couldn't be read", theme::caution);

    const float mid = static_cast<float>(getHeight()) * 0.5f;
    const float half = static_cast<float>(height - 2 * waveInset) * 0.5f;
    const float gain = peak_ > 1.0e-6f ? 0.9f * half / peak_ : 0.0f;
    // The mockup's canvas.wf: a 1.3 px bar every 2 px, at 75 % opacity, so a dense part keeps its texture.
    g.setColour(colour_.withMultipliedAlpha(0.75f));
    for (std::size_t x = 0; x < lo_.size(); x += 2) {
        float lo = lo_[x], hi = hi_[x];
        if (x + 1 < lo_.size() && !std::isnan(lo_[x + 1])) {     // the bar covers both pixels' columns
            lo = std::isnan(lo) ? lo_[x + 1] : std::min(lo, lo_[x + 1]);
            hi = std::isnan(hi) ? hi_[x + 1] : std::max(hi, hi_[x + 1]);
        }
        if (std::isnan(lo)) continue;
        // From the bar's top to its bottom, always through the centre line: a bar wholly above or below zero still
        // reaches it.
        const float top = mid - hi * gain, bottom = mid - lo * gain;
        const float y = std::min(top, mid - 0.5f);
        g.fillRect(static_cast<float>(x), y, 1.3f, std::max(bottom, mid + 0.5f) - y);
    }
}

void PartLane::build() {
    built_ = true;
    hasWave_ = false;
    lo_.clear();
    hi_.clear();
    if (loaded_ == nullptr || scale() == nullptr) return;
    const auto found = loaded_->waves.find(partId_);
    if (found == loaded_->waves.end()) return;
    hasWave_ = true;
    const auto& summary = found->second;
    const auto& scale = *this->scale();
    const int width = static_cast<int>(scale.width());
    const auto blocks = static_cast<std::int64_t>(summary.lo.size());
    const double rate = loaded_->sampleRate;
    peak_ = summary.peak;
    lo_.assign(static_cast<std::size_t>(std::max(0, width)), std::numeric_limits<float>::quiet_NaN());
    hi_ = lo_;
    constexpr double kBlock = app::WaveSummary::blockFrames;
    const auto frameAt = [&](float px) { return (scale.secondsAtX(px) + barOne_) * rate; };
    for (int x = 0; x < width; ++x) {
        const auto b0 = static_cast<std::int64_t>(std::floor(frameAt(static_cast<float>(x)) / kBlock));
        const auto b1 = std::max(static_cast<std::int64_t>(std::ceil(frameAt(static_cast<float>(x + 1)) / kBlock)), b0 + 1);
        float lo = std::numeric_limits<float>::max(), hi = std::numeric_limits<float>::lowest();
        for (auto b = std::max<std::int64_t>(0, b0); b < std::min(b1, blocks); ++b) {
            lo = std::min(lo, summary.lo[static_cast<std::size_t>(b)]);
            hi = std::max(hi, summary.hi[static_cast<std::size_t>(b)]);
        }
        if (lo <= hi) {
            lo_[static_cast<std::size_t>(x)] = lo;
            hi_[static_cast<std::size_t>(x)] = hi;
        }
    }
}

void PartLane::headerPressed(const juce::MouseEvent&) {
    // The song and the part are looked up now: an undo can take either away before the page has heard.
    const auto* song = session_.model().song(songId_);
    if (song == nullptr) return;
    const auto part = std::find_if(song->parts.begin(), song->parts.end(), [&](const auto& p) { return p.id == partId_; });
    if (part == song->parts.end()) return;
    const bool edit = session_.mode() == app::Mode::Edit;
    juce::PopupMenu roles;
    juce::StringArray roleIds;
    for (const auto& role : session_.model().band().roles) {
        if (role.kind != model::RoleKind::Part) continue;
        roleIds.add(role.id);
        roles.addItem(100 + roleIds.size() - 1, role.name, edit, role.id == part->roleId);
    }
    juce::PopupMenu menu;
    menu.addItem(1, "Rename" + kEllipsis, edit);
    menu.addSubMenu("Role", roles, edit);
    menu.addItem(2, "Remove" + kEllipsis, edit);
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(),
        [safe = juce::Component::SafePointer<PartLane>(this), name = part->name, songTitle = song->title, roleIds](int result) {
            if (safe == nullptr) return;
            const auto with = [safe](const char* key, const juce::var& value) {
                auto params = jsonObject({{"songId", safe->songId_}, {"partId", safe->partId_}});
                if (key != nullptr) params.getDynamicObject()->setProperty(key, value);
                return params;
            };
            if (result == 1) {
                askUserText(*safe, "Rename the part", name, "Rename", [safe, with](const juce::String& newName) {
                    if (safe != nullptr && newName.isNotEmpty()) safe->run(safe->session_, "part.set", with("name", newName));
                });
            } else if (result == 2) {
                safe->message_ = juce::AlertWindow::showScopedAsync(juce::MessageBoxOptions()
                                                                        .withIconType(juce::MessageBoxIconType::NoIcon)
                                                                        .withAssociatedComponent(safe.getComponent())
                                                                        .withTitle("Remove " + name + " from " + songTitle + "?")
                                                                        .withMessage("Undo brings it back.")
                                                                        .withButton("Remove")
                                                                        .withButton("Cancel"),
                                                                    [safe, with](int answer) {
                                                                        if (safe != nullptr && answer == 1)
                                                                            safe->run(safe->session_, "part.remove", with(nullptr, {}));
                                                                    });
            } else if (result >= 100 && result - 100 < roleIds.size()) {
                safe->run(safe->session_, "part.set", with("roleId", roleIds[result - 100]));
            }
        });
}

// ---- The click ---------------------------------------------------------------------------------------------------------

ClickLane::ClickLane(app::Session& session, juce::String songId) : session_(session), songId_(std::move(songId)) { refresh(); }

juce::Colour ClickLane::dot() const { return theme::ember; }

void ClickLane::refresh() {
    const auto* song = session_.model().song(songId_);
    if (song == nullptr) return;
    const bool on = song->click.enabled;
    const int subdivision = std::max(0, kSubdivisionNames.indexOf(song->click.subdivision));
    const int countIn = std::max(0, song->countInBars);
    if (on != on_ || subdivision != subdivision_ || countIn != countInBars_) built_ = false;
    on_ = on;
    subdivision_ = subdivision;
    countInBars_ = countIn;
    repaint();
}

void ClickLane::paint(juce::Graphics& g) {
    if (scale() == nullptr) return;
    if (!built_) build();
    const float mid = static_cast<float>(getHeight()) * 0.5f;
    for (const auto& tick : ticks_) {
        const float h = tick.kind == engine::ClickKind::Downbeat ? 24.0f : tick.kind == engine::ClickKind::Beat ? 12.0f : 6.0f;
        // Off, the click still sounds through the count-in, so only the count-in's ticks stay bright: those before it
        // (audio before bar 1 that is longer than the count-in) and those from bar 1 on are dimmed.
        g.setColour(on_ || tick.countIn ? theme::ember : theme::ember.withAlpha(0.35f));
        g.fillRect(tick.x, mid - h * 0.5f, 1.2f, h);
    }
}

void ClickLane::build() {
    built_ = true;
    ticks_.clear();
    const auto& scale = *this->scale();
    // The ticks at a millisecond's precision, which is far finer than a pixel.
    constexpr double kRate = 1000.0;
    const auto from = static_cast<engine::SamplePos>(std::llround(scale.startSeconds() * kRate));
    const auto to = static_cast<engine::SamplePos>(std::llround(scale.endSeconds() * kRate));
    const auto subdivision = static_cast<engine::Subdivision>(juce::jlimit(0, 3, subdivision_));
    // The count-in is [-its length, 0), as prepareSong's countInStart; it is empty with no count-in.
    const auto countInStart = static_cast<engine::SamplePos>(std::llround(scale.tempo().secondsAt({1 - countInBars_, 1.0}) * kRate));
    for (const auto& tick : engine::buildClickTicks(scale.tempo(), kRate, from, to, subdivision))
        ticks_.push_back({scale.xAtSeconds(static_cast<double>(tick.at) / kRate), tick.kind, tick.at >= countInStart && tick.at < 0});
}

}  // namespace sqlive::ui
