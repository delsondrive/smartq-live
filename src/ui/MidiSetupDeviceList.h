#pragma once

// The MIDI setup page's MIDI DEVICES rows: MidiSetupPage::DeviceList. Only ever included by MidiSetupPage.cpp, after
// MidiSetupPageParts.h, whose helpers it uses.

#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "ui/MidiSetupPage.h"
#include "ui/MidiSetupPageParts.h"
#include "ui/Widgets.h"

namespace sqlive::ui {

// The rows of the MIDI DEVICES table, in the page's viewport: each row's controls, its name, timing and status drawn.
class MidiSetupPage::DeviceList : public juce::Component {
public:
    explicit DeviceList(MidiSetupPage& page) : page_(page) {}

    void update() {
        const auto& devices = page_.state_.devices;
        while (rows_.size() < devices.size()) addRow(static_cast<int>(rows_.size()));
        while (rows_.size() > devices.size()) rows_.pop_back();
        juce::StringArray channels;
        for (int c = 1; c <= 16; ++c) channels.add(juce::String(c));
        const bool edit = page_.editable();
        for (std::size_t i = 0; i < devices.size(); ++i) {
            const auto& d = devices[i];
            auto& r = *rows_[i];
            auto ports = page_.state_.ports;
            if (d.hasPort && !ports.contains(d.port)) ports.add(d.port);   // chosen here, not plugged in now
            ports.add(kNone);
            r.port.setItems(ports, d.hasPort ? ports.indexOf(d.port) : -1);
            r.port.setNeedsAttention(!d.hasPort);
            r.channel.setItems(channels, d.channel.getIntValue() - 1);
            r.clock.setOn(d.clock);
            r.startStop.setOn(d.startStop);
            r.patches.setButtonText(d.patches);
            r.test.setEnabled(d.canTest);
            r.test.setTooltip(d.canTest ? "Sends " + d.name + "'s first patch" : d.name + " isn't connected");
            for (juce::Component* c : std::initializer_list<juce::Component*>{&r.port, &r.channel, &r.clock, &r.startStop, &r.patches})
                c->setEnabled(edit);
        }
        keepOffKeyboard(*this);
        layout();
        repaint();
    }

    juce::Rectangle<int> cell(int row, Column column) const {
        const auto edges = columnEdges(getWidth());
        return {edges[static_cast<std::size_t>(column)], row * kDeviceRow, edges[static_cast<std::size_t>(column) + 1] - edges[static_cast<std::size_t>(column)],
                kDeviceRow};
    }

    void resized() override { layout(); }

    void paint(juce::Graphics& g) override {
        const auto& devices = page_.state_.devices;
        for (int i = 0; i < static_cast<int>(devices.size()); ++i) {
            const auto& d = devices[static_cast<std::size_t>(i)];
            const int y = i * kDeviceRow;
            g.setColour(theme::rowLine);
            g.fillRect(0, y + kDeviceRow - 1, getWidth(), 1);

            // NAME: its colour bar, then its name.
            auto name = cell(i, Name).reduced(kCellPad, 0);
            g.setColour(d.colour);
            g.fillRoundedRectangle(juce::Rectangle<float>(static_cast<float>(name.getX()), static_cast<float>(y) + (kDeviceRow - 14) / 2.0f, 4.0f, 14.0f), 2.0f);
            name.removeFromLeft(std::min(12, name.getWidth()));
            g.setColour(theme::text);
            g.setFont(nameFont());
            g.drawText(shorten(d.name, nameFont(), static_cast<float>(name.getWidth())), name, juce::Justification::centredLeft, false);

            // TIMING: the value on a field, on the right of its box.
            const auto value = valueBox(i).toFloat();
            g.setColour(theme::field);
            g.fillRoundedRectangle(value, theme::radiusField);
            g.setColour(theme::hairline);
            g.drawRoundedRectangle(value.reduced(0.5f), theme::radiusField, 1.0f);
            g.setColour(theme::text);
            g.setFont(nameFont());
            g.drawText(d.timing, value.reduced(8.0f, 0.0f), juce::Justification::centredRight, false);

            // STATUS.
            const auto status = cell(i, Status).reduced(kCellPad, 0);
            g.setColour(d.statusLevel == 0 ? theme::mint : d.statusLevel == 1 ? theme::caution : theme::dim);
            g.setFont(tdFont());
            g.drawText(shorten(d.status, tdFont(), static_cast<float>(status.getWidth())), status, juce::Justification::centredLeft, false);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        drag_row_ = -1;
        const int row = rowAt(e.position);
        if (row < 0) return;
        const auto id = page_.state_.devices[static_cast<std::size_t>(row)].id;
        if (cell(row, Name).contains(e.getPosition())) {
            if (e.mods.isPopupMenu()) page_.deviceMenu(row);
            else if (page_.editable()) askRename(id);
            return;
        }
        if (!e.mods.isPopupMenu() && valueBox(row).expanded(4).contains(e.getPosition()) && page_.editable()) {
            drag_row_ = row;
            drag_from_ = page_.state_.devices[static_cast<std::size_t>(row)].offsetMs;
            gesture_ = juce::Uuid().toDashedString();
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (drag_row_ < 0) return;
        // Right or up is later, 1 ms a pixel.
        const double delta = e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY();
        page_.setTiming(drag_row_, drag_from_ + delta, gesture_);
    }

    void mouseUp(const juce::MouseEvent&) override { drag_row_ = -1; }

    void mouseDoubleClick(const juce::MouseEvent& e) override {
        const int row = rowAt(e.position);
        if (row >= 0 && !e.mods.isPopupMenu() && valueBox(row).expanded(4).contains(e.getPosition())) page_.setTiming(row, 0.0);
    }

    void mouseMove(const juce::MouseEvent& e) override {
        const int row = rowAt(e.position);
        const bool edit = page_.editable() && row >= 0;
        setMouseCursor(edit && valueBox(row).expanded(4).contains(e.getPosition()) ? juce::MouseCursor::LeftRightResizeCursor
                       : edit && cell(row, Name).contains(e.getPosition())       ? juce::MouseCursor::PointingHandCursor
                                                                                   : juce::MouseCursor::NormalCursor);
    }

private:
    struct Row {
        Choice port, channel;
        Switch clock, startStop;
        PillButton patches{juce::String()}, test{"Test"};
    };

    void addRow(int index) {
        auto row = std::make_unique<Row>();
        row->port.setPlaceholder(chooseAPort());
        row->port.onChange = [this, index](int chosen) {
            const auto& items = rows_[static_cast<std::size_t>(index)]->port.items();
            page_.choosePort(index, chosen == items.size() - 1 ? juce::String() : items[chosen]);   // the last is "None"
        };
        row->channel.onChange = [this, index](int chosen) { page_.chooseChannel(index, chosen + 1); };
        row->clock.onChange = [this, index](bool on) { page_.setClock(index, on); };
        row->startStop.onChange = [this, index](bool on) { page_.setStartStop(index, on); };
        row->patches.onClick = [this, index] { page_.openPatches(index); };
        row->test.onClick = [this, index] { page_.testDevice(index); };
        for (juce::Component* c : std::initializer_list<juce::Component*>{&row->port, &row->channel, &row->clock, &row->startStop,
                                                                          &row->patches, &row->test})
            addAndMakeVisible(c);
        rows_.push_back(std::move(row));
    }

    void layout() {
        for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
            auto& r = *rows_[static_cast<std::size_t>(i)];
            const auto inside = [&](Column column) { return cell(i, column).reduced(kCellPad, 0); };
            const auto centred = [&](Column column, int w, int h) {
                const auto c = inside(column);
                return juce::Rectangle<int>(c.getX(), c.getCentreY() - h / 2, std::min(w, c.getWidth()), h);
            };
            r.port.setBounds(centred(Port, inside(Port).getWidth(), kChoiceHeight));
            r.channel.setBounds(centred(Channel, inside(Channel).getWidth(), kChoiceHeight));
            r.clock.setBounds(centred(Clock, kSwitchW, kSwitchH));
            r.startStop.setBounds(centred(StartStop, kSwitchW, kSwitchH));
            r.patches.setBounds(centred(Patches, r.patches.idealWidth(), kButtonHeight));
            r.test.setBounds(centred(Test, r.test.idealWidth(), kButtonHeight));
        }
    }

    // The timing value's field: at least 80 wide, as wide as "200 ms early" needs.
    juce::Rectangle<int> valueBox(int row) const {
        const auto c = cell(row, Timing).reduced(kCellPad, 0);
        const int w = std::min(c.getWidth(), std::max(kValueMin, static_cast<int>(std::ceil(textWidth(nameFont(), "200 ms early"))) + 16));
        return {c.getX(), c.getCentreY() - kValueHeight / 2, w, kValueHeight};
    }

    int rowAt(juce::Point<float> at) const {
        const int row = static_cast<int>(std::floor(at.y / kDeviceRow));
        return at.y >= 0.0f && row < static_cast<int>(page_.state_.devices.size()) ? row : -1;
    }

    void askRename(const juce::String& id) {
        const int row = page_.rowOf(id);
        if (row < 0) return;
        askUserText(page_, "Rename the device", page_.state_.devices[static_cast<std::size_t>(row)].name, "Rename",
                    [page = &page_, id](const juce::String& name) {
                        if (const int now = page->rowOf(id); now >= 0) page->renameDevice(now, name);
                    });
    }

    MidiSetupPage& page_;
    std::vector<std::unique_ptr<Row>> rows_;
    int drag_row_ = -1;
    double drag_from_ = 0.0;
    juce::String gesture_;
};

}  // namespace sqlive::ui
