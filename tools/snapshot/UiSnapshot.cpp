// NovaPitchSnapshot — renders the editor to PNG after feeding the processor a
// synthetic sung phrase, so the UI can be reviewed without opening a DAW
// (CI uploads the images as artefacts).
//
// Usage: NovaPitchSnapshot <output-dir>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "plugin/PluginProcessor.h"
#include "signals/SignalGenerators.h"

#include <functional>
#include <iostream>

using namespace novapitch;

namespace
{
/** A short "sung" phrase: note steps with scoops, vibrato and a flat final note. */
std::vector<float> phrase (double sr)
{
    struct Note { double start, midi; };
    static constexpr Note notes[] = { { 0.0, 57.0 }, { 0.5, 59.0 }, { 0.9, 60.0 }, { 1.6, 64.0 }, { 2.4, 62.0 }, { 3.0, 60.88 } };
    constexpr double total = 3.8;

    auto midiAt = [] (double t) {
        double m = notes[0].midi, prevM = m, start = 0.0;
        for (const auto& n : notes)
            if (t >= n.start) { prevM = m; m = n.midi; start = n.start; }
        const double glide = std::min (1.0, (t - start) / 0.06); // 60 ms scoop into each note
        const double vib = (t - start > 0.3 && t < 3.0) ? 0.35 * std::sin (2.0 * 3.14159265 * 5.5 * t) : 0.0;
        return prevM + (m - prevM) * glide + vib;
    };

    auto voice = signals::harmonicSweep (sr, total, { 0.25f, 0.2f, 0.12f, 0.07f, 0.04f }, [&] (double t) {
        return 440.0 * std::exp2 ((midiAt (t) - 69.0) / 12.0);
    });
    // Short breath gap
    for (size_t i = static_cast<size_t> (sr * 1.45); i < static_cast<size_t> (sr * 1.6); ++i)
        voice[i] *= 0.0f;
    return voice;
}

/** Minimal transport (FL plays from bar 1). */
struct SnapshotPlayHead final : juce::AudioPlayHead
{
    int64_t samples = 0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setIsPlaying (playing);
        info.setTimeInSamples (samples);
        info.setTimeInSeconds (static_cast<double> (samples) / 48000.0);
        return info;
    }
};

bool writePng (const juce::Image& image, const juce::File& file)
{
    file.deleteFile();
    juce::FileOutputStream out (file);
    juce::PNGImageFormat png;
    return out.openedOk() && png.writeImageToStream (image, out);
}
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto outDir = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "snapshots");
    outDir.createDirectory();

    NovaPitchProcessor proc;
    const double sr = 48000.0;
    proc.prepareToPlay (sr, 256);

    const auto sig = phrase (sr);
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    auto processUntil = [&] (size_t& pos, size_t end) {
        for (; pos + 256 <= end; pos += 256)
        {
            buffer.copyFrom (0, 0, sig.data() + pos, 256);
            buffer.copyFrom (1, 0, sig.data() + pos, 256);
            proc.processBlock (buffer, midi);
        }
    };

    // Most of the phrase before the editor opens (the FIFO keeps ~10 s)...
    size_t pos = 0;
    const size_t tail = sig.size() - static_cast<size_t> (sr * 0.5);
    processUntil (pos, tail);
    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());

    // ...and the last 0.5 s in real time while the UI timer runs, like a live input.
    while (pos + 256 <= sig.size())
    {
        processUntil (pos, std::min (sig.size(), pos + static_cast<size_t> (sr * 0.03)));
        juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
    }

    bool ok = true;
    for (const float scale : { 1.0f, 2.0f })
    {
        // Render at `scale` (HiDPI check) without waiting: the meter must still be "live".
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);
        const auto file = outDir.getChildFile ("editor-" + juce::String (juce::roundToInt (scale * 100)) + ".png");
        const bool written = writePng (image, file);
        std::cout << (written ? "wrote " : "FAILED ") << file.getFullPathName() << std::endl;
        ok = ok && written;
    }
    // One image per tab (found by its text in the component tree), at 100 %.
    std::function<juce::Button* (juce::Component&, const juce::String&)> findButton =
        [&] (juce::Component& c, const juce::String& text) -> juce::Button* {
        if (auto* b = dynamic_cast<juce::Button*> (&c); b != nullptr && b->getButtonText() == text)
            return b;
        for (auto* child : c.getChildren())
            if (auto* found = findButton (*child, text))
                return found;
        return nullptr;
    };

    // Microtonal example for the Notas tab: E a just third (-14 c), Bb a harmonic seventh (-31 c).
    for (const auto& [id, cents] : { std::pair { "noteDetune4", -14.0f }, std::pair { "noteDetune10", -31.0f } })
        if (auto* p = proc.getValueTreeState().getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (cents));

    const std::pair<const char*, const char*> tabs[] = { { "Notas", "notas" }, { "Formantes", "formantes" }, { "Vibrato", "vibrato" },
                                                         { "Armonías", "armonias" }, { "Bus", "bus" }, { "MIDI", "midi" } };
    for (const auto& [tab, slug] : tabs)
    {
        if (auto* button = findButton (*editor, juce::String::fromUTF8 (tab)))
        {
            button->triggerClick();
            // Keep audio flowing so the displays are live: a sustained note with a
            // natural vibrato (5.3 Hz, ±40 c) for the vibrato view.
            const auto held = novapitch::signals::vibratoSine (261.63 * std::exp2 (-18.0 / 1200.0), 5.3, 40.0, sr, 1.5, 0.4f);
            for (size_t p = 0; p + 256 <= held.size(); p += 256)
            {
                buffer.copyFrom (0, 0, held.data() + p, 256);
                buffer.copyFrom (1, 0, held.data() + p, 256);
                proc.processBlock (buffer, midi);
                if ((p / 256) % 6 == 0)
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
            }
            const auto file = outDir.getChildFile ("editor-tab-" + juce::String (slug) + ".png");
            const bool written = writePng (editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f), file);
            std::cout << (written ? "wrote " : "FAILED ") << file.getFullPathName() << std::endl;
            ok = ok && written;
        }
        else
        {
            std::cout << "tab not found: " << slug << std::endl;
            ok = false;
        }
    }

    // Graphical mode: capture the phrase by "playing" it, import, edit, then play
    // again up to the middle so the playhead is visible.
    if (auto* button = findButton (*editor, juce::String::fromUTF8 ("Gráfico")))
    {
        button->triggerClick();
        SnapshotPlayHead head;
        proc.setPlayHead (&head);
        auto& state = proc.getValueTreeState();
        if (auto* scale = state.getParameter ("scale"))
            scale->setValueNotifyingHost (scale->convertTo0to1 (0.0f)); // C major
        auto playFrom = [&] (size_t from, size_t to) {
            for (size_t p = from; p + 256 <= to; p += 256)
            {
                head.samples = static_cast<int64_t> (p);
                buffer.copyFrom (0, 0, sig.data() + p, 256);
                buffer.copyFrom (1, 0, sig.data() + p, 256);
                proc.processBlock (buffer, midi);
                if ((p / 256) % 6 == 0)
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
            }
        };
        const auto captureRoot = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("NovaPitchCaptures-tool");
        proc.setCaptureRoot (captureRoot); // keep tool runs out of the user's capture folder
        proc.setGraphCapture (true);
        playFrom (0, sig.size());
        proc.setGraphCapture (false);
        proc.flushGraphCapture();
        proc.importGraphFromAuto();

        auto& model = proc.getGraphModel();
        model.beginEdit();
        const auto objects = model.getObjects();
        if (objects.getNumChildren() > 0) // the flat final note goes up to the D above
            model.setObjectProperty (objects.getChild (objects.getNumChildren() - 1), juce::Identifier ("pitch"), 62.0f);
        model.addLine (1.42, 1.62, 60.0f, 64.0f);
        model.addCurve ({ { 3.70, 62.0f }, { 3.74, 62.6f }, { 3.78, 63.1f } });
        if (auto* graphMode = state.getParameter ("graphMode"))
            graphMode->setValueNotifyingHost (1.0f);
        // Time edit: the E4 syllable (1.6 s) starts 100 ms later; neighbours absorb it.
        novapitch::dsp::TimeMap map;
        map.anchors = { { 0.9, 0.9 }, { 1.7, 1.6 }, { 2.4, 2.4 } };
        model.setTimeMap (map);
        proc.renderTimeEditsNow();
        if (auto* timeTool = findButton (*editor, juce::String::fromUTF8 ("Tiempo")))
            timeTool->triggerClick();

        playFrom (0, static_cast<size_t> (sr * 2.2));
        head.samples = static_cast<int64_t> (sr * 2.2);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        for (const float scale : { 1.0f, 2.0f })
        {
            const auto file = outDir.getChildFile ("editor-graph-" + juce::String (juce::roundToInt (scale * 100)) + ".png");
            const bool written = writePng (editor->createComponentSnapshot (editor->getLocalBounds(), true, scale), file);
            std::cout << (written ? "wrote " : "FAILED ") << file.getFullPathName() << std::endl;
            ok = ok && written;
        }
        proc.setPlayHead (nullptr);
    }
    else
    {
        std::cout << "graph mode button not found" << std::endl;
        ok = false;
    }

    editor.reset();
    return ok ? 0 : 1;
}
