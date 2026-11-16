#pragma once

#include <initializer_list>
#include <utility>

#include <juce_core/juce_core.h>

namespace sqlive::model {

// A JSON object from its keys and values, as commands take their parameters: jsonObject({{"songId", id}, {"gainDb", -6.0}}).
inline juce::var jsonObject(std::initializer_list<std::pair<const char*, juce::var>> items) {
    auto* o = new juce::DynamicObject();
    for (const auto& [key, value] : items) o->setProperty(juce::Identifier(key), value);
    return juce::var(o);
}

}  // namespace sqlive::model
