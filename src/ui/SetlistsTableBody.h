#pragma once

// The Setlists page's running order, the table in the middle: SetlistsPage::TableBody. Only ever included by
// SetlistsPage.cpp, after SetlistsPageParts.h, whose helpers it uses.

#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/SetlistsPage.h"
#include "ui/SetlistsPageParts.h"
#include "ui/Widgets.h"

namespace sqlive::ui {

class SetlistsPage::TableBody : public juce::Component, public juce::DragAndDropTarget {
public:
    explicit TableBody(SetlistsPage& page) : page_(page) { setWantsKeyboardFocus(true); }

    int idealHeight() const { return count() * kRowHeight; }
    Columns columns() const { return columnsFor(getWidth()); }
    Choice* thenChoice(int row) { return choiceAt(choices_, row); }
    Choice* arrangementChoice(int row) { return choiceAt(arrangements_, row); }
    juce::String drawnTitle(int row) const {
        return juce::isPositiveAndBelow(row, static_cast<int>(titles_.size())) ? titles_[static_cast<std::size_t>(row)] : juce::String();
    }

    // After the page's state changed: a THEN choice for each row but the last, as the state says.
    void update() {
        const auto& rows = page_.state_.rows;
        const std::size_t wanted = rows.empty() ? 0 : rows.size() - 1;
        while (choices_.size() > wanted) {
            removeChildComponent(choices_.back().get());
            choices_.pop_back();
        }
        while (choices_.size() < wanted) {
            auto choice = std::make_unique<Choice>();
            auto* raw = choice.get();
            // Set once: the row and the item are looked up when it is used, as the rows change under it.
            choice->onChange = [this, raw](int index) {
                for (std::size_t i = 0; i < choices_.size(); ++i)
                    if (choices_[i].get() == raw && i < page_.state_.rows.size()) {
                        const auto entryId = page_.state_.rows[i].entryId;
                        const auto item = raw->items()[index];
                        page_.chooseThen(entryId, item);
                        return;
                    }
            };
            addAndMakeVisible(*choice);
            choices_.push_back(std::move(choice));
        }
        const bool continuous = page_.state_.continuous;
        for (std::size_t i = 0; i < wanted; ++i) {
            auto& choice = *choices_[i];
            const auto& then = rows[i].then;
            if (!continuous) {
                choice.setItems({"Stop"}, 0);                     // song by song: every song stops, dimmed
            } else if (then == "Stop" || then == "Seamless") {
                choice.setItems({"Stop", "Gap" + kEllipsis, "Seamless", "Crossfade" + kEllipsis}, then == "Stop" ? 0 : 2);
            } else if (then.startsWith("Gap")) {                  // "Gap 4 s", and "Gap…" to choose another length
                choice.setItems({"Stop", then, "Seamless", "Gap" + kEllipsis, "Crossfade" + kEllipsis}, 1);
            } else {                                              // "Crossfade 4 s", and "Crossfade…" to choose another length
                choice.setItems({"Stop", "Gap" + kEllipsis, "Seamless", then, "Crossfade" + kEllipsis}, 3);
            }
            choice.setEnabled(continuous && page_.editable());
        }

        // The ARRANGEMENT choice of every row; dimmed, and "In order", for a song with no arrangements.
        while (arrangements_.size() > rows.size()) {
            removeChildComponent(arrangements_.back().get());
            arrangements_.pop_back();
        }
        while (arrangements_.size() < rows.size()) {
            auto choice = std::make_unique<Choice>();
            auto* raw = choice.get();
            choice->onChange = [this, raw](int index) {
                for (std::size_t i = 0; i < arrangements_.size(); ++i)
                    if (arrangements_[i].get() == raw && i < page_.state_.rows.size()) {
                        const auto entryId = page_.state_.rows[i].entryId;
                        page_.chooseArrangement(entryId, index);
                        return;
                    }
            };
            addAndMakeVisible(*choice);
            arrangements_.push_back(std::move(choice));
        }
        for (std::size_t i = 0; i < rows.size(); ++i) {
            arrangements_[i]->setItems(rows[i].arrangementChoices, rows[i].arrangementIndex);
            arrangements_[i]->setEnabled(rows[i].arrangementChoices.size() > 1 && page_.editable());
        }
        const bool stillThere = std::any_of(rows.begin(), rows.end(), [this](const auto& r) { return r.entryId == selected_; });
        if (!stillThere) selected_.clear();
        layOut();
        repaint();
    }

    void resized() override { layOut(); }

    void paint(juce::Graphics& g) override {
        const auto& rows = page_.state_.rows;
        if (rows.empty()) {
            g.setColour(theme::dim);
            g.setFont(bodyFont());
            g.drawFittedText("Drag songs here from the list on the right, or drop a folder of stems to add a new song.",
                             getLocalBounds().reduced(40, 20), juce::Justification::centred, 3);
        }
        const auto c = columns();
        for (int i = 0; i < count(); ++i) {
            const auto& row = rows[static_cast<std::size_t>(i)];
            const auto area = rowArea(i);
            if (row.entryId == selected_) {
                g.setColour(theme::mint.withAlpha(0.06f));
                g.fillRect(area);
            }
            g.setColour(theme::rowLine);
            g.fillRect(area.getX(), area.getBottom() - 1, area.getWidth(), 1);

            drawGrip(g, {static_cast<float>(c.number) * 0.5f + 2.0f, static_cast<float>(area.getCentreY())}, theme::dim);

            const auto cell = [&](int x0, int x1) { return juce::Rectangle<int>(x0 + kCellPad, area.getY(), std::max(0, x1 - x0 - 2 * kCellPad), kRowHeight - 1); };
            g.setColour(theme::mid);
            g.setFont(bodyFont());
            g.drawText(juce::String(row.number), cell(c.number, c.song), juce::Justification::centredLeft, true);
            g.drawText(row.length, cell(c.length, c.arrangement), juce::Justification::centredLeft, true);

            const auto song = cell(c.song, c.length);
            const auto line = song.withTrimmedTop(7).withHeight(17);
            const auto& title = titles_[static_cast<std::size_t>(i)];
            g.setColour(theme::text);
            g.setFont(boldFont());
            g.drawText(title, line, juce::Justification::centredLeft, false);
            const auto& warning = warnings_[static_cast<std::size_t>(i)];
            if (warning.isNotEmpty()) {
                g.setColour(theme::caution);
                g.setFont(hintFont());
                g.drawText(warning, line.withTrimmedLeft(static_cast<int>(std::ceil(widthOf(boldFont(), title)))),
                           juce::Justification::centredLeft, false);
            }
            g.setColour(theme::dim);
            g.setFont(smallFont());
            g.drawText(subs_[static_cast<std::size_t>(i)], song.withTrimmedTop(24).withHeight(14), juce::Justification::centredLeft, false);

            if (i == count() - 1) {
                g.setColour(theme::dim);
                g.setFont(hintFont());
                g.drawText(row.then, cell(c.then, c.end), juce::Justification::centredLeft, true);
            }
        }
        if (dropAt_ >= 0) {
            const int y = juce::jlimit(1, std::max(1, getHeight() - 1), dropAt_ * kRowHeight);
            g.setColour(theme::mint);
            g.fillRect(0, y - 1, getWidth(), 2);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        pressed_ = rowAt(e.y);
        onGrip_ = false;
        dragging_ = false;
        if (pressed_ < 0) {
            select({});
            return;
        }
        const auto row = page_.state_.rows[static_cast<std::size_t>(pressed_)];
        select(row.entryId);
        if (isShowing()) grabKeyboardFocus();
        if (e.mods.isPopupMenu()) {
            juce::PopupMenu menu;
            menu.addItem(1, "Open in the Song editor", row.songId.isNotEmpty() && page_.session_.model().song(row.songId) != nullptr);
            menu.addItem(2, "Remove from the setlist", page_.editable());
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(),
                               [safe = juce::Component::SafePointer<SetlistsPage>(&page_), row](int result) {
                                   if (safe == nullptr) return;
                                   if (result == 1) safe->openSong(row.songId);
                                   if (result == 2) safe->removeEntry(row.entryId);
                               });
            return;
        }
        onGrip_ = e.x < columns().number;
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (!onGrip_ || dragging_ || pressed_ < 0 || e.getDistanceFromDragStart() < kDragThreshold || !page_.editable()) return;
        dragging_ = true;
        const auto area = rowArea(pressed_);
        const auto offset = area.getPosition() - e.getMouseDownPosition();
        page_.startDragging(kEntryDrag + page_.state_.rows[static_cast<std::size_t>(pressed_)].entryId, this, dragImage(*this, area),
                            false, &offset, &e.source);
    }

    void mouseUp(const juce::MouseEvent&) override {
        dragging_ = false;
        onGrip_ = false;
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override {
        const int row = rowAt(e.y);
        if (row >= 0 && e.x >= columns().number) page_.openSong(page_.state_.rows[static_cast<std::size_t>(row)].songId);
    }

    bool keyPressed(const juce::KeyPress& key) override {
        if (selected_.isEmpty() || (key != juce::KeyPress::deleteKey && key != juce::KeyPress::backspaceKey)) return false;
        const auto& rows = page_.state_.rows;
        const auto at = std::find_if(rows.begin(), rows.end(), [this](const auto& r) { return r.entryId == selected_; });
        if (at == rows.end()) return false;
        // The row that takes its place is selected next, so Delete can be pressed again.
        const auto index = static_cast<std::size_t>(at - rows.begin());
        const auto next = index + 1 < rows.size() ? rows[index + 1].entryId : index > 0 ? rows[index - 1].entryId : juce::String();
        const auto removed = selected_;
        page_.removeEntry(removed);
        select(next);
        return true;
    }

    bool isInterestedInDragSource(const SourceDetails& details) override {
        const auto description = details.description.toString();
        return page_.editable() && (description.startsWith(kEntryDrag) || description.startsWith(kSongDrag));
    }
    void itemDragEnter(const SourceDetails& details) override { itemDragMove(details); }
    void itemDragMove(const SourceDetails& details) override {
        setDropAt(insertionAt(details.localPosition.y));
        const auto inView = page_.tableView_.getLocalPoint(this, details.localPosition);
        page_.tableView_.autoScroll(inView.x, inView.y, 24, 12);
    }
    void itemDragExit(const SourceDetails&) override { setDropAt(-1); }
    void itemDropped(const SourceDetails& details) override {
        const int at = insertionAt(details.localPosition.y);
        setDropAt(-1);
        const auto description = details.description.toString();
        if (description.startsWith(kSongDrag)) {
            page_.addSong(description.fromFirstOccurrenceOf(kSongDrag, false, false), at);
            return;
        }
        const auto id = description.fromFirstOccurrenceOf(kEntryDrag, false, false);
        const auto& rows = page_.state_.rows;
        const auto from = std::find_if(rows.begin(), rows.end(), [&](const auto& r) { return r.entryId == id; });
        if (from == rows.end()) return;
        const int fromIndex = static_cast<int>(from - rows.begin());
        const int to = at > fromIndex ? at - 1 : at;           // the place among the others, once it is lifted out
        if (to != fromIndex) page_.moveEntry(id, to);
    }

private:
    static Choice* choiceAt(const std::vector<std::unique_ptr<Choice>>& choices, int row) {
        return juce::isPositiveAndBelow(row, static_cast<int>(choices.size())) ? choices[static_cast<std::size_t>(row)].get() : nullptr;
    }
    int count() const { return static_cast<int>(page_.state_.rows.size()); }
    juce::Rectangle<int> rowArea(int i) const { return {0, i * kRowHeight, getWidth(), kRowHeight}; }
    int rowAt(int y) const { return y >= 0 && y < idealHeight() ? y / kRowHeight : -1; }
    int insertionAt(int y) const { return juce::jlimit(0, count(), static_cast<int>(std::lround(static_cast<double>(y) / kRowHeight))); }

    void select(const juce::String& entryId) {
        if (entryId == selected_) return;
        selected_ = entryId;
        repaint();
    }

    void setDropAt(int at) {
        if (at == dropAt_) return;
        dropAt_ = at;
        repaint();
    }

    // The words of each row cut to its width, and each THEN choice in its cell.
    void layOut() {
        const auto& rows = page_.state_.rows;
        const auto c = columns();
        const float songWidth = static_cast<float>(std::max(0, c.length - c.song - 2 * kCellPad));
        titles_.clear();
        warnings_.clear();
        subs_.clear();
        for (const auto& row : rows) {
            juce::String title = row.title, warning;
            if (row.warning.isEmpty()) {
                title = shorten(title, boldFont(), songWidth);
            } else {
                warning = " " + kMiddleDot + " " + row.warning;
                const float room = std::max(songWidth - widthOf(hintFont(), warning), songWidth * 0.5f);
                title = shorten(title, boldFont(), room);
                warning = shorten(warning, hintFont(), songWidth - widthOf(boldFont(), title));
            }
            titles_.push_back(title);
            warnings_.push_back(warning);
            subs_.push_back(shorten(row.sub, smallFont(), songWidth));
        }
        const auto place = [&](Choice& choice, int row, int from, int to) {
            const int width = std::max(0, std::min(to - from - 2 * kCellPad, choice.idealWidth() + 24));
            choice.setBounds(from + kCellPad, rowArea(row).getY() + (kRowHeight - kChoiceHeight) / 2, width, kChoiceHeight);
        };
        for (std::size_t i = 0; i < choices_.size(); ++i) place(*choices_[i], static_cast<int>(i), c.then, c.end);
        for (std::size_t i = 0; i < arrangements_.size(); ++i) place(*arrangements_[i], static_cast<int>(i), c.arrangement, c.then);
    }

    SetlistsPage& page_;
    std::vector<std::unique_ptr<Choice>> choices_;                // THEN, for each row but the last
    std::vector<std::unique_ptr<Choice>> arrangements_;           // ARRANGEMENT, for each row
    std::vector<juce::String> titles_, warnings_, subs_;          // as drawn
    juce::String selected_;                                       // an entry id
    int dropAt_ = -1;                                             // where a drop would go; -1 = no drag over the table
    int pressed_ = -1;
    bool onGrip_ = false, dragging_ = false;
};

}  // namespace sqlive::ui
