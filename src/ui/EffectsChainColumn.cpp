#include "ui/EffectsChainColumn.h"

#include <algorithm>
#include <cmath>

#include "ui/Helpers.h"
#include "ui/Theme.h"
#include "ui/fx/ModuleHistory.h"
#include "ui/fx/ModuleMiniature.h"

namespace sqlive::ui {

namespace {

// The mockup's .chain (12 px around, 8 between), .mcard (9 px corners, 8 px by 10 px inside, 9 below; a 13 px semibold name, the
// grip in #5b5361, a 12 px reading and the icons in dim; the 58 px picture 6 below the line, 5 px corners on #0d0b10), .addm (a
// dashed 9 px outline, 13 px semibold words in mid) and .note (12 px dim).
constexpr int kPad = 12, kGap = 8;
constexpr int kInsetX = 11, kHeaderTop = 9, kHeaderHeight = 18, kPictureGap = 6, kPictureHeight = 58;
constexpr int kIcon = 18, kIconGap = 4, kGripWidth = 8, kNameGap = 8, kReadingWidth = 48;
constexpr int kAddHeight = 38, kMenuGap = 6, kMenuIndent = 12;
constexpr int kDragThreshold = 4;                     // a click that wobbles is still a click
constexpr float kCardRadius = 9.0f, kPictureRadius = 5.0f;
constexpr double kReadingSeconds = 0.1;

const juce::Colour kColumnFill{0xff0f0d13}, kCardFill{0xff1a1620}, kShownFill{0xff16211e}, kPictureFill{0xff0d0b10};
const juce::Colour kCardBorder{0x1affffff}, kGrip{0xff5b5361}, kAddBorder{0x38ffffff};
const juce::Colour kMenuFill{0xff1f1a26}, kMenuBorder{0x2effffff}, kMenuHot{0x1f6ff0c8};

juce::Font nameFont() { return theme::font(theme::sizeBody, true); }
juce::Font readingFont() { return theme::font(theme::sizeSmall); }

bool readsReduction(const juce::String& kind) { return kind == "dynamic" || kind == "deess" || kind == "limit"; }

// "−3 dB": a whole number, or a tenth where a whole number would say nothing; "" under a tenth (Smart Q's card's).
juce::String reductionText(double db) {
    if (db < smartq::ModuleHistory::kReadingFloorDb) return {};
    return utf8("\xe2\x88\x92") + (db < 0.95 ? juce::String(db, 1) : juce::String(juce::roundToInt(db))) + " dB";
}

void drawDashedRoundedRectangle(juce::Graphics& g, juce::Rectangle<float> area, float radius, juce::Colour colour) {
    juce::Path outline, dashed;
    outline.addRoundedRectangle(area, radius);
    const float dashes[] = {4.0f, 3.0f};
    juce::PathStrokeType(1.0f).createDashedStroke(dashed, outline, dashes, 2);
    g.setColour(colour);
    g.fillPath(dashed);
}

}  // namespace

// ---- What the add menu offers ------------------------------------------------------------------------------------------

juce::String effectHint(const juce::String& kind) {
    if (kind == "eq") return "tone";
    if (kind == "dynamic") return "evens out";
    if (kind == "deess") return utf8("harsh \xe2\x80\x9cs\xe2\x80\x9d");
    if (kind == "width") return "stereo, mono bass";
    if (kind == "transient") return "attack, tail";
    if (kind == "limit") return "ceiling";
    return {};
}

std::vector<AddChoice> addChoicesFor(const std::vector<model::EffectDoc>& chain, model::EffectPlace place, bool stereo) {
    std::vector<AddChoice> choices;
    for (const auto& kind : model::effectKindsFor(place, stereo))
        choices.push_back({kind, model::effectName(kind), effectHint(kind), model::whyEffectCantBeAdded(chain, place, stereo, kind)});
    return choices;
}

// ---- The buttons ---------------------------------------------------------------------------------------------------------

// A card's power or remove button: its glyph in dim, bright under the mouse.
class EffectsChainColumn::IconButton : public juce::Button {
public:
    enum class Glyph { Power, Remove };

    IconButton(Glyph glyph, const juce::String& name) : juce::Button(name), glyph_(glyph) {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void paintButton(juce::Graphics& g, bool hot, bool down) override {
        const auto c = getLocalBounds().toFloat().getCentre();
        g.setColour(hot || down ? theme::text : theme::dim);
        if (glyph_ == Glyph::Remove) {
            constexpr float r = 3.6f;
            g.drawLine(c.x - r, c.y - r, c.x + r, c.y + r, 1.3f);
            g.drawLine(c.x - r, c.y + r, c.x + r, c.y - r, 1.3f);
            return;
        }
        // ⏻: a ring open at the top, and a stroke down into it.
        constexpr float r = 4.6f;
        juce::Path ring;
        ring.addCentredArc(c.x, c.y + 0.6f, r, r, 0.0f, juce::degreesToRadians(40.0f), juce::degreesToRadians(320.0f), true);
        g.strokePath(ring, juce::PathStrokeType(1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.drawLine(c.x, c.y - r - 0.4f, c.x, c.y + 0.6f, 1.3f);
    }

private:
    const Glyph glyph_;
};

// "+ Add effect": a dashed outline, a little brighter under the mouse and while its menu is open.
class EffectsChainColumn::AddButton : public juce::Button {
public:
    AddButton() : juce::Button("Add effect") { setMouseCursor(juce::MouseCursor::PointingHandCursor); }

    void paintButton(juce::Graphics& g, bool hot, bool down) override {
        const auto area = getLocalBounds().toFloat().reduced(0.5f);
        const bool lit = getToggleState();
        if (hot || down) {
            g.setColour(theme::field);
            g.fillRoundedRectangle(area, kCardRadius);
        }
        drawDashedRoundedRectangle(g, area, kCardRadius, hot || lit ? kAddBorder.withMultipliedAlpha(1.6f) : kAddBorder);
        g.setColour(hot ? theme::text : theme::mid);
        g.setFont(nameFont());
        g.drawText("+ Add effect", getLocalBounds(), juce::Justification::centred, false);
    }
};

// ---- A card --------------------------------------------------------------------------------------------------------------

EffectsChainColumn::Card::Card(EffectsChainColumn& column, int index, Effect effect)
    : column_(column), index_(index), kind_(effect.kind), on_(effect.on), miniature_(std::move(effect.miniature)),
      history_(readsReduction(effect.kind) ? effect.history : nullptr),
      power_(std::make_unique<IconButton>(IconButton::Glyph::Power, "Power")),
      remove_(std::make_unique<IconButton>(IconButton::Glyph::Remove, "Remove")) {
    setTitle(model::effectName(kind_));
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    power_->onClick = [this] { if (column_.onSwitch) column_.onSwitch(index_, !on_); };
    remove_->onClick = [this] { if (column_.onRemove) column_.onRemove(index_); };
    remove_->setTooltip("Remove " + model::effectName(kind_));
    addAndMakeVisible(*power_);
    addAndMakeVisible(*remove_);
    if (miniature_ != nullptr) {
        miniature_->setHistory(effect.history);
        addAndMakeVisible(*miniature_);
    }
    setOn(on_);
}

EffectsChainColumn::Card::~Card() = default;

juce::Button& EffectsChainColumn::Card::powerButton() { return *power_; }
juce::Button& EffectsChainColumn::Card::removeButton() { return *remove_; }

void EffectsChainColumn::Card::setOn(bool on) {
    on_ = on;
    power_->setTooltip(on ? "Switch off" : "Switch on");
    if (miniature_ != nullptr) miniature_->setDimmed(!on);
    repaint();
}

void EffectsChainColumn::Card::setSelected(bool selected) {
    if (selected == selected_) return;
    selected_ = selected;
    repaint();
}

void EffectsChainColumn::Card::setLifted(bool lifted) {
    if (lifted == lifted_) return;
    lifted_ = lifted;
    setAlpha(lifted ? 0.5f : 1.0f);
}

void EffectsChainColumn::Card::step(double dtSeconds) {
    if (history_ != nullptr) {
        // The most it turned the audio down since the last reading, so a short peak between readings is still told.
        readingPeakDb_ = std::max(readingPeakDb_, history_->reductionNowDb());
        readingSeconds_ += dtSeconds;
        if (readingSeconds_ >= kReadingSeconds - 1.0e-9) {
            readingSeconds_ = 0.0;
            if (auto text = reductionText(on_ ? readingPeakDb_ : 0.0); text != reading_) {
                reading_ = std::move(text);
                repaint(readingArea_);
            }
            readingPeakDb_ = 0.0;
        }
    }
    if (miniature_ != nullptr) miniature_->update(dtSeconds);
}

void EffectsChainColumn::Card::resized() {
    auto header = juce::Rectangle<int>(kInsetX, kHeaderTop, getWidth() - 2 * kInsetX, kHeaderHeight);
    remove_->setBounds(header.removeFromRight(kIcon).withSizeKeepingCentre(kIcon, kIcon));
    header.removeFromRight(kIconGap);
    power_->setBounds(header.removeFromRight(kIcon).withSizeKeepingCentre(kIcon, kIcon));
    header.removeFromRight(kIconGap + 2);
    readingArea_ = readsReduction(kind_) ? header.removeFromRight(kReadingWidth) : juce::Rectangle<int>();
    header_ = header;
    picture_ = {kInsetX, kHeaderTop + kHeaderHeight + kPictureGap, getWidth() - 2 * kInsetX, kPictureHeight};
    if (miniature_ != nullptr) miniature_->setBounds(picture_.reduced(1));
}

void EffectsChainColumn::Card::paint(juce::Graphics& g) {
    const auto area = getLocalBounds().toFloat();
    if (selected_) {
        // The mockup's .mcard.on: a mint border and a faint mint ring outside it, on a green-black.
        g.setColour(theme::mint.withAlpha(0.35f));
        g.drawRoundedRectangle(area.reduced(0.5f), kCardRadius + 1.0f, 1.0f);
        g.setColour(kShownFill);
        g.fillRoundedRectangle(area.reduced(1.0f), kCardRadius);
        g.setColour(theme::mint);
        g.drawRoundedRectangle(area.reduced(1.5f), kCardRadius, 1.0f);
    } else {
        g.setColour(kCardFill);
        g.fillRoundedRectangle(area.reduced(1.0f), kCardRadius);
        g.setColour(kCardBorder);
        g.drawRoundedRectangle(area.reduced(1.5f), kCardRadius, 1.0f);
    }

    // ⋮⋮: two columns of three dots.
    auto header = header_;
    const auto grip = header.removeFromLeft(kGripWidth).toFloat();
    g.setColour(kGrip);
    for (int column = 0; column < 2; ++column)
        for (int row = 0; row < 3; ++row)
            g.fillEllipse(grip.getX() + 1.0f + 4.0f * (float) column, grip.getCentreY() - 5.0f + 4.0f * (float) row, 2.0f, 2.0f);
    header.removeFromLeft(kNameGap);

    g.setColour(on_ ? theme::text : theme::dim);
    g.setFont(nameFont());
    g.drawText(model::effectName(kind_), header, juce::Justification::centredLeft, true);

    if (!reading_.isEmpty()) {
        g.setColour(theme::dim);
        g.setFont(readingFont());
        g.drawText(reading_, readingArea_, juce::Justification::centredRight, false);
    }

    g.setColour(kPictureFill);
    g.fillRoundedRectangle(picture_.toFloat(), kPictureRadius);
}

void EffectsChainColumn::Card::mouseDown(const juce::MouseEvent&) { carried_ = false; }

void EffectsChainColumn::Card::mouseDrag(const juce::MouseEvent& e) {
    if (!carried_) {
        if (e.getDistanceFromDragStart() <= kDragThreshold) return;
        carried_ = true;
        column_.beginDrag(index_);
    }
    column_.moveDrag(index_, e.getEventRelativeTo(&column_).position.y);
}

void EffectsChainColumn::Card::mouseUp(const juce::MouseEvent& e) {
    if (carried_) {
        carried_ = false;
        column_.endDrag(index_);
        return;
    }
    if (e.mouseWasClicked() || e.getDistanceFromDragStart() <= kDragThreshold)
        if (column_.onSelect) column_.onSelect(index_);
}

// ---- The add menu --------------------------------------------------------------------------------------------------------

namespace {
// The menu's box sits inside the component, which leaves room around it for its shadow (the mockup's 0 14px 40px).
constexpr int kShadowLeft = 10, kShadowRight = 6, kShadowTop = 4, kShadowBottom = 24;
}  // namespace

EffectsChainColumn::AddMenu::Item::Item(AddMenu& menu, AddChoice choice) : menu_(menu), choice_(std::move(choice)) {
    setTitle(choice_.name);
    setTooltip(choice_.refusal);
    setMouseCursor(isRefused() ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
}

void EffectsChainColumn::AddMenu::Item::paint(juce::Graphics& g) {
    const auto area = getLocalBounds();
    if (!isRefused() && isMouseOver(true)) {
        g.setColour(kMenuHot);
        g.fillRoundedRectangle(area.toFloat(), 6.0f);
    }
    const float alpha = isRefused() ? 0.4f : 1.0f;
    auto inner = area.reduced(10, 0);
    const auto square = inner.removeFromLeft(9).toFloat().withSizeKeepingCentre(9.0f, 9.0f);
    g.setColour(theme::effectColour(choice_.kind).withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(square, 2.0f);
    inner.removeFromLeft(10);
    g.setColour(theme::dim.withMultipliedAlpha(alpha));
    g.setFont(theme::font(theme::sizeCaption));
    const int hintWidth = widthInPixels(theme::font(theme::sizeCaption), choice_.hint) + 2;
    g.drawText(choice_.hint, inner.removeFromRight(hintWidth), juce::Justification::centredRight, false);
    g.setColour(theme::text.withMultipliedAlpha(alpha));
    g.setFont(nameFont());
    g.drawText(choice_.name, inner, juce::Justification::centredLeft, true);
}

void EffectsChainColumn::AddMenu::Item::mouseUp(const juce::MouseEvent& e) {
    if (isRefused() || !getLocalBounds().contains(e.getPosition())) return;
    auto& column = menu_.column_;
    const auto kind = choice_.kind;
    column.closeAddMenu();                                        // keeps this alive until the call has returned
    if (column.onAdd) column.onAdd(kind);
}

EffectsChainColumn::AddMenu::AddMenu(EffectsChainColumn& column, const std::vector<AddChoice>& choices) : column_(column) {
    for (const auto& choice : choices) {
        items_.push_back(std::make_unique<Item>(*this, choice));
        addAndMakeVisible(*items_.back());
    }
}

int EffectsChainColumn::AddMenu::idealHeight() const {
    return kShadowTop + 2 * pad + itemHeight * itemCount() + kShadowBottom;
}

void EffectsChainColumn::AddMenu::resized() {
    auto box = getLocalBounds().withTrimmedLeft(kShadowLeft).withTrimmedRight(kShadowRight).withTrimmedTop(kShadowTop)
                   .withTrimmedBottom(kShadowBottom).reduced(pad);
    for (auto& item : items_) item->setBounds(box.removeFromTop(itemHeight));
}

void EffectsChainColumn::AddMenu::paint(juce::Graphics& g) {
    const auto box = getLocalBounds().withTrimmedLeft(kShadowLeft).withTrimmedRight(kShadowRight).withTrimmedTop(kShadowTop)
                         .withTrimmedBottom(kShadowBottom);
    juce::DropShadow(juce::Colours::black.withAlpha(0.6f), 22, {0, 10}).drawForRectangle(g, box);
    g.setColour(kMenuFill);
    g.fillRoundedRectangle(box.toFloat(), kCardRadius);
    g.setColour(kMenuBorder);
    g.drawRoundedRectangle(box.toFloat().reduced(0.5f), kCardRadius, 1.0f);
}

// ---- The column ----------------------------------------------------------------------------------------------------------

EffectsChainColumn::EffectsChainColumn() : add_(std::make_unique<AddButton>()) {
    add_->onClick = [this] {
        if (addMenuOpen()) closeAddMenu();
        else openAddMenu();
    };
    addAndMakeVisible(*add_);
}

EffectsChainColumn::~EffectsChainColumn() = default;

juce::Component& EffectsChainColumn::addButton() { return *add_; }

void EffectsChainColumn::setEffects(std::vector<Effect> effects) {
    closeAddMenu();
    drag_ = {};
    for (auto& card : cards_) removeChildComponent(card.get());
    cards_.clear();
    count_ = static_cast<int>(effects.size());
    for (int i = 0; i < count_; ++i) {
        cards_.push_back(std::make_unique<Card>(*this, i, std::move(effects[static_cast<size_t>(i)])));
        cards_.back()->setSelected(i == selected_);
        addAndMakeVisible(*cards_.back());
    }
    layOutCards();
    repaint();
}

void EffectsChainColumn::setOn(int index, bool on) {
    if (index >= 0 && index < cardCount()) card(index).setOn(on);
}

void EffectsChainColumn::setSelected(int index) {
    selected_ = index;
    for (int i = 0; i < cardCount(); ++i) card(i).setSelected(i == index);
}

void EffectsChainColumn::setAddChoices(std::vector<AddChoice> choices) {
    if (choices == choices_) return;
    choices_ = std::move(choices);
    if (addMenuOpen()) {                                          // the same menu, saying what is true now
        closeAddMenu();
        openAddMenu();
    }
}

void EffectsChainColumn::openAddMenu() {
    if (menu_ != nullptr) return;
    menu_ = std::make_unique<AddMenu>(*this, choices_);
    addAndMakeVisible(*menu_);
    menu_->setBounds(menuBounds());
    add_->setToggleState(true, juce::dontSendNotification);
}

void EffectsChainColumn::closeAddMenu() {
    if (menu_ == nullptr) return;
    // Often from one of its own items: it is hidden now and deleted once the call that closed it has returned.
    menu_->setVisible(false);
    removeChildComponent(menu_.get());
    juce::MessageManager::callAsync([menu = std::shared_ptr<AddMenu>(std::move(menu_))] {});
    add_->setToggleState(false, juce::dontSendNotification);
}

void EffectsChainColumn::step(double dtSeconds) {
    for (auto& card : cards_) card->step(dtSeconds);
}

juce::String EffectsChainColumn::slotsText() const {
    return juce::String(count_) + " of " + juce::String(model::kMaxEffects) + " slots used.";
}

juce::Rectangle<int> EffectsChainColumn::menuBounds() const {
    if (menu_ == nullptr) return {};
    const int h = menu_->idealHeight();
    const int x = add_->getX() + kMenuIndent - kShadowLeft;
    const int top = add_->getBottom() + kMenuGap - kShadowTop;
    const int y = std::max(0, std::min(top, getHeight() - h + kShadowBottom - kPad));   // kept in the column when it is short
    return {x, y, AddMenu::width + kShadowLeft + kShadowRight, h};
}

void EffectsChainColumn::layOutCards() {
    const int w = getWidth() - 2 * kPad;
    int y = kPad;
    for (auto& card : cards_) {
        card->setBounds(kPad, y, w, Card::height);
        y += Card::height + kGap;
    }
    add_->setBounds(kPad, y, w, kAddHeight);
    if (menu_ != nullptr) menu_->setBounds(menuBounds());
}

void EffectsChainColumn::resized() { layOutCards(); }

void EffectsChainColumn::paint(juce::Graphics& g) {
    g.fillAll(kColumnFill);
    g.setColour(theme::hairline);
    g.fillRect(getWidth() - 1, 0, 1, getHeight());
    g.setColour(theme::dim);
    g.setFont(theme::font(theme::sizeSmall));
    g.drawText(slotsText(), juce::Rectangle<int>(kPad, getHeight() - kPad - 18, getWidth() - 2 * kPad, 18),
               juce::Justification::centredLeft, true);
}

// Where the card carried would land: a mint line in the gap it would go into.
void EffectsChainColumn::paintOverChildren(juce::Graphics& g) {
    if (drag_.index < 0 || drag_.target < 0 || drag_.target == drag_.index) return;
    const auto& at = card(drag_.target).getBounds();
    const int y = drag_.target < drag_.index ? at.getY() - kGap / 2 : at.getBottom() + kGap / 2;
    g.setColour(theme::mint);
    g.fillRoundedRectangle(juce::Rectangle<float>((float) at.getX(), (float) y - 1.0f, (float) at.getWidth(), 2.0f), 1.0f);
}

void EffectsChainColumn::beginDrag(int index) {
    closeAddMenu();
    drag_ = {index, index};
    card(index).setLifted(true);
}

// The place it would have once moved: as many of the others as have their middles above the pointer.
void EffectsChainColumn::moveDrag(int index, float columnY) {
    if (drag_.index != index) return;
    int target = 0;
    for (int i = 0; i < cardCount(); ++i)
        if (i != index && (float) card(i).getBounds().getCentreY() < columnY) ++target;
    if (target != drag_.target) {
        drag_.target = target;
        repaint();
    }
}

void EffectsChainColumn::endDrag(int index) {
    if (drag_.index != index) return;
    const int to = drag_.target;
    drag_ = {};
    card(index).setLifted(false);
    repaint();
    if (to >= 0 && to != index && onMove) onMove(index, to);
}

}  // namespace sqlive::ui
