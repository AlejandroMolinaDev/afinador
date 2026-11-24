// Editor lifetime: FL Studio closes and reopens plugin windows at any moment,
// also with a pop-up menu or a dialog open, and keeps the audio running while
// the user works in the UI.

#include "UiHarness.h"
#include "plugin/graph/GraphModel.h"
#include "signals/SignalGenerators.h"

#include <atomic>
#include <cmath>
#include <thread>

using namespace novapitch;
using namespace novapitch::uitest;

namespace
{
void addCapture (NovaPitchProcessor& p)
{
    std::vector<CaptureFrame> frames;
    for (int k = 0; k < 200; ++k)
        frames.push_back ({ k * 0.01, 57.0f, -18.0f });
    p.getGraphModel().appendCaptured (frames.data(), static_cast<int> (frames.size()));
    p.getGraphModel().commitCapture();
}

/** After the window was closed with something open: callbacks have run, a new
    editor opens and works. */
void reopenAndUse (Harness& h)
{
    pump (300); // menus / dialogs deliver their (cancelled) results now
    h.openEditor();
    Bot bot;
    if (UiTestAccess::graphView (h.ed()))
    {
        bot.click (h.get<juce::Button> ("tool2"));
        CHECK (UiTestAccess::graph (h.ed()).getTool() == GraphEditor::Tool::Line);
        return;
    }
    bot.click (h.get<juce::Button> ("tab2"));
    CHECK (UiTestAccess::currentPage (h.ed()) == 2);
}
} // namespace

TEST_CASE ("Closing the window with a menu or a dialog open is safe", "[ui][lifetime]")
{
    NOVAPITCH_REQUIRE_DISPLAY();
    Bot bot;

    SECTION ("Escuchar menu")
    {
        Harness h;
        bot.click (h.get<juce::Button> ("listen"));
        h.closeEditor();
        reopenAndUse (h);
    }
    SECTION ("options menu")
    {
        Harness h;
        bot.click (h.get<juce::Button> ("options"));
        h.closeEditor();
        reopenAndUse (h);
    }
    SECTION ("knob MIDI Learn menu")
    {
        Harness h;
        bot.rightClick (h.get<LabeledKnob> (ParamIDs::retuneSpeed).getSlider());
        h.closeEditor();
        reopenAndUse (h);
    }
    SECTION ("preset list")
    {
        Harness h;
        bot.click (h.get<juce::ComboBox> ("presetBox"));
        h.closeEditor();
        reopenAndUse (h);
    }
    SECTION ("save preset dialog")
    {
        Harness h;
        bot.click (h.get<juce::Button> ("presetSave"));
        REQUIRE (UiTestAccess::saveDialog (h.ed()) != nullptr);
        h.closeEditor();
        reopenAndUse (h);
        CHECK (h.p().getPresetManager().getPresets().size() > 0);
    }
    SECTION ("note menu and exact detune dialog")
    {
        Harness h;
        bot.click (h.get<juce::Button> ("tab1"));
        auto& k = UiTestAccess::keyboard (h.ed());
        bot.rightClick (k, k.keyBounds (0).getCentre());
        closeOpenMenus();
        UiTestAccess::chooseKeyItem (k, 0, itemId (UiTestAccess::keyMenu (k, 0), u8 ("Otro valor")));
        pump (60);
        REQUIRE (UiTestAccess::detuneDialog (k) != nullptr);
        h.closeEditor();
        reopenAndUse (h);
    }
    SECTION ("graph import and clear menus")
    {
        Harness h;
        addCapture (h.p());
        bot.click (h.get<juce::Button> ("modeGraph"));
        bot.click (h.get<juce::Button> ("import"));
        h.closeEditor();
        reopenAndUse (h); // reopens in the graphical view (the parameter is on)
        CHECK (UiTestAccess::graphView (h.ed()));
        bot.click (h.get<juce::Button> ("clear"));
        h.closeEditor();
        pump (300);
        CHECK_FALSE (h.p().getGraphModel().getCaptured().empty()); // a dismissed menu changes nothing
    }
}

TEST_CASE ("Reopening the editor shows the saved view and the live state", "[ui][lifetime]")
{
    NOVAPITCH_REQUIRE_DISPLAY();
    Harness h;
    Bot bot;
    bot.click (h.get<juce::Button> ("modeGraph"));
    h.p().getPresetManager().selectSlot (1);
    h.closeEditor();
    h.openEditor (Harness::fitWidth (1140)); // as large as the screen allows
    CHECK (UiTestAccess::graphView (h.ed()));
    CHECK (h.get<juce::Button> ("abB").getToggleState());
    CHECK (h.window->getWidth() == Harness::fitWidth (1140));
    bot.click (h.get<juce::Button> ("modeAuto"));
    CHECK_FALSE (UiTestAccess::graphView (h.ed()));
}

TEST_CASE ("Two instances: audio threads run while both editors are used, closed and reopened", "[ui][lifetime][threads]")
{
    NOVAPITCH_REQUIRE_DISPLAY();
    Harness a, b (Harness::fitWidth (900));
    std::atomic<bool> run { true };
    std::atomic<int> nonFinite { 0 };
    std::atomic<long> blocks { 0 };

    auto audio = [&] (NovaPitchProcessor& p, double hz) {
        test::TestPlayHead head;
        p.setPlayHead (&head);
        const auto voice = signals::vibratoSine (hz, 5.5, 30.0, Harness::kSampleRate, 2.0, 0.4f);
        juce::AudioBuffer<float> buffer (2, Harness::kBlock);
        juce::MidiBuffer midi;
        size_t pos = 0;
        while (run.load())
        {
            if (pos + Harness::kBlock > voice.size())
                pos = 0;
            head.timeInSamples = static_cast<int64_t> (pos);
            buffer.copyFrom (0, 0, voice.data() + pos, Harness::kBlock);
            buffer.copyFrom (1, 0, voice.data() + pos, Harness::kBlock);
            p.processBlock (buffer, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < Harness::kBlock; ++i)
                    if (! std::isfinite (buffer.getSample (ch, i)))
                        ++nonFinite;
            pos += Harness::kBlock;
            ++blocks;
            std::this_thread::sleep_for (std::chrono::milliseconds (4)); // ~real time
        }
        p.setPlayHead (nullptr);
    };
    std::thread ta (audio, std::ref (a.p()), 220.0), tb (audio, std::ref (b.p()), 330.0);
    struct Joiner // the threads stop even if a REQUIRE below throws
    {
        std::atomic<bool>& flag;
        std::thread &x, &y;
        ~Joiner()
        {
            flag = false;
            if (x.joinable()) x.join();
            if (y.joinable()) y.join();
        }
    } joiner { run, ta, tb };

    Bot bot;
    for (int round = 0; round < 2; ++round)
        for (auto* h : { &a, &b })
        {
            for (int page = 0; page < UiTestAccess::numPages(); ++page)
                bot.click (h->get<juce::Button> ("tab" + juce::String (page)));
            bot.click (h->get<juce::Button> ("tab0"));
            bot.click (h->get<juce::Button> ("presetNext"));
            bot.click (h->get<juce::Button> ("abB"));
            bot.click (h->get<juce::Button> ("abA"));
            auto& knob = h->get<LabeledKnob> (ParamIDs::retuneSpeed).getSlider();
            const auto c = knob.getLocalBounds().toFloat().withTrimmedBottom (18.0f).getCentre();
            bot.drag (knob, c, c - juce::Point<float> (0.0f, 30.0f));
            bot.click (h->get<juce::Button> ("modeGraph"));
            CHECK (UiTestAccess::graphView (h->ed()));
            bot.click (h->get<juce::Button> ("capture"));
            pump (400);
            bot.click (h->get<juce::Button> ("capture"));
            pump (200);
            UiTestAccess::chooseImport (UiTestAccess::graph (h->ed()), 2);
            bot.click (h->get<juce::Button> ("modeAuto"));
            h->closeEditor();
            pump (100);
            h->openEditor (Harness::fitWidth (round == 0 ? 760 : 1140));
        }

    run = false;
    ta.join();
    tb.join();
    CHECK (blocks.load() > 200);
    CHECK (nonFinite.load() == 0);
    CHECK_FALSE (a.p().getGraphModel().getCaptured().empty());
}
