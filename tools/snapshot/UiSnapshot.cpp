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

    for (const auto* tab : { "Formantes", "Vibrato" })
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
            const auto file = outDir.getChildFile ("editor-tab-" + juce::String (tab).toLowerCase() + ".png");
            const bool written = writePng (editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f), file);
            std::cout << (written ? "wrote " : "FAILED ") << file.getFullPathName() << std::endl;
            ok = ok && written;
        }
        else
        {
            std::cout << "tab not found: " << tab << std::endl;
            ok = false;
        }
    }

    editor.reset();
    return ok ? 0 : 1;
}
