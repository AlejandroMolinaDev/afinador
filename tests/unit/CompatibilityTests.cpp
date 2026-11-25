// Compatibility with projects already saved by FL Studio (release guard).
//
// FL Studio stores the plugin codes, the parameter list (by index and ID) and
// the plugin state in every project. These tests compare the current build
// with reference files written by version 0.9.0 (tests/data/):
//   - parameters-0.9.0.txt: every parameter's index, ID, version hint, range,
//     default and step count. Only new parameters appended at the end are allowed.
//   - state-0.9.0.bin: a full project state with non-default settings in every
//     area. It must load back exactly.
// Never edit the reference files to make a test pass: fix the code (or add a
// migration in migrateState()). To regenerate them on purpose (a new major
// version), run the tests with NOVAPITCH_WRITE_GOLDEN=1.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.h"
#include "plugin/Parameters.h"
#include "plugin/graph/AudioCapture.h"
#include "plugin/graph/GraphModel.h"

#include <cstdlib>

using namespace novapitch;
using namespace novapitch::test;
using Catch::Approx;

namespace
{
juce::File dataDir() { return juce::File (juce::String::fromUTF8 (NOVAPITCH_TEST_DATA_DIR)); }
bool writeGolden() { return std::getenv ("NOVAPITCH_WRITE_GOLDEN") != nullptr; }
juce::String u8 (const char* t) { return juce::String::fromUTF8 (t); }

juce::String describe (const juce::RangedAudioParameter& p, int index)
{
    const auto& r = p.getNormalisableRange();
    return juce::String (index) + "|" + p.getParameterID() + "|" + juce::String (p.getVersionHint()) + "|"
           + juce::String (r.start, 4) + "|" + juce::String (r.end, 4) + "|"
           + juce::String (p.convertFrom0to1 (p.getDefaultValue()), 4) + "|" + juce::String (p.getNumSteps());
}

juce::StringArray currentParameters (NovaPitchProcessor& p)
{
    juce::StringArray lines;
    const auto& params = p.getParameters();
    for (int i = 0; i < params.size(); ++i)
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (params[i]))
            lines.add (describe (*ranged, i));
    return lines;
}

/** Real (denormalised) values set in the reference project: one per area. */
const std::vector<std::pair<const char*, float>>& projectValues()
{
    static const std::vector<std::pair<const char*, float>> values {
        { "inputProfile", 2.0f }, { "sensitivity", 63.0f }, { "qualityMode", 2.0f }, { "key", 9.0f }, { "scale", 1.0f },
        { "referenceA4", 442.0f }, { "retuneSpeed", 37.0f }, { "tolerance", 12.0f }, { "naturalHold", 40.0f },
        { "transpose", -3.0f }, { "mix", 80.0f }, { "noteState4", 2.0f }, { "noteState6", 3.0f },
        { "formantPreserve", 0.0f }, { "formantShift", 2.5f }, { "formantLink", 1.0f }, { "character", -20.0f },
        { "vibratoPreserve", 150.0f }, { "vibRate", 6.2f }, { "vibDepth", 30.0f }, { "vibDelay", 450.0f },
        { "midiTarget", 1.0f }, { "midiScaleLearn", 0.0f }, { "hvMode1", 1.0f }, { "hvDegrees1", 2.0f },
        { "hvPan1", -40.0f }, { "hvLevel1", -6.0f }, { "hvMode2", 2.0f }, { "hvSemitones2", -5.0f },
        { "hBusHpf", 180.0f }, { "hBusWidth", 140.0f }, { "graphMode", 1.0f }, { "midiOut", 1.0f },
        { "midiTargetExact", 1.0f }, { "vibShape", 2.0f }, { "noteDetune4", -14.0f }, { "noteDetune10", -31.0f },
    };
    return values;
}

constexpr int kLearntCc = 21;

/** Builds the reference project: every area of the state with non-default content. */
std::unique_ptr<NovaPitchProcessor> makeReferenceProject()
{
    auto p = std::make_unique<NovaPitchProcessor>();
    auto& pm = p->getPresetManager();
    pm.loadPreset (u8 ("Pop moderno"));
    pm.selectSlot (1); // A/B: B active, A = the preset
    for (const auto& [id, value] : projectValues())
        setParam (*p, id, value);

    // MIDI Learn: CC 21 -> retune speed.
    auto& learn = p->getMidiLearn();
    learn.startLearning (p->getValueTreeState().getParameter (ParamIDs::retuneSpeed)->getParameterIndex());
    juce::MidiBuffer cc;
    cc.addEvent (juce::MidiMessage::controllerEvent (1, kLearntCc, 64), 0);
    learn.processMidi (cc);
    learn.dispatchPending();
    setParam (*p, ParamIDs::retuneSpeed, 37.0f); // back to the project value

    // Graphical mode: a captured curve, a note, a line, a curve and a time edit.
    auto& model = p->getGraphModel();
    std::vector<CaptureFrame> frames;
    for (int k = 0; k < 250; ++k)
        frames.push_back ({ k * 0.01, 57.0f + 0.1f * static_cast<float> (k % 7), -20.0f });
    model.appendCaptured (frames.data(), static_cast<int> (frames.size()));
    model.commitCapture();
    model.beginEdit();
    auto note = model.addNote (0.2, 1.1, 59.0f);
    model.setObjectProperty (note, GraphIds::retuneMs, 25.0f);
    model.addLine (1.1, 1.4, 59.0f, 62.0f);
    model.addCurve ({ { 1.5, 62.0f }, { 1.6, 62.4f }, { 1.7, 62.1f } });
    dsp::TimeMap map;
    map.anchors = { { 0.1, 0.1 }, { 0.3, 0.2 }, { 1.2, 1.1 }, { 1.6, 1.6 } };
    model.setTimeMap (map);
    model.getAudioId (true);
    AudioPass pass;
    pass.fileName = "pass-0001.wav";
    pass.start = 0;
    pass.length = 120000;
    pass.sampleRate = 48000.0;
    pass.channels = 2;
    model.addAudioPasses ({ pass });

    p->getValueTreeState().state.setProperty ("accentColour", juce::Colour (0xff2ec4b6).toString(), nullptr);
    return p;
}

float value (NovaPitchProcessor& p, const juce::String& id) { return p.getValueTreeState().getRawParameterValue (id)->load(); }
} // namespace

//==============================================================================
TEST_CASE ("Parameter list matches 0.9.0: same IDs, order and ranges; new ones only appended", "[compat]")
{
    JuceFixture fixture;
    NovaPitchProcessor p;
    const auto current = currentParameters (p);
    const auto file = dataDir().getChildFile ("parameters-0.9.0.txt");
    if (writeGolden())
        REQUIRE (file.replaceWithText (current.joinIntoString ("\n") + "\n"));

    juce::StringArray reference;
    reference.addLines (file.loadFileAsString());
    reference.removeEmptyStrings();
    REQUIRE (reference.size() == 96);
    REQUIRE (current.size() >= reference.size());
    for (int i = 0; i < reference.size(); ++i)
    {
        INFO ("parameter " << i << " changed (FL projects would load the wrong values).\n  0.9.0: " << reference[i]
                           << "\n  now:   " << current[i]);
        CHECK (current[i] == reference[i]);
    }
}

TEST_CASE ("Plugin codes and IDs that FL stores in projects are unchanged", "[compat]")
{
    const auto cmake = juce::File (juce::String::fromUTF8 (NOVAPITCH_SOURCE_DIR)).getChildFile ("CMakeLists.txt").loadFileAsString();
    REQUIRE (cmake.isNotEmpty());
    auto field = [&cmake] (const char* key) {
        const auto line = cmake.fromFirstOccurrenceOf (juce::String (key) + " ", false, false).upToFirstOccurrenceOf ("\n", false, false);
        return line.upToFirstOccurrenceOf ("#", false, false).trim().unquoted();
    };
    CHECK (field ("PLUGIN_MANUFACTURER_CODE") == "Nvpt");
    CHECK (field ("PLUGIN_CODE") == "Npc1");
    CHECK (field ("BUNDLE_ID") == "com.novapitch.novapitch");
    CHECK (field ("CLAP_ID") == "com.novapitch.novapitch");
    CHECK (field ("AU_MAIN_TYPE") == "kAudioUnitType_MusicEffect");
    CHECK (field ("NEEDS_MIDI_INPUT") == "TRUE");
    CHECK (field ("NEEDS_MIDI_OUTPUT") == "TRUE");
    CHECK (juce::String (kStateVersionProperty) == "stateVersion");
}

TEST_CASE ("A project saved by 0.9.0 loads back every setting", "[compat][state]")
{
    JuceFixture fixture;
    const auto file = dataDir().getChildFile ("state-0.9.0.bin");
    if (writeGolden())
    {
        auto reference = makeReferenceProject();
        juce::MemoryBlock saved;
        reference->getStateInformation (saved);
        REQUIRE (file.replaceWithData (saved.getData(), saved.getSize()));
    }

    juce::MemoryBlock data;
    REQUIRE (file.loadFileAsData (data));
    REQUIRE (data.getSize() > 1000);

    auto p = std::make_unique<NovaPitchProcessor>();
    p->setStateInformation (data.getData(), static_cast<int> (data.getSize()));

    for (const auto& [id, expected] : projectValues())
    {
        INFO ("parameter " << id);
        CHECK (value (*p, id) == Approx (expected).margin (0.01));
    }

    // MIDI Learn mapping
    CHECK (p->getMidiLearn().getCcForParam (p->getValueTreeState().getParameter (ParamIDs::retuneSpeed)->getParameterIndex()) == kLearntCc);

    // Presets: name and A/B
    auto& pm = p->getPresetManager();
    CHECK (pm.getCurrentPresetName() == u8 ("Pop moderno"));
    CHECK (pm.getActiveSlot() == 1);
    pm.selectSlot (0); // A is the preset as loaded
    CHECK (value (*p, ParamIDs::retuneSpeed) != Approx (37.0f));
    CHECK (value (*p, ParamIDs::key) == Approx (9.0f)); // song settings never move with A/B

    // Graphical mode
    auto& model = p->getGraphModel();
    CHECK (model.getCaptured().size() == 250);
    int notes = 0, lines = 0, curves = 0;
    for (const auto& o : model.getObjects())
    {
        notes += o.hasType (GraphIds::note) ? 1 : 0;
        lines += o.hasType (GraphIds::line) ? 1 : 0;
        curves += o.hasType (GraphIds::curve) ? 1 : 0;
        if (o.hasType (GraphIds::note))
        {
            CHECK (static_cast<double> (o.getProperty (GraphIds::pitch)) == Approx (59.0));
            CHECK (static_cast<double> (o.getProperty (GraphIds::retuneMs)) == Approx (25.0));
        }
    }
    CHECK (notes == 1);
    CHECK (lines == 1);
    CHECK (curves == 1);
    REQUIRE (model.getTimeMap().anchors.size() == 4);
    CHECK (model.getTimeMap().toOutput (0.2) == Approx (0.3));
    CHECK (model.getAudioId (false).isNotEmpty());
    REQUIRE (model.getAudioPasses().size() == 1);
    CHECK (model.getAudioPasses()[0].fileName == "pass-0001.wav");
    CHECK (model.getAudioPasses()[0].length == 120000);

    // Editor accent colour and state version
    const auto& state = p->getValueTreeState().state;
    CHECK (state.getProperty ("accentColour").toString() == juce::Colour (0xff2ec4b6).toString());
    CHECK (static_cast<int> (state.getProperty (kStateVersionProperty)) == kStateVersion);

    // Saving again and reopening keeps everything (FL saves on every project save).
    juce::MemoryBlock again;
    p->getStateInformation (again);
    auto q = std::make_unique<NovaPitchProcessor>();
    q->setStateInformation (again.getData(), static_cast<int> (again.getSize()));
    CHECK (q->getGraphModel().getObjects().getNumChildren() == 3);
    CHECK (value (*q, ParamIDs::noteDetune (10)) == Approx (-31.0f).margin (0.01));
}
