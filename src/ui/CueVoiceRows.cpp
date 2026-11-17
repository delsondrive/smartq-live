#include "ui/CueVoiceRows.h"

#include <algorithm>

#include "ui/Format.h"

namespace sqlive::ui {

using namespace cuevoice;

void CueVoicePage::Rows::update() {
    const auto& rows = page_.state_.rows;
    while (choices_.size() > rows.size()) {
        choices_.pop_back();
        shown_.pop_back();
    }
    while (choices_.size() < rows.size()) {
        const int index = static_cast<int>(choices_.size());
        auto choice = std::make_unique<Choice>();
        choice->onChange = [this, index](int item) { page_.chooseCue(index, item); };
        addAndMakeVisible(*choice);
        choices_.push_back(std::move(choice));
        shown_.emplace_back();
    }
    const bool edit = page_.editable();
    for (std::size_t i = 0; i < rows.size(); ++i) {
        auto next = makeCueChoice(page_.state_, rows[i]);
        auto& choice = *choices_[i];
        if (!(next == shown_[i]) || choice.items() != next.items) choice.setItems(next.items, next.selected);
        else choice.setSelected(next.selected);
        shown_[i] = std::move(next);
        choice.setNeedsAttention(rows[i].clipMissing);
        choice.setEnabled(edit);
    }
    layout();
    repaint();
}

void CueVoicePage::Rows::layout() {
    setSize(getWidth(), static_cast<int>(page_.state_.rows.size()) * kRowHeight);
    const int x = page_.name_width_ + page_.colour_width_ + kCellPadX;
    for (std::size_t i = 0; i < choices_.size(); ++i) {
        const auto& row = page_.state_.rows[i];
        const int room = page_.cue_width_ - 2 * kCellPadX - (row.clipMissing ? hintWidth(row) + kHintGap : 0);
        const int w = std::clamp(choices_[i]->idealWidth(), std::min(120, room), std::max(0, room));
        choices_[i]->setBounds(x, static_cast<int>(i) * kRowHeight + (kRowHeight - kChoiceHeight) / 2, w, kChoiceHeight);
    }
}

void CueVoicePage::Rows::resized() { layout(); }

const Choice* CueVoicePage::Rows::choice(int row) const {
    return juce::isPositiveAndBelow(row, static_cast<int>(choices_.size())) ? choices_[static_cast<std::size_t>(row)].get() : nullptr;
}

void CueVoicePage::Rows::paint(juce::Graphics& g) {
    const auto& rows = page_.state_.rows;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto& row = rows[i];
        const auto area = juce::Rectangle<int>(0, static_cast<int>(i) * kRowHeight, getWidth(), kRowHeight);
        g.setColour(theme::rowLine);
        g.fillRect(area.withTop(area.getBottom() - 1));

        // SECTION TYPE: the name, semibold; "· yours" after a type the band added.
        auto name = area.withWidth(page_.name_width_).reduced(kCellPadX, 0);
        const float nameW = std::min(widthOf(nameFont(), row.name), static_cast<float>(name.getWidth()));
        g.setColour(theme::text);
        g.setFont(nameFont());
        g.drawText(shorten(row.name, nameFont(), nameW + 1.0f), name.withWidth(juce::roundToInt(nameW) + 1),
                   juce::Justification::centredLeft, false);
        if (!row.builtIn) {
            const auto yours = name.withTrimmedLeft(juce::roundToInt(nameW) + 4);
            g.setColour(theme::dim);
            g.setFont(hintFont());
            g.drawText(shorten(kMiddleDot + " yours", hintFont(), static_cast<float>(yours.getWidth())), yours,
                       juce::Justification::centredLeft, false);
        }

        // COLOUR: the type's pill.
        const auto pill = pillArea(static_cast<int>(i)).toFloat();
        g.setColour(row.colour);
        g.fillRoundedRectangle(pill, 4.0f);
        g.setColour(theme::onAccent);
        g.setFont(pillFont());
        const auto words = shorten(row.name, pillFont(), pill.getWidth() - 2.0f * kPillPadX);
        g.drawText(words, pill.reduced(static_cast<float>(kPillPadX), 0.0f),
                   juce::Justification::centred, false);

        // CUE: after a missing clip, why it is in caution.
        if (row.clipMissing) {
            const auto* c = choice(static_cast<int>(i));
            const int x = c != nullptr ? c->getRight() + kHintGap : page_.name_width_ + page_.colour_width_ + kCellPadX;
            const int right = page_.name_width_ + page_.colour_width_ + page_.cue_width_ - kCellPadX;
            g.setColour(theme::caution);
            g.setFont(hintFont());
            g.drawText(shorten(hintOf(row), hintFont(), static_cast<float>(right - x)), x, area.getY(), std::max(0, right - x),
                       kRowHeight, juce::Justification::centredLeft, false);
        }

        // ▶: to hear it, when there is a clip on disk.
        if (row.hasClip) {
            g.setColour(row.clipMissing ? theme::dim.withAlpha(0.4f) : hovered_ == static_cast<int>(i) ? theme::text : theme::mid);
            g.setFont(bodyFont());
            g.drawText(kPlay, playArea(static_cast<int>(i)), juce::Justification::centred, false);
        }
    }
}

void CueVoicePage::Rows::mouseDown(const juce::MouseEvent& e) {
    const int i = e.y / kRowHeight;
    if (!juce::isPositiveAndBelow(i, static_cast<int>(page_.state_.rows.size()))) return;
    const auto& row = page_.state_.rows[static_cast<std::size_t>(i)];
    if (playArea(i).contains(e.getPosition())) {
        page_.hear(i);
    } else if (pillArea(i).expanded(2).contains(e.getPosition())) {
        page_.colourMenu(i);
    } else if (e.x < page_.name_width_ && page_.editable()) {
        askUserText(page_, "Rename the section type", row.name, "Rename",
                    [safe = juce::Component::SafePointer<CueVoicePage>(&page_), typeId = row.typeId](const juce::String& name) {
                        if (safe == nullptr) return;
                        for (int r = 0; r < static_cast<int>(safe->state_.rows.size()); ++r)
                            if (safe->state_.rows[static_cast<std::size_t>(r)].typeId == typeId) safe->renameType(r, name);
                    });
    }
}

void CueVoicePage::Rows::mouseMove(const juce::MouseEvent& e) {
    const int i = e.y / kRowHeight;
    const bool inRow = juce::isPositiveAndBelow(i, static_cast<int>(page_.state_.rows.size()));
    const bool play = inRow && page_.state_.rows[static_cast<std::size_t>(i)].hasClip && playArea(i).contains(e.getPosition());
    const bool click = inRow && (play || pillArea(i).expanded(2).contains(e.getPosition())
                                 || (e.x < page_.name_width_ && page_.editable()));
    setMouseCursor(click ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    const int hovered = play ? i : -1;
    if (hovered != hovered_) {
        hovered_ = hovered;
        repaint();
    }
}

void CueVoicePage::Rows::mouseExit(const juce::MouseEvent&) {
    if (hovered_ < 0) return;
    hovered_ = -1;
    repaint();
}

juce::String CueVoicePage::Rows::hintOf(const CueVoiceState::Row& row) {
    return isBuiltInClip(row.clip) ? "not made yet" : "file missing";
}

int CueVoicePage::Rows::hintWidth(const CueVoiceState::Row& row) { return widthInPixels(hintFont(), hintOf(row)); }

juce::Rectangle<int> CueVoicePage::Rows::pillArea(int i) const {
    const auto& row = page_.state_.rows[static_cast<std::size_t>(i)];
    const int w = std::min(page_.colour_width_ - 2 * kCellPadX, widthInPixels(pillFont(), row.name) + 2 * kPillPadX);
    return {page_.name_width_ + kCellPadX, i * kRowHeight + (kRowHeight - kPillHeight) / 2, std::max(0, w), kPillHeight};
}

juce::Rectangle<int> CueVoicePage::Rows::playArea(int i) const {
    return {page_.name_width_ + page_.colour_width_ + page_.cue_width_, i * kRowHeight, kPlayWidth, kRowHeight};
}

}  // namespace sqlive::ui
