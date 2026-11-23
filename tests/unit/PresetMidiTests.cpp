#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "TestHelpers.h"

using namespace novapitch;
using namespace novapitch::test;
using Catch::Matchers::WithinAbs;

namespace
{
float value (NovaPitchProcessor& p, const juce::String& id)
{
    auto* param = p.getValueTreeState().getParameter (id);
    return param->convertFrom0to1 (param->getValue());
}

int indexOf (NovaPitchProcessor& p, const juce::String& id)
{
    const auto& params = p.getParameters();
    for (int i = 0; i < params.size(); ++i)
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (params[i]); r != nullptr && r->getParameterID() == id)
            return i;
    return -1;
}

struct TempFolder
{
    juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                         .getChildFile ("NovaPitchPresetTest-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
    ~TempFolder() { dir.deleteRecursively(); }
};
} // namespace

TEST_CASE ("Factory presets set the sound but keep the song settings", "[presets]")
{
    JuceFixture fixture;
    NovaPitchProcessor p;
    TempFolder tmp;
    PresetManager pm (p.getValueTreeState(), tmp.dir);

    setParam (p, ParamIDs::key, 9.0f);
    setParam (p, ParamIDs::scale, 1.0f);
    setParam (p, ParamIDs::referenceA4, 442.0f);
    setParam (p, ParamIDs::transpose, 5.0f);

    REQUIRE (pm.loadPreset ("Hard tune"));
    CHECK (value (p, ParamIDs::retuneSpeed) == 0.0f);
    CHECK (value (p, ParamIDs::vibratoPreserve) == 0.0f);
    CHECK (value (p, ParamIDs::transpose) == 0.0f);       // non-listed sound parameter -> default
    CHECK (value (p, ParamIDs::key) == 9.0f);             // song settings untouched
    CHECK (value (p, ParamIDs::scale) == 1.0f);
    CHECK_THAT (value (p, ParamIDs::referenceA4), WithinAbs (442.0, 0.05));
    CHECK (pm.getCurrentPresetName() == "Hard tune");

    REQUIRE (pm.loadPreset (juce::String::fromUTF8 ("Corrección natural")));
    CHECK_THAT (value (p, ParamIDs::retuneSpeed), WithinAbs (60.0, 0.1));
    CHECK_FALSE (pm.loadPreset ("does not exist"));
}

TEST_CASE ("Preset navigation wraps around", "[presets]")
{
    JuceFixture fixture;
    NovaPitchProcessor p;
    TempFolder tmp;
    PresetManager pm (p.getValueTreeState(), tmp.dir);
    const auto presets = pm.getPresets();
    REQUIRE (presets.size() >= 7);

    pm.loadPreset (presets.front().name);
    pm.loadNext (-1);
    CHECK (pm.getCurrentPresetName() == presets.back().name);
    pm.loadNext (+1);
    CHECK (pm.getCurrentPresetName() == presets.front().name);
}

TEST_CASE ("User presets save, list, load and delete", "[presets]")
{
    JuceFixture fixture;
    NovaPitchProcessor p;
    TempFolder tmp;
    PresetManager pm (p.getValueTreeState(), tmp.dir);

    setParam (p, ParamIDs::retuneSpeed, 123.0f);
    setParam (p, ParamIDs::formantShift, -3.0f);
    setParam (p, ParamIDs::qualityMode, 2.0f);
    REQUIRE (pm.saveUserPreset (juce::String::fromUTF8 ("Mi voz ñ")));

    bool listed = false;
    for (const auto& preset : pm.getPresets())
        listed = listed || (! preset.factory && preset.name == juce::String::fromUTF8 ("Mi voz ñ"));
    CHECK (listed);

    pm.loadPreset ("Hard tune");
    CHECK (value (p, ParamIDs::retuneSpeed) == 0.0f);

    REQUIRE (pm.loadPreset (juce::String::fromUTF8 ("Mi voz ñ")));
    CHECK_THAT (value (p, ParamIDs::retuneSpeed), WithinAbs (123.0, 0.1));
    CHECK_THAT (value (p, ParamIDs::formantShift), WithinAbs (-3.0, 0.01));
    CHECK (value (p, ParamIDs::qualityMode) == 2.0f);

    CHECK (pm.deleteUserPreset (juce::String::fromUTF8 ("Mi voz ñ")));
    CHECK_FALSE (pm.loadPreset (juce::String::fromUTF8 ("Mi voz ñ")));
    CHECK (pm.getCurrentPresetName() == PresetManager::kDefaultPresetName);
}

TEST_CASE ("The current preset name is saved with the project", "[presets][state]")
{
    JuceFixture fixture;
    NovaPitchProcessor a;
    a.getPresetManager().loadPreset ("Directo");
    juce::MemoryBlock block;
    a.getStateInformation (block);

    NovaPitchProcessor b;
    b.setStateInformation (block.getData(), static_cast<int> (block.getSize()));
    CHECK (b.getPresetManager().getCurrentPresetName() == "Directo");
    CHECK (value (b, ParamIDs::qualityMode) == 0.0f);
}

TEST_CASE ("MIDI learn binds the next CC and drives the parameter", "[midi]")
{
    JuceFixture fixture;
    NovaPitchProcessor p;
    p.prepareToPlay (48000.0, 256);
    const int speed = indexOf (p, ParamIDs::retuneSpeed);
    REQUIRE (speed >= 0);

    juce::AudioBuffer<float> buffer (2, 256);
    buffer.clear();
    auto sendCc = [&] (int cc, int v) {
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::controllerEvent (1, cc, v), 10);
        p.processBlock (buffer, midi);
        p.getMidiLearn().dispatchPending();
    };

    sendCc (21, 100); // not learning yet: ignored
    CHECK (p.getMidiLearn().getCcForParam (speed) == -1);

    p.getMidiLearn().startLearning (speed);
    sendCc (21, 64);
    CHECK (p.getMidiLearn().getLearningParam() == -1);
    CHECK (p.getMidiLearn().getCcForParam (speed) == 21);
    CHECK_THAT (p.getParameters()[speed]->getValue(), WithinAbs (64.0 / 127.0, 1e-4));

    sendCc (21, 127);
    CHECK_THAT (p.getParameters()[speed]->getValue(), WithinAbs (1.0, 1e-4));

    // Mapping survives save / reopen
    juce::MemoryBlock block;
    p.getStateInformation (block);
    NovaPitchProcessor q;
    q.setStateInformation (block.getData(), static_cast<int> (block.getSize()));
    CHECK (q.getMidiLearn().getCcForParam (speed) == 21);

    p.getMidiLearn().clearMapping (speed);
    CHECK (p.getMidiLearn().getCcForParam (speed) == -1);
}
