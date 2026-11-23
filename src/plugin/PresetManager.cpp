#include "plugin/PresetManager.h"
#include "plugin/Parameters.h"

namespace novapitch
{

namespace
{
using Values = std::vector<std::pair<juce::String, float>>;

struct FactoryPreset
{
    const char* name; // UTF-8
    Values values;    // real (denormalised) values; anything not listed = default
};

const std::vector<FactoryPreset>& factoryPresets()
{
    // Modes: 0 Live, 1 Tracking, 2 HQ, 3 Vintage. Profiles: 0 Soprano, 1 Alto/Tenor,
    // 2 Voz grave, 3 Instrumento, 4 Instrumento grave.
    static const std::vector<FactoryPreset> presets {
        { PresetManager::kDefaultPresetName, {} },
        { "Corrección natural", { { ParamIDs::qualityMode, 1.0f }, { ParamIDs::retuneSpeed, 60.0f },
                                  { ParamIDs::tolerance, 15.0f }, { ParamIDs::naturalHold, 60.0f },
                                  { ParamIDs::vibratoPreserve, 100.0f } } },
        { "Pop moderno", { { ParamIDs::qualityMode, 1.0f }, { ParamIDs::retuneSpeed, 15.0f },
                           { ParamIDs::tolerance, 5.0f }, { ParamIDs::naturalHold, 30.0f },
                           { ParamIDs::vibratoPreserve, 80.0f } } },
        { "Hard tune", { { ParamIDs::qualityMode, 1.0f }, { ParamIDs::retuneSpeed, 0.0f },
                         { ParamIDs::tolerance, 0.0f }, { ParamIDs::naturalHold, 0.0f },
                         { ParamIDs::vibratoPreserve, 0.0f } } },
        { "Trap / urbano latino", { { ParamIDs::qualityMode, 3.0f }, { ParamIDs::sensitivity, 60.0f },
                                    { ParamIDs::vibratoPreserve, 0.0f }, { ParamIDs::character, 5.0f } } },
        { "Directo", { { ParamIDs::qualityMode, 0.0f }, { ParamIDs::retuneSpeed, 25.0f },
                       { ParamIDs::tolerance, 10.0f }, { ParamIDs::naturalHold, 20.0f } } },
        { "Voz grave", { { ParamIDs::inputProfile, 2.0f }, { ParamIDs::qualityMode, 1.0f },
                         { ParamIDs::retuneSpeed, 50.0f }, { ParamIDs::naturalHold, 40.0f } } },
        { "Armonías 3as", { { ParamIDs::qualityMode, 1.0f }, { ParamIDs::retuneSpeed, 30.0f },
                            { ParamIDs::harmonyVoice ("hvMode", 1), 1.0f }, { ParamIDs::harmonyVoice ("hvDegrees", 1), 2.0f },
                            { ParamIDs::harmonyVoice ("hvPan", 1), -45.0f }, { ParamIDs::harmonyVoice ("hvLevel", 1), -7.0f },
                            { ParamIDs::harmonyVoice ("hvMode", 2), 1.0f }, { ParamIDs::harmonyVoice ("hvDegrees", 2), -2.0f },
                            { ParamIDs::harmonyVoice ("hvPan", 2), 45.0f }, { ParamIDs::harmonyVoice ("hvLevel", 2), -9.0f },
                            { ParamIDs::harmonyVoice ("hvFormant", 2), -1.0f } } },
    };
    return presets;
}
} // namespace

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& s, juce::File folder)
    : state (s), userFolder (folder == juce::File() ? defaultUserFolder() : folder)
{
    if (! state.state.hasProperty (kPresetNameProperty))
        state.state.setProperty (kPresetNameProperty, juce::String::fromUTF8 (kDefaultPresetName), nullptr);
}

juce::File PresetManager::defaultUserFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("NovaPitch").getChildFile ("Presets");
}

bool PresetManager::isSongParameter (const juce::String& id)
{
    return id == ParamIDs::key || id == ParamIDs::scale || id == ParamIDs::referenceA4 || id.startsWith ("noteState")
           || id.startsWith ("noteDetune")
           || id == ParamIDs::graphMode || id == ParamIDs::midiOut;
}

std::vector<PresetManager::Preset> PresetManager::getPresets() const
{
    std::vector<Preset> out;
    for (const auto& f : factoryPresets())
        out.push_back ({ juce::String::fromUTF8 (f.name), true, {} });

    std::vector<Preset> user;
    for (const auto& file : userFolder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + kFileExtension))
        user.push_back ({ file.getFileNameWithoutExtension(), false, file });
    std::sort (user.begin(), user.end(), [] (const Preset& a, const Preset& b) { return a.name.compareNatural (b.name) < 0; });
    out.insert (out.end(), user.begin(), user.end());
    return out;
}

void PresetManager::applyValues (const Values& values)
{
    // Everything that is not a song setting goes back to its default first.
    for (auto* p : state.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (! isSongParameter (ranged->getParameterID()))
                ranged->setValueNotifyingHost (ranged->getDefaultValue());

    for (const auto& [id, value] : values)
        if (! isSongParameter (id))
            if (auto* param = state.getParameter (id))
                param->setValueNotifyingHost (param->convertTo0to1 (value));
}

PresetManager::Values PresetManager::captureSound() const
{
    Values out;
    for (auto* p : state.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (! isSongParameter (ranged->getParameterID()))
                out.emplace_back (ranged->getParameterID(), ranged->convertFrom0to1 (ranged->getValue()));
    return out;
}

bool PresetManager::presetSound (const juce::String& name, Values& out) const
{
    Values listed;
    bool found = false;
    for (const auto& f : factoryPresets())
        if (juce::String::fromUTF8 (f.name) == name)
        {
            listed = f.values;
            found = true;
            break;
        }
    if (! found)
    {
        const auto file = userFolder.getChildFile (name + kFileExtension);
        if (! file.existsAsFile())
            return false;
        if (name == cachedPresetName && file.getLastModificationTime() == cachedPresetTime)
        {
            out = cachedPresetValues;
            return true;
        }
        auto xml = juce::XmlDocument::parse (file);
        if (xml == nullptr || ! xml->hasTagName ("NovaPitchPreset"))
            return false;
        for (auto* e : xml->getChildWithTagNameIterator ("Param"))
            listed.emplace_back (e->getStringAttribute ("id"), static_cast<float> (e->getDoubleAttribute ("value")));
        cachedPresetName = name;
        cachedPresetTime = file.getLastModificationTime();
    }

    out.clear();
    for (auto* p : state.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (! isSongParameter (ranged->getParameterID()))
            {
                float v = ranged->convertFrom0to1 (ranged->getDefaultValue());
                for (const auto& [id, value] : listed)
                    if (id == ranged->getParameterID())
                        v = value;
                out.emplace_back (ranged->getParameterID(), v);
            }
    if (! found)
        cachedPresetValues = out;
    return true;
}

bool PresetManager::isModified() const
{
    Values expected;
    if (! presetSound (getCurrentPresetName(), expected))
        return false;
    for (const auto& [id, value] : expected)
        if (auto* p = state.getParameter (id))
            if (std::abs (p->getValue() - p->convertTo0to1 (value)) > 1.0e-4f)
                return true;
    return false;
}

juce::ValueTree PresetManager::compareTree() const
{
    auto t = state.state.getChildWithName (kCompareTag);
    if (! t.isValid())
    {
        t = juce::ValueTree (kCompareTag);
        t.setProperty ("active", 0, nullptr);
        state.state.appendChild (t, nullptr);
    }
    return t;
}

int PresetManager::getActiveSlot() const
{
    const auto t = state.state.getChildWithName (kCompareTag);
    return t.isValid() ? juce::jlimit (0, 1, static_cast<int> (t.getProperty ("active", 0))) : 0;
}

void PresetManager::storeSlot (int slot)
{
    auto t = compareTree();
    auto s = t.getChildWithProperty ("index", slot);
    if (! s.isValid())
    {
        s = juce::ValueTree ("Slot");
        s.setProperty ("index", slot, nullptr);
        t.appendChild (s, nullptr);
    }
    s.removeAllProperties (nullptr);
    s.setProperty ("index", slot, nullptr);
    s.setProperty (kPresetNameProperty, getCurrentPresetName(), nullptr);
    for (const auto& [id, value] : captureSound())
        s.setProperty (juce::Identifier (id), value, nullptr);
}

void PresetManager::selectSlot (int slot)
{
    slot = juce::jlimit (0, 1, slot);
    const int active = getActiveSlot();
    if (slot == active)
        return;

    storeSlot (active); // the current sound belongs to the slot we leave
    auto t = compareTree();
    t.setProperty ("active", slot, nullptr);

    const auto target = t.getChildWithProperty ("index", slot);
    if (! target.isValid())
    {
        storeSlot (slot); // first visit: B starts as a copy of A
        return;
    }
    Values values;
    for (auto* p : state.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const auto id = ranged->getParameterID();
            if (! isSongParameter (id) && target.hasProperty (id))
                values.emplace_back (id, static_cast<float> (static_cast<double> (target.getProperty (id))));
        }
    applyValues (values); // parameters missing from the slot (added later) go to their defaults
    state.state.setProperty (kPresetNameProperty, target.getProperty (kPresetNameProperty, getCurrentPresetName()), nullptr);
}

void PresetManager::copyActiveToOther()
{
    const int active = getActiveSlot();
    storeSlot (active);
    storeSlot (1 - active); // same settings and preset name
}

bool PresetManager::loadPreset (const juce::String& name)
{
    for (const auto& f : factoryPresets())
        if (juce::String::fromUTF8 (f.name) == name)
        {
            applyValues (f.values);
            state.state.setProperty (kPresetNameProperty, name, nullptr);
            return true;
        }

    const auto file = userFolder.getChildFile (name + kFileExtension);
    if (auto xml = juce::XmlDocument::parse (file); xml != nullptr && xml->hasTagName ("NovaPitchPreset"))
    {
        Values values;
        for (auto* e : xml->getChildWithTagNameIterator ("Param"))
            values.emplace_back (e->getStringAttribute ("id"), static_cast<float> (e->getDoubleAttribute ("value")));
        applyValues (values);
        state.state.setProperty (kPresetNameProperty, name, nullptr);
        return true;
    }
    return false;
}

bool PresetManager::saveUserPreset (const juce::String& rawName)
{
    const auto name = juce::File::createLegalFileName (rawName.trim());
    if (name.isEmpty())
        return false;

    juce::XmlElement xml ("NovaPitchPreset");
    xml.setAttribute ("version", 1);
    xml.setAttribute ("name", name);
    for (auto* p : state.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (! isSongParameter (ranged->getParameterID()))
            {
                auto* e = xml.createNewChildElement ("Param");
                e->setAttribute ("id", ranged->getParameterID());
                e->setAttribute ("value", ranged->convertFrom0to1 (ranged->getValue()));
            }

    if (! userFolder.createDirectory())
        return false;
    if (! xml.writeTo (userFolder.getChildFile (name + kFileExtension)))
        return false;
    state.state.setProperty (kPresetNameProperty, name, nullptr);
    return true;
}

bool PresetManager::deleteUserPreset (const juce::String& name)
{
    const auto file = userFolder.getChildFile (name + kFileExtension);
    if (! file.existsAsFile() || ! file.deleteFile())
        return false;
    if (getCurrentPresetName() == name)
        state.state.setProperty (kPresetNameProperty, juce::String::fromUTF8 (kDefaultPresetName), nullptr);
    return true;
}

juce::String PresetManager::getCurrentPresetName() const
{
    return state.state.getProperty (kPresetNameProperty, juce::String::fromUTF8 (kDefaultPresetName)).toString();
}

int PresetManager::getCurrentIndex() const
{
    const auto presets = getPresets();
    const auto current = getCurrentPresetName();
    for (size_t i = 0; i < presets.size(); ++i)
        if (presets[i].name == current)
            return static_cast<int> (i);
    return -1;
}

bool PresetManager::loadNext (int direction)
{
    const auto presets = getPresets();
    if (presets.empty())
        return false;
    const int n = static_cast<int> (presets.size());
    const int index = (std::max (0, getCurrentIndex()) + (direction >= 0 ? 1 : -1) + n) % n;
    return loadPreset (presets[static_cast<size_t> (index)].name);
}

} // namespace novapitch
