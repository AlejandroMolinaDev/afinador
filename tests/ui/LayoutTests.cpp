// Layout of every page and view at 75, 100, 150 and 200 %, and resizing like a
// host (corner drag, constrainer).

#include "LayoutChecks.h"
#include "UiHarness.h"

#include <catch2/catch_approx.hpp>

using namespace novapitch;
using namespace novapitch::uitest;
using Catch::Approx;

namespace
{
const char* const kPageSlugs[] = { "correccion", "notas", "formantes", "vibrato", "armonias", "bus", "midi" };

void checkView (Harness& h, const juce::String& context, std::vector<juce::String>& issues, int& controls)
{
    pump (60);
    const auto report = checkLayout (*h.window);
    controls += report.controls;
    for (const auto& issue : report.issues)
        issues.push_back (context + ": " + issue);
}

/** Notes, a line and a curve on a captured phrase, so the graph view has content. */
void addGraphContent (Harness& h)
{
    auto& model = h.p().getGraphModel();
    std::vector<CaptureFrame> frames;
    for (int k = 0; k < 300; ++k)
        frames.push_back ({ k * 0.01, 57.0f + (k > 150 ? 2.0f : 0.0f) + 0.2f * std::sin (k * 0.3f), -18.0f });
    model.appendCaptured (frames.data(), static_cast<int> (frames.size()));
    model.commitCapture();
    model.beginEdit();
    model.addNote (0.1, 1.4, 57.0f);
    model.addNote (1.55, 2.9, 59.0f);
    model.addLine (1.4, 1.55, 57.0f, 59.0f);
    model.addCurve ({ { 2.9, 59.0f }, { 2.95, 59.4f }, { 2.99, 59.8f } });
}
} // namespace

TEST_CASE ("Layout: every page and view fits, nothing overlaps or is covered, every text fits", "[ui][layout]")
{
    NOVAPITCH_REQUIRE_DISPLAY();
    std::vector<juce::String> issues;
    int controls = 0;

    for (const int width : { 570, 760, 1140, 1520 }) // 75 / 100 / 150 / 200 %
    {
        Harness h (width);
        const auto scaleText = juce::String (juce::roundToInt (100.0 * width / PluginEditor::kBaseWidth)) + " %";
        INFO ("scale " << scaleText);
        REQUIRE (h.window->getWidth() == width);
        CHECK (h.content().getTransform().mat00 == Approx (width / 760.0f).margin (1.0e-4f));

        Bot bot;
        for (int page = 0; page < UiTestAccess::numPages(); ++page)
        {
            bot.click (h.get<juce::Button> ("tab" + juce::String (page)));
            REQUIRE (UiTestAccess::currentPage (h.ed()) == page);
            for (int other = 0; other < UiTestAccess::numPages(); ++other)
                CHECK (UiTestAccess::page (h.ed(), other).isShowing() == (other == page));
            checkView (h, scaleText + " · " + kPageSlugs[page], issues, controls);
            if (width == 760)
                h.shot (juce::String ("layout-") + kPageSlugs[page]);
        }

        // Harmony: every voice, and a voice in semitone mode (other knob set).
        bot.click (h.get<juce::Button> ("tab4"));
        for (int v = 1; v <= 4; ++v)
        {
            bot.click (h.get<juce::Button> ("voice" + juce::String (v)));
            checkView (h, scaleText + " · voz " + juce::String (v), issues, controls);
        }
        h.setParam (ParamIDs::harmonyVoice ("hvMode", 4), 2.0f);
        checkView (h, scaleText + " · voz 4 semitonos", issues, controls);

        // Graphical mode: empty, then with objects and a selected note.
        bot.click (h.get<juce::Button> ("modeGraph"));
        REQUIRE (UiTestAccess::graphView (h.ed()));
        checkView (h, scaleText + " · grafico vacio", issues, controls);
        addGraphContent (h);
        auto& graph = UiTestAccess::graph (h.ed());
        UiTestAccess::setView (graph, 0.0, 3.2, 64.0f, 14.0f);
        pump (80);
        auto& roll = UiTestAccess::roll (graph);
        bot.click (roll, UiTestAccess::rollPoint (graph, 0.7, 57.0f)); // select tool: selects the first note
        CHECK (UiTestAccess::selection (graph).size() == 1);
        checkView (h, scaleText + " · grafico con seleccion", issues, controls);
        if (width == 760 || width == 570)
            h.shot ("layout-grafico-" + juce::String (juce::roundToInt (100.0 * width / PluginEditor::kBaseWidth)));
    }

    INFO (juce::StringArray (issues.data(), static_cast<int> (issues.size())).joinIntoString ("\n"));
    CHECK (controls > 400);
    CHECK (issues.empty());
}

TEST_CASE ("Resize: the corner keeps the aspect ratio and the 75-200 % limits", "[ui][layout]")
{
    NOVAPITCH_REQUIRE_DISPLAY();
    Harness h;
    juce::ResizableCornerComponent* corner = nullptr;
    forEach (*h.window, [&] (juce::Component& c) {
        if (auto* rc = dynamic_cast<juce::ResizableCornerComponent*> (&c))
            corner = rc;
    });
    REQUIRE (corner != nullptr);
    CHECK (corner->isShowing());

    Bot bot;
    auto dragCorner = [&] (float dx, float dy) {
        const auto from = corner->getLocalBounds().toFloat().getCentre();
        bot.drag (*corner, from, from + juce::Point<float> (dx, dy));
        pump (60);
    };
    auto checkAspect = [&] {
        const double ratio = static_cast<double> (h.window->getWidth()) / h.window->getHeight();
        CHECK (ratio == Approx (760.0 / 640.0).margin (0.01));
        CHECK (h.content().getTransform().mat00 == Approx (h.window->getWidth() / 760.0f).margin (1.0e-4f));
    };

    dragCorner (300.0f, 40.0f); // mostly horizontal: height follows
    CHECK (h.window->getWidth() > 900);
    checkAspect();
    dragCorner (2000.0f, 2000.0f); // beyond 200 %
    CHECK (h.window->getWidth() <= 1520);
    CHECK (h.window->getHeight() <= 1280);
    checkAspect();
    dragCorner (-3000.0f, -3000.0f); // below 75 %
    CHECK (h.window->getWidth() >= 570);
    CHECK (h.window->getHeight() >= 480);
    checkAspect();

    // A host asking for an odd size through the constrainer gets a valid one.
    auto* constrainer = h.window->getConstrainer();
    REQUIRE (constrainer != nullptr);
    juce::Rectangle<int> wanted (0, 0, 1000, 500);
    constrainer->checkBounds (wanted, h.window->getBounds(), { 0, 0, 4000, 4000 }, false, false, true, true);
    CHECK (static_cast<double> (wanted.getWidth()) / wanted.getHeight() == Approx (760.0 / 640.0).margin (0.01));

    // After resizing, the layout is still sound (real clicks reach the tabs).
    h.setWidth (1000);
    bot.click (h.get<juce::Button> ("tab3"));
    CHECK (UiTestAccess::currentPage (h.ed()) == 3);
    const auto report = checkLayout (*h.window);
    INFO (report.summary());
    CHECK (report.issues.empty());
}
