#pragma once

#include <initializer_list>
#include <utility>

#include <juce_core/juce_core.h>

namespace sqlive::test {

inline juce::var params(std::initializer_list<std::pair<const char*, juce::var>> items) {
    auto* object = new juce::DynamicObject();
    for (const auto& item : items) object->setProperty(juce::Identifier(item.first), item.second);
    return juce::var(object);
}

}  // namespace sqlive::test
