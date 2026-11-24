// NovaPitchUiTests entry point: one JUCE GUI session for the whole Catch2 run.

#include <catch2/catch_session.hpp>
#include <juce_gui_basics/juce_gui_basics.h>

#include "support/XvfbSupport.h"

int main (int argc, char* argv[])
{
    novapitch::test::prepareX11Display(); // bare Xvfb: see XvfbSupport.h
    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::Process::makeForegroundProcess(); // like a DAW in front: pop-up menus keep the focus
    return Catch::Session().run (argc, argv);
}
