#include "ui/SectionsLane.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "model/JsonObject.h"
#include "model/Sections.h"
#include "ui/Format.h"
#include "ui/Helpers.h"
#include "ui/Theme.h"

namespace sqlive::ui {

namespace {

// The mockup's .esec: 3 px from the top, 22 px high, 5 px corners, 7 px either side of 12 px bold text in the dark ink, a
// pixel between one block and the next.
constexpr float kBlockTop = 3.0f, kBlockHeight = 22.0f, kBlockRadius = 5.0f, kBlockPad = 7.0f, kBlockGap = 1.0f;
constexpr float kEdgeGrab = 4.0f;                                 // pixels either side of a left edge that take a drag
constexpr int kEditorWidth = 110, kCarryStart = 4, kCarryMaxWidth = 160;

juce::Font blockFont() { return theme::font(theme::sizeSmall, true); }

void drawBlock(juce::Graphics& g, juce::Rectangle<float> area, const SectionsLaneState::Block& block) {
    g.setColour(block.colour);
    g.fillRoundedRectangle(area, kBlockRadius);
    const auto words = area.reduced(kBlockPad, 0.0f);
    if (words.getWidth() <= 0.0f) return;
    g.setColour(theme::onAccent);
    g.setFont(blockFont());
    g.drawText(shorten(block.name, blockFont(), words.getWidth()), words, juce::Justification::centredLeft, false);
}

}  // namespace

SectionsLaneState makeSectionsLaneState(const app::Session& session, const juce::String& songId, const EditorSelection& selection) {
    SectionsLaneState state;
    const auto* song = session.model().song(songId);
    if (song == nullptr) return state;
    const auto& band = session.model().band();
    for (const auto& t : model::tileSections(*song, model::barsInSong(*song))) {
        const bool selected = selection.kind == EditorSelection::Kind::Section && t.id.isNotEmpty() && selection.id == t.id;
        state.blocks.push_back({t.id, t.name, theme::sectionColour(band, t.typeId), t.startBar, t.endBar, selected});
    }
    return state;
}

// ---- The lane ----------------------------------------------------------------------------------------------------------

SectionsLane::SectionsLane(app::Session& session, juce::String songId) : session_(session), songId_(std::move(songId)) {
    editor_.setFont(blockFont());
    editor_.setIndents(6, 3);
    editor_.setJustification(juce::Justification::centredLeft);
    editor_.onReturnKey = [this] { finishEdit(true); };
    editor_.onEscapeKey = [this] { finishEdit(false); };
    editor_.onFocusLost = [this] { finishEdit(true); };
    addChildComponent(editor_);
    refresh();
}

bool SectionsLane::editable() const { return session_.mode() == app::Mode::Edit && session_.model().song(songId_) != nullptr; }

void SectionsLane::refresh() {
    state_ = makeSectionsLaneState(session_, songId_, selection());
    const bool stillThere = std::any_of(state_.blocks.begin(), state_.blocks.end(), [this](const auto& b) { return b.id == editing_; });
    if (editing_.isNotEmpty() && !stillThere) finishEdit(false);  // undone away while it was edited
    resized();
    repaint();
}

void SectionsLane::contextChanged() {
    state_ = makeSectionsLaneState(session_, songId_, selection());
    resized();
}

// ---- Edits -------------------------------------------------------------------------------------------------------------

juce::String SectionsLane::addAt(int bar) {
    if (!editable() || bar < 1) return {};
    const auto now = makeSectionsLaneState(session_, songId_, selection());
    // "Section <n>", n the sections there will be, or the first number after it that no section has. At bar 1 of a song
    // with none stored, the new one is all of it, in place of the whole song.
    const auto taken = [&now](const juce::String& name) {
        return std::any_of(now.blocks.begin(), now.blocks.end(), [&](const auto& b) { return b.name == name; });
    };
    const bool wholeSong = now.blocks.size() == 1 && now.blocks[0].id.isEmpty() && bar == 1;
    int n = static_cast<int>(now.blocks.size()) + (wholeSong ? 0 : 1);
    while (taken("Section " + juce::String(n))) ++n;
    juce::var made;
    const auto params = model::jsonObject({{"songId", songId_}, {"name", "Section " + juce::String(n)}, {"startBar", bar}});
    if (!run(session_, "section.add", params, {}, &made)) return {};
    const auto id = made.toString();
    if (onSelect) onSelect({EditorSelection::Kind::Section, {}, id, -1});
    refresh();
    return id;
}

void SectionsLane::moveStart(const juce::String& sectionId, int bar) {
    if (!editable() || sectionId.isEmpty() || bar < 1) return;
    run(session_, "section.set", model::jsonObject({{"songId", songId_}, {"sectionId", sectionId}, {"startBar", bar}}));
    refresh();
}

void SectionsLane::remove(const juce::String& sectionId) {
    if (!editable() || sectionId.isEmpty()) return;
    if (!run(session_, "section.remove", model::jsonObject({{"songId", songId_}, {"sectionId", sectionId}}))) return;
    if (selection().kind == EditorSelection::Kind::Section && selection().id == sectionId && onSelect) onSelect({});
    refresh();
}

void SectionsLane::rename(const juce::String& sectionId, const juce::String& name) {
    if (!editable() || sectionId.isEmpty() || name.trim().isEmpty()) return;
    run(session_, "section.set", model::jsonObject({{"songId", songId_}, {"sectionId", sectionId}, {"name", name.trim()}}));
    refresh();
}

bool SectionsLane::removeSelected() {
    const auto& picked = selection();
    if (picked.kind != EditorSelection::Kind::Section || picked.id.isEmpty()) return false;
    const bool here = std::any_of(state_.blocks.begin(), state_.blocks.end(), [&](const auto& b) { return b.id == picked.id; });
    if (!here || !editable()) return false;
    remove(picked.id);                                            // the last section stays, and says why
    return true;
}

void SectionsLane::addOrRenameAt(int bar) {
    const auto* song = session_.model().song(songId_);
    if (song == nullptr) return;
    bar = std::clamp(bar, 1, model::barsInSong(*song));           // the line at the song's end is its last bar's
    for (const auto& block : state_.blocks)
        if (block.startBar == bar && block.id.isNotEmpty()) {    // a section already starts there: its name, then
            const auto id = block.id;                             // selecting it reads the blocks again
            if (onSelect) onSelect({EditorSelection::Kind::Section, {}, id, -1});
            editInPlace(id);
            return;
        }
    const auto id = addAt(bar);
    if (id.isNotEmpty()) editInPlace(id);
}

// ---- Drawing and the mouse ---------------------------------------------------------------------------------------------

int SectionsLane::barAt(float x) const { return scale() != nullptr ? scale()->snap(x, 0).bar : 1; }

juce::Rectangle<float> SectionsLane::blockArea(int startBar, int endBar) const {
    if (scale() == nullptr) return {};
    const float left = std::round(scale()->xAtBar(startBar)), right = std::round(scale()->xAtBar(endBar)) - kBlockGap;
    return {left, kBlockTop, std::max(1.0f, right - left), kBlockHeight};
}

int SectionsLane::blockAt(juce::Point<float> at) const {
    for (int i = 0; i < static_cast<int>(state_.blocks.size()); ++i) {
        const auto& b = state_.blocks[static_cast<std::size_t>(i)];
        if (blockArea(b.startBar, b.endBar).withY(0.0f).withHeight(static_cast<float>(getHeight())).contains(at)) return i;
    }
    return -1;
}

int SectionsLane::edgeAt(juce::Point<float> at) const {
    if (scale() == nullptr) return -1;
    for (int i = 1; i < static_cast<int>(state_.blocks.size()); ++i)
        if (std::abs(at.x - std::round(scale()->xAtBar(state_.blocks[static_cast<std::size_t>(i)].startBar))) <= kEdgeGrab) return i;
    return -1;
}

void SectionsLane::paint(juce::Graphics& g) {
    if (scale() == nullptr) return;
    for (int i = 0; i < static_cast<int>(state_.blocks.size()); ++i) {
        const auto& b = state_.blocks[static_cast<std::size_t>(i)];
        int start = b.startBar, end = b.endBar;
        if (edgeTo_ && i == edge_) start = *edgeTo_;              // the edge being dragged, where it would land
        if (edgeTo_ && i + 1 == edge_) end = *edgeTo_;
        const auto area = blockArea(start, end);
        if (area.getRight() < 0.0f || area.getX() > static_cast<float>(getWidth())) continue;
        drawBlock(g, area, b);
        if (b.selected) {                                         // the mockup's .picked: a 2 px white outline, 1 px out
            g.setColour(juce::Colours::white);
            g.drawRoundedRectangle(area.expanded(2.0f), kBlockRadius + 2.0f, 2.0f);
        }
    }
}

void SectionsLane::resized() {
    for (const auto& b : state_.blocks)
        if (b.id == editing_ && editing_.isNotEmpty()) {
            const auto area = blockArea(b.startBar, b.endBar);
            editor_.setBounds(area.withWidth(std::max(area.getWidth(), static_cast<float>(kEditorWidth))).toNearestInt());
        }
}

void SectionsLane::mouseMove(const juce::MouseEvent& e) {
    const bool onEdge = editable() && tool() == EditTool::Select && edgeAt(e.position) >= 0;
    setMouseCursor(onEdge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void SectionsLane::mouseDown(const juce::MouseEvent& e) {
    pressed_ = edge_ = -1;
    edgeTo_.reset();
    emptyPress_ = carried_ = false;
    if (e.mods.isPopupMenu() || scale() == nullptr) return;
    if (tool() == EditTool::Draw && editable()) {                 // in Show mode the press is the cursor's
        addOrRenameAt(barAt(e.position.x));
        return;
    }
    if (editable() && (edge_ = edgeAt(e.position)) >= 0) return;
    const int hit = blockAt(e.position);
    if (hit >= 0 && state_.blocks[static_cast<std::size_t>(hit)].id.isNotEmpty()) {
        pressed_ = hit;
        if (onSelect) onSelect({EditorSelection::Kind::Section, {}, state_.blocks[static_cast<std::size_t>(hit)].id, -1});
        return;
    }
    emptyPress_ = true;
    pointer(e, true);                                             // nothing stored there: the cursor, and nothing selected
}

void SectionsLane::mouseDrag(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) return;
    if (edge_ >= 0) {
        if (!edgeTo_ && e.getDistanceFromDragStart() < 3) return;
        // Between the starts of the sections either side, as section.set keeps it.
        const int lowest = state_.blocks[static_cast<std::size_t>(edge_) - 1].startBar + 1;
        const int highest = state_.blocks[static_cast<std::size_t>(edge_)].endBar - 1;
        edgeTo_ = std::clamp(barAt(e.position.x), lowest, std::max(lowest, highest));
        repaint();
    } else if (pressed_ >= 0 && !carried_ && editable() && e.getDistanceFromDragStart() >= kCarryStart) {
        carried_ = true;
        startCarrying(pressed_, e);
    } else if (emptyPress_) {
        pointer(e, false);
    }
}

void SectionsLane::mouseUp(const juce::MouseEvent&) {
    const int edge = std::exchange(edge_, -1);
    const auto to = std::exchange(edgeTo_, std::nullopt);
    pressed_ = -1;
    emptyPress_ = carried_ = false;
    if (edge >= 0 && edge < static_cast<int>(state_.blocks.size()) && to && *to != state_.blocks[static_cast<std::size_t>(edge)].startBar)
        moveStart(state_.blocks[static_cast<std::size_t>(edge)].id, *to);   // one section.set: one undo step
    repaint();
}

void SectionsLane::mouseDoubleClick(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu() || tool() != EditTool::Select || !editable() || scale() == nullptr) return;
    addOrRenameAt(barAt(e.position.x));
}

// The block, as a picture under the mouse, carried to the arrangement row (a DragAndDropTarget) by the page's container.
void SectionsLane::startCarrying(int block, const juce::MouseEvent& e) {
    auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this);
    if (container == nullptr || block < 0 || block >= static_cast<int>(state_.blocks.size())) return;
    const auto& b = state_.blocks[static_cast<std::size_t>(block)];
    const auto area = blockArea(b.startBar, b.endBar);
    const int width = juce::jlimit(24, kCarryMaxWidth, static_cast<int>(area.getWidth()));
    juce::Image picture(juce::Image::ARGB, width, static_cast<int>(kBlockHeight), true);
    {
        juce::Graphics g(picture);
        drawBlock(g, picture.getBounds().toFloat(), b);
    }
    // Where the block was taken hold of, kept under the mouse (within the picture, which may be narrower than the block).
    const int across = std::clamp(e.getMouseDownX() - static_cast<int>(area.getX()), 0, width - 1);
    const juce::Point<int> offset(-across, static_cast<int>(kBlockTop) - e.getMouseDownY());
    container->startDragging(kSectionDragPrefix + b.id, this, juce::ScaledImage(picture), false, &offset);
}

// ---- The name, edited in place -----------------------------------------------------------------------------------------

void SectionsLane::editInPlace(const juce::String& sectionId) {
    const auto found = std::find_if(state_.blocks.begin(), state_.blocks.end(), [&](const auto& b) { return b.id == sectionId; });
    if (!editable() || sectionId.isEmpty() || found == state_.blocks.end()) return;
    editing_ = sectionId;
    editor_.setText(found->name, juce::dontSendNotification);
    resized();
    editor_.setVisible(true);
    if (editor_.isShowing()) editor_.grabKeyboardFocus();
    editor_.selectAll();
}

void SectionsLane::finishEdit(bool keep) {
    if (editing_.isEmpty()) return;
    const auto id = std::exchange(editing_, juce::String());
    const auto text = editor_.getText().trim();
    editor_.setVisible(false);
    if (!keep || text.isEmpty()) return;
    const auto found = std::find_if(state_.blocks.begin(), state_.blocks.end(), [&](const auto& b) { return b.id == id; });
    if (found != state_.blocks.end() && found->name != text) rename(id, text);
}

}  // namespace sqlive::ui
