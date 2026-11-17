#include "ui/RoutingGrid.h"

// The routing grid's menus and questions, and what they do with what was typed. The rest of it is in RoutingGrid.cpp.

#include <algorithm>
#include <cmath>

#include "model/JsonObject.h"
#include "model/Routing.h"
#include "model/Units.h"
#include "ui/Format.h"
#include "ui/Helpers.h"
#include "ui/OutputLabelPill.h"
#include "ui/RoutingGridParts.h"
#include "ui/Theme.h"

namespace sqlive::ui {

using model::jsonObject;

// ---- Menus and questions -----------------------------------------------------------------------------------------------

void RoutingGrid::outputMenu(const juce::String& outputId, juce::Component& target) {
    const auto* out = output(outputId);
    if (out == nullptr) return;
    const bool edit = editable();
    juce::PopupMenu labels;
    for (int i = 0; i < 4; ++i) labels.addItem(10 + i, outputLabelName(kLabels[i]), edit, out->label == kLabels[i]);
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
                           if (result == 3) safe->run("output.remove", jsonObject({{"rigId", safe->session_.rigId()}, {"outputId", outputId}}));
                           if (result >= 10 && result < 14) safe->setOutputLabel(outputId, kLabels[result - 10]);
                       });
}

juce::PopupMenu RoutingGrid::roleMenuFor(const juce::String& roleId) const {
    juce::PopupMenu menu;
    const auto* role = session_.model().role(roleId);
    if (role == nullptr) return menu;
    const bool edit = editable();
    menu.addItem(1, "Rename" + kEllipsis, edit);
    if (role->kind != model::RoleKind::Click) {                   // the click is always drawn in ember
        juce::PopupMenu colours;
        for (int i = 0; i < kColourCount; ++i)
            colours.addColouredItem(10 + i, kRoleColours[i].name, kRoleColours[i].colour, edit, role->colour == kRoleColours[i].colour);
        menu.addSubMenu("Colour", colours, edit);
    }
    menu.addSeparator();
    menu.addItem(2, "Remove", edit && role->kind == model::RoleKind::Part);
    return menu;
}

void RoutingGrid::roleMenu(const juce::String& roleId, juce::Component& target) {
    if (session_.model().role(roleId) == nullptr) return;
    roleMenuFor(roleId).showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&target).withMousePosition(),
                       [safe = juce::Component::SafePointer<RoutingGrid>(this), roleId](int result) {
                           if (safe == nullptr) return;
                           if (result == 1) safe->askRenameRole(roleId);
                           if (result == 2) safe->run("role.remove", jsonObject({{"roleId", roleId}}));
                           if (result >= 10 && result < 10 + kColourCount)
                               safe->run("role.set", jsonObject({{"roleId", roleId}, {"colour", kRoleColours[result - 10].colour.toDisplayString(false)}}));
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
    askText("How loud is " + r.name + " in " + c.name + ", in dB?", level ? formatDbNumber(*level) : "0", "Set",
            [this, row, column](const juce::String& text) { typeCellLevel(row, column, text); });
}

void RoutingGrid::askTrim(int column) {
    if (!juce::isPositiveAndBelow(column, static_cast<int>(state_.columns.size()))) return;
    const auto& c = state_.columns[static_cast<std::size_t>(column)];
    const auto* out = output(c.outputId);
    if (out == nullptr) return;
    askText("Overall level of " + c.name + ", in dB", formatDbNumber(out->trimDb), "Set",
            [this, column](const juce::String& text) { typeTrim(column, text); });
}

void RoutingGrid::typeCellLevel(int row, int column, const juce::String& typed) {
    if (!editable() || !juce::isPositiveAndBelow(row, static_cast<int>(state_.rows.size()))
        || !juce::isPositiveAndBelow(column, static_cast<int>(state_.columns.size())))
        return;
    const auto& cell = state_.rows[static_cast<std::size_t>(row)].cells[static_cast<std::size_t>(column)];
    if (!cell.allowed) {
        say(cell.whyNot);
        return;
    }
    const auto outputId = state_.columns[static_cast<std::size_t>(column)].outputId;
    const auto roleId = state_.rows[static_cast<std::size_t>(row)].roleId;
    const auto text = typed.trim();
    if (text.isEmpty() || text.equalsIgnoreCase("off")) {         // stop sending it
        setLevel(outputId, roleId, std::nullopt, {});
        return;
    }
    const auto db = parseDb(text);
    if (!db) {
        say(kNotALevel);
        return;
    }
    setLevel(outputId, roleId, toTenth(model::clampDb(*db)), {});
}

void RoutingGrid::typeTrim(int column, const juce::String& typed) {
    if (!editable() || !juce::isPositiveAndBelow(column, static_cast<int>(state_.columns.size()))) return;
    const auto text = typed.trim();
    const auto db = text.equalsIgnoreCase("off") ? std::optional<float>(model::kMinDb) : parseDb(text);
    if (!db) {
        say(kNotALevel);
        return;
    }
    run("output.set", jsonObject({{"rigId", session_.rigId()}, {"outputId", state_.columns[static_cast<std::size_t>(column)].outputId},
                              {"trimDb", toTenth(model::clampDb(*db))}}));
}

void RoutingGrid::typeOutputChannels(const juce::String& outputId, const juce::String& typedFirst, bool stereo) {
    if (!editable()) return;
    if (const auto first = parseChannel(typedFirst)) setOutputChannels(outputId, *first, stereo);
    else say(kNotAChannel);
}

void RoutingGrid::addOutput(const juce::String& name, const juce::String& typedFirst, bool stereo, model::OutputLabel label) {
    if (!editable()) return;
    const auto first = parseChannel(typedFirst);
    if (!first) {
        say(kNotAChannel);
        return;
    }
    run("output.add", jsonObject({{"rigId", session_.rigId()}, {"name", name.trim()}, {"firstChannel", *first}, {"stereo", stereo},
                              {"label", labelKey(label)}}));
}

void RoutingGrid::askRenameOutput(const juce::String& outputId) {
    const auto* out = output(outputId);
    if (out == nullptr) return;
    askText("Rename the output", out->name, "Rename", [this, outputId](const juce::String& name) {
        run("output.set", jsonObject({{"rigId", session_.rigId()}, {"outputId", outputId}, {"name", name}}));
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
                                safe->typeOutputChannels(outputId, window->getTextEditorContents("first"),
                                                         window->getComboBoxComponent("kind")->getSelectedItemIndex() == 0);
                            }),
                            true);
}

void RoutingGrid::askAddRole() {
    askText("Add a role", {}, "Add", [this](const juce::String& name) { run("role.add", jsonObject({{"name", name}})); });
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
    for (const auto label : kLabels) labels.add(outputLabelName(label));
    window->addComboBox("label", labels, "Label");
    window->getComboBoxComponent("label")->setSelectedItemIndex(0, juce::dontSendNotification);
    window->addButton("Add", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    window->enterModalState(true,
                            juce::ModalCallbackFunction::create([safe = juce::Component::SafePointer<RoutingGrid>(this), window](int result) {
                                if (safe == nullptr || result != 1) return;
                                const int label = juce::jlimit(0, 3, window->getComboBoxComponent("label")->getSelectedItemIndex());
                                safe->addOutput(window->getTextEditorContents("name"), window->getTextEditorContents("first"),
                                                window->getComboBoxComponent("kind")->getSelectedItemIndex() == 0, kLabels[label]);
                            }),
                            true);
}

void RoutingGrid::askRenameRole(const juce::String& roleId) {
    const auto* role = session_.model().role(roleId);
    if (role == nullptr) return;
    askText("Rename the role", role->name, "Rename", [this, roleId](const juce::String& name) {
        run("role.set", jsonObject({{"roleId", roleId}, {"name", name}}));
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
