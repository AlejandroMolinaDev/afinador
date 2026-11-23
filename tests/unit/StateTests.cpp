#include <catch2/catch_test_macros.hpp>

#include "TestHelpers.h"
#include "plugin/Parameters.h"

#include <algorithm>

using namespace novapitch;
using namespace novapitch::test;

TEST_CASE ("State round-trips and carries a version number", "[state]")
{
    JuceFixture fixture;
    NovaPitchProcessor a;

    juce::MemoryBlock block;
    a.getStateInformation (block);
    REQUIRE (block.getSize() > 0);

    auto xml = juce::AudioProcessor::getXmlFromBinary (block.getData(), static_cast<int> (block.getSize()));
    REQUIRE (xml != nullptr);
    CHECK (xml->hasTagName (kStateTreeType));
    CHECK (xml->getIntAttribute (kStateVersionProperty) == kStateVersion);

    NovaPitchProcessor b;
    b.setStateInformation (block.getData(), static_cast<int> (block.getSize()));
    CHECK (static_cast<int> (b.getValueTreeState().state.getProperty (kStateVersionProperty)) == kStateVersion);
}

TEST_CASE ("Invalid or foreign state data is ignored safely", "[state]")
{
    JuceFixture fixture;
    NovaPitchProcessor p;

    const char garbage[] = "definitely not a NovaPitch state";
    p.setStateInformation (garbage, static_cast<int> (sizeof (garbage)));
    p.setStateInformation (nullptr, 0);

    juce::MemoryBlock foreign;
    juce::AudioProcessor::copyXmlToBinary (juce::XmlElement ("SomeOtherPlugin"), foreign);
    p.setStateInformation (foreign.getData(), static_cast<int> (foreign.getSize()));

    CHECK (p.getValueTreeState().state.getType().toString() == kStateTreeType);
}

TEST_CASE ("Older state versions are migrated to the current version", "[state]")
{
    JuceFixture fixture;
    NovaPitchProcessor p;

    juce::XmlElement old (kStateTreeType);
    old.setAttribute (kStateVersionProperty, 0);
    juce::MemoryBlock block;
    juce::AudioProcessor::copyXmlToBinary (old, block);

    p.setStateInformation (block.getData(), static_cast<int> (block.getSize()));
    CHECK (static_cast<int> (p.getValueTreeState().state.getProperty (kStateVersionProperty)) == kStateVersion);
}

#include <catch2/interfaces/catch_interfaces_registry_hub.hpp>
#include <catch2/interfaces/catch_interfaces_testcase.hpp>
#include <catch2/catch_test_case_info.hpp>

TEST_CASE ("Test names are plain ASCII (CTest on Windows mangles other characters)", "[meta]")
{
    const auto& tests = Catch::getRegistryHub().getTestCaseRegistry().getAllTests();
    for (const auto& handle : tests)
    {
        const auto& name = handle.getTestCaseInfo().name;
        INFO (name);
        CHECK (std::all_of (name.begin(), name.end(), [] (char c) { return static_cast<unsigned char> (c) < 128; }));
    }
}
