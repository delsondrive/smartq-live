#include "ui/SetlistsPage.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <map>
#include <utility>

#include "app/PartFiles.h"
#include "model/JsonObject.h"
#include "ui/Format.h"
#include "ui/Helpers.h"
#include "ui/SetlistEntries.h"
#include "ui/SetlistsPageParts.h"
#include "ui/SetlistsTableBody.h"
#include "ui/Theme.h"

namespace sqlive::ui {

using model::jsonObject;

SetlistsState makeSetlistsState(const app::Session& session, const juce::String& search) {
    SetlistsState state;
    state.search = search;
    const auto& model = session.model();

    for (const auto& id : model.setlistIds()) {
        const auto& list = *model.setlist(id);
        state.lists.push_back({id, list.name, summaryOf(model, list), id == session.setlistId()});
    }

    juce::StringArray inThisSet;
    if (const auto* list = model.setlist(session.setlistId())) {
        state.title = list->name;
        state.summary = summaryOf(model, *list);
        state.continuous = list->playMode == model::PlayMode::Continuous;
        std::map<juce::String, juce::String> warnings;            // by song: each song's files are looked at once
        for (std::size_t i = 0; i < list->entries.size(); ++i) {
            const auto& entry = list->entries[i];
            SetlistsState::Row row;
            row.entryId = entry.id;
            row.songId = entry.songId;
            row.number = static_cast<int>(i) + 1;
            if (const auto* song = model.song(entry.songId)) {
                row.title = song->title;
                row.sub = subOf(*song);
                row.length = formatTime(arrangedSeconds(*song, entry));
                row.arrangement = arrangementNameOf(*song, entry);
                row.arrangementChoices = arrangementChoicesOf(*song);
                row.arrangementIndex = chosenArrangementIndex(*song, entry);
                auto found = warnings.find(entry.songId);
                if (found == warnings.end()) found = warnings.emplace(entry.songId, warningOf(session, entry.songId)).first;
                row.warning = found->second;
            } else {
                row.title = "Missing song";
                row.arrangement = "In order";
                row.arrangementChoices = {"In order"};
            }
            const bool last = i + 1 == list->entries.size();
            row.then = last ? juce::String("end of the set") : state.continuous ? transitionText(entry.transition) : juce::String("Stop");
            inThisSet.addIfNotAlreadyThere(entry.songId);
            state.rows.push_back(std::move(row));
        }
    }

    const auto needle = search.trim();
    for (const auto& id : model.songIds()) {
        const auto& song = *model.song(id);
        ++state.songCount;
        if (needle.isNotEmpty() && !song.title.containsIgnoreCase(needle)) continue;
        const bool in = inThisSet.contains(id);
        state.library.push_back({id, song.title, in ? juce::String("in this set") : formatTime(song.lengthSeconds), in});
    }
    return state;
}

// ---- The setlists, left ------------------------------------------------------------------------------------------------

class SetlistsPage::ListsBody : public juce::Component {
public:
    explicit ListsBody(SetlistsPage& page) : page_(page) {}

    int idealHeight() const { return count() * (kListItemHeight + kListGap) + kListNewHeight; }

    void paint(juce::Graphics& g) override {
        const auto& lists = page_.state_.lists;
        for (int i = 0; i < count(); ++i) {
            const auto& item = lists[static_cast<std::size_t>(i)];
            const auto area = itemArea(i).toFloat();
            if (item.selected || i == hovered_) {
                g.setColour(item.selected ? theme::field : theme::field.withMultipliedAlpha(0.5f));
                g.fillRoundedRectangle(area, theme::radiusButton);
            }
            if (item.selected) {                                  // the inset 3 px mint edge
                juce::Graphics::ScopedSaveState saved(g);
                juce::Path shape;
                shape.addRoundedRectangle(area, theme::radiusButton);
                g.reduceClipRegion(shape);
                g.setColour(theme::mint);
                g.fillRect(area.withWidth(3.0f));
            }
            const auto text = area.reduced(static_cast<float>(kListPadX), 8.0f);
            g.setColour(item.selected ? theme::text : theme::mid);
            g.setFont(boldFont());
            g.drawText(shorten(item.name, boldFont(), text.getWidth()), text.withHeight(16.0f), juce::Justification::centredLeft, false);
            g.setColour(theme::dim);
            g.setFont(smallFont());
            g.drawText(shorten(item.detail, smallFont(), text.getWidth()), text.withTrimmedTop(16.0f).withHeight(14.0f),
                       juce::Justification::centredLeft, false);
        }
        const auto add = newArea().toFloat();
        if (hovered_ == count()) {
            g.setColour(theme::field.withMultipliedAlpha(0.5f));
            g.fillRoundedRectangle(add, theme::radiusButton);
        }
        g.setColour(theme::mint);
        g.setFont(boldFont());
        g.drawText("+ New setlist", add.reduced(static_cast<float>(kListPadX), 0.0f), juce::Justification::centredLeft, true);
    }

    void mouseMove(const juce::MouseEvent& e) override { setHovered(itemAt(e.y)); }
    void mouseExit(const juce::MouseEvent&) override { setHovered(-1); }

    void mouseDown(const juce::MouseEvent& e) override {
        const int index = itemAt(e.y);
        if (index < 0) return;
        if (index == count()) {
            if (!e.mods.isPopupMenu()) page_.newSetlist();
            return;
        }
        const auto id = page_.state_.lists[static_cast<std::size_t>(index)].id;
        if (!e.mods.isPopupMenu()) {
            page_.session_.chooseSetlist(id);
            return;
        }
        juce::PopupMenu menu;
        menu.addItem(1, "Rename" + kEllipsis, page_.editable());
        menu.addItem(2, "Remove", page_.editable() && count() > 1);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(),
                           [safe = juce::Component::SafePointer<SetlistsPage>(&page_), id](int result) {
                               if (safe == nullptr) return;
                               if (result == 1) safe->renameSetlist(id);
                               if (result == 2) safe->removeSetlist(id);
                           });
    }

private:
    int count() const { return static_cast<int>(page_.state_.lists.size()); }
    juce::Rectangle<int> itemArea(int i) const { return {0, i * (kListItemHeight + kListGap), getWidth(), kListItemHeight}; }
    juce::Rectangle<int> newArea() const { return {0, count() * (kListItemHeight + kListGap), getWidth(), kListNewHeight}; }

    // The item under y; count() for "+ New setlist"; -1 for none.
    int itemAt(int y) const {
        for (int i = 0; i < count(); ++i)
            if (itemArea(i).contains(0, y)) return i;
        return newArea().contains(0, y) ? count() : -1;
    }

    void setHovered(int index) {
        if (index == hovered_) return;
        hovered_ = index;
        repaint();
    }

    SetlistsPage& page_;
    int hovered_ = -1;
};

// ---- Every song, right -------------------------------------------------------------------------------------------------

class SetlistsPage::LibraryBody : public juce::Component {
public:
    explicit LibraryBody(SetlistsPage& page) : page_(page) {}

    int idealHeight() const { return count() == 0 ? kLibItemHeight : count() * (kLibItemHeight + kLibGap) - kLibGap; }

    void paint(juce::Graphics& g) override {
        if (count() == 0) {
            g.setColour(theme::dim);
            g.setFont(hintFont());
            const auto words = page_.state_.songCount == 0 ? juce::String("No songs yet") : juce::String("No song has that in its title");
            g.drawText(words, getLocalBounds().withHeight(kLibItemHeight).reduced(kLibPadX, 0), juce::Justification::centredLeft, true);
            return;
        }
        for (int i = 0; i < count(); ++i) {
            const auto& item = page_.state_.library[static_cast<std::size_t>(i)];
            const auto area = itemArea(i);
            if (i == hovered_) {
                g.setColour(theme::field);
                g.fillRoundedRectangle(area.toFloat(), 7.0f);
            }
            const float alpha = item.inThisSet ? 0.45f : 1.0f;     // .lib span.in
            auto inner = area.reduced(kLibPadX, 0);
            if (!item.inThisSet) {
                drawGrip(g, {static_cast<float>(inner.getX()) + 4.0f, static_cast<float>(area.getCentreY())}, theme::dim);
                inner.removeFromLeft(8 + 6);
            }
            const float noteWidth = std::ceil(widthOf(smallFont(), item.note));
            const auto note = inner.removeFromRight(static_cast<int>(noteWidth));
            inner.removeFromRight(8);
            g.setColour(theme::mid.withMultipliedAlpha(alpha));
            g.setFont(bodyFont());
            g.drawText(shorten(item.title, bodyFont(), static_cast<float>(inner.getWidth())), inner, juce::Justification::centredLeft, false);
            g.setColour(theme::dim.withMultipliedAlpha(alpha));
            g.setFont(smallFont());
            g.drawText(item.note, note, juce::Justification::centredRight, false);
        }
    }

    void mouseMove(const juce::MouseEvent& e) override { setHovered(itemAt(e.y)); }
    void mouseExit(const juce::MouseEvent&) override { setHovered(-1); }

    void mouseDown(const juce::MouseEvent& e) override {
        pressed_ = itemAt(e.y);
        dragging_ = false;
        if (pressed_ < 0 || !e.mods.isPopupMenu()) return;
        const auto songId = page_.state_.library[static_cast<std::size_t>(pressed_)].songId;
        pressed_ = -1;
        juce::PopupMenu menu;
        menu.addItem(1, "Open in the Song editor");
        menu.addItem(2, "Remove from the band" + kEllipsis, page_.editable());
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(),
                           [safe = juce::Component::SafePointer<SetlistsPage>(&page_), songId](int result) {
                               if (safe == nullptr) return;
                               if (result == 1) safe->openSong(songId);
                               if (result == 2) safe->askRemoveSong(songId);
                           });
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (pressed_ < 0 || dragging_ || e.getDistanceFromDragStart() < kDragThreshold || !page_.editable()) return;
        dragging_ = true;
        const auto area = itemArea(pressed_);
        const auto offset = area.getPosition() - e.getMouseDownPosition();
        page_.startDragging(kSongDrag + page_.state_.library[static_cast<std::size_t>(pressed_)].songId, this, dragImage(*this, area),
                            false, &offset, &e.source);
    }

    void mouseUp(const juce::MouseEvent&) override {
        pressed_ = -1;
        dragging_ = false;
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override {
        const int index = itemAt(e.y);
        if (index >= 0) page_.addSong(page_.state_.library[static_cast<std::size_t>(index)].songId, -1);
    }

private:
    int count() const { return static_cast<int>(page_.state_.library.size()); }
    juce::Rectangle<int> itemArea(int i) const { return {0, i * (kLibItemHeight + kLibGap), getWidth(), kLibItemHeight}; }
    int itemAt(int y) const {
        for (int i = 0; i < count(); ++i)
            if (itemArea(i).contains(0, y)) return i;
        return -1;
    }

    void setHovered(int index) {
        if (index == hovered_) return;
        hovered_ = index;
        repaint();
    }

    SetlistsPage& page_;
    int hovered_ = -1, pressed_ = -1;
    bool dragging_ = false;
};

// ---- The page ----------------------------------------------------------------------------------------------------------

SetlistsPage::SetlistsPage(app::Session& session)
    : session_(session),
      newSong_("+ New song", PillButton::Style::Primary),
      lists_(std::make_unique<ListsBody>(*this)),
      table_(std::make_unique<TableBody>(*this)),
      library_(std::make_unique<LibraryBody>(*this)) {
    listsView_.setViewedComponent(lists_.get(), false);
    tableView_.setViewedComponent(table_.get(), false);
    libraryView_.setViewedComponent(library_.get(), false);
    for (auto* view : {&listsView_, &tableView_, &libraryView_}) {
        view->setScrollBarsShown(true, false);
        view->setScrollBarThickness(10);
        addAndMakeVisible(*view);
    }

    playMode_.setItems({"Carry on (continuous)", "Stop (song by song)"}, 1);
    playMode_.onChange = [this](int index) { setContinuous(index == 0); };

    search_.setFont(bodyFont());
    search_.setTextToShowWhenEmpty("Search" + kEllipsis, theme::dim);
    search_.setIndents(8, 4);
    search_.onTextChange = [this] { refresh(); };
    search_.onEscapeKey = [this] { search_.clear(); refresh(); };

    newSong_.onClick = [this] {
        if (onNewSong) onNewSong({});
    };

    for (juce::Component* child : std::initializer_list<juce::Component*>{&listsLabel_, &libraryLabel_, &playMode_, &search_, &newSong_})
        addAndMakeVisible(child);

    session_.changes().addChangeListener(this);
    setVisible(true);                                             // AppShell hides it while another page shows
    refresh(true);
}

SetlistsPage::~SetlistsPage() { session_.changes().removeChangeListener(this); }

const juce::Viewport& SetlistsPage::tableViewport() const { return tableView_; }
const juce::Viewport& SetlistsPage::listsViewport() const { return listsView_; }
juce::String SetlistsPage::drawnTitle(int row) const { return table_->drawnTitle(row); }
juce::String SetlistsPage::drawnHeading() const { return heading_; }
Choice* SetlistsPage::thenChoice(int row) { return table_->thenChoice(row); }
Choice* SetlistsPage::arrangementChoice(int row) { return table_->arrangementChoice(row); }

void SetlistsPage::changeListenerCallback(juce::ChangeBroadcaster*) { refresh(); }

void SetlistsPage::visibilityChanged() {
    if (isVisible() && !fresh_) refresh();
}

void SetlistsPage::refresh(bool force) {
    // Hidden, it waits until it is shown: the session's changes come 30 times a second while a song plays.
    if (!isVisible() && !force) {
        fresh_ = false;
        return;
    }
    auto next = makeSetlistsState(session_, search_.getText());
    fresh_ = true;
    if (next == state_ && editable() == shownEditable_ && !force) return;
    shownEditable_ = editable();
    state_ = std::move(next);
    libraryLabel_.setText("ALL SONGS " + kMiddleDot + " " + juce::String(state_.songCount));
    playMode_.setSelected(state_.continuous ? 0 : 1);
    playMode_.setEnabled(editable() && state_.title.isNotEmpty());
    table_->update();
    lists_->repaint();
    library_->repaint();
    resized();
    repaint();
}

void SetlistsPage::resized() {
    auto area = getLocalBounds().reduced(kPadX, kPadY);
    auto left = area.removeFromLeft(kListsWidth);
    area.removeFromLeft(kColumnGap);
    auto right = area.removeFromRight(kLibraryWidth);
    area.removeFromRight(kColumnGap);
    auto middle = area;

    listsLabel_.setBounds(left.removeFromTop(kCapsHeight).reduced(4, 0));
    left.removeFromTop(kCapsGap);
    listsView_.setBounds(left);
    fit(listsView_, *lists_, lists_->idealHeight());

    // The title row: the name and the summary on the left, "After each song" and the switch on the right.
    auto header = middle.removeFromTop(kHeaderHeight);
    middle.removeFromTop(kHeaderGap);
    const int switchWidth = std::min(playMode_.idealWidth(), header.getWidth() / 2);
    playMode_.setBounds(header.removeFromRight(switchWidth).withSizeKeepingCentre(switchWidth, kSegmentedHeight));
    headingArea_ = header;
    const float afterWidth = widthOf(hintFont(), "After each song") + 12.0f;
    const float summaryWidth = widthOf(hintFont(), state_.summary);
    const float room = static_cast<float>(header.getWidth()) - afterWidth - 12.0f - summaryWidth - 12.0f;
    heading_ = shorten(state_.title, titleFont(), std::max(0.0f, room));

    tableBox_ = middle;
    auto inner = middle.reduced(1);
    inner.removeFromTop(kTableHeadHeight);
    inner.removeFromBottom(static_cast<int>(theme::radiusPanel) / 2);
    tableView_.setBounds(inner);
    fit(tableView_, *table_, table_->idealHeight());

    libraryBox_ = right;
    auto box = right.reduced(kBoxPadX, kBoxPadY);
    libraryLabel_.setBounds(box.removeFromTop(kCapsHeight));
    box.removeFromTop(kBoxGap);
    search_.setBounds(box.removeFromTop(kSearchHeight));
    box.removeFromTop(kBoxGap);
    newSong_.setBounds(box.removeFromBottom(kButtonHeight));
    box.removeFromBottom(kBoxGap);
    libraryView_.setBounds(box);
    fit(libraryView_, *library_, library_->idealHeight());
}

void SetlistsPage::paint(juce::Graphics& g) {
    // The title row.
    auto header = headingArea_.toFloat();
    g.setColour(theme::text);
    g.setFont(titleFont());
    const float headingWidth = std::ceil(widthOf(titleFont(), heading_));
    g.drawText(heading_, header.withWidth(headingWidth + 1.0f), juce::Justification::centredLeft, false);
    const float afterWidth = widthOf(hintFont(), "After each song");
    g.setColour(theme::dim);
    g.setFont(hintFont());
    g.drawText("After each song", header.removeFromRight(afterWidth + 12.0f).withTrimmedRight(12.0f), juce::Justification::centredRight, false);
    const auto summaryArea = header.withTrimmedLeft(headingWidth + (heading_.isEmpty() ? 0.0f : 12.0f));
    g.drawText(shorten(state_.summary, hintFont(), summaryArea.getWidth()), summaryArea, juce::Justification::centredLeft, false);

    // The table's box and its column heads.
    theme::paintPanel(g, tableBox_.toFloat());
    const auto c = table_->columns();
    const int x0 = tableView_.getX(), headY = tableBox_.getY() + 1;
    g.setColour(theme::dim);
    g.setFont(headFont());
    const auto head = [&](const juce::String& text, int from, int to) {
        g.drawText(text, x0 + from + kCellPad, headY, std::max(0, to - from - 2 * kCellPad), kTableHeadHeight - 1, juce::Justification::centredLeft, true);
    };
    head("#", c.number, c.song);
    head("SONG", c.song, c.length);
    head("LENGTH", c.length, c.arrangement);
    head("ARRANGEMENT", c.arrangement, c.then);
    head("THEN", c.then, c.end);
    g.setColour(theme::hairline);
    g.fillRect(tableBox_.getX() + 1, headY + kTableHeadHeight - 1, tableBox_.getWidth() - 2, 1);

    theme::paintPanel(g, libraryBox_.toFloat());

    if (filesOver_) {                                             // files from the system are over the page
        g.setColour(theme::mint.withAlpha(0.7f));
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(3.0f), theme::radiusPanel, 2.0f);
    }
}

// ---- Gestures ----------------------------------------------------------------------------------------------------------

bool SetlistsPage::editable() const { return session_.mode() == app::Mode::Edit; }

bool SetlistsPage::run(const juce::String& command, const juce::var& params) {
    if (!editable()) return false;
    const auto result = session_.commands().run(command, params);
    if (!result.ok) {
        tell(result.message);
        // What the controls show goes back to what the band says, once the gesture that changed them has finished.
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<SetlistsPage>(this)] {
            if (safe != nullptr) safe->refresh(true);
        });
    }
    return result.ok;
}

void SetlistsPage::tell(const juce::String& message) {
    dialog_ = juce::AlertWindow::showScopedAsync(juce::MessageBoxOptions()
                                                     .withIconType(juce::MessageBoxIconType::NoIcon)
                                                     .withAssociatedComponent(this)
                                                     .withTitle(message)
                                                     .withButton("OK"),
                                                 nullptr);
}

void SetlistsPage::askText(const juce::String& title, const juce::String& message, const juce::String& text, const juce::String& button,
                           std::function<void(const juce::String&)> then) {
    auto* window = new juce::AlertWindow(title, message, juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor("text", text);
    window->addButton(button, 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    window->enterModalState(true,
                            juce::ModalCallbackFunction::create(
                                [safe = juce::Component::SafePointer<SetlistsPage>(this), window, then = std::move(then)](int result) {
                                    if (safe == nullptr) return;
                                    if (result == 1) then(window->getTextEditorContents("text").trim());
                                    else safe->refresh(true);                 // a choice that asked goes back
                                }),
                            true);
}

void SetlistsPage::newSetlist() {
    if (!editable()) return;
    askText("New setlist", "What is the setlist called?", {}, "Make it", [this](const juce::String& name) {
        if (!editable()) return;
        const auto result = session_.commands().run("setlist.create", jsonObject({{"name", name}}));
        if (!result.ok) {
            tell(result.message);
            return;
        }
        session_.chooseSetlist(result.value.toString());
    });
}

void SetlistsPage::renameSetlist(const juce::String& setlistId) {
    const auto* list = session_.model().setlist(setlistId);
    if (list == nullptr || !editable()) return;
    askText("Rename the setlist", {}, list->name, "Rename", [this, setlistId](const juce::String& name) {
        run("setlist.set", jsonObject({{"setlistId", setlistId}, {"name", name}}));
    });
}

void SetlistsPage::removeSetlist(const juce::String& setlistId) { run("setlist.remove", jsonObject({{"setlistId", setlistId}})); }

void SetlistsPage::setContinuous(bool continuous) {
    run("setlist.set", jsonObject({{"setlistId", session_.setlistId()}, {"playMode", continuous ? "continuous" : "songBySong"}}));
}

void SetlistsPage::chooseThen(const juce::String& entryId, const juce::String& item) {
    if (item == "Gap" + kEllipsis) {
        askGap(entryId);
        return;
    }
    if (item == "Crossfade" + kEllipsis) {
        askCrossfade(entryId);
        return;
    }
    if (item != "Stop" && item != "Seamless") return;             // "Gap 4 s" or "Crossfade 4 s": already so
    run("setlist.setTransition", jsonObject({{"setlistId", session_.setlistId()}, {"entryId", entryId}, {"kind", item == "Seamless" ? "seamless" : "stop"}}));
}

void SetlistsPage::askGap(const juce::String& entryId) {
    const auto* list = session_.model().setlist(session_.setlistId());
    if (list == nullptr || !editable()) return;
    const auto entry = std::find_if(list->entries.begin(), list->entries.end(), [&](const auto& e) { return e.id == entryId; });
    if (entry == list->entries.end()) return;
    const auto* song = session_.model().song(entry->songId);
    const auto seconds = entry->transition.kind == model::TransitionKind::Gap ? entry->transition.seconds : 4.0;
    askText("Gap after " + (song != nullptr ? song->title : juce::String("this song")), "How many seconds before the next song starts?",
            formatSeconds(seconds), "OK", [this, entryId](const juce::String& text) {
                const bool number = text.containsAnyOf("0123456789") && text.containsOnly("0123456789.");
                const double value = text.getDoubleValue();
                if (!number || value < 0.0 || value > 60.0) {
                    tell("A gap is 0 to 60 seconds.");
                    refresh(true);
                    return;
                }
                run("setlist.setTransition", jsonObject({{"setlistId", session_.setlistId()}, {"entryId", entryId}, {"kind", "gap"}, {"seconds", value}}));
            });
}

void SetlistsPage::addSong(const juce::String& songId, int index) {
    auto params = jsonObject({{"setlistId", session_.setlistId()}, {"songId", songId}});
    if (index >= 0) params.getDynamicObject()->setProperty("index", index);
    run("setlist.addSong", params);
}

void SetlistsPage::moveEntry(const juce::String& entryId, int toIndex) {
    run("setlist.moveEntry", jsonObject({{"setlistId", session_.setlistId()}, {"entryId", entryId}, {"toIndex", toIndex}}));
}

void SetlistsPage::removeEntry(const juce::String& entryId) {
    run("setlist.removeEntry", jsonObject({{"setlistId", session_.setlistId()}, {"entryId", entryId}}));
}

void SetlistsPage::askRemoveSong(const juce::String& songId) {
    const auto* song = session_.model().song(songId);
    if (song == nullptr || !editable()) return;
    const auto title = song->title;
    dialog_ = juce::AlertWindow::showScopedAsync(
        juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::NoIcon)
            .withAssociatedComponent(this)
            .withTitle("Remove " + title + " from the band?")
            .withMessage("It comes out of every setlist, and its folder goes to the band folder's trash. Undo brings it back.")
            .withButton("Remove")
            .withButton("Cancel"),
        [safe = juce::Component::SafePointer<SetlistsPage>(this), songId](int result) {
            if (safe != nullptr && result == 1) safe->run("song.remove", jsonObject({{"songId", songId}}));
        });
}

void SetlistsPage::openSong(const juce::String& songId) {
    if (songId.isNotEmpty() && session_.model().song(songId) != nullptr && onOpenSong) onOpenSong(songId);
}

// ---- Files from the system ---------------------------------------------------------------------------------------------

bool SetlistsPage::isInterestedInFileDrag(const juce::StringArray& files) { return editable() && !files.isEmpty(); }

void SetlistsPage::fileDragEnter(const juce::StringArray&, int, int) {
    filesOver_ = true;
    repaint();
}

void SetlistsPage::fileDragExit(const juce::StringArray&) {
    filesOver_ = false;
    repaint();
}

void SetlistsPage::filesDropped(const juce::StringArray& files, int, int) {
    filesOver_ = false;
    repaint();
    if (!editable() || !onNewSong) return;
    juce::Array<juce::File> dropped;
    for (const auto& path : files) dropped.add(juce::File(path));
    onNewSong(dropped);
}

}  // namespace sqlive::ui
