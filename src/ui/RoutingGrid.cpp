#include "ui/RoutingGrid.h"

#include <algorithm>
#include <cmath>

#include "app/AudioHost.h"
#include "model/Defaults.h"
#include "model/Routing.h"
#include "model/Units.h"
#include "ui/Format.h"
#include "ui/Theme.h"

namespace sqlive::ui {

namespace {

// The mockup's CSS: th and td (padding 7 8, 11 px bold letter-spaced th in dim, a hairline under th and a faint line
// under td), .grid-route th small (the output's name, 12 px semibold), .lbl-out (11 px bold, padding 1 7, 4 px corners),
// .bar4 (4 x 14, 2 px corners, 8 px after), .lv (at least 38 wide, padding 1 6, round, 11 px bold mint), .nolv (a faint
// "·"), td.dot i.block (14 x 2, 1 px corners), .val (a field, 6 px corners, padding 3 8), .addl (12 px semibold mint),
// .hint (12 px dim), the footer (padding 10 12, gap 14).
constexpr int kCellPadX = 8, kCellPadY = 7, kHeaderHeight = 66, kRowHeight = 32;
constexpr int kMinColumnWidth = 72, kMaxColumnWidth = 140, kMinNameWidth = 150, kMaxNameWidth = 240;
constexpr int kBarWidth = 4, kBarHeight = 14, kBarGap = 8;
constexpr int kPillHeight = 16, kPillPadX = 7, kLevelMinWidth = 38, kLevelPadX = 6, kLevelHeight = 18;
constexpr int kTrimPadX = 8, kTrimHeight = 20, kBlockWidth = 14, kBlockHeight = 2;
constexpr int kFooterHeight = 40, kFooterPadX = 12, kFooterGap = 14;
constexpr int kDragThreshold = 3, kPixelsPerStep = 2;
constexpr float kDbPerStep = 0.5f;
constexpr int kMessageMs = 4000;

const juce::String kEllipsis = juce::String::fromUTF8("\xe2\x80\xa6");                                  // …
const juce::String kCornerTitle = juce::String::fromUTF8("ROLE \xe2\x86\x93   OUTPUT MIX \xe2\x86\x92");   // ROLE ↓   OUTPUT MIX →
const juce::String kNotSent = juce::String::fromUTF8("\xc2\xb7");                                        // ·
const juce::String kAddRole = "+ Add a role", kAddOutput = "+ Add an output", kOnePerRole = "One output per role";
const juce::String kHint = "Every song uses these mixes. Using personal mixers instead? Give each role an output of its own: ";

juce::Font nameFont() { return theme::font(theme::sizeBody, true); }
juce::Font headFont() { return theme::tracked(theme::font(theme::sizeCaption, true), 0.6f); }
juce::Font outputFont() { return theme::font(theme::sizeSmall, true); }
juce::Font pillFont() { return theme::font(theme::sizeCaption, true); }
juce::Font smallFont() { return theme::font(theme::sizeSmall); }
juce::Font linkFont() { return theme::font(theme::sizeSmall, true); }

float widthOf(const juce::Font& font, const juce::String& text) { return juce::GlyphArrangement::getStringWidth(font, text); }
int ceilToInt(float x) { return static_cast<int>(std::ceil(x)); }

juce::var object(std::initializer_list<std::pair<const char*, juce::var>> items) {
    auto* o = new juce::DynamicObject();
    for (const auto& [key, value] : items) o->setProperty(juce::Identifier(key), value);
    return juce::var(o);
}

void keepOffKeyboard(juce::Component& component) {
    component.setWantsKeyboardFocus(false);
    component.setMouseClickGrabsKeyboardFocus(false);
    for (auto* child : component.getChildren()) keepOffKeyboard(*child);
}

float toTenth(float db) {
    const float r = std::round(db * 10.0f) / 10.0f;
    return r == 0.0f ? 0.0f : r;
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

const char* labelKey(model::OutputLabel label) {
    switch (label) {
    case model::OutputLabel::Monitor: return "monitor";
    case model::OutputLabel::Foh: return "foh";
    case model::OutputLabel::Timecode: return "timecode";
    case model::OutputLabel::Unused: return "unused";
    }
    return "monitor";
}

const model::OutputLabel kLabels[] = {model::OutputLabel::Monitor, model::OutputLabel::Foh, model::OutputLabel::Timecode,
                                      model::OutputLabel::Unused};

// The .lbl-out pill's colours for each label: fill, text, border (dashed for an output that isn't used). As the Mixer's.
struct PillLook { juce::Colour fill, text, border; bool dashed; };
PillLook pillLook(model::OutputLabel label) {
    switch (label) {
    case model::OutputLabel::Foh: return {juce::Colour(110, 168, 255).withAlpha(0.15f), juce::Colour(0xff8fbcff), juce::Colour(110, 168, 255).withAlpha(0.35f), false};
    case model::OutputLabel::Timecode: return {juce::Colour(182, 157, 255).withAlpha(0.15f), juce::Colour(0xffc4b1ff), juce::Colour(182, 157, 255).withAlpha(0.35f), false};
    case model::OutputLabel::Unused: return {juce::Colours::transparentBlack, theme::dim, theme::hairline, true};
    case model::OutputLabel::Monitor: break;
    }
    return {juce::Colours::white.withAlpha(0.1f), theme::text, theme::hairline, false};
}

void drawLabelPill(juce::Graphics& g, const juce::String& text, model::OutputLabel kind, juce::Rectangle<float> area) {
    const auto look = pillLook(kind);
    const auto shown = shorten(text, pillFont(), area.getWidth() - 2.0f * kPillPadX);
    const float w = std::min(area.getWidth(), widthOf(pillFont(), shown) + 2.0f * kPillPadX);
    const auto pill = area.withSizeKeepingCentre(w, static_cast<float>(kPillHeight));
    g.setColour(look.fill);
    g.fillRoundedRectangle(pill, 4.0f);
    g.setColour(look.border);
    if (look.dashed) {
        juce::Path outline, dashed;
        outline.addRoundedRectangle(pill.reduced(0.5f), 4.0f);
        const float dashes[] = {3.0f, 2.0f};
        juce::PathStrokeType(1.0f).createDashedStroke(dashed, outline, dashes, 2);
        g.fillPath(dashed);
    } else {
        g.drawRoundedRectangle(pill.reduced(0.5f), 4.0f, 1.0f);
    }
    g.setColour(look.text);
    g.setFont(pillFont());
    g.drawText(shown, pill, juce::Justification::centred, false);
}

// The palette's colours by name, for a role's Colour menu (model::roleColour's order).
const char* const kColourNames[] = {"Orange", "Violet", "Green", "Mint", "Yellow", "Grey", "Teal", "Pink", "Blue"};

juce::String sampleRateText(double rate) {
    auto text = juce::String(rate / 1000.0, 1);
    return (text.endsWith(".0") ? text.dropLastCharacters(2) : text) + " kHz";
}

juce::String bufferText(int size) { return juce::String(size) + (size >= 512 ? juce::String::fromUTF8(" \xc2\xb7 safest") : juce::String()); }

}  // namespace

// ---- The state ---------------------------------------------------------------------------------------------------------

RoutingState makeRoutingState(const app::Session& session, const app::AudioHost* host) {
    RoutingState state;
    const auto& model = session.model();
    const bool connected = host != nullptr && host->isOpen();
    const int channels = connected ? host->outputChannels() : 0;
    state.connection = connected ? "Connected " + juce::String::fromUTF8("\xc2\xb7 ") + juce::String(channels)
                                       + (channels == 1 ? " output" : " outputs")
                                 : juce::String("Not connected");
    const auto* rig = model.rig(session.rigId());
    if (rig == nullptr) return state;

    state.rigName = rig->name;
    state.sampleRate = sampleRateText(rig->sampleRate);
    state.buffer = bufferText(rig->bufferSize);
    const auto binding = rig->computers.find(session.settings().computerName());
    if (binding != rig->computers.end() && binding->second.deviceName.isNotEmpty())
        state.device = binding->second.deviceName + " (" + binding->second.deviceType + ")";

    for (const auto& out : rig->outputs)
        state.columns.push_back({out.id, out.name, formatChannels(out.firstChannel, out.stereo), labelName(out.label), out.label,
                                 out.label == model::OutputLabel::Unused ? juce::String() : formatDb(out.trimDb) + " dB"});

    // The click first, then the parts, each in the band's order.
    std::vector<const model::Role*> roles;
    for (const auto& role : model.band().roles)
        if (role.kind == model::RoleKind::Click) roles.push_back(&role);
    for (const auto& role : model.band().roles)
        if (role.kind == model::RoleKind::Part) roles.push_back(&role);
    for (const auto* role : roles) {
        RoutingState::Row row{role->id, role->name, role->kind == model::RoleKind::Click ? theme::ember : role->colour, {}};
        for (const auto& out : rig->outputs) {
            RoutingState::Cell cell;
            cell.allowed = model::mayRoute(role->kind, out.label);
            cell.whyNot = model::whyNot(role->kind, out.label);
            const auto sent = out.mix.find(role->id);
            cell.sent = cell.allowed && sent != out.mix.end();
            if (cell.sent) {
                cell.text = formatDb(sent->second);
                cell.boosted = toTenth(sent->second) > 0.0f;
            }
            row.cells.push_back(cell);
        }
        state.rows.push_back(std::move(row));
    }

    const auto problems = model::checkRig(model.band(), *rig, channels);
    for (const bool blocks : {true, false})
        for (const auto& problem : problems)
            if (problem.blocksShow == blocks) state.problems.push_back(problem.message);
    state.blockingProblems = static_cast<int>(std::count_if(problems.begin(), problems.end(), [](const auto& p) { return p.blocksShow; }));
    return state;
}

// ---- The scrolling parts -----------------------------------------------------------------------------------------------

// A viewport that says when what it shows has moved, so the role names can follow the outputs up and down.
class RoutingGrid::FollowingViewport : public juce::Viewport {
public:
    std::function<void(const juce::Rectangle<int>&)> onMoved;
    void visibleAreaChanged(const juce::Rectangle<int>& area) override {
        if (onMoved) onMoved(area);
    }
};

// The outputs' columns: each header, each role's cell, and "Overall level". What the viewport scrolls.
class RoutingGrid::Body : public juce::Component, public juce::TooltipClient {
public:
    explicit Body(RoutingGrid& grid) : grid_(grid) {}

    void paint(juce::Graphics& g) override {
        const auto& state = grid_.state_;
        const int w = grid_.column_width_;
        const int rows = static_cast<int>(state.rows.size());

        g.setColour(theme::hairline);
        g.fillRect(0, kHeaderHeight - 1, getWidth(), 1);
        g.setColour(theme::rowLine);
        for (int r = 0; r <= rows; ++r) g.fillRect(0, kHeaderHeight + (r + 1) * kRowHeight - 1, getWidth(), 1);

        for (int c = 0; c < static_cast<int>(state.columns.size()); ++c) {
            const auto& column = state.columns[static_cast<std::size_t>(c)];
            const auto head = juce::Rectangle<int>(c * w, 0, w, kHeaderHeight);
            if (hovered_.part == Part::Header && hovered_.column == c) {
                g.setColour(theme::field);
                g.fillRect(head.withTrimmedBottom(1));
            }
            auto inside = head.reduced(kCellPadX / 2, kCellPadY);
            g.setColour(theme::text);
            g.setFont(outputFont());
            const auto nameRow = inside.removeFromTop(16);
            g.drawText(shorten(column.name, outputFont(), static_cast<float>(nameRow.getWidth())), nameRow, juce::Justification::centred, false);
            inside.removeFromTop(2);
            const auto channelRow = inside.removeFromTop(14);
            g.setColour(theme::dim);
            g.setFont(headFont());
            g.drawText(shorten(column.channels, headFont(), static_cast<float>(channelRow.getWidth())), channelRow, juce::Justification::centred, false);
            inside.removeFromTop(4);
            drawLabelPill(g, column.label, column.kind, inside.removeFromTop(kPillHeight).toFloat());

            for (int r = 0; r < rows; ++r) paintCell(g, state.rows[static_cast<std::size_t>(r)].cells[static_cast<std::size_t>(c)], r, c);

            if (column.trim.isNotEmpty()) {
                const auto cell = grid_.cellBounds(rows, c);
                const auto font = theme::font(theme::sizeCaption, true);
                const float tw = std::min(static_cast<float>(cell.getWidth() - 4), widthOf(font, column.trim) + 2.0f * kTrimPadX);
                const auto box = cell.toFloat().withSizeKeepingCentre(tw, static_cast<float>(kTrimHeight));
                const bool hot = hovered_.part == Part::Trim && hovered_.column == c;
                g.setColour(hot ? theme::fieldHover : theme::field);
                g.fillRoundedRectangle(box, theme::radiusField);
                g.setColour(theme::hairline);
                g.drawRoundedRectangle(box.reduced(0.5f), theme::radiusField, 1.0f);
                g.setColour(theme::text);
                g.setFont(font);
                g.drawText(column.trim, box.reduced(static_cast<float>(kTrimPadX), 0.0f), juce::Justification::centredRight, false);
            }
        }
    }

    juce::String getTooltip() override {
        const auto hit = hitAt(getMouseXYRelative());
        if (hit.part != Part::Cell) return {};
        const auto& cell = grid_.state_.rows[static_cast<std::size_t>(hit.row)].cells[static_cast<std::size_t>(hit.column)];
        return cell.allowed ? juce::String() : cell.whyNot;
    }

    void mouseMove(const juce::MouseEvent& e) override { setHovered(hitAt(e.getPosition())); }
    void mouseExit(const juce::MouseEvent&) override { setHovered({}); }

    void mouseDown(const juce::MouseEvent& e) override {
        pressed_ = hitAt(e.getPosition());
        dragging_ = false;
        steps_ = 0;
        gesture_ = juce::Uuid().toString();
        if (pressed_.part == Part::Header) {
            const auto id = grid_.state_.columns[static_cast<std::size_t>(pressed_.column)].outputId;
            if (!e.mods.isPopupMenu() && grid_.onOutputPressed && grid_.onOutputPressed(id)) return;
            grid_.outputMenu(id, *this);
            pressed_ = {};
        }
        if (e.mods.isPopupMenu()) pressed_ = {};
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (pressed_.part != Part::Cell && pressed_.part != Part::Trim) return;
        const int dy = e.getDistanceFromDragStartY();
        if (!dragging_ && std::abs(dy) < kDragThreshold) return;
        dragging_ = true;
        const int steps = -dy / kPixelsPerStep;
        if (steps == steps_) return;
        steps_ = steps;
        const float delta = static_cast<float>(steps) * kDbPerStep;
        if (pressed_.part == Part::Cell) grid_.dragCell(pressed_.row, pressed_.column, delta, gesture_);
        else grid_.dragTrim(pressed_.column, delta, gesture_);
    }

    void mouseUp(const juce::MouseEvent& e) override {
        // The second click of a double-click doesn't toggle again: the double-click asks for a number instead.
        if (pressed_.part == Part::Cell && !dragging_ && e.getNumberOfClicks() < 2 && hitAt(e.getPosition()) == pressed_)
            grid_.clickCell(pressed_.row, pressed_.column);
        pressed_ = {};
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override {
        const auto hit = hitAt(e.getPosition());
        if (hit.part == Part::Cell) grid_.askCellLevel(hit.row, hit.column);
        if (hit.part == Part::Trim) grid_.askTrim(hit.column);
    }

private:
    enum class Part { None, Header, Cell, Trim };
    struct Hit {
        Part part = Part::None;
        int row = -1, column = -1;
        bool operator==(const Hit&) const = default;
    };

    Hit hitAt(juce::Point<int> p) const {
        const auto& state = grid_.state_;
        const int w = grid_.column_width_;
        if (w <= 0 || p.x < 0 || p.y < 0) return {};
        const int column = p.x / w;
        if (column >= static_cast<int>(state.columns.size())) return {};
        if (p.y < kHeaderHeight) return {Part::Header, -1, column};
        const int row = (p.y - kHeaderHeight) / kRowHeight;
        const int rows = static_cast<int>(state.rows.size());
        if (row < rows) return {Part::Cell, row, column};
        if (row == rows && state.columns[static_cast<std::size_t>(column)].trim.isNotEmpty()) return {Part::Trim, -1, column};
        return {};
    }

    void setHovered(const Hit& hit) {
        if (hit == hovered_) return;
        hovered_ = hit;
        const bool allowed = hit.part == Part::Cell
                             && grid_.state_.rows[static_cast<std::size_t>(hit.row)].cells[static_cast<std::size_t>(hit.column)].allowed;
        setMouseCursor(hit.part == Part::Header ? juce::MouseCursor::PointingHandCursor
                       : allowed || hit.part == Part::Trim ? juce::MouseCursor::UpDownResizeCursor
                                                           : juce::MouseCursor::NormalCursor);
        repaint();
    }

    void paintCell(juce::Graphics& g, const RoutingState::Cell& cell, int row, int column) {
        const auto area = grid_.cellBounds(row, column).toFloat();
        if (cell.allowed && hovered_.part == Part::Cell && hovered_.row == row && hovered_.column == column) {
            g.setColour(theme::field);
            g.fillRect(area.withTrimmedBottom(1.0f));
        }
        if (!cell.allowed) {
            g.setColour(juce::Colours::white.withAlpha(0.12f));
            g.fillRoundedRectangle(area.withSizeKeepingCentre(static_cast<float>(kBlockWidth), static_cast<float>(kBlockHeight)), 1.0f);
        } else if (!cell.sent) {
            g.setColour(juce::Colours::white.withAlpha(0.16f));
            g.setFont(theme::font(theme::sizeBody));
            g.drawText(kNotSent, area, juce::Justification::centred, false);
        } else {
            const auto font = theme::font(theme::sizeCaption, true);
            const float w = std::min(area.getWidth() - 4.0f, std::max(static_cast<float>(kLevelMinWidth), widthOf(font, cell.text) + 2.0f * kLevelPadX));
            const auto pill = area.withSizeKeepingCentre(w, static_cast<float>(kLevelHeight));
            g.setColour(theme::mint.withAlpha(cell.boosted ? 0.3f : 0.14f));
            g.fillRoundedRectangle(pill, 0.5f * kLevelHeight);
            g.setColour(theme::mint.withAlpha(0.55f));
            g.drawRoundedRectangle(pill.reduced(0.5f), 0.5f * kLevelHeight - 0.5f, 1.0f);
            g.setColour(theme::mint);
            g.setFont(font);
            g.drawText(cell.text, pill, juce::Justification::centred, false);
        }
    }

    RoutingGrid& grid_;
    Hit pressed_, hovered_;
    bool dragging_ = false;
    int steps_ = 0;
    juce::String gesture_;
};

// The role names, each with its colour bar, and "Overall level"; what the second viewport shows, level with the rows.
class RoutingGrid::Names : public juce::Component {
public:
    explicit Names(RoutingGrid& grid) : grid_(grid) {}

    void paint(juce::Graphics& g) override {
        const auto& state = grid_.state_;
        const int rows = static_cast<int>(state.rows.size());
        const int w = getWidth() - 2 * kCellPadX;
        g.setColour(theme::dim);
        g.setFont(headFont());
        g.drawText(shorten(kCornerTitle, headFont(), static_cast<float>(w)), kCellPadX, 0, w, kHeaderHeight, juce::Justification::centredLeft, false);
        g.setColour(theme::hairline);
        g.fillRect(0, kHeaderHeight - 1, getWidth(), 1);

        for (int r = 0; r <= rows; ++r) {
            const auto row = juce::Rectangle<int>(0, kHeaderHeight + r * kRowHeight, getWidth(), kRowHeight);
            g.setColour(theme::rowLine);
            g.fillRect(row.withTop(row.getBottom() - 1));
            auto inside = row.reduced(kCellPadX, 0);
            g.setFont(nameFont());
            if (r == rows) {
                g.setColour(theme::dim);
                g.drawText("Overall level", inside, juce::Justification::centredLeft, false);
                continue;
            }
            const auto& role = state.rows[static_cast<std::size_t>(r)];
            const auto bar = inside.removeFromLeft(kBarWidth).withSizeKeepingCentre(kBarWidth, kBarHeight);
            g.setColour(role.colour);
            g.fillRoundedRectangle(bar.toFloat(), 2.0f);
            inside.removeFromLeft(kBarGap);
            g.setColour(theme::text);
            g.drawText(shorten(role.name, nameFont(), static_cast<float>(inside.getWidth())), inside, juce::Justification::centredLeft, false);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        if (!e.mods.isPopupMenu()) return;
        const int row = (e.y - kHeaderHeight) / kRowHeight;
        if (e.y < kHeaderHeight || row >= static_cast<int>(grid_.state_.rows.size())) return;
        grid_.roleMenu(grid_.state_.rows[static_cast<std::size_t>(row)].roleId, *this);
    }

private:
    RoutingGrid& grid_;
};

// ---- The grid ----------------------------------------------------------------------------------------------------------

RoutingGrid::RoutingGrid(app::Session& session)
    : session_(session),
      viewport_(std::make_unique<FollowingViewport>()),
      names_viewport_(std::make_unique<FollowingViewport>()),
      body_(std::make_unique<Body>(*this)),
      names_(std::make_unique<Names>(*this)) {
    viewport_->setViewedComponent(body_.get(), false);
    viewport_->setScrollBarsShown(true, true);
    names_viewport_->setViewedComponent(names_.get(), false);
    names_viewport_->setScrollBarsShown(false, false, true, false);   // no bar of its own; the wheel still scrolls it
    viewport_->onMoved = [this](const juce::Rectangle<int>& area) { names_viewport_->setViewPosition(0, area.getY()); };
    names_viewport_->onMoved = [this](const juce::Rectangle<int>& area) {
        viewport_->setViewPosition(viewport_->getViewPositionX(), area.getY());
    };
    addAndMakeVisible(*names_viewport_);
    addAndMakeVisible(*viewport_);
    keepOffKeyboard(*this);
    session_.changes().addChangeListener(this);
    refresh(true);
}

RoutingGrid::~RoutingGrid() { session_.changes().removeChangeListener(this); }

const juce::Viewport& RoutingGrid::viewport() const { return *viewport_; }
const juce::Viewport& RoutingGrid::roleNames() const { return *names_viewport_; }

juce::Rectangle<int> RoutingGrid::cellBounds(int row, int column) const {
    return {column * column_width_, kHeaderHeight + row * kRowHeight, column_width_, kRowHeight};
}

bool RoutingGrid::editable() const { return session_.mode() == app::Mode::Edit; }

void RoutingGrid::changeListenerCallback(juce::ChangeBroadcaster*) { refresh(); }

void RoutingGrid::visibilityChanged() {
    if (isVisible() && !fresh_) refresh();
}

bool RoutingGrid::shownWithParents() const {
    for (const auto* c = static_cast<const juce::Component*>(this); c->getParentComponent() != nullptr; c = c->getParentComponent())
        if (!c->isVisible()) return false;
    return true;
}

void RoutingGrid::refresh(bool force) {
    // Hidden, it waits until it is shown: the session's changes come 30 times a second while a song plays.
    if (!force && !shownWithParents()) {
        fresh_ = false;
        return;
    }
    fresh_ = true;
    auto next = makeRoutingState(session_, nullptr);
    if (next == state_) return;
    state_ = std::move(next);
    layoutGrid();
    body_->repaint();
    names_->repaint();
}

void RoutingGrid::resized() {
    auto area = getLocalBounds().reduced(1);                      // inside the box's border
    footer_ = area.removeFromBottom(kFooterHeight);
    layoutGrid();

    auto row = footer_.reduced(kFooterPadX, 0);
    const auto take = [&row](const juce::String& text) {
        const auto r = row.removeFromLeft(std::min(row.getWidth(), ceilToInt(widthOf(linkFont(), text))));
        row.removeFromLeft(std::min(row.getWidth(), kFooterGap));
        return r;
    };
    add_role_ = take(kAddRole);
    add_output_ = take(kAddOutput);
    // The hint, cut to leave room for the link at its end.
    hint_ = row;
    const int linkWidth = std::min(row.getWidth(), ceilToInt(widthOf(linkFont(), kOnePerRole)));
    const auto hint = shorten(kHint, smallFont(), static_cast<float>(row.getWidth() - linkWidth));
    one_per_role_ = row.withTrimmedLeft(std::min(row.getWidth() - linkWidth, ceilToInt(widthOf(smallFont(), hint)))).withWidth(linkWidth);
    keepOffKeyboard(*this);                                       // the scroll bars, made again when the look changes
}

void RoutingGrid::layoutGrid() {
    auto area = getLocalBounds().reduced(1);
    area.removeFromBottom(kFooterHeight);
    const int columns = static_cast<int>(state_.columns.size()), rows = static_cast<int>(state_.rows.size());
    const int contentHeight = kHeaderHeight + (rows + 1) * kRowHeight;

    // The role names' column: wide enough for the longest name, within limits, and never more than a third of the grid.
    float widest = widthOf(headFont(), kCornerTitle);
    for (const auto& row : state_.rows) widest = std::max(widest, widthOf(nameFont(), row.name) + kBarWidth + kBarGap);
    name_width_ = std::min({std::max(kMinNameWidth, ceilToInt(widest) + 2 * kCellPadX), kMaxNameWidth, std::max(0, area.getWidth() / 3)});
    const auto namesArea = area.removeFromLeft(name_width_);

    // The columns share the width, each 72 to 140 px; when 72 each doesn't fit, they scroll sideways.
    const int bar = viewport_->getScrollBarThickness();
    bool down = contentHeight > area.getHeight();
    int available = std::max(0, area.getWidth() - (down ? bar : 0));
    const bool across = columns * kMinColumnWidth > available;
    if (across && !down && contentHeight > area.getHeight() - bar) {
        down = true;
        available = std::max(0, area.getWidth() - bar);
    }
    column_width_ = across || columns == 0 ? kMinColumnWidth : juce::jlimit(kMinColumnWidth, kMaxColumnWidth, available / columns);
    viewport_->setBounds(area);
    body_->setSize(across ? columns * kMinColumnWidth : std::max(available, columns * column_width_), contentHeight);

    // As tall as what the outputs show, so both reach the same last row.
    names_viewport_->setBounds(namesArea.withHeight(viewport_->getViewHeight()));
    names_->setSize(name_width_, contentHeight);
    names_viewport_->setViewPosition(0, viewport_->getViewPositionY());
}

void RoutingGrid::paint(juce::Graphics& g) {
    theme::paintPanel(g, getLocalBounds().toFloat());

    const auto centred = juce::Justification::centredLeft;
    g.setFont(linkFont());
    g.setColour(theme::mint);
    g.drawText(kAddRole, add_role_, centred, false);
    g.drawText(kAddOutput, add_output_, centred, false);
    if (message_.isNotEmpty()) {
        g.setColour(theme::caution);
        g.setFont(smallFont());
        g.drawText(shorten(message_, smallFont(), static_cast<float>(hint_.getWidth())), hint_, centred, false);
        return;
    }
    const auto hintArea = hint_.withRight(one_per_role_.getX());
    g.setColour(theme::dim);
    g.setFont(smallFont());
    g.drawText(shorten(kHint, smallFont(), static_cast<float>(hintArea.getWidth())), hintArea, centred, false);
    g.setColour(theme::mint);
    g.setFont(linkFont());
    g.drawText(shorten(kOnePerRole, linkFont(), static_cast<float>(one_per_role_.getWidth())), one_per_role_, centred, false);
}

RoutingGrid::Link RoutingGrid::linkAt(juce::Point<int> p) const {
    if (add_role_.contains(p)) return Link::AddRole;
    if (add_output_.contains(p)) return Link::AddOutput;
    if (message_.isEmpty() && one_per_role_.contains(p)) return Link::OnePerRole;
    return Link::None;
}

void RoutingGrid::mouseMove(const juce::MouseEvent& e) {
    setMouseCursor(linkAt(e.getPosition()) != Link::None && editable() ? juce::MouseCursor::PointingHandCursor
                                                                        : juce::MouseCursor::NormalCursor);
}

void RoutingGrid::mouseExit(const juce::MouseEvent&) { setMouseCursor(juce::MouseCursor::NormalCursor); }

void RoutingGrid::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) return;
    switch (linkAt(e.getPosition())) {
    case Link::AddRole: askAddRole(); break;
    case Link::AddOutput: askAddOutput(); break;
    case Link::OnePerRole: askOneOutputPerRole(); break;
    case Link::None: break;
    }
}

// ---- Changes -----------------------------------------------------------------------------------------------------------

bool RoutingGrid::run(const juce::String& command, const juce::var& params, const juce::String& coalesceKey) {
    if (!editable()) return false;
    const auto result = session_.commands().run(command, params, coalesceKey);
    say(result.message);                                          // a refusal, or what was done besides; "" clears it
    refresh(true);
    return result.ok;
}

void RoutingGrid::say(const juce::String& message) {
    if (message == message_) {
        if (message.isNotEmpty()) startTimer(kMessageMs);
        return;
    }
    message_ = message;
    if (message_.isNotEmpty()) startTimer(kMessageMs);
    else stopTimer();
    repaint(footer_);
}

void RoutingGrid::timerCallback() {
    stopTimer();
    message_.clear();
    repaint(footer_);
}

const model::OutputDoc* RoutingGrid::output(const juce::String& outputId) const {
    if (const auto* rig = session_.model().rig(session_.rigId()))
        for (const auto& out : rig->outputs)
            if (out.id == outputId) return &out;
    return nullptr;
}

std::optional<float> RoutingGrid::sentLevel(const juce::String& outputId, const juce::String& roleId) const {
    if (const auto* out = output(outputId))
        if (const auto sent = out->mix.find(roleId); sent != out->mix.end()) return sent->second;
    return std::nullopt;
}

void RoutingGrid::setLevel(const juce::String& outputId, const juce::String& roleId, std::optional<float> db, const juce::String& key) {
    run("mix.setLevel", object({{"rigId", session_.rigId()}, {"outputId", outputId}, {"roleId", roleId}, {"db", db ? juce::var(*db) : juce::var()}}),
        key);
}

void RoutingGrid::clickCell(int row, int column) {
    if (!editable() || !juce::isPositiveAndBelow(row, static_cast<int>(state_.rows.size()))
        || !juce::isPositiveAndBelow(column, static_cast<int>(state_.columns.size())))
        return;
    const auto& cell = state_.rows[static_cast<std::size_t>(row)].cells[static_cast<std::size_t>(column)];
    if (!cell.allowed) {                                          // never offered: the command would refuse it too
        say(cell.whyNot);
        return;
    }
    const auto outputId = state_.columns[static_cast<std::size_t>(column)].outputId;
    const auto roleId = state_.rows[static_cast<std::size_t>(row)].roleId;
    setLevel(outputId, roleId, sentLevel(outputId, roleId) ? std::nullopt : std::optional<float>(0.0f), {});
}

void RoutingGrid::dragCell(int row, int column, float deltaDb, const juce::String& gestureId) {
    if (!editable()) return;
    if (gestureId != gestureId_) {
        if (!juce::isPositiveAndBelow(row, static_cast<int>(state_.rows.size()))
            || !juce::isPositiveAndBelow(column, static_cast<int>(state_.columns.size())))
            return;
        gestureId_ = gestureId;
        gestureOutputId_ = state_.columns[static_cast<std::size_t>(column)].outputId;
        gestureRoleId_ = state_.rows[static_cast<std::size_t>(row)].roleId;
        gestureStartDb_ = sentLevel(gestureOutputId_, gestureRoleId_).value_or(0.0f);
        gestureRefused_ = false;
    }
    if (gestureRoleId_.isEmpty() || gestureRefused_) return;     // a trim's gesture, or one already refused
    const auto* out = output(gestureOutputId_);
    const auto* role = session_.model().role(gestureRoleId_);
    if (out != nullptr && role != nullptr && !model::mayRoute(role->kind, out->label)) {
        gestureRefused_ = true;
        say(model::whyNot(role->kind, out->label));
        return;
    }
    setLevel(gestureOutputId_, gestureRoleId_, toTenth(model::clampDb(gestureStartDb_ + deltaDb)), gestureId);
}

void RoutingGrid::dragTrim(int column, float deltaDb, const juce::String& gestureId) {
    if (!editable()) return;
    if (gestureId != gestureId_) {
        if (!juce::isPositiveAndBelow(column, static_cast<int>(state_.columns.size()))) return;
        gestureId_ = gestureId;
        gestureOutputId_ = state_.columns[static_cast<std::size_t>(column)].outputId;
        gestureRoleId_.clear();
        const auto* out = output(gestureOutputId_);
        gestureStartDb_ = out != nullptr ? out->trimDb : 0.0f;
        gestureRefused_ = false;
    }
    if (gestureRoleId_.isNotEmpty()) return;                     // a cell's gesture
    run("output.set", object({{"rigId", session_.rigId()}, {"outputId", gestureOutputId_},
                              {"trimDb", toTenth(model::clampDb(gestureStartDb_ + deltaDb))}}),
        gestureId);
}

void RoutingGrid::setOutputChannels(const juce::String& outputId, int firstChannel, bool stereo) {
    run("output.set", object({{"rigId", session_.rigId()}, {"outputId", outputId}, {"firstChannel", firstChannel}, {"stereo", stereo}}));
}

void RoutingGrid::setOutputLabel(const juce::String& outputId, model::OutputLabel label) {
    run("output.set", object({{"rigId", session_.rigId()}, {"outputId", outputId}, {"label", labelKey(label)}}));
}

// Each part in stereo from output 1/2 on, then the click in mono after them, each an in-ear output with only its own role
// in its mix. One key for every command, so one undo takes it all back.
void RoutingGrid::oneOutputPerRole() {
    const auto* rig = session_.model().rig(session_.rigId());
    if (!editable() || rig == nullptr) return;
    const auto rigId = rig->id;
    std::vector<juce::String> old;
    for (const auto& out : rig->outputs) old.push_back(out.id);
    std::vector<model::Role> roles;
    for (const bool parts : {true, false})
        for (const auto& role : session_.model().band().roles)
            if (role.kind == (parts ? model::RoleKind::Part : model::RoleKind::Click)) roles.push_back(role);

    const auto key = juce::Uuid().toString();
    auto& commands = session_.commands();
    const auto step = [&](const juce::String& name, const juce::var& params) {
        const auto result = commands.run(name, params, key);
        if (!result.ok) say(result.message);
        return result;
    };
    [&] {
        for (const auto& id : old)
            if (!step("output.remove", object({{"rigId", rigId}, {"outputId", id}})).ok) return;
        int channel = 1;
        for (const auto& role : roles) {
            const bool stereo = role.kind == model::RoleKind::Part;
            const auto added = step("output.add", object({{"rigId", rigId}, {"name", role.name}, {"firstChannel", channel},
                                                          {"stereo", stereo}, {"label", "monitor"}}));
            if (!added.ok) return;
            if (!step("mix.setLevel", object({{"rigId", rigId}, {"outputId", added.value}, {"roleId", role.id}, {"db", 0.0f}})).ok) return;
            channel += stereo ? 2 : 1;
        }
        say({});
    }();
    refresh(true);
}

// ---- Menus and questions -----------------------------------------------------------------------------------------------

void RoutingGrid::outputMenu(const juce::String& outputId, juce::Component& target) {
    const auto* out = output(outputId);
    if (out == nullptr) return;
    const bool edit = editable();
    juce::PopupMenu labels;
    for (int i = 0; i < 4; ++i) labels.addItem(10 + i, labelName(kLabels[i]), edit, out->label == kLabels[i]);
    juce::PopupMenu menu;
    menu.addItem(1, "Rename" + kEllipsis, edit);
    menu.addItem(2, "Outputs" + kEllipsis, edit);
    menu.addSubMenu("Label", labels, edit);
    menu.addSeparator();
    menu.addItem(3, "Remove", edit);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&target).withMousePosition(),
                       [safe = juce::Component::SafePointer<RoutingGrid>(this), outputId](int result) {
                           if (safe == nullptr) return;
                           if (result == 1) safe->askRenameOutput(outputId);
                           if (result == 2) safe->askOutputChannels(outputId);
                           if (result == 3) safe->run("output.remove", object({{"rigId", safe->session_.rigId()}, {"outputId", outputId}}));
                           if (result >= 10 && result < 14) safe->setOutputLabel(outputId, kLabels[result - 10]);
                       });
}

void RoutingGrid::roleMenu(const juce::String& roleId, juce::Component& target) {
    const auto* role = session_.model().role(roleId);
    if (role == nullptr) return;
    const bool edit = editable();
    juce::PopupMenu colours;
    for (int i = 0; i < 9; ++i) {
        const auto colour = model::roleColour(i);
        colours.addColouredItem(10 + i, kColourNames[i], colour, edit, role->colour == colour);
    }
    juce::PopupMenu menu;
    menu.addItem(1, "Rename" + kEllipsis, edit);
    menu.addSubMenu("Colour", colours, edit);
    menu.addSeparator();
    menu.addItem(2, "Remove", edit && role->kind == model::RoleKind::Part);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&target).withMousePosition(),
                       [safe = juce::Component::SafePointer<RoutingGrid>(this), roleId](int result) {
                           if (safe == nullptr) return;
                           if (result == 1) safe->askRenameRole(roleId);
                           if (result == 2) safe->run("role.remove", object({{"roleId", roleId}}));
                           if (result >= 10 && result < 19)
                               safe->run("role.set", object({{"roleId", roleId}, {"colour", model::roleColour(result - 10).toDisplayString(false)}}));
                       });
}

void RoutingGrid::askText(const juce::String& title, const juce::String& text, const juce::String& button,
                          std::function<void(const juce::String&)> then) {
    if (!editable()) return;
    auto* window = new juce::AlertWindow(title, {}, juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor("text", text);
    window->addButton(button, 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    window->enterModalState(true,
                            juce::ModalCallbackFunction::create(
                                [safe = juce::Component::SafePointer<RoutingGrid>(this), window, then = std::move(then)](int result) {
                                    if (safe != nullptr && result == 1 && safe->editable()) then(window->getTextEditorContents("text").trim());
                                }),
                            true);
}

void RoutingGrid::askCellLevel(int row, int column) {
    if (!juce::isPositiveAndBelow(row, static_cast<int>(state_.rows.size()))
        || !juce::isPositiveAndBelow(column, static_cast<int>(state_.columns.size())))
        return;
    const auto& r = state_.rows[static_cast<std::size_t>(row)];
    const auto& c = state_.columns[static_cast<std::size_t>(column)];
    const auto& cell = r.cells[static_cast<std::size_t>(column)];
    if (!cell.allowed) {
        say(cell.whyNot);
        return;
    }
    const auto level = sentLevel(c.outputId, r.roleId);
    askText("How loud is " + r.name + " in " + c.name + ", in dB?", level ? formatDb(*level) : "0", "Set",
            [this, outputId = c.outputId, roleId = r.roleId](const juce::String& text) {
                if (text.isEmpty() || text.equalsIgnoreCase("off")) setLevel(outputId, roleId, std::nullopt, {});
                else run("mix.setLevel", object({{"rigId", session_.rigId()}, {"outputId", outputId}, {"roleId", roleId}, {"db", text}}));
            });
}

void RoutingGrid::askTrim(int column) {
    if (!juce::isPositiveAndBelow(column, static_cast<int>(state_.columns.size()))) return;
    const auto& c = state_.columns[static_cast<std::size_t>(column)];
    const auto* out = output(c.outputId);
    if (out == nullptr) return;
    askText("Overall level of " + c.name + ", in dB", formatDb(out->trimDb), "Set", [this, outputId = c.outputId](const juce::String& text) {
        run("output.set", object({{"rigId", session_.rigId()}, {"outputId", outputId}, {"trimDb", text}}));
    });
}

void RoutingGrid::askRenameOutput(const juce::String& outputId) {
    const auto* out = output(outputId);
    if (out == nullptr) return;
    askText("Rename the output", out->name, "Rename", [this, outputId](const juce::String& name) {
        run("output.set", object({{"rigId", session_.rigId()}, {"outputId", outputId}, {"name", name}}));
    });
}

void RoutingGrid::askOutputChannels(const juce::String& outputId) {
    const auto* out = output(outputId);
    if (out == nullptr || !editable()) return;
    auto* window = new juce::AlertWindow("Outputs of " + out->name, {}, juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor("first", juce::String(out->firstChannel), "First output");
    window->addComboBox("kind", {"Stereo", "Mono"}, {});
    window->getComboBoxComponent("kind")->setSelectedItemIndex(out->stereo ? 0 : 1, juce::dontSendNotification);
    window->addButton("Set", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    window->enterModalState(true,
                            juce::ModalCallbackFunction::create([safe = juce::Component::SafePointer<RoutingGrid>(this), window, outputId](int result) {
                                if (safe == nullptr || result != 1) return;
                                const auto first = window->getTextEditorContents("first").trim();
                                safe->run("output.set", object({{"rigId", safe->session_.rigId()}, {"outputId", outputId}, {"firstChannel", first},
                                                                {"stereo", window->getComboBoxComponent("kind")->getSelectedItemIndex() == 0}}));
                            }),
                            true);
}

void RoutingGrid::askAddRole() {
    askText("Add a role", {}, "Add", [this](const juce::String& name) { run("role.add", object({{"name", name}})); });
}

void RoutingGrid::askAddOutput() {
    const auto* rig = session_.model().rig(session_.rigId());
    if (rig == nullptr || !editable()) return;
    int next = 1;                                                 // the first channel after every output the rig has
    for (const auto& out : rig->outputs) next = std::max(next, out.firstChannel + (out.stereo ? 2 : 1));
    auto* window = new juce::AlertWindow("Add an output", {}, juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor("name", {}, "Name");
    window->addTextEditor("first", juce::String(next), "First output");
    window->addComboBox("kind", {"Stereo", "Mono"}, {});
    window->getComboBoxComponent("kind")->setSelectedItemIndex(0, juce::dontSendNotification);
    juce::StringArray labels;
    for (const auto label : kLabels) labels.add(labelName(label));
    window->addComboBox("label", labels, "Label");
    window->getComboBoxComponent("label")->setSelectedItemIndex(0, juce::dontSendNotification);
    window->addButton("Add", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    window->enterModalState(true,
                            juce::ModalCallbackFunction::create([safe = juce::Component::SafePointer<RoutingGrid>(this), window](int result) {
                                if (safe == nullptr || result != 1) return;
                                const int label = juce::jlimit(0, 3, window->getComboBoxComponent("label")->getSelectedItemIndex());
                                safe->run("output.add", object({{"rigId", safe->session_.rigId()},
                                                                {"name", window->getTextEditorContents("name").trim()},
                                                                {"firstChannel", window->getTextEditorContents("first").trim()},
                                                                {"stereo", window->getComboBoxComponent("kind")->getSelectedItemIndex() == 0},
                                                                {"label", labelKey(kLabels[label])}}));
                            }),
                            true);
}

void RoutingGrid::askRenameRole(const juce::String& roleId) {
    const auto* role = session_.model().role(roleId);
    if (role == nullptr) return;
    askText("Rename the role", role->name, "Rename", [this, roleId](const juce::String& name) {
        run("role.set", object({{"roleId", roleId}, {"name", name}}));
    });
}

void RoutingGrid::askOneOutputPerRole() {
    if (!editable()) return;
    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                     .withIconType(juce::MessageBoxIconType::NoIcon)
                                     .withAssociatedComponent(this)
                                     .withTitle("One output per role?")
                                     .withMessage("This replaces the outputs of " + state_.rigName
                                                  + " with one in-ear output for each role: stereo for each part, then mono for the click.")
                                     .withButton("Replace them")
                                     .withButton("Cancel"),
                                 [safe = juce::Component::SafePointer<RoutingGrid>(this)](int result) {
                                     if (safe != nullptr && result == 1) safe->oneOutputPerRole();
                                 });
}

}  // namespace sqlive::ui
